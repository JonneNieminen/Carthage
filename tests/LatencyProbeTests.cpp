#include <engine/LatencyProbe.h>

#include <thread>

using carthage::engine::LatencyProbe;

class LatencyProbeTests final : public juce::UnitTest
{
public:
    LatencyProbeTests() : juce::UnitTest ("Latency probe", "engine") {}

    /** Drives the probe through a simulated loopback cable: output channel 1 comes back on input
        channel 0 `delay` samples later, scaled, optionally inverted, with noise. */
    LatencyProbe::Measurement measureThroughCable (int delay, float gain, int blockSize, float noise)
    {
        constexpr double rate = 48000.0;
        LatencyProbe probe;
        probe.prepare (rate);
        expect (probe.start (1, 0));

        std::vector<float> cable (static_cast<size_t> (rate * 2), 0.0f);
        juce::AudioBuffer<float> in (2, blockSize), out (2, blockSize);
        juce::Random rng (7);
        size_t t = 0;

        for (int b = 0; b < static_cast<int> (rate * 1.5) / blockSize && probe.isBusy(); ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                in.setSample (0, i, cable[t + static_cast<size_t> (i)] + noise * (rng.nextFloat() - 0.5f));
                in.setSample (1, i, 0.0f);
            }

            out.clear();
            probe.process (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, blockSize);

            for (int i = 0; i < blockSize; ++i)
                if (t + static_cast<size_t> (i + delay) < cable.size())
                    cable[t + static_cast<size_t> (i + delay)] += gain * out.getSample (1, i);

            t += static_cast<size_t> (blockSize);
        }

        for (int i = 0; i < 5000; ++i)
        {
            if (auto r = probe.takeResult())
                return *r;

            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }

        expect (false, "the probe never finished");
        return {};
    }

    void runTest() override
    {
        beginTest ("Finds the round trip through a clean cable, to the sample");
        {
            const auto m = measureThroughCable (1234, 1.0f, 64, 0.0f);
            expectEquals (m.samples, 1234);
            expectGreaterThan (m.confidence, LatencyProbe::minimumConfidence);
        }

        beginTest ("Works at large buffers, with an inverting, quiet, noisy interface");
        {
            const auto m = measureThroughCable (4111, -0.1f, 1024, 0.02f);
            expectEquals (m.samples, 4111);
            expectGreaterThan (m.confidence, LatencyProbe::minimumConfidence);
        }

        beginTest ("Says so when nothing comes back");
        {
            const auto m = measureThroughCable (500, 0.0f, 128, 0.02f);
            expectLessThan (m.confidence, LatencyProbe::minimumConfidence);
        }

        beginTest ("The other outputs are silent while the probe plays");
        {
            LatencyProbe probe;
            probe.prepare (48000.0);
            expect (probe.start (0, 0));
            juce::AudioBuffer<float> in (1, 256), out (2, 256);
            out.applyGain (0.0f);
            for (int i = 0; i < 256; ++i) out.setSample (1, i, 0.5f);   // what the engine rendered
            probe.process (in.getArrayOfReadPointers(), 1, out.getArrayOfWritePointers(), 2, 256);
            expectEquals (out.getMagnitude (1, 0, 256), 0.0f);
        }
    }
};

static LatencyProbeTests latencyProbeTests;
