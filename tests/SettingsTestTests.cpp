#include <engine/SettingsTest.h>

using carthage::engine::SettingsTest;

class SettingsTestTests final : public juce::UnitTest
{
public:
    SettingsTestTests() : juce::UnitTest ("Settings test recommendations", "engine") {}

    void runTest() override
    {
        beginTest ("The smallest clean buffer, not below the processor's floor; half the cores for plugin checks");
        {
            const auto r = SettingsTest::recommend ({ { 32, 3, 0.4f, false }, { 64, 0, 0.3f, false }, { 128, 0, 0.1f, true } }, 48000.0, 10);
            expect (r.valid);
            expectEquals (r.bufferSize, 128);
            expectEquals (r.sampleRate, 48000.0);
            expectEquals (r.parallelChecks, 5);
            expectEquals (SettingsTest::recommend ({ { 32, 0, 0.05f, true } }, 44100.0, 16).bufferSize, 64);
            expectEquals (SettingsTest::recommend ({ { 32, 0, 0.05f, true } }, 44100.0, 8).bufferSize, 128);
            expectEquals (SettingsTest::recommend ({ { 32, 0, 0.05f, true } }, 44100.0, 4).bufferSize, 256);
            expectEquals (SettingsTest::recommend ({ { 32, 0, 0.05f, true } }, 44100.0, 1).parallelChecks, 1);
            expectEquals (SettingsTest::recommend ({ { 32, 0, 0.05f, true } }, 44100.0, 32).parallelChecks, 8);
        }

        beginTest ("Above 48 kHz with a large buffer it suggests 48 kHz; nothing clean is no recommendation");
        {
            expectEquals (SettingsTest::recommend ({ { 256, 0, 0.2f, true } }, 96000.0, 8).sampleRate, 48000.0);
            expectEquals (SettingsTest::recommend ({ { 128, 0, 0.2f, true } }, 96000.0, 8).sampleRate, 96000.0);
            const auto none = SettingsTest::recommend ({ { 512, 4, 0.9f, false } }, 48000.0, 8);
            expect (! none.valid);
            expect (none.reason.isNotEmpty());
        }
    }
};

static SettingsTestTests settingsTestTests;
