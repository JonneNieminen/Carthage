#include <engine/AudioCallback.h>

#include <juce_core/juce_core.h>

#include <array>

using namespace carthage;

/** App loudness (owner): the app's own output gain, applied after master in the device callback. */
class OutputGainTests final : public juce::UnitTest
{
public:
    OutputGainTests() : juce::UnitTest ("Output gain", "engine") {}

    void runTest() override
    {
        constexpr int n = 4;

        beginTest ("A steady gain scales every sample of every channel");
        {
            std::array<float, n> left { 1.0f, 1.0f, 1.0f, 1.0f }, right { -0.5f, -0.5f, -0.5f, -0.5f };
            std::array<float*, 2> out { left.data(), right.data() };
            engine::applyOutputGain (out.data(), 2, n, 0.5f, 0.5f);

            for (int i = 0; i < n; ++i)
            {
                expectWithinAbsoluteError (left[static_cast<size_t> (i)], 0.5f, 1.0e-6f);
                expectWithinAbsoluteError (right[static_cast<size_t> (i)], -0.25f, 1.0e-6f);
            }
        }

        beginTest ("A change glides across the block and lands on the new gain at its end");
        {
            std::array<float, n> ones { 1.0f, 1.0f, 1.0f, 1.0f };
            std::array<float*, 1> out { ones.data() };
            engine::applyOutputGain (out.data(), 1, n, 1.0f, 0.0f);
            expectWithinAbsoluteError (ones[0], 0.75f, 1.0e-6f);
            expectWithinAbsoluteError (ones[1], 0.5f, 1.0e-6f);
            expectWithinAbsoluteError (ones[3], 0.0f, 1.0e-6f, "the new gain on the last sample");
        }

        beginTest ("Channels the device leaves out are skipped");
        {
            std::array<float, n> ones { 1.0f, 1.0f, 1.0f, 1.0f };
            std::array<float*, 2> out { nullptr, ones.data() };
            engine::applyOutputGain (out.data(), 2, n, 2.0f, 2.0f);
            expectWithinAbsoluteError (ones[2], 2.0f, 1.0e-6f);
        }
    }
};

static OutputGainTests outputGainTests;
