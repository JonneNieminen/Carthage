#include "Session.h"

#include <tracktion_graph/tracktion_graph.h>

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
        folders.project.createDirectory();
        folders.recordings.createDirectory();

        engine = std::make_unique<tracktion::Engine> (std::make_unique<Storage> (folders.settings),
                                                      std::make_unique<tracktion::UIBehaviour>(),
                                                      std::make_unique<Behaviour> (loadGate));

        auto& pm = engine->getPluginManager();
        plugins = std::make_unique<PluginCatalogue> (pm.knownPluginList, pm.pluginFormatManager, folders.pluginCache);
        loadGate = plugins.get();

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

        edit = tracktion::createEmptyEdit (*engine, folders.project.getChildFile ("Session.tracktionedit"));
        edit->playInStopEnabled = true;    // the graph runs while stopped, so the input is always heard
        edit->ensureNumberOfAudioTracks (1);
        track = tracktion::getAudioTracks (*edit).getFirst();
        track->setName ("Guitar");

        dm.addChangeListener (this);
        prepareForDevice();
        applyInputRouting();
    }

    Session::~Session()
    {
        engine->getDeviceManager().removeChangeListener (this);

        if (isRecording())
            recorder.stop();

        edit->getTransport().stop (false, true);
        callback->detach();
        callback.reset();
        track = nullptr;
        edit.reset();
        loadGate = nullptr;
        engine.reset();
    }

    //==============================================================================================
    void Session::changeListenerCallback (juce::ChangeBroadcaster*)
    {
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
        const auto file = folders.recordings.getNonexistentChildFile (
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

    int Session::getAlignmentSamples() const
    {
        return measuredRoundTrip.value_or (getReportedLatency().roundTripSamples());
    }
}
