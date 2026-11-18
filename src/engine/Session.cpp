#include "Session.h"
#include "Editing.h"

#include <tracktion_graph/tracktion_graph.h>

#include <algorithm>
#include <cmath>

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
        plugins = std::make_unique<PluginCatalogue> (pm.knownPluginList, pm.pluginFormatManager, folders.pluginCache);
        loadGate = plugins.get();

        // The browser's list, kept in step with the catalogue. Nothing scans until the app asks
        // (tools and tests never scan by themselves).
        library = std::make_unique<PluginLibrary> (folders.pluginCache);
        library->setPlugins (plugins->getScanned());
        pluginScanner = std::make_unique<PluginScanner> (*plugins, scanner);
        pluginScanner->onChange = [this]
        {
            library->setPlugins (plugins->getScanned());

            if (onPluginsChanged)
                onPluginsChanged();
        };

        // Engine render threads join the device's audio workgroup (F8). Must be set before the
        // playback context is created.
        tracktion::EditPlaybackContext::enableAudioWorkgroup (true);

        auto& dm = engine->getDeviceManager();
        dm.initialise (tracktion::DeviceManager::defaultNumChannelsToOpen, tracktion::DeviceManager::defaultNumChannelsToOpen);

        // The engine's first MIDI device scan runs on a 4-second timer and always rebuilds the
        // device list, which stops playback: a take started in those first seconds would be cut
        // short. Run it now, while nothing plays. Later scans only rebuild on a hardware change.
        dm.rescanMidiDeviceList();

        callback = std::make_unique<AudioCallback> (*engine, recorder, probe);
        callback->attach();

        dm.addChangeListener (this);
        prepareForDevice();

        const auto file = project::fileIn (folders.project);
        adopt (project::createEdit (*engine, file), file, project::Info::createNew(), true, true);
    }

    Session::~Session()
    {
        autosaveTimer.stopTimer();
        settleTimer.stopTimer();
        writer.waitUntilIdle();
        pluginScanner.reset();   // stops a scan and waits for its scanners before anything else goes
        engine->getDeviceManager().removeChangeListener (this);

        if (isRecording())
            recorder.stop();

        edit->getTransport().stop (false, true);
        edit->getUndoManager().removeChangeListener (this);
        callback->detach();
        callback.reset();
        track = nullptr;
        edit.reset();
        loadGate = nullptr;
        engine.reset();
    }

    //==============================================================================================
    void Session::changeListenerCallback (juce::ChangeBroadcaster* source)
    {
        if (edit != nullptr && source == &edit->getUndoManager())
        {
            // An edit: autosave after the next pause (W30).
            ++changeCount;
            changedSinceAutosave = true;
            namedChangeSinceAdopt = namedChangeSinceAdopt || edit->getUndoManager().getUndoDescription().isNotEmpty();
            lastChange = juce::Time::getMillisecondCounterHiRes() / 1000.0;
            projectStateChanged();
            return;
        }

        if (routing)
            return;   // our own re-routing makes the engine broadcast; nothing new to do

        prepareForDevice();
        applyInputRouting();

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
        const juce::ScopedValueSetter<bool> guard (routing, true);
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
        if (! plugins->isScanned (description))
            return juce::Result::fail ("This plugin has not been checked by the scanner yet");

        if (plugins->isBlocked (description.fileOrIdentifier))
            return juce::Result::fail ("This plugin is on the blocklist");

        removePlugin();

        auto plugin = edit->getPluginCache().createNewPlugin (tracktion::ExternalPlugin::xmlTypeName, description);

        if (plugin == nullptr)
            return juce::Result::fail ("The engine could not create " + description.name);

        // Index 0: before the track's own volume/pan and meter, which are channel features.
        track->pluginList.insertPlugin (plugin, 0, nullptr);
        library->markUsed (description.createIdentifierString());

        if (auto* external = dynamic_cast<tracktion::ExternalPlugin*> (plugin.get()))
            if (! external->getLoadError().isEmpty())
                return juce::Result::fail (external->getLoadError());

        return juce::Result::ok();
    }

    tracktion::Plugin* Session::getInsertedPlugin() const
    {
        for (auto* p : track->pluginList)
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
        if (! recordings.createDirectory())
            return juce::Result::fail ("Carthage could not create " + recordings.getFullPathName());

        const auto file = recordings.getNonexistentChildFile (
            name + juce::Time::getCurrentTime().formatted (" %Y-%m-%d %H%M%S"), ".wav", false);

        TakeRecorder::Format format;
        format.sampleRate = device->getCurrentSampleRate();
        format.numChannels = inputStereo ? 2 : 1;
        format.sampleFormat = recordFormat;

        if (auto r = recorder.start (file, format); r.failed())
            return r;

        auto& transport = edit->getTransport();
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
        auto* context = edit->getTransport().getCurrentPlaybackContext();
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
        auto& transport = edit->getTransport();
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
        editing::begin (*edit, "Record take");
        tracktion::insertWaveClip (*track, clipName, result.file, position, tracktion::DeleteExistingClips::yes);

        lastTake = TakeInfo { result, startSeconds, alignment, clipName, interrupted, placedByGuess };
        return result;
    }

    void Session::play()
    {
        edit->getTransport().play (false);
    }

    void Session::stop()
    {
        if (isRecording())
            stopRecording();
        else
            edit->getTransport().stop (false, false);
    }

    bool Session::isPlaying() const
    {
        return edit->getTransport().isPlaying();
    }

    void Session::returnToStart()
    {
        edit->getTransport().setPosition (tracktion::TimePosition());
    }

    double Session::getPositionSeconds() const
    {
        return edit->getTransport().getPosition().inSeconds();
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
    void Session::adopt (std::unique_ptr<tracktion::Edit> newEdit, const juce::File& file, project::Info newInfo, bool isUntitled, bool isNew)
    {
        jassert (newEdit != nullptr && ! isRecording());

        if (edit != nullptr)
        {
            if (onProjectClosing)
                onProjectClosing();

            edit->getTransport().stop (false, true);
            edit->getUndoManager().removeChangeListener (this);

            if (autosaves)
                autosaves->markClosed (info.id);
        }

        track = nullptr;
        edit = std::move (newEdit);   // the old edit goes here, after the new one exists
        projectFile = file;
        recordings = project::recordingsIn (file.getParentDirectory());
        info = std::move (newInfo);
        untitled = isUntitled;
        recovered = false;
        lastTake.reset();

        edit->playInStopEnabled = true;    // the graph runs while stopped, so the input is always heard

        // Eight lanes; the first holds the guitar input and its takes.
        const auto existing = isNew ? 0 : tracktion::getAudioTracks (*edit).size();
        edit->ensureNumberOfAudioTracks (8);
        const auto lanes = tracktion::getAudioTracks (*edit);
        track = lanes.getFirst();

        if (existing == 0)
            track->setName ("Guitar");

        for (int i = std::max (1, existing); i < lanes.size(); ++i)
            lanes[i]->setName ("Lane " + juce::String (i + 1));

        applyInputRouting();

        // Setting up is not an edit: nothing to undo, nothing unsaved.
        auto& um = edit->getUndoManager();
        um.clearUndoHistory();
        um.dispatchPendingMessages();
        edit->resetChangedStatus();
        um.addChangeListener (this);

        // Tracktion registers the audio inputs with the new edit a moment later, as an unnamed
        // undoable step, and clears its changed flag after ~200 ms. Once that has happened, and if
        // the user has not edited yet, the step is dropped so Undo starts at the user's first edit.
        namedChangeSinceAdopt = false;
        settleTimer.startTimer (400);

        changedSinceAutosave = false;
        lastAutosave.reset();

        if (autosaves)
        {
            autosaves->markOpen (info.id, projectFile);
            autosaveTimer.startTimer (500);
        }

        if (onProjectChanged)
            onProjectChanged();

        projectStateChanged();
    }

    void Session::settle()
    {
        settleTimer.stopTimer();

        if (edit == nullptr || namedChangeSinceAdopt)
            return;

        auto& um = edit->getUndoManager();
        um.removeChangeListener (this);
        um.clearUndoHistory();
        um.dispatchPendingMessages();
        um.addChangeListener (this);

        if (! recovered)
            edit->resetChangedStatus();

        changedSinceAutosave = false;
        projectStateChanged();
    }

    void Session::projectStateChanged()
    {
        if (onProjectStateChanged)
            onProjectStateChanged();
    }

    bool Session::hasUnsavedChanges() const
    {
        return recovered || edit->hasChangedSinceSaved();
    }

    juce::ValueTree Session::currentView() const
    {
        return getViewState ? getViewState() : juce::ValueTree();
    }

    juce::Result Session::newProject (const juce::File& folder)
    {
        if (isRecording())
            return juce::Result::fail ("Stop recording first");

        const auto file = project::fileIn (folder);
        adopt (project::createEdit (*engine, file), file, project::Info::createNew(), true, true);
        return juce::Result::ok();
    }

    juce::Result Session::openProject (const juce::File& chosen)
    {
        if (isRecording())
            return juce::Result::fail ("Stop recording first");

        const auto file = chosen.isDirectory() ? project::fileIn (chosen) : chosen;
        auto opened = project::open (*engine, file, file);

        if (opened.edit == nullptr)
            return juce::Result::fail (opened.error);

        adopt (std::move (opened.edit), file, std::move (opened.info), false, false);
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
        adopt (std::move (opened.edit), r.projectFile, std::move (opened.info), ! r.projectFile.existsAsFile(), false);
        recovered = true;
        projectStateChanged();
        return juce::Result::ok();
    }

    void Session::savedAt (const juce::File& file, juce::uint64 changesWhenTaken)
    {
        // Edits made while the file was being written are still unsaved.
        edit->getUndoManager().dispatchPendingMessages();

        if (changeCount == changesWhenTaken)
            edit->resetChangedStatus();

        recovered = false;
        juce::ignoreUnused (file);
        projectStateChanged();
    }

    void Session::save (std::function<void (juce::Result)> done)
    {
        if (untitled)
        {
            if (done) done (juce::Result::fail ("This project has not been saved yet: choose where"));
            return;
        }

        info.view = currentView();
        auto snapshot = project::snapshot (*edit, projectFile, info);
        const auto file = projectFile;
        const auto taken = changeCount;

        writer.post ([snapshot, file] { return project::write (snapshot, file); },
                     [this, file, taken, done] (juce::Result r)
                     {
                         if (r.wasOk() && file == projectFile)
                             savedAt (file, taken);

                         if (done) done (r);
                     });
    }

    void Session::saveAs (const juce::File& folder, std::function<void (juce::Result)> done)
    {
        if (isRecording())
        {
            if (done) done (juce::Result::fail ("Stop recording first"));
            return;
        }

        const auto file = project::fileIn (folder);
        const auto oldFolder = getProjectFolder();
        const auto wasUntitled = untitled;

        // A saved project copied somewhere else is a second project: it gets its own id, so the
        // two never share autosaves. An untitled one keeps its id and its autosaves.
        auto newInfo = info;
        newInfo.view = currentView();

        if (! wasUntitled)
            newInfo.id = project::Info::createNew().id;

        auto snapshot = project::snapshot (*edit, file, newInfo, oldFolder);
        const auto taken = changeCount;

        writer.post ([snapshot, file, oldFolder, folder]
                     {
                         if (auto r = project::copyRecordings (oldFolder, folder); r.failed())
                             return r;

                         return project::write (snapshot, file);
                     },
                     [this, file, oldFolder, wasUntitled, newInfo, taken, done] (juce::Result r)
                     {
                         if (r.wasOk())
                         {
                             // Carry on in the new folder: point the clips at the copies.
                             project::moveTo (*edit, file, oldFolder);

                             if (autosaves && newInfo.id != info.id)
                                 autosaves->markClosed (info.id);

                             projectFile = file;
                             recordings = project::recordingsIn (file.getParentDirectory());
                             info = newInfo;
                             untitled = false;

                             if (autosaves)
                                 autosaves->markOpen (info.id, projectFile);

                             // An untitled project's folder held only its recordings, now copied:
                             // it goes to the bin, where it can still be found.
                             if (wasUntitled && oldFolder.isDirectory() && oldFolder != file.getParentDirectory())
                                 oldFolder.moveToTrash();

                             savedAt (file, taken);
                         }

                         if (done) done (r);
                     });
    }

    void Session::autosaveIfDue()
    {
        if (! autosaves)
            return;

        const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;

        if (! autosavePolicy.isDue (now, changedSinceAutosave, lastChange, lastAutosave))
            return;

        changedSinceAutosave = false;

        // Tracktion's own updates right after loading reach the undo manager too; they are not
        // the user's edits, and Tracktion clears its changed flag after them.
        if (! hasUnsavedChanges())
            return;

        lastAutosave = now;
        info.view = currentView();
        auto snapshot = project::snapshot (*edit, projectFile, info);
        const auto target = autosaves->nextFile (info.id);
        const auto id = info.id;
        const auto keep = autosavePolicy.keep;
        const auto store = *autosaves;

        writer.post ([snapshot, target, store, id, keep]
        {
            auto r = project::write (snapshot, target);
            store.prune (id, keep);
            return r;
        });
    }

    void Session::close()
    {
        autosaveTimer.stopTimer();
        writer.waitUntilIdle();

        if (autosaves)
            autosaves->markClosed (info.id);
    }
}
