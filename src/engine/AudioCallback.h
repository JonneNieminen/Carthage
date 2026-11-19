#pragma once

#include "LatencyProbe.h"
#include "RealtimeCheck.h"
#include "SamplePreview.h"
#include "TakeRecorder.h"

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <atomic>

namespace carthage::engine
{
    /** The one callback Carthage registers with the audio device.

        Tracktion Engine normally registers its own DeviceManager as the device callback. Carthage
        registers this instead and calls the engine from inside it, so that in one block, in a fixed
        order and under one real-time check:
          1. the engine renders (inputs, monitored plugin chain, playback);
          2. the recorder takes the raw input for the armed channel(s), with the block's stream time;
          3. the sample browser's preview is added to the main outputs;
          4. the latency probe, if running, owns the outputs;
          4. input meters and audio-thread load are published through atomics for the UI to poll.

        Everything here runs on the device's own I/O thread, which is already in the device's audio
        workgroup. The engine's worker threads join it through EditPlaybackContext (see Session).
    */
    class AudioCallback final : public juce::AudioIODeviceCallback
    {
    public:
        AudioCallback (tracktion::Engine&, TakeRecorder&, LatencyProbe&, SamplePreview&);
        ~AudioCallback() override;

        /** Message thread. Puts this callback in place of the engine's own. */
        void attach();
        void detach();

        /** Message thread. Which device input channels are recorded and metered (1 or 2). */
        void setRecordChannels (int firstDeviceChannel, int numChannels);

        /** Message thread. The device output channel the preview's left side goes to; the right
            side goes to the next one. */
        void setPreviewOutput (int firstDeviceChannel);

        struct Load
        {
            float average = 0.0f;   ///< Mean of callback time / block duration since the last read
            float peak = 0.0f;      ///< Worst block since the last read
        };

        /** Message thread, from the UI's frame timer. Reads and resets the window. */
        Load takeLoad() noexcept;

        /** Message thread. The worst block's load since the last call, for the settings test
            alone: the toolbar's meter reads its own window (takeLoad). */
        float takeTestPeak() noexcept   { return testPeak.exchange (0.0f, std::memory_order_relaxed); }

        /** Message thread. Peak level of each recorded input since the last read, linear. */
        std::array<float, 2> takeInputPeaks() noexcept;

        std::int64_t getCallbackCount() const noexcept  { return callbacks.load (std::memory_order_relaxed); }

        /** Blocks that arrived more than half a block late, judged by the device's own host
            timestamps: a dropout count that does not depend on the driver reporting overloads. */
        int getLateCallbackCount() const noexcept       { return lateCallbacks.load (std::memory_order_relaxed); }

        /** Samples the device has asked for since it started. */
        std::int64_t getSamplesProcessed() const noexcept { return samplesProcessed.load (std::memory_order_relaxed); }
        double getSampleRate() const noexcept           { return sampleRate.load (std::memory_order_relaxed); }

        // juce::AudioIODeviceCallback
        void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                               float* const* outputChannelData, int numOutputChannels,
                                               int numSamples,
                                               const juce::AudioIODeviceCallbackContext&) override;
        void audioDeviceAboutToStart (juce::AudioIODevice*) override;
        void audioDeviceStopped() override;
        void audioDeviceError (const juce::String&) override;

    private:
        tracktion::Engine& engine;
        juce::AudioIODeviceCallback& engineCallback;
        TakeRecorder& recorder;
        LatencyProbe& probe;
        SamplePreview& preview;
        bool attached = false;
        int previewDeviceOutput = 0;
        std::atomic<int> previewLeft { -1 }, previewRight { -1 };   // indices into the active outputs

        // The recorded channels as device channel numbers (message thread), and as indices into the
        // callback's input array, which only holds the device's active channels (audio thread).
        int firstDeviceInput = 0, numDeviceInputs = 1;
        std::atomic<int> firstInput { 0 }, numInputs { 1 };

        std::atomic<double> sampleRate { 0.0 };
        std::atomic<std::int64_t> callbacks { 0 }, samplesProcessed { 0 };
        std::atomic<int> lateCallbacks { 0 };
        std::uint64_t lastHostTimeNs = 0;   // audio thread only
        int lastBlockSize = 0;              // audio thread only
        std::atomic<float> loadSum { 0.0f }, loadPeak { 0.0f }, testPeak { 0.0f };
        std::atomic<int> loadCount { 0 };
        std::array<std::atomic<float>, 2> inputPeak {};

        void updateInputIndices();
        void updateOutputIndices();
    };
}
