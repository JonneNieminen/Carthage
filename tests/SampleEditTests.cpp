#include <engine/SampleEdits.h>
#include <engine/FormantPlugin.h>
#include <engine/RealtimeCheck.h>

using namespace carthage;

namespace
{
    struct Headless final : tracktion::EngineBehaviour
    {
        bool autoInitialiseDeviceManager() override   { return false; }
    };

    juce::File writeTone (const juce::File& file)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (file);
        auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
        juce::AudioBuffer<float> b (1, 48000 * 2);
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, 0.25f * std::sin (static_cast<float> (i) * 0.05f));
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        return file;
    }
}

class SampleEditTests final : public juce::UnitTest
{
public:
    SampleEditTests() : juce::UnitTest ("Sample editor model", "engine") {}

    void runTest() override
    {
        juce::TemporaryFile holder;
        const auto& folder = holder.getFile();
        folder.createDirectory();
        const auto kick = writeTone (folder.getChildFile ("Kick.wav"));
        const auto snare = writeTone (folder.getChildFile ("Snare.wav"));

        tracktion::Engine engine ("CarthageTests", nullptr, std::make_unique<Headless>());
        engine.getPluginManager().createBuiltInType<engine::FormantPlugin>();   // as the session does
        auto edit = tracktion::createEmptyEdit (engine, folder.getChildFile ("Test.tracktionedit"));
        edit->ensureNumberOfAudioTracks (2);
        const auto lanes = editing::lanes (*edit);
        auto& um = edit->getUndoManager();

        auto* a = editing::insertFile (*lanes[0], kick, 0.0);
        auto* b = editing::insertFile (*lanes[1], kick, 8.0);
        auto* other = editing::insertFile (*lanes[1], snare, 16.0);

        beginTest ("Clips of one file are one sample, named after it");
        {
            expectEquals (editing::clipsOfSample (*a).size(), 2);
            expect (editing::clipsOfSample (*a).contains (b) && ! editing::clipsOfSample (*a).contains (other));
            expectEquals (editing::sampleName (*a), juce::String ("Kick"));
        }

        beginTest ("An edit reaches every clip of the sample, and undoes in one step");
        {
            auto s = editing::soundOf (*a);
            s.gainDb = -6.0f;
            s.reversed = true;
            s.pitch = 3.0f;
            s.formant = 4.0f;
            s.fadeIn = 0.1;
            s.fadeInShape = tracktion::AudioFadeCurve::sCurve;
            s.fadeOutShape = tracktion::AudioFadeCurve::convex;
            s.start = 0.25;
            s.length = 1.0;
            editing::begin (*edit, "Edit sample");
            editing::setSound (*a, s);

            for (auto* c : { a, b })
            {
                const auto got = editing::soundOf (*c);
                expectWithinAbsoluteError (got.gainDb, -6.0f, 1.0e-4f);
                expect (got.reversed);
                expectWithinAbsoluteError (got.pitch, 3.0f, 1.0e-4f);
                expectWithinAbsoluteError (got.formant, 4.0f, 1.0e-4f, "the formant shift too");
                expectWithinAbsoluteError (got.fadeIn, 0.1, 1.0e-6);
                expect (got.fadeInShape == tracktion::AudioFadeCurve::sCurve && got.fadeOutShape == tracktion::AudioFadeCurve::convex, "the shapes too");
                expectWithinAbsoluteError (got.start, 0.25, 1.0e-6);
                expectWithinAbsoluteError (got.length, 1.0, 1.0e-6);
            }

            expectWithinAbsoluteError (editing::soundOf (*other).gainDb, 0.0f, 1.0e-4f, "another file is not touched");
            um.undo();
            expectWithinAbsoluteError (editing::soundOf (*b).gainDb, 0.0f, 1.0e-4f, "undone");
            expect (! editing::soundOf (*a).reversed, "undone");
            expectWithinAbsoluteError (editing::soundOf (*a).formant, 0.0f, 1.0e-4f, "the formant plugin goes with the undo");
        }

        beginTest ("The envelope: on every clip of the sample, its curve where the clip is, one undo step");
        {
            const auto curveTimes = [] (tracktion::Clip& c)
            {
                std::vector<double> times;

                for (auto* plugin : *c.getPluginList())
                    if (auto* vp = dynamic_cast<tracktion::VolumeAndPanPlugin*> (plugin))
                        for (int i = 0; i < vp->volParam->getCurve().getNumPoints(); ++i)
                            times.push_back (vp->volParam->getCurve().getPointTime (i).inSeconds());

                return times;
            };

            // a: beat 0 (0 s); b: beat 8 (4 s), both Kick, the first second of the file playing.
            editing::begin (*edit, "Envelope");
            editing::setEnvelope (*a, { { 0.0, 0.0f, 0.0f }, { 0.5, -60.0f, 0.3f } });
            expectEquals (static_cast<int> (editing::envelopeOf (*b).size()), 2, "every clip of the sample");
            expectEquals (static_cast<int> (editing::envelopeOf (*other).size()), 0, "another file is not touched");

            const auto onB = curveTimes (*b);
            expect (onB.size() == 2 && std::abs (onB[0] - 4.0) < 1.0e-6 && std::abs (onB[1] - 4.5) < 1.0e-6, "in timeline time, from the clip's start");

            editing::begin (*edit, "Move clip");
            editing::move ({ b }, 2.0, 0);   // one second later
            editing::syncEnvelope (*b);
            const auto moved = curveTimes (*b);
            expect (moved.size() == 2 && std::abs (moved[0] - 5.0) < 1.0e-6, "follows the clip");

            um.undo();
            um.undo();
            editing::syncEnvelopes (*edit);
            expectEquals (static_cast<int> (editing::envelopeOf (*a).size()), 0, "undone");
            expect (curveTimes (*a).empty(), "and its plugin gone");
        }

        beginTest ("Renaming a sample renames every clip of it; a name in use is refused");
        {
            expect (editing::renameSample (*a, "Big kick").wasOk());
            expectEquals (editing::sampleName (*b), juce::String ("Big kick"), "every clip of it");
            expectEquals (editing::clipsOfSample (*a).size(), 2, "still one sample");
            expect (editing::renameSample (*a, "Snare").failed(), "the snare's name is taken");
            expect (editing::renameSample (*a, "  ").failed(), "a blank name is refused");
            um.undo();
            expectEquals (editing::sampleName (*a), juce::String ("Kick"), "undone in one step");
        }

        beginTest ("The formant plugin processes blocks without allocating or locking (checked in the rtsan build)");
        {
            auto plugin = edit->getPluginCache().createNewPlugin (engine::FormantPlugin::xmlTypeName, {});
            auto* formant = dynamic_cast<engine::FormantPlugin*> (plugin.get());
            expect (formant != nullptr, "the type is registered");

            if (formant != nullptr)
            {
                formant->semitones = 5.0f;
                formant->initialise ({ tracktion::TimePosition(), 48000.0, 512 });
                expect (formant->getLatencySeconds() > 0.0, "it reports its latency");

                for (int channels : { 2, 1 })
                {
                    juce::AudioBuffer<float> block (std::max (1, channels), 512);
                    tracktion::MidiMessageArray midi;

                    for (int n = 0; n < 40; ++n)
                    {
                        for (int ch = 0; ch < channels; ++ch)
                            for (int i = 0; i < 512; ++i)
                                block.setSample (ch, i, 0.2f * std::sin (static_cast<float> (n * 512 + i) * 0.03f));

                        const tracktion::PluginRenderContext context (&block, 0, 512, &midi, 0.0,
                                                                     { tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (512.0 / 48000.0) },
                                                                     true, false, false, false);
                        { const carthage::rt::ScopedRealtime realtime; formant->applyToBuffer (context); }   // checked in the rtsan build
                    }

                    expect (block.getMagnitude (0, 0, 512) > 0.01f, channels == 2 ? "stereo comes through" : "mono comes through");
                }

                formant->deinitialise();
            }
        }

        beginTest ("Make unique splits a clip off as Kick (2); its copies stay with it");
        {
            expectEquals (editing::makeUnique (*b), juce::String ("Kick (2)"));
            expectEquals (editing::sampleName (*b), juce::String ("Kick (2)"));
            expectEquals (editing::clipsOfSample (*a).size(), 1, "the original is alone now");

            auto* copy = editing::placeCopy (*b, *lanes[0], 24.0);
            expect (copy != nullptr && editing::clipsOfSample (*b).contains (copy), "a copy keeps the sample");

            auto s = editing::soundOf (*b);
            s.gainDb = -12.0f;
            editing::begin (*edit, "Edit sample");
            editing::setSound (*b, s);
            expectWithinAbsoluteError (editing::soundOf (*copy).gainDb, -12.0f, 1.0e-4f);
            expectWithinAbsoluteError (editing::soundOf (*a).gainDb, 0.0f, 1.0e-4f, "the original is left alone");

            expectEquals (editing::makeUnique (*copy), juce::String ("Kick (3)"), "the next free number");
            um.undo();
            expectEquals (editing::sampleName (*copy), juce::String ("Kick (2)"), "Make unique undoes");
        }
    }
};

static SampleEditTests sampleEditTests;
