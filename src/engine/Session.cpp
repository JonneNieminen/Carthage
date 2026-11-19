#include "Session.h"
#include "FormantPlugin.h"
#include "SampleEdits.h"
#include "Trace.h"
#include "Editing.h"

#include <tracktion_graph/tracktion_graph.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace carthage::engine
{
    namespace
    {
        // How often a running take checks that playback is still running under it (TakeWatch).
        constexpr int watchIntervalMs = 5;

        /** Puts the engine's settings file in Carthage's settings folder. */
        class Storage final : public tracktion::PropertyStorage
        {
        public:
            explicit Storage (juce::File f) : tracktion::PropertyStorage ("Carthage"), folder (std::move (f)) {}

            juce::File getAppPrefsFolder() override
            {
                folder.createDirectory();
                return folder;
            }

            juce::String getApplicationVersion() override   { return CARTHAGE_VERSION_STRING; }

        private:
            juce::File folder;
        };

        class Behaviour final : public tracktion::EngineBehaviour
        {
        public:
            explicit Behaviour (PluginCatalogue*& c) : catalogue (c) {}

            // Carthage initialises the device itself so it can put its own callback in place.
            bool autoInitialiseDeviceManager() override   { return false; }

           #if CARTHAGE_RTSAN
            // RTSan checks threads that are inside a real-time scope. The device thread is (see
            // AudioCallback); the engine's worker threads can not be marked without patching it. So
            // the checked build renders the whole graph on the device thread, where every node is
            // checked. Release builds use every core.
            int getNumberOfCPUsToUseForAudio() override    { return 1; }
           #endif

            // The load gate (F9): nothing is loaded in this process unless carthage-scan loaded it
            // first and lived.
            bool shouldLoadPlugin (tracktion::ExternalPlugin& p) override
            {
                return catalogue != nullptr && catalogue->isScanned (p.desc) && ! catalogue->isBlocked (p.desc.fileOrIdentifier);
            }

        private:
            PluginCatalogue*& catalogue;
        };
    }

    Session::Session (Folders f, juce::File scannerExecutable)
        : folders (std::move (f)), scanner (std::move (scannerExecutable))
    {
        folders.settings.createDirectory();
        folders.pluginCache.createDirectory();
        if (folders.autosave != juce::File())
            autosaves.emplace (folders.autosave);

        engine = std::make_unique<tracktion::Engine> (std::make_unique<Storage> (folders.settings),
                                                      std::make_unique<tracktion::UIBehaviour>(),
                                                      std::make_unique<Behaviour> (loadGate));

        auto& pm = engine->getPluginManager();
        pm.createBuiltInType<FormantPlugin>();   // the sample editor's formant shift (4e)
        plugins = std::make_unique<PluginCatalogue> (pm.knownPluginList, pm.pluginFormatManager, folders.pluginCache);
        loadGate = plugins.get();

        // The browser's list, kept in step with the catalogue. Nothing scans until the app asks
        // (tools and tests never scan by themselves).
        library = std::make_unique<PluginLibrary> (folders.pluginCache);
        library->setPlugins (plugins->getScanned());
        pluginScanner = std::make_unique<PluginScanner> (*plugins, scanner);
        pluginScanner->maxParallel = getParallelChecks();
        pluginScanner->onChange = [this]
        {
            library->setPlugins (plugins->getScanned());

            if (const auto p = pluginScanner->getProgress(); ! p.running && p.total > 0 && ! scanLogged)
            {
                scanLogged = true;
                log::info ("Plugin check: " + juce::String (p.done) + " checked, " + juce::String (p.found) + " found, "
                           + juce::String (p.blocked) + " blocked, " + juce::String (p.seconds, 1) + " s"
                           + (p.newlyBlocked.isEmpty() ? juce::String() : "; newly blocked: " + p.newlyBlocked.joinIntoString (", ")));
            }
            else if (p.running)
            {
                scanLogged = false;
            }

            if (onPluginsChanged)
                onPluginsChanged();
        };

        // The metronome's sound, written afresh each launch (a few kB), so a new build's sounds
        // replace the old files. A custom file that has gone falls back to the tick-tock.
        if (setMetronomeSound (getMetronomeSound(), getMetronomeCustomFile()).failed())
            setMetronomeSound (metronome::Sound::tickTock);

        sampleLibrary = std::make_unique<SampleLibrary> (folders.settings);
        sampleAnalyser = std::make_unique<SampleAnalyser> (*sampleLibrary);
        sampleLibrary->onIndexed = [this] { sampleAnalyser->update(); };

        // Engine render threads join the device's audio workgroup (F8). Must be set before the
        // playback context is created.
        tracktion::EditPlaybackContext::enableAudioWorkgroup (true);

        auto& dm = engine->getDeviceManager();
        dm.initialise (tracktion::DeviceManager::defaultNumChannelsToOpen, tracktion::DeviceManager::defaultNumChannelsToOpen);

        // The engine's first MIDI device scan runs on a 4-second timer and always rebuilds the
        // device list, which stops playback: a take started in those first seconds would be cut
        // short. Run it now, while nothing plays. Later scans only rebuild on a hardware change.
        dm.rescanMidiDeviceList();

        callback = std::make_unique<AudioCallback> (*engine, recorder, probe, preview);
        callback->attach();

        dm.addChangeListener (this);
        prepareForDevice();

        const auto file = project::fileIn (folders.project);
        add (project::createEdit (*engine, file), file, project::Info::createNew(), true, true);
    }

    Session::~Session()
    {
        autosaveTimer.stopTimer();
        writer.waitUntilIdle();
        pluginScanner.reset();   // stops a scan and waits for its scanners before anything else goes
        engine->getDeviceManager().removeChangeListener (this);

        if (isRecording())
            recorder.stop();

        for (auto& p : projects)
            p->edit->getTransport().stop (false, true);

        callback->detach();
        callback.reset();
        projects.clear();   // after the callback: the device no longer reaches their graphs
        loadGate = nullptr;
        engine.reset();
    }

    //==============================================================================================
    void Session::changeListenerCallback (juce::ChangeBroadcaster* source)
    {
        juce::ignoreUnused (source);   // only the device manager: projects listen to their own undo

        if (routing)
            return;   // our own re-routing makes the engine broadcast; nothing new to do

        prepareForDevice();
        applyInputRouting();
        log::info ("Audio: " + describeAudio());

        if (onDeviceChanged)
            onDeviceChanged();
    }

    void Session::prepareForDevice()
    {
        auto* device = getDeviceManager().getCurrentAudioDevice();

        if (device == nullptr || isRecording() || probe.isBusy())
            return;

        const auto rate = device->getCurrentSampleRate();
        const auto inputs = device->getActiveInputChannels().countNumberOfSetBits();

        // The browser's preview plays where the project does: the main output pair.
        preview.prepare (rate);

        if (auto* main = engine->getDeviceManager().getDefaultWaveOutDevice(); main != nullptr && main->getChannels().size() > 0)
            callback->setPreviewOutput (main->getChannels()[0].indexInDevice);

        if (rate == preparedRate && inputs == preparedInputs)
            return;

        // Eight seconds of FIFO: the writer drains it every few milliseconds, so this only matters
        // if the disk stalls. Allocated here, never on the audio thread.
        recorder.prepare (2, static_cast<int> (rate * 8.0));
        probe.prepare (rate);
        preparedRate = rate;
        preparedInputs = inputs;
    }

    //==============================================================================================
    void Session::setInput (int firstDeviceChannel, bool stereo)
    {
        inputChannel = std::max (0, stereo ? (firstDeviceChannel & ~1) : firstDeviceChannel);
        inputStereo = stereo;
        applyInputRouting();
    }

    void Session::setMonitoring (bool shouldMonitor)
    {
        monitoring = shouldMonitor;
        applyInputRouting();
    }

    tracktion::WaveInputDevice* Session::findWaveInput() const
    {
        auto& dm = engine->getDeviceManager();

        for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
            if (auto* wi = dm.getWaveInDevice (i))
                if (wi->getChannels().containsDeviceChannel (inputChannel)
                     && wi->getChannels().getNumChannels() == (inputStereo ? 2 : 1))
                    return wi;

        return nullptr;
    }

    void Session::applyInputRouting()
    {
        if (projects.empty())
            return;   // starting up: the first project routes itself when it is added

        const juce::ScopedValueSetter<bool> guard (routing, true);
        auto& edit = front().edit;
        auto* track = front().track;
        auto& dm = engine->getDeviceManager();
        const auto wanted = inputStereo ? 2u : 1u;

        if (groupedChannels != wanted)
        {
            dm.setAllWaveInputsToNumChannels (wanted);
            groupedChannels = wanted;
        }

        callback->setRecordChannels (inputChannel, inputStereo ? 2 : 1);

        auto& transport = edit->getTransport();
        transport.ensureContextAllocated();

        auto* chosen = findWaveInput();

        for (auto* instance : edit->getAllInputDevices())
        {
            auto& device = instance->getInputDevice();

            if (device.getDeviceType() != tracktion::InputDevice::waveDevice)
                continue;

            const bool isChosen = &device == chosen;

            for (auto target : instance->getTargets())
                if (! isChosen || target != track->itemID)
                    [[maybe_unused]] auto r = instance->removeTarget (target, nullptr);

            if (isChosen)
            {
                device.setEnabled (true);
                device.setMonitorMode (monitoring ? tracktion::InputDevice::MonitorMode::on
                                                  : tracktion::InputDevice::MonitorMode::off);

                if (! instance->getTargets().contains (track->itemID))
                    [[maybe_unused]] auto r = instance->setTarget (track->itemID, true, nullptr, 0);
            }
        }

        edit->restartPlayback();
    }

    //==============================================================================================
    juce::Result Session::insertPlugin (const juce::PluginDescription& description)
    {
        const trace::Scope traced ("Load plugin");
        if (! plugins->isScanned (description))
            return juce::Result::fail ("This plugin has not been checked by the scanner yet");

        if (plugins->isBlocked (description.fileOrIdentifier))
            return juce::Result::fail ("This plugin is on the blocklist");

        removePlugin();

        auto plugin = front().edit->getPluginCache().createNewPlugin (tracktion::ExternalPlugin::xmlTypeName, description);

        if (plugin == nullptr)
        {
            log::error ("Plugin " + description.name + " (" + description.pluginFormatName + "): the engine could not create it");
            return juce::Result::fail ("The engine could not create " + description.name);
        }

        // Index 0: before the track's own volume/pan and meter, which are channel features.
        front().track->pluginList.insertPlugin (plugin, 0, nullptr);
        library->markUsed (description.createIdentifierString());

        if (auto* external = dynamic_cast<tracktion::ExternalPlugin*> (plugin.get()))
        {
            if (! external->getLoadError().isEmpty())
            {
                log::error ("Plugin " + description.name + " (" + description.pluginFormatName + "): " + external->getLoadError());
                return juce::Result::fail (external->getLoadError());
            }
        }

        log::info ("Plugin loaded: " + description.name + " " + description.version + " (" + description.pluginFormatName + ", "
                   + description.manufacturerName + ")");
        return juce::Result::ok();
    }

    tracktion::Plugin* Session::getInsertedPlugin() const
    {
        for (auto* p : front().track->pluginList)
            if (dynamic_cast<tracktion::ExternalPlugin*> (p) != nullptr)
                return p;

        return nullptr;
    }

    void Session::removePlugin()
    {
        while (auto* p = getInsertedPlugin())
            p->deleteFromParent();
    }

    //==============================================================================================
    juce::Result Session::startRecording()
    {
        if (isRecording())
            return juce::Result::fail ("Already recording");

        auto* device = getDeviceManager().getCurrentAudioDevice();

        if (device == nullptr)
            return juce::Result::fail ("No audio interface is open");

        prepareForDevice();

        ++takeNumber;
        const auto name = "Take " + juce::String (takeNumber);
        if (! front().recordings.createDirectory())
            return juce::Result::fail ("Carthage could not create " + front().recordings.getFullPathName());

        const auto file = front().recordings.getNonexistentChildFile (
            name + juce::Time::getCurrentTime().formatted (" %Y-%m-%d %H%M%S"), ".wav", false);

        TakeRecorder::Format format;
        format.sampleRate = device->getCurrentSampleRate();
        format.numChannels = inputStereo ? 2 : 1;
        format.sampleFormat = recordFormat;

        if (auto r = recorder.start (file, format); r.failed())
            return r;

        auto& transport = front().edit->getTransport();
        takeStartedMs = juce::Time::getMillisecondCounter();
        takeStartPosition = transport.getPosition().inSeconds();
        takeBlockSamples = device->getCurrentBufferSizeSamples();
        takeWatch = TakeWatch (takeBlockSamples / device->getCurrentSampleRate());

        if (! transport.isPlaying())
            transport.play (false);

        startTimer (watchIntervalMs);
        return juce::Result::ok();
    }

    std::optional<double> Session::currentStreamToEditOffset() const
    {
        auto* context = front().edit->getTransport().getCurrentPlaybackContext();
        auto* head = context != nullptr ? context->getNodePlayHead() : nullptr;

        if (head == nullptr || ! head->isPlaying())
            return std::nullopt;

        // Any stream time gives the same offset while the mapping is linear; use the current one.
        const auto stream = engine->getDeviceManager().getCurrentStreamTime();
        const auto offset = context->globalStreamTimeToEditTimeUnlooped (stream).inSeconds() - stream;

        if (! head->isPlaying())
            return std::nullopt;   // stopped while we read it

        return offset;
    }

    void Session::timerCallback()
    {
        if (! isRecording())
        {
            stopTimer();
            return;
        }

        const auto elapsed = static_cast<int> (juce::Time::getMillisecondCounter() - takeStartedMs);

        const auto now = currentStreamToEditOffset();

        if (takeWatch.update (now, elapsed) == TakeWatch::State::interrupted)
        {
            const auto result = stopRecording();

            if (onTakeEnded)
                onTakeEnded (result);
        }
    }

    TakeRecorder::Result Session::stopRecording()
    {
        stopTimer();
        auto result = recorder.stop();

        // Map the first captured sample to edit time with the offset seen while the take played.
        // If the take is stopped before the first check, use the live mapping; if playback never
        // ran, place it where it began.
        auto& transport = front().edit->getTransport();
        const bool interrupted = takeWatch.getState() == TakeWatch::State::interrupted;
        auto offset = takeWatch.getOffset();

        if (! offset && ! interrupted)
            offset = currentStreamToEditOffset();

        transport.stop (false, false);

        if (! result.succeeded() || result.framesWritten == 0)
            return result;

        const auto rate = preparedRate > 0 ? preparedRate : 48000.0;
        const auto alignment = getAlignmentSamples();
        const bool placedByGuess = ! offset.has_value();
        const auto startSeconds = placedByGuess
                                    ? takeStartPosition - alignment / rate
                                    : takeStartSeconds (result.firstBlockStreamTime, *offset, takeBlockSamples, alignment, rate);

        const auto length = static_cast<double> (result.framesWritten) / rate;
        tracktion::ClipPosition position;

        if (startSeconds >= 0.0)
        {
            position.time = { tracktion::TimePosition::fromSeconds (startSeconds), tracktion::TimeDuration::fromSeconds (length) };
        }
        else
        {
            // The take began before zero (latency at the very start): trim its head, keep it in time.
            const auto trimmed = -startSeconds;
            position.time = { tracktion::TimePosition(), tracktion::TimeDuration::fromSeconds (std::max (0.0, length - trimmed)) };
            position.offset = tracktion::TimeDuration::fromSeconds (trimmed);
        }

        const auto clipName = result.file.getFileNameWithoutExtension();
        editing::begin (*front().edit, "Record take");
        tracktion::insertWaveClip (*front().track, clipName, result.file, position, tracktion::DeleteExistingClips::yes);

        lastTake = TakeInfo { result, startSeconds, alignment, clipName, interrupted, placedByGuess };
        return result;
    }

    void Session::play()
    {
        front().edit->getTransport().play (false);
    }

    void Session::stop()
    {
        if (isRecording())
            stopRecording();
        else
            front().edit->getTransport().stop (false, false);
    }

    bool Session::isPlaying() const
    {
        return front().edit->getTransport().isPlaying();
    }

    void Session::returnToStart()
    {
        front().edit->getTransport().setPosition (tracktion::TimePosition());
    }

    double Session::getPositionSeconds() const
    {
        return front().edit->getTransport().getPosition().inSeconds();
    }

    //==============================================================================================
    Session::ReportedLatency Session::getReportedLatency() const
    {
        ReportedLatency l;

        if (auto* device = engine->getDeviceManager().deviceManager.getCurrentAudioDevice())
        {
            l.inputSamples = device->getInputLatencyInSamples();
            l.outputSamples = device->getOutputLatencyInSamples();
            l.bufferSize = device->getCurrentBufferSizeSamples();
            l.sampleRate = device->getCurrentSampleRate();
        }

        return l;
    }

    Session::LoopbackChannels Session::getLoopbackChannels() const
    {
        LoopbackChannels result;
        auto* device = engine->getDeviceManager().deviceManager.getCurrentAudioDevice();

        if (device == nullptr)
            return result;

        const auto outs = device->getOutputChannelNames().size();
        const auto ins = device->getInputChannelNames().size();
        auto& props = engine->getPropertyStorage().getPropertiesFile();
        const auto saved = juce::StringArray::fromTokens (props.getValue ("loopback " + device->getName()), ",", {});

        if (saved.size() == 2)
        {
            result = { saved[0].getIntValue(), saved[1].getIntValue() };
        }
        else
        {
            auto* listening = engine->getDeviceManager().getDefaultWaveOutDevice();
            result.output = 0;
            while (listening != nullptr && result.output < outs - 1 && listening->getChannels().containsDeviceChannel (result.output))
                ++result.output;

            result.input = 0;
            while (result.input < ins - 1 && (result.input == inputChannel || (inputStereo && result.input == inputChannel + 1)))
                ++result.input;
        }

        result.output = std::clamp (result.output, 0, std::max (0, outs - 1));
        result.input = std::clamp (result.input, 0, std::max (0, ins - 1));
        return result;
    }

    void Session::setLoopbackChannels (LoopbackChannels c)
    {
        if (auto* device = engine->getDeviceManager().deviceManager.getCurrentAudioDevice())
            engine->getPropertyStorage().getPropertiesFile().setValue ("loopback " + device->getName(),
                                                                     juce::String (c.output) + "," + juce::String (c.input));
    }

    int Session::getAlignmentSamples() const
    {
        return measuredRoundTrip.value_or (getReportedLatency().roundTripSamples());
    }

    //==============================================================================================
    // Projects
    Session::Project::Project (Session& s, std::unique_ptr<tracktion::Edit> e, const juce::File& f, project::Info i, bool isUntitled)
        : session (s), edit (std::move (e)), file (f), recordings (project::recordingsIn (f.getParentDirectory())),
          info (std::move (i)), untitled (isUntitled)
    {
    }

    Session::Project::~Project()
    {
        settleTimer.stopTimer();
        edit->getUndoManager().removeChangeListener (this);
    }

    bool Session::Project::isPristine() const
    {
        return untitled && ! recovered && ! namedChangeSinceAdopt && ! hasUnsavedChanges() && ! recordings.isDirectory();
    }

    void Session::Project::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        // Clips moved, trimmed or undone take their envelopes with them (4e).
        editing::syncEnvelopes (*edit);

        // An edit: autosave after the next pause (W30).
        ++changeCount;
        changedSinceAutosave = true;
        namedChangeSinceAdopt = namedChangeSinceAdopt || edit->getUndoManager().getUndoDescription().isNotEmpty();
        lastChange = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        session.projectStateChanged();
    }

    void Session::Project::settle()
    {
        settleTimer.stopTimer();

        if (namedChangeSinceAdopt)
            return;

        auto& um = edit->getUndoManager();
        um.removeChangeListener (this);
        um.clearUndoHistory();
        um.dispatchPendingMessages();
        um.addChangeListener (this);

        if (! recovered)
            edit->resetChangedStatus();

        changedSinceAutosave = false;
        session.projectStateChanged();
    }

    void Session::add (std::unique_ptr<tracktion::Edit> newEdit, const juce::File& file, project::Info newInfo, bool isUntitled, bool isNew)
    {
        jassert (newEdit != nullptr && ! isRecording());

        auto p = std::make_unique<Project> (*this, std::move (newEdit), file, std::move (newInfo), isUntitled);
        auto& edit = *p->edit;
        edit.playInStopEnabled = true;    // the graph runs while stopped, so the input is always heard

        // A new project has eight lanes, Lane 1 to Lane 8 (W38); one opened keeps the lanes it was
        // saved with, so a deleted lane stays deleted. The first lane records the input.
        const auto existing = isNew ? 0 : tracktion::getAudioTracks (edit).size();
        edit.ensureNumberOfAudioTracks (existing == 0 ? 8 : existing);
        const auto lanes = tracktion::getAudioTracks (edit);
        p->track = lanes.getFirst();

        for (int i = existing; i < lanes.size(); ++i)
            lanes[i]->setName ("Lane " + juce::String (i + 1));

        // A new project's metronome marks the first beat of the bar (the tick-tock's deeper tock).
        if (existing == 0)
            edit.clickTrackEmphasiseBars = true;

        // An untouched untitled tab gives its place to this one.
        int index = static_cast<int> (projects.size());

        if (frontIndex >= 0 && front().isPristine())
        {
            index = frontIndex;
            putToBack();

            if (autosaves)
                autosaves->markClosed (front().info.id);

            projects.erase (projects.begin() + index);
            frontIndex = -1;
        }
        else if (frontIndex >= 0)
        {
            putToBack();
            index = frontIndex + 1;
        }

        auto& added = **projects.insert (projects.begin() + index, std::move (p));
        bringForward (index);

        // Setting up is not an edit: nothing to undo, nothing unsaved.
        auto& um = added.edit->getUndoManager();
        um.clearUndoHistory();
        um.dispatchPendingMessages();
        added.edit->resetChangedStatus();
        um.addChangeListener (&added);

        // Tracktion registers the audio inputs with the new edit a moment later, as an unnamed
        // undoable step, and clears its changed flag after ~200 ms. Once that has happened, and if
        // the user has not edited yet, the step is dropped so Undo starts at the user's first edit.
        added.namedChangeSinceAdopt = false;
        added.settleTimer.startTimer (400);

        if (autosaves)
        {
            autosaves->markOpen (added.info.id, added.file);
            autosaveTimer.startTimer (500);
        }

        projectStateChanged();
    }

    void Session::putToBack()
    {
        if (frontIndex < 0)
            return;

        if (onProjectClosing)
            onProjectClosing();

        auto& p = front();
        p.info.view = currentView();   // it comes back to the same place

        // Its graph goes; its plugins stay loaded (W30), so coming back is quick.
        auto& transport = p.edit->getTransport();
        transport.stop (false, true);

        for (auto* instance : p.edit->getAllInputDevices())
            for (auto target : instance->getTargets())
                [[maybe_unused]] auto r = instance->removeTarget (target, nullptr);

        transport.freePlaybackContext();
        lastTake.reset();
    }

    void Session::bringForward (int index)
    {
        frontIndex = index;
        applyInputRouting();   // allocates its graph and gives it the input

        if (onProjectChanged)
            onProjectChanged();
    }

    Session::ProjectSummary Session::describeProject (int index) const
    {
        const auto& p = *projects[static_cast<size_t> (index)];
        return { p.file.getParentDirectory().getFileName(), p.file, p.hasUnsavedChanges(), p.untitled };
    }

    juce::Result Session::showProject (int index)
    {
        if (index < 0 || index >= getNumProjects())
            return juce::Result::fail ("There is no such project");

        if (index == frontIndex)
            return juce::Result::ok();

        if (isRecording())
            return juce::Result::fail ("Stop recording first");

        putToBack();
        bringForward (index);
        projectStateChanged();
        return juce::Result::ok();
    }

    void Session::closeProject()
    {
        if (getNumProjects() < 2 || isRecording())
        {
            jassertfalse;   // the app closes the window instead, or stops the take first
            return;
        }

        const auto index = frontIndex;
        putToBack();

        if (autosaves)
            autosaves->markClosed (front().info.id);

        projects.erase (projects.begin() + index);
        frontIndex = -1;
        bringForward (std::min (index, getNumProjects() - 1));
        projectStateChanged();
    }

    void Session::moveProject (int from, int to)
    {
        const auto n = getNumProjects();

        if (from < 0 || from >= n || to < 0 || to >= n || from == to)
            return;

        auto moving = std::move (projects[static_cast<size_t> (from)]);
        projects.erase (projects.begin() + from);
        projects.insert (projects.begin() + to, std::move (moving));

        if (frontIndex == from)
            frontIndex = to;
        else if (from < frontIndex && to >= frontIndex)
            --frontIndex;
        else if (from > frontIndex && to <= frontIndex)
            ++frontIndex;

        projectStateChanged();
    }

    void Session::projectStateChanged()
    {
        if (onProjectStateChanged)
            onProjectStateChanged();
    }

    juce::ValueTree Session::currentView() const
    {
        return getViewState ? getViewState() : juce::ValueTree();
    }

    juce::Result Session::newProject (const juce::File& folder, const juce::File& templateFile)
    {
        if (isRecording())
            return juce::Result::fail ("Stop recording first");

        const auto file = project::fileIn (folder);

        if (templateFile == juce::File())
        {
            add (project::createEdit (*engine, file), file, project::Info::createNew(), true, true);
            return juce::Result::ok();
        }

        // A template is a project opened as a new, untitled one: its own id, no file yet.
        auto opened = project::open (*engine, templateFile, file);

        if (opened.edit == nullptr)
            return juce::Result::fail (opened.error);

        auto info = project::Info::createNew();
        info.view = opened.info.view;
        add (std::move (opened.edit), file, std::move (info), true, false);
        return juce::Result::ok();
    }

    juce::Result Session::openProject (const juce::File& chosen)
    {
        const trace::Scope traced ("Open project");
        if (isRecording())
            return juce::Result::fail ("Stop recording first");

        const auto file = chosen.isDirectory() ? project::fileIn (chosen) : chosen;

        for (int i = 0; i < getNumProjects(); ++i)
            if (projects[static_cast<size_t> (i)]->file == file && ! projects[static_cast<size_t> (i)]->untitled)
                return showProject (i);

        auto opened = project::open (*engine, file, file);

        if (opened.edit == nullptr)
            return juce::Result::fail (opened.error);

        add (std::move (opened.edit), file, std::move (opened.info), false, false);
        noteRecent (file);
        return juce::Result::ok();
    }

    juce::Result Session::recover (const autosave::Store::Recovery& r)
    {
        if (isRecording())
            return juce::Result::fail ("Stop recording first");

        auto opened = project::open (*engine, r.autosave, r.projectFile);

        if (opened.edit == nullptr)
            return juce::Result::fail (opened.error);

        opened.info.id = r.projectId;   // its autosaves carry on in the same folder
        add (std::move (opened.edit), r.projectFile, std::move (opened.info), ! r.projectFile.existsAsFile(), false);
        front().recovered = true;
        projectStateChanged();
        return juce::Result::ok();
    }

    void Session::savedAt (Project& p, juce::uint64 changesWhenTaken)
    {
        // Edits made while the file was being written are still unsaved.
        p.edit->getUndoManager().dispatchPendingMessages();

        if (p.changeCount == changesWhenTaken)
            p.edit->resetChangedStatus();

        p.recovered = false;
        projectStateChanged();
    }

    void Session::save (const std::function<void (juce::Result)>& done)
    {
        auto& p = front();

        if (p.untitled)
        {
            if (done) done (juce::Result::fail ("This project has not been saved yet: choose where"));
            return;
        }

        p.info.view = currentView();
        auto snapshot = project::snapshot (*p.edit, p.file, p.info);
        const auto file = p.file;
        const auto taken = p.changeCount;
        auto* target = &p;

        const auto picture = takePicture ? takePicture() : juce::MemoryBlock();

        writer.post ([snapshot, file, picture]
                     {
                         project::keepVersion (file);   // the file this save replaces (W38), with its picture (W41)
                         auto r = project::write (snapshot, file);

                         if (r.wasOk() && picture.getSize() > 0)
                             project::previewOf (file).replaceWithData (picture.getData(), picture.getSize());

                         return r;
                     },
                     [this, target, file, taken, done] (const juce::Result& r)
                     {
                         if (r.wasOk() && isOpen (target) && file == target->file)
                         {
                             savedAt (*target, taken);
                             noteRecent (file);
                         }

                         if (done) done (r);
                     });
    }

    juce::Result Session::restoreVersion (const juce::File& version)
    {
        if (isRecording())
            return juce::Result::fail ("Stop recording first");

        auto& p = front();

        if (p.untitled || ! version.existsAsFile())
            return juce::Result::fail ("There is no such version");

        // Opened as if it were the project file, so its clips find their audio.
        auto opened = project::open (*engine, version, p.file);

        if (opened.edit == nullptr)
            return juce::Result::fail (opened.error);

        // Now kept: the project as it is, unsaved changes and all, with paths as the project file
        // has them. Written here, not on the writer: the tab is replaced next.
        p.info.view = currentView();
        const auto now = project::versionsIn (p.file.getParentDirectory())
                             .getChildFile (p.file.getFileNameWithoutExtension() + " " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S")
                                            + " before restoring" + project::extension)
                             .getNonexistentSibling();

        if (auto r = project::write (project::snapshot (*p.edit, p.file, p.info), now); r.failed())
            return r;

        if (takePicture)
            if (const auto picture = takePicture(); picture.getSize() > 0)
                project::pictureOfVersion (now).replaceWithData (picture.getData(), picture.getSize());

        auto info = opened.info;
        info.id = p.info.id;   // the same project
        info.view = p.info.view;
        const auto file = p.file;
        const auto index = frontIndex;

        putToBack();
        projects.erase (projects.begin() + index);
        frontIndex = -1;

        add (std::move (opened.edit), file, std::move (info), false, false);
        moveProject (frontIndex, index);
        front().recovered = true;   // unsaved: the file on disk is still the newer one
        projectStateChanged();
        return juce::Result::ok();
    }

    void Session::saveAs (const juce::File& folder, const std::function<void (juce::Result)>& done)
    {
        saveInto (folder, false, false, done);
    }

    juce::File Session::renamedFolder (const juce::String& newName) const
    {
        const auto name = juce::File::createLegalFileName (newName.trim()).trim();

        if (name.isEmpty() || name.startsWithChar ('.'))
            return {};

        return front().file.getParentDirectory().getSiblingFile (name);
    }

    void Session::renameProject (const juce::String& newName, const std::function<void (juce::Result)>& done)
    {
        const auto fail = [&done] (const juce::String& why) { if (done) done (juce::Result::fail (why)); };
        const auto folder = renamedFolder (newName);

        if (front().untitled)                          return fail ("Save the project first");
        if (folder == juce::File())                    return fail ("That name cannot be a folder name");
        if (folder == front().file.getParentDirectory()) return fail ("That is its name already");
        if (folder.exists())                           return fail ("A project named " + folder.getFileName() + " is there already");

        saveInto (folder, true, true, done);
    }

    void Session::saveInto (const juce::File& folder, bool keepIdentity, bool binOldFolder, const std::function<void (juce::Result)>& done)
    {
        if (isRecording())
        {
            if (done) done (juce::Result::fail ("Stop recording first"));
            return;
        }

        auto& p = front();
        const auto file = project::fileIn (folder);
        const auto oldFolder = p.file.getParentDirectory();
        const auto wasUntitled = p.untitled;

        // A saved project copied somewhere else is a second project: it gets its own id, so the
        // two never share autosaves. An untitled one keeps its id and its autosaves.
        auto newInfo = p.info;
        newInfo.view = currentView();

        if (! wasUntitled && ! keepIdentity)
            newInfo.id = project::Info::createNew().id;

        auto snapshot = project::snapshot (*p.edit, file, newInfo, oldFolder);
        const auto taken = p.changeCount;
        auto* target = &p;

        const auto oldFile = p.file;
        const auto picture = takePicture ? takePicture() : juce::MemoryBlock();

        writer.post ([snapshot, file, oldFile, oldFolder, folder, binOldFolder, picture]
                     {
                         // A rename carries everything in the folder; Save as, only the media.
                         if (binOldFolder)
                         {
                             if (! oldFolder.copyDirectoryTo (folder))
                                 return juce::Result::fail ("Could not copy the project to " + folder.getFullPathName());

                             folder.getChildFile (oldFile.getFileName()).deleteFile();   // written anew below, under the new name
                         }
                         else if (auto r = project::copyMedia (oldFolder, folder); r.failed())
                         {
                             return r;
                         }

                         auto r = project::write (snapshot, file);

                         if (r.wasOk() && picture.getSize() > 0)
                             project::previewOf (file).replaceWithData (picture.getData(), picture.getSize());

                         return r;
                     },
                     [this, target, file, oldFolder, wasUntitled, binOldFolder, newInfo, taken, done] (const juce::Result& r)
                     {
                         if (r.wasOk() && isOpen (target))
                         {
                             auto& q = *target;

                             // Carry on in the new folder: point the clips at the copies.
                             project::moveTo (*q.edit, file, oldFolder);

                             if (autosaves && newInfo.id != q.info.id)
                                 autosaves->markClosed (q.info.id);

                             q.file = file;
                             q.recordings = project::recordingsIn (file.getParentDirectory());
                             q.info = newInfo;
                             q.untitled = false;

                             if (autosaves)
                                 autosaves->markOpen (q.info.id, q.file);

                             // An untitled project's folder held only its recordings, now copied;
                             // a renamed one was copied whole. Either goes to the bin, where it
                             // can still be found.
                             if ((wasUntitled || binOldFolder) && oldFolder.isDirectory() && oldFolder != file.getParentDirectory())
                                 oldFolder.moveToTrash();

                             savedAt (q, taken);
                             noteRecent (file);
                         }

                         if (done) done (r);
                     });
    }

    void Session::collectSamples (const std::function<void (juce::Result, int)>& done)
    {
        auto& p = front();
        const auto folder = p.file.getParentDirectory();
        const auto samples = project::samplesIn (folder);

        struct Move { tracktion::EditItemID clip; juce::File from, to; };
        std::vector<Move> moves;
        juce::Array<juce::File> targets;

        for (auto* lane : editing::lanes (*p.edit))
        {
            for (auto* c : lane->getClips())
            {
                if (c == nullptr)
                    continue;

                auto* audio = dynamic_cast<tracktion::AudioClipBase*> (c);
                const auto from = audio != nullptr ? audio->getOriginalFile() : juce::File();

                if (! from.existsAsFile() || from.isAChildOf (folder))
                    continue;

                // The same file used by several clips is copied once.
                auto to = juce::File();

                for (const auto& m : moves)
                    if (m.from == from)
                        to = m.to;

                if (to == juce::File())
                {
                    to = project::collectTarget (samples, from, targets);
                    targets.add (to);
                }

                moves.push_back ({ c->itemID, from, to });
            }
        }

        if (moves.empty())
        {
            if (done) done (juce::Result::ok(), 0);
            return;
        }

        auto* target = &p;

        writer.post ([moves, samples]
                     {
                         if (! samples.createDirectory())
                             return juce::Result::fail ("Carthage could not create " + samples.getFullPathName());

                         for (const auto& m : moves)
                             if (! m.to.existsAsFile() && ! m.from.copyFileTo (m.to))
                                 return juce::Result::fail ("Carthage could not copy " + m.from.getFileName() + " into the project");

                         return juce::Result::ok();
                     },
                     [this, target, moves, done] (const juce::Result& r)
                     {
                         int copied = 0;

                         if (r.wasOk() && isOpen (target))
                         {
                             auto& edit = *target->edit;
                             editing::begin (edit, "Collect samples");
                             juce::Array<juce::File> counted;

                             for (const auto& m : moves)
                             {
                                 auto* audio = dynamic_cast<tracktion::AudioClipBase*> (tracktion::findClipForID (edit, m.clip));

                                 if (audio == nullptr || audio->getOriginalFile() != m.from)
                                     continue;   // removed or changed while the files were copied

                                 audio->getSourceFileReference().setToFile (m.to, tracktion::SourceFileReference::PathStyle::alwaysRelative, false);
                                 audio->sourceMediaChanged();
                                 counted.addIfNotAlreadyThere (m.to);
                             }

                             copied = counted.size();
                             projectStateChanged();
                         }

                         if (done) done (r, copied);
                     });
    }

    bool Session::getAlwaysCopySamples() const
    {
        return engine->getPropertyStorage().getPropertiesFile().getBoolValue ("alwaysCopySamples", false);
    }

    void Session::setAlwaysCopySamples (bool shouldCopy)
    {
        engine->getPropertyStorage().getPropertiesFile().setValue ("alwaysCopySamples", shouldCopy);
    }

    void Session::saveCopy (const juce::File& folder, const std::function<void (juce::Result)>& done)
    {
        auto& p = front();
        const auto file = project::fileIn (folder);
        const auto oldFolder = p.file.getParentDirectory();
        auto copyInfo = p.info;
        copyInfo.id = project::Info::createNew().id;   // a copy is a project of its own
        copyInfo.view = currentView();
        auto snapshot = project::snapshot (*p.edit, file, copyInfo, oldFolder);

        writer.post ([snapshot, file, oldFolder, folder]
                     {
                         if (auto r = project::copyMedia (oldFolder, folder); r.failed())
                             return r;

                         return project::write (snapshot, file);
                     },
                     [done] (juce::Result r) { if (done) done (std::move(r)); });
    }

    juce::Array<juce::File> Session::getRecentProjects() const
    {
        juce::Array<juce::File> files;

        for (const auto& path : juce::StringArray::fromLines (engine->getPropertyStorage().getPropertiesFile().getValue ("recentProjects")))
            if (const juce::File f (path); path.isNotEmpty() && f.existsAsFile())
                files.add (f);

        return files;
    }

    void Session::noteRecent (const juce::File& file)
    {
        auto& props = engine->getPropertyStorage().getPropertiesFile();
        auto list = juce::StringArray::fromLines (props.getValue ("recentProjects"));
        list.removeString (file.getFullPathName());
        list.removeEmptyStrings();
        list.insert (0, file.getFullPathName());

        while (list.size() > 12)
            list.remove (list.size() - 1);

        props.setValue ("recentProjects", list.joinIntoString ("\n"));
    }

    void Session::forgetRecentProject (const juce::File& file)
    {
        auto& props = engine->getPropertyStorage().getPropertiesFile();
        auto list = juce::StringArray::fromLines (props.getValue ("recentProjects"));
        list.removeString (file.getFullPathName());
        props.setValue ("recentProjects", list.joinIntoString ("\n"));
    }

    Session::AtLaunch Session::getAtLaunch() const
    {
        auto& props = engine->getPropertyStorage().getPropertiesFile();
        const auto value = props.getValue ("atLaunch");

        if (value == "last")  return AtLaunch::lastSession;
        if (value == "empty") return AtLaunch::emptyProject;
        if (value == "start") return AtLaunch::startScreen;

        return props.getBoolValue ("showStartScreen", true) ? AtLaunch::startScreen : AtLaunch::emptyProject;
    }

    void Session::setAtLaunch (AtLaunch a)
    {
        engine->getPropertyStorage().getPropertiesFile().setValue ("atLaunch", a == AtLaunch::lastSession ? "last"
                                                                             : a == AtLaunch::emptyProject ? "empty" : "start");
    }

    bool Session::getFitLoopsToTempo() const
    {
        return engine->getPropertyStorage().getPropertiesFile().getBoolValue ("fitLoopsToTempo", false);
    }

    void Session::setFitLoopsToTempo (bool fit)
    {
        engine->getPropertyStorage().getPropertiesFile().setValue ("fitLoopsToTempo", fit);
    }

    juce::String Session::describeAudio()
    {
        auto& adm = getDeviceManager();
        auto* device = adm.getCurrentAudioDevice();

        if (device == nullptr)
            return "no audio device open";

        return device->getTypeName() + ", " + device->getName() + ", " + juce::String (device->getCurrentSampleRate(), 0) + " Hz, "
               + juce::String (device->getCurrentBufferSizeSamples()) + " samples, latency in "
               + juce::String (device->getInputLatencyInSamples()) + " / out " + juce::String (device->getOutputLatencyInSamples()) + " samples";
    }

    bool Session::isMetronomeOn() const     { return front().edit->clickTrackEnabled.get(); }

    void Session::setMetronomeOn (bool on)
    {
        front().edit->clickTrackEnabled = on;
    }

    metronome::Sound Session::getMetronomeSound() const
    {
        return static_cast<metronome::Sound> (juce::jlimit (0, 3, engine->getPropertyStorage().getPropertiesFile().getIntValue ("metronomeSound", 0)));
    }

    juce::File Session::getMetronomeCustomFile() const
    {
        return juce::File (engine->getPropertyStorage().getPropertiesFile().getValue ("metronomeFile"));
    }

    juce::Result Session::setMetronomeSound (metronome::Sound sound, const juce::File& customFile)
    {
        auto r = metronome::use (*engine, sound, customFile, folders.settings.getChildFile ("Metronome"));

        if (r.wasOk())
        {
            auto& props = engine->getPropertyStorage().getPropertiesFile();
            props.setValue ("metronomeSound", static_cast<int> (sound));
            props.setValue ("metronomeFile", sound == metronome::Sound::custom ? customFile.getFullPathName() : juce::String());

            // The click track reads its sounds when playback is prepared: prepare it again, but
            // never under a take (the new sound is then heard from the next start).
            if (! isRecording())
                for (auto& p : projects)
                    if (p->edit != nullptr)
                        p->edit->restartPlayback();
        }

        return r;
    }

    int Session::getBeatShading() const      { return juce::jlimit (0, 4, engine->getPropertyStorage().getPropertiesFile().getIntValue ("beatShading", 0)); }
    void Session::setBeatShading (int steps) { engine->getPropertyStorage().getPropertiesFile().setValue ("beatShading", juce::jlimit (0, 4, steps)); }

    int Session::getParallelChecks() const
    {
        return juce::jlimit (1, 16, engine->getPropertyStorage().getPropertiesFile().getIntValue ("parallelChecks", 4));
    }

    void Session::setParallelChecks (int n)
    {
        engine->getPropertyStorage().getPropertiesFile().setValue ("parallelChecks", juce::jlimit (1, 16, n));
        pluginScanner->maxParallel = getParallelChecks();
    }

    bool Session::takeFirstSettingsTest()
    {
        auto& props = engine->getPropertyStorage().getPropertiesFile();

        if (props.getBoolValue ("settingsTestOffered", false))
            return false;

        props.setValue ("settingsTestOffered", true);
        return true;
    }

    bool Session::getCheckPluginsAtLaunch() const    { return engine->getPropertyStorage().getPropertiesFile().getBoolValue ("checkPluginsAtLaunch", false); }
    void Session::setCheckPluginsAtLaunch (bool on)  { engine->getPropertyStorage().getPropertiesFile().setValue ("checkPluginsAtLaunch", on); }

    bool Session::takeFirstPluginCheck()
    {
        auto& props = engine->getPropertyStorage().getPropertiesFile();

        if (props.getBoolValue ("pluginsCheckedOnce", false))
            return false;

        props.setValue ("pluginsCheckedOnce", true);
        return true;
    }

    bool Session::getClickPlacesClip() const     { return engine->getPropertyStorage().getPropertiesFile().getBoolValue ("clickPlacesClip", false); }
    void Session::setClickPlacesClip (bool on)   { engine->getPropertyStorage().getPropertiesFile().setValue ("clickPlacesClip", on); }
    bool Session::getRightClickDeletes() const   { return engine->getPropertyStorage().getPropertiesFile().getBoolValue ("rightClickDeletes", false); }
    void Session::setRightClickDeletes (bool on) { engine->getPropertyStorage().getPropertiesFile().setValue ("rightClickDeletes", on); }

    std::optional<editing::TempoFollow> Session::getTempoFollow() const
    {
        const auto v = engine->getPropertyStorage().getPropertiesFile().getIntValue ("tempoFollow", 0);

        if (v < 1 || v > 3)
            return std::nullopt;

        return static_cast<editing::TempoFollow> (v - 1);
    }

    void Session::setTempoFollow (std::optional<editing::TempoFollow> how)
    {
        engine->getPropertyStorage().getPropertiesFile().setValue ("tempoFollow", how ? 1 + static_cast<int> (*how) : 0);
    }

    bool Session::isOpen (const Project* p) const
    {
        return std::any_of (projects.begin(), projects.end(), [p] (const auto& q) { return q.get() == p; });
    }

    void Session::autosaveIfDue()
    {
        if (! autosaves)
            return;

        const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;

        for (auto& owned : projects)
        {
            auto& p = *owned;

            if (! autosavePolicy.isDue (now, p.changedSinceAutosave, p.lastChange, p.lastAutosave))
                continue;

            p.changedSinceAutosave = false;

            // Tracktion's own updates right after loading reach the undo manager too; they are
            // not the user's edits, and Tracktion clears its changed flag after them.
            if (! p.hasUnsavedChanges())
                continue;

            p.lastAutosave = now;

            if (&p == &front())
                p.info.view = currentView();

            auto snapshot = project::snapshot (*p.edit, p.file, p.info);
            const auto target = autosaves->nextFile (p.info.id);
            const auto id = p.info.id;
            const auto keep = autosavePolicy.keep;
            const auto store = *autosaves;

            writer.post ([snapshot, target, store, id, keep]
            {
                auto r = project::write (snapshot, target);
                store.prune (id, keep);
                return r;
            });
        }
    }

    void Session::close()
    {
        autosaveTimer.stopTimer();
        writer.waitUntilIdle();

        if (autosaves)
            for (auto& p : projects)
                autosaves->markClosed (p->info.id);
    }
}
