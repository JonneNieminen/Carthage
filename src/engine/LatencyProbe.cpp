#include "LatencyProbe.h"

#include <chrono>
#include <cmath>
#include <numbers>
#include <thread>

namespace carthage::engine
{
    LatencyProbe::~LatencyProbe()
    {
        // The analysis worker holds `this`; let it finish.
        while (state.load (std::memory_order_acquire) == analysing)
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    void LatencyProbe::prepare (double sampleRate)
    {
        jassert (! isBusy());
        rate = sampleRate;

        // A Hann-windowed linear chirp from 300 Hz to 6 kHz, 40 ms long: wide enough in frequency for a
        // sharp correlation peak, low enough in level (-6 dBFS) to be safe on headphones.
        const auto length = static_cast<size_t> (std::lround (sampleRate * 0.040));
        chirp.assign (length, 0.0f);

        const double f0 = 300.0, f1 = 6000.0, duration = static_cast<double> (length) / sampleRate;

        for (size_t i = 0; i < length; ++i)
        {
            const double t = static_cast<double> (i) / sampleRate;
            const double phase = 2.0 * std::numbers::pi * (f0 * t + 0.5 * (f1 - f0) / duration * t * t);
            const double window = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * static_cast<double> (i) / static_cast<double> (length - 1));
            chirp[i] = static_cast<float> (0.5 * window * std::sin (phase));
        }

        // One second covers any plausible round trip, even at large buffers.
        capture.assign (static_cast<size_t> (std::lround (sampleRate * 1.0)), 0.0f);
    }

    bool LatencyProbe::start (int outputChannel, int inputChannel)
    {
        if (chirp.empty())
            return false;

        int expected = idle;

        if (! state.compare_exchange_strong (expected, requested))
        {
            expected = done;    // a finished result nobody collected is discarded

            if (! state.compare_exchange_strong (expected, requested))
                return false;
        }

        outChannel.store (outputChannel, std::memory_order_relaxed);
        inChannel.store (inputChannel, std::memory_order_relaxed);
        return true;
    }

    bool LatencyProbe::isBusy() const noexcept
    {
        const auto s = state.load (std::memory_order_acquire);
        return s != idle && s != done;
    }

    std::optional<LatencyProbe::Measurement> LatencyProbe::takeResult()
    {
        auto s = state.load (std::memory_order_acquire);

        if (s == captured)
        {
            // Analysis is heavy enough to keep off the message thread.
            if (state.compare_exchange_strong (s, analysing))
                std::thread ([this] { analyse(); }).detach();

            return std::nullopt;
        }

        if (s == done)
        {
            state.store (idle, std::memory_order_release);
            return result;
        }

        return std::nullopt;
    }

    void LatencyProbe::analyse()
    {
        result = findDelay (chirp, capture, rate);
        state.store (done, std::memory_order_release);
    }

    void LatencyProbe::process (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                                int numFrames) noexcept
    {
        auto s = state.load (std::memory_order_acquire);

        if (s == requested)
        {
            position = 0;
            state.store (running, std::memory_order_release);
            s = running;
        }

        if (s != running)
            return;

        const auto out = outChannel.load (std::memory_order_relaxed);
        const auto in = inChannel.load (std::memory_order_relaxed);
        const auto chirpLength = static_cast<int> (chirp.size());
        const auto captureLength = static_cast<int> (capture.size());

        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputs[ch], numFrames);

        if (out >= 0 && out < numOutputs && outputs[out] != nullptr && position < chirpLength)
        {
            const auto n = std::min (numFrames, chirpLength - position);
            juce::FloatVectorOperations::copy (outputs[out], chirp.data() + position, n);
        }

        const auto toCapture = std::min (numFrames, captureLength - position);

        if (toCapture > 0)
        {
            if (in >= 0 && in < numInputs && inputs[in] != nullptr)
                juce::FloatVectorOperations::copy (capture.data() + position, inputs[in], toCapture);
            else
                juce::FloatVectorOperations::clear (capture.data() + position, toCapture);
        }

        position += numFrames;

        if (position >= captureLength)
            state.store (captured, std::memory_order_release);
    }

    LatencyProbe::Measurement LatencyProbe::findDelay (const std::vector<float>& reference,
                                                       const std::vector<float>& recorded,
                                                       double sampleRate)
    {
        Measurement m;
        m.sampleRate = sampleRate;

        const auto refLength = reference.size();

        if (refLength == 0 || recorded.size() < refLength)
            return m;

        const auto numLags = recorded.size() - refLength + 1;
        std::vector<float> correlation (numLags);

        for (size_t lag = 0; lag < numLags; ++lag)
        {
            float sum = 0.0f;
            const float* r = recorded.data() + lag;

            for (size_t i = 0; i < refLength; ++i)
                sum += reference[i] * r[i];

            correlation[lag] = sum;
        }

        // Converters may invert polarity, so the peak is taken on magnitude.
        size_t best = 0;
        double mean = 0.0;

        for (size_t lag = 0; lag < numLags; ++lag)
        {
            const auto v = std::abs (correlation[lag]);
            mean += v;

            if (v > std::abs (correlation[best]))
                best = lag;
        }

        mean /= static_cast<double> (numLags);

        m.samples = static_cast<int> (best);
        m.confidence = mean > 0.0 ? static_cast<float> (std::abs (correlation[best]) / mean) : 0.0f;
        return m;
    }
}
