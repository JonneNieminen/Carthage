#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include <functional>
#include <vector>

namespace carthage::engine
{
    class Session;

    /** The settings test (W41): at the first launch and from Settings, Carthage tries each buffer
        size the interface offers, smallest first, for a few seconds each, silently (the input
        is not heard while it runs), and counts dropouts and the audio thread's worst block.
        It then recommends the smallest buffer that ran clean with room to spare (but not below a
        floor set by the processor's cores: 256 up to 4, 128 up to 8, else 64), a sample rate,
        and how many plugins to check at once. Nothing changes until the person says Yes.

        Message thread; it drives the device through the session's device manager and puts the
        setup back as it was when it ends or is cancelled. */
    class SettingsTest final : private juce::Timer
    {
    public:
        explicit SettingsTest (Session&);
        ~SettingsTest() override;

        struct Step
        {
            int bufferSize = 0;
            int dropouts = 0;
            float worstLoad = 0.0f;   ///< worst block: its time over its duration
            bool clean = false;
        };

        struct Recommendation
        {
            bool valid = false;            ///< false: no interface, or nothing ran clean
            int bufferSize = 0;
            double sampleRate = 0.0;
            int parallelChecks = 1;
            std::vector<Step> steps;
            juce::String reason;           ///< why not valid, or what was measured
        };

        /** Seconds per buffer size, and the worst block's load a size may reach and still count
            as clean: plugins and a full project need the rest. */
        static constexpr double secondsPerStep = 3.0;
        static constexpr float headroomLoad = 0.25f;

        /** Starts; `progress` gets a line per step, `done` the result (also when cancelled). */
        void start (std::function<void (const juce::String&)> progress, std::function<void (Recommendation)> done);
        void cancel();
        bool isRunning() const noexcept   { return running; }

        /** The choice from measured steps, kept apart so it can be tested without a device. */
        static Recommendation recommend (std::vector<Step> steps, double currentRate, int cores);

    private:
        Session& session;
        bool running = false, wasMonitoring = false;
        juce::AudioDeviceManager::AudioDeviceSetup original;
        juce::Array<int> sizes;
        int index = 0, xrunsAtStart = 0, lateAtStart = 0;
        double stepStarted = 0.0, rate = 0.0;
        std::vector<Step> steps;
        std::function<void (const juce::String&)> onProgress;
        std::function<void (Recommendation)> onDone;

        void beginStep();
        void finish (bool cancelled);
        void timerCallback() override;
    };
}
