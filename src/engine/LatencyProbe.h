#pragma once

#include "RealtimeCheck.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <optional>
#include <vector>

namespace carthage::engine
{
    /** Measures round-trip latency through a physical loopback (docs/TESTING.md, "Measuring").

        The audio thread plays a short chirp on one output channel and, from the same block, captures
        one input channel. A worker then finds the chirp in the capture by cross-correlation. Output
        and input share the device callback's sample clock, so the lag is the true round trip:
        output buffering + converter + cable + converter + input buffering.

        While a measurement runs the probe owns the outputs: everything else is silenced, so the
        monitored input can not feed back through the cable.
    */
    class LatencyProbe
    {
    public:
        struct Measurement
        {
            int samples = 0;          ///< Round trip, in samples
            double sampleRate = 0.0;
            float confidence = 0.0f;  ///< Correlation peak over its mean magnitude; > ~8 is a clean hit

            double milliseconds() const noexcept { return sampleRate > 0 ? samples * 1000.0 / sampleRate : 0.0; }
        };

        LatencyProbe() = default;
        ~LatencyProbe();

        /** Message thread: allocates the chirp and capture buffers for this sample rate. */
        void prepare (double sampleRate);

        /** Message thread: starts one measurement. Returns false if one is already running. */
        bool start (int outputChannel, int inputChannel);

        /** Message thread: true while a measurement is playing, capturing or being analysed. */
        bool isBusy() const noexcept;

        /** Message thread: the finished result, once. nullopt while busy. A failed measurement (no
            signal came back) returns a Measurement with confidence below the threshold. */
        std::optional<Measurement> takeResult();

        /** Audio thread. Called after the engine has rendered into the outputs. */
        void process (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                      int numFrames) noexcept CARTHAGE_NONBLOCKING;

        /** Finds `reference` inside `recorded`: returns the lag of the correlation peak and its
            confidence. Pure; used by the worker and by tests. */
        static Measurement findDelay (const std::vector<float>& reference, const std::vector<float>& recorded,
                                      double sampleRate);

        static constexpr float minimumConfidence = 8.0f;

    private:
        enum State : int { idle, requested, running, captured, analysing, done };

        std::atomic<int> state { idle };
        std::atomic<int> outChannel { 0 }, inChannel { 0 };
        double rate = 0.0;
        std::vector<float> chirp, capture;
        int position = 0;   // audio thread only, while running
        Measurement result;

        void analyse();
    };
}
