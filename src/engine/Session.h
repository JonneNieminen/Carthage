#pragma once

#include "AudioCallback.h"
#include "LatencyProbe.h"
#include "PluginScan.h"
#include "TakeRecorder.h"

#include <tracktion_engine/tracktion_engine.h>

#include <optional>

namespace carthage::engine
{
    /** Folders the engine writes to. The app's path provider decides them (AGENTS.md: "Paths to
        user folders come from one place in app/"); the engine only receives them. */
    struct Folders
    {
        juce::File settings;      ///< Audio device settings (the engine's property storage)
        juce::File pluginCache;   ///< Known plugins and the blocklist
        juce::File project;       ///< The current project folder
        juce::File recordings;    ///< Where takes are written; inside the project folder
    };

    /** Phase 1's engine: one audio interface, one input, one plugin, one lane of takes.

        It owns the Tracktion engine and an edit with a single audio track. The chosen input is
        monitored live through the track's plugin, in-process (F8). Takes are recorded by
        TakeRecorder, not by the engine's own recorder, because the engine's path takes locks and
        allocates on the audio thread (docs/ARCHITECTURE.md, "Recording"). A finished take becomes
        a clip on the track at the position it was played, corrected for the interface's latency.

        All methods: message thread.
    */
    class Session final : private juce::ChangeListener
    {
    public:
        Session (Folders, juce::File scannerExecutable);
        ~Session() override;

        tracktion::Engine& getEngine() noexcept                 { return *engine; }
        juce::AudioDeviceManager& getDeviceManager() noexcept   { return engine->getDeviceManager().deviceManager; }
        tracktion::Edit& getEdit() noexcept                     { return *edit; }
        tracktion::AudioTrack& getTrack() noexcept              { return *track; }
        AudioCallback& getCallback() noexcept                   { return *callback; }
        TakeRecorder& getRecorder() noexcept                    { return recorder; }
        LatencyProbe& getProbe() noexcept                       { return probe; }
        PluginCatalogue& getPlugins() noexcept                  { return *plugins; }
        const Folders& getFolders() const noexcept              { return folders; }
        const juce::File& getScanner() const noexcept           { return scanner; }

        //==========================================================================================
        // Input
        void setInput (int firstDeviceChannel, bool stereo);
        int getInputChannel() const noexcept        { return inputChannel; }
        bool isInputStereo() const noexcept         { return inputStereo; }
        void setMonitoring (bool);
        bool isMonitoring() const noexcept          { return monitoring; }

        //==========================================================================================
        // The insert. Only descriptions the out-of-process scanner confirmed are accepted.
        juce::Result insertPlugin (const juce::PluginDescription&);
        tracktion::Plugin* getInsertedPlugin() const;
        void removePlugin();

        //==========================================================================================
        // Transport and takes
        void setRecordFormat (TakeRecorder::SampleFormat f) noexcept    { recordFormat = f; }
        TakeRecorder::SampleFormat getRecordFormat() const noexcept    { return recordFormat; }

        juce::Result startRecording();
        TakeRecorder::Result stopRecording();
        bool isRecording() const noexcept       { return recorder.isRecording(); }

        void play();
        void stop();
        bool isPlaying() const;
        void returnToStart();
        double getPositionSeconds() const;

        /** Where the last take landed, for reporting: its file, its position, the correction used. */
        struct TakeInfo
        {
            TakeRecorder::Result recording;
            double startSeconds = 0.0;
            int alignmentSamples = 0;
            juce::String clipName;
        };
        const std::optional<TakeInfo>& getLastTake() const noexcept     { return lastTake; }

        //==========================================================================================
        // Latency
        struct ReportedLatency
        {
            int inputSamples = 0, outputSamples = 0, bufferSize = 0;
            double sampleRate = 0.0;

            int roundTripSamples() const noexcept   { return inputSamples + outputSamples; }
            double roundTripMs() const noexcept     { return sampleRate > 0 ? roundTripSamples() * 1000.0 / sampleRate : 0.0; }
        };

        /** What the interface's driver says, which is what the app displays. */
        ReportedLatency getReportedLatency() const;

        /** Takes are shifted earlier by the round trip. By default that is the driver's figure; a
            loopback measurement can replace it. */
        void setMeasuredRoundTrip (std::optional<int> samples) noexcept    { measuredRoundTrip = samples; }
        std::optional<int> getMeasuredRoundTrip() const noexcept           { return measuredRoundTrip; }
        int getAlignmentSamples() const;

        /** Called after the device changed (rate, buffer, channels). */
        std::function<void()> onDeviceChanged;

    private:
        Folders folders;
        juce::File scanner;
        PluginCatalogue* loadGate = nullptr;   // read by the engine's plugin load gate
        std::unique_ptr<PluginCatalogue> plugins;
        std::unique_ptr<tracktion::Engine> engine;
        TakeRecorder recorder;
        LatencyProbe probe;
        std::unique_ptr<AudioCallback> callback;
        std::unique_ptr<tracktion::Edit> edit;
        tracktion::AudioTrack* track = nullptr;

        int inputChannel = 0;
        bool inputStereo = false, monitoring = true;
        TakeRecorder::SampleFormat recordFormat = TakeRecorder::SampleFormat::int24;
        std::optional<int> measuredRoundTrip;
        std::optional<TakeInfo> lastTake;
        int takeNumber = 0;
        double preparedRate = 0.0;
        int preparedInputs = -1;
        unsigned groupedChannels = 0;
        bool routing = false;

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void prepareForDevice();
        void applyInputRouting();
        tracktion::WaveInputDevice* findWaveInput() const;
    };
}
