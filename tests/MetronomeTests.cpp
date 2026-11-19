#include <engine/Metronome.h>

using namespace carthage;

namespace
{
    struct HeadlessClickBehaviour final : tracktion::EngineBehaviour
    {
        bool autoInitialiseDeviceManager() override   { return false; }
    };
}

class MetronomeTests final : public juce::UnitTest
{
public:
    MetronomeTests() : juce::UnitTest ("Metronome sounds", "engine") {}

    void runTest() override
    {
        using metronome::Sound;

        for (auto sound : { Sound::wood, Sound::hiHat, Sound::cowbell })
        {
            beginTest ("Sound " + juce::String (static_cast<int> (sound)) + ": short, the accent louder, the same each time");

            const auto accent = metronome::synthesize (sound, true, 48000.0);
            const auto beat = metronome::synthesize (sound, false, 48000.0);

            expect (accent.getNumSamples() > 0 && accent.getNumSamples() <= static_cast<int> (0.3 * 48000.0), "under 0.3 s");
            expectWithinAbsoluteError (accent.getMagnitude (0, accent.getNumSamples()), 0.85f, 1.0e-4f);
            expectWithinAbsoluteError (beat.getMagnitude (0, beat.getNumSamples()), 0.55f, 1.0e-4f);
            expectWithinAbsoluteError (accent.getSample (0, accent.getNumSamples() - 1), 0.0f, 1.0e-4f, "fades to silence");

            const auto again = metronome::synthesize (sound, true, 48000.0);
            bool same = true;
            for (int i = 0; i < again.getNumSamples(); ++i)
                same = same && again.getSample (0, i) == accent.getSample (0, i);
            expect (same, "deterministic");
        }

        beginTest ("The click track is pointed at the written files; a file with no sound changes nothing");
        {
            juce::TemporaryFile folderHolder;
            const auto folder = folderHolder.getFile();
            tracktion::Engine engine ("CarthageTests", nullptr, std::make_unique<HeadlessClickBehaviour>());
            auto& storage = engine.getPropertyStorage();

            expect (metronome::use (engine, Sound::cowbell, {}, folder).wasOk());
            const juce::File accent (storage.getProperty (tracktion::SettingID::clickTrackSampleBig).toString());
            expect (accent.existsAsFile() && accent.getParentDirectory() == folder, "written into the folder");

            const auto junk = folder.getChildFile ("junk.wav");
            junk.replaceWithText ("not a sound");
            expect (metronome::use (engine, Sound::custom, junk, folder).failed(), "refused");
            expectEquals (storage.getProperty (tracktion::SettingID::clickTrackSampleBig).toString(), accent.getFullPathName(), "still the cowbell");

            folder.deleteRecursively();
        }
    }
};

static MetronomeTests metronomeTests;
