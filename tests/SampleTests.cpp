#include <engine/SampleAnalysis.h>
#include <engine/SampleLibrary.h>
#include <engine/SamplePreview.h>

#include <juce_audio_formats/juce_audio_formats.h>

using namespace carthage;

namespace
{
    /** A sine of `seconds` at `rate`, written as WAV. */
    juce::File writeSine (const juce::File& file, double seconds, double rate = 48000.0, float level = 0.5f)
    {
        file.getParentDirectory().createDirectory();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate)
                                                                                 .withNumChannels (1).withBitsPerSample (24));
        juce::AudioBuffer<float> b (1, static_cast<int> (seconds * rate));

        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, level * std::sin (static_cast<float> (i) * 0.06f));

        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        return file;
    }
}

namespace synth
{
    constexpr double rate = 44100.0;

    double hz (int midi)   { return 440.0 * std::pow (2.0, (midi - 69) / 12.0); }

    /** Chords, each held for `seconds`, with a second harmonic. */
    std::vector<float> chords (const std::vector<std::vector<int>>& progression, double seconds)
    {
        std::vector<float> out;

        for (const auto& chord : progression)
        {
            const auto n = static_cast<int> (seconds * rate);

            for (int i = 0; i < n; ++i)
            {
                const auto t = i / rate;
                const auto env = std::min (1.0, std::min (i, n - i) / 2000.0);
                double v = 0.0;

                for (auto m : chord)
                    v += std::sin (2 * juce::MathConstants<double>::pi * hz (m) * t) + 0.3 * std::sin (4 * juce::MathConstants<double>::pi * hz (m) * t);

                out.push_back (static_cast<float> (0.15 * env * v));
            }
        }

        return out;
    }

    /** One plucked note per beat at `bpm`. */
    std::vector<float> line (double bpm, const std::vector<int>& notes)
    {
        std::vector<float> out;
        const auto n = static_cast<int> (60.0 / bpm * rate);

        for (auto m : notes)
            for (int i = 0; i < n; ++i)
            {
                const auto t = i / rate;
                out.push_back (static_cast<float> (0.5 * std::exp (-t * 4.0)
                                                   * (std::sin (2 * juce::MathConstants<double>::pi * hz (m) * t)
                                                      + 0.5 * std::sin (4 * juce::MathConstants<double>::pi * hz (m) * t))));
            }

        return out;
    }

    /** A click on every beat at `bpm` for `seconds`. */
    std::vector<float> clicks (double bpm, double seconds)
    {
        std::vector<float> out (static_cast<size_t> (seconds * rate), 0.0f);

        for (double beat = 0.0; beat * 60.0 / bpm < seconds; beat += 1.0)
        {
            const auto start = static_cast<size_t> (beat * 60.0 / bpm * rate);

            for (size_t i = 0; i < 1200 && start + i < out.size(); ++i)
                out[start + i] = static_cast<float> (0.8 * std::exp (-static_cast<double> (i) / 180.0)
                                                     * std::sin (2 * juce::MathConstants<double>::pi * 1000.0 * static_cast<double> (i) / rate));
        }

        return out;
    }

    std::vector<float> noise (double seconds)
    {
        juce::Random r (7);
        std::vector<float> out (static_cast<size_t> (seconds * rate));

        for (size_t i = 0; i < out.size(); ++i)
            out[i] = static_cast<float> (0.6 * std::exp (-static_cast<double> (i) / 3000.0) * (r.nextDouble() * 2.0 - 1.0));

        return out;
    }
}

class SampleTests final : public juce::UnitTest
{
public:
    SampleTests() : juce::UnitTest ("Sample browser", "engine") {}

    void runTest() override
    {
        juce::TemporaryFile holder;
        const auto root = holder.getFile();
        const auto settings = root.getChildFile ("Settings");
        const auto library = root.getChildFile ("My samples");

        writeSine (library.getChildFile ("Drums/Kicks/Kick hard.wav"), 0.1);
        writeSine (library.getChildFile ("Drums/Kicks/Kick soft.wav"), 0.1);
        writeSine (library.getChildFile ("Drums/Snare 1.wav"), 0.1);
        writeSine (library.getChildFile ("Loops/Bass loop 120.wav"), 0.2);
        library.getChildFile ("Loops/notes.txt").replaceWithText ("not a sound");
        library.getChildFile ("Drums/.hidden.wav").replaceWithText ("hidden");

        beginTest ("It starts empty; an added folder is read with every folder under it");
        {
            engine::SampleLibrary samples (settings);
            samples.indexNow();
            expect (samples.getRoots().isEmpty() && samples.getSamples().empty(), "nothing until a folder is added");

            samples.addRoot (library);
            samples.indexNow();
            expectEquals (static_cast<int> (samples.getSamples().size()), 4, "four sounds; the text file and the hidden file are not");

            const auto& folders = samples.getFolders();
            expectEquals (folders.front().name, juce::String ("My samples"), "the root first");
            expectEquals (folders.front().parent, -1);

            int kicks = -1;
            for (size_t i = 0; i < folders.size(); ++i)
                if (folders[i].name == "Kicks")
                    kicks = static_cast<int> (i);

            expect (kicks >= 0 && folders[static_cast<size_t> (kicks)].depth == 2, "Drums/Kicks is two levels down");
            expectEquals (static_cast<int> (folders[static_cast<size_t> (kicks)].samples.size()), 2);
        }

        beginTest ("Search: every word must match the name or the folder; favourites persist");
        {
            engine::SampleLibrary samples (settings);   // the folder came back from Samples.xml
            samples.indexNow();
            expect (samples.getRoots().contains (library), "the added folder is remembered");

            const auto names = [&] (const std::vector<size_t>& found)
            {
                juce::StringArray out;
                for (auto i : found)
                    out.add (samples.getSamples()[i].name);
                return out;
            };

            expectEquals (names (samples.find ("kick", false)).joinIntoString (","), juce::String ("Kick hard,Kick soft"));
            expectEquals (names (samples.find ("kick soft", false)).joinIntoString (","), juce::String ("Kick soft"));
            expectEquals (names (samples.find ("loops", false)).joinIntoString (","), juce::String ("Bass loop 120"),
                          "a folder name finds what is in it");
            expect (samples.find ("zzz", false).empty());

            samples.setFavourite (library.getChildFile ("Drums/Snare 1.wav"), true);
        }
        {
            engine::SampleLibrary samples (settings);
            samples.indexNow();
            expectEquals (static_cast<int> (samples.favourites().size()), 1, "the favourite survives a restart");
        }

        beginTest ("An analysis is kept until the file changes");
        {
            engine::SampleLibrary samples (settings);
            const auto loop = library.getChildFile ("Loops/Bass loop 120.wav");
            samples.setAnalysis (loop, { 120.0, "A minor", 0.2 });
            expect (samples.getAnalysis (loop).has_value() && samples.getAnalysis (loop)->key == "A minor");

            writeSine (loop, 0.3);   // rewritten: another size
            expect (! samples.getAnalysis (loop).has_value(), "a changed file is analysed again");
        }

        beginTest ("Key and tempo of sounds whose key and tempo are known");
        {
            const auto run = [] (const std::vector<float>& v) { return engine::analysis::analyse (v.data(), static_cast<int> (v.size()), synth::rate); };

            // A minor with its raised seventh (Am Dm E Am), and C major (C F G C): two bars each chord.
            const auto aMinor = run (synth::chords ({ { 57, 60, 64 }, { 50, 53, 57 }, { 52, 56, 59 }, { 57, 60, 64 } }, 2.0));
            expectEquals (aMinor.key, juce::String ("A minor"));
            const auto cMajor = run (synth::chords ({ { 48, 52, 55 }, { 53, 57, 60 }, { 55, 59, 62 }, { 48, 52, 55 } }, 2.0));
            expectEquals (cMajor.key, juce::String ("C major"));
            expect (! aMinor.bpm.has_value() && ! cMajor.bpm.has_value(), "held chords have no beat, so no tempo");

            // A bass line in E minor at 90 bpm, 16 beats.
            const auto bass = run (synth::line (90.0, { 40, 40, 43, 45, 47, 45, 43, 40, 40, 40, 43, 45, 47, 45, 43, 40 }));
            expect (bass.bpm.has_value(), "the bass line has a tempo");
            expectWithinAbsoluteError (bass.bpm.value_or (0.0), 90.0, 0.5, "90 bpm");
            expectEquals (bass.key, juce::String ("E minor"));

            const auto click = run (synth::clicks (120.0, 8.0));
            expectWithinAbsoluteError (click.bpm.value_or (0.0), 120.0, 0.5, "clicks at 120 bpm");
            const auto fast = run (synth::clicks (174.0, 8.0));
            expectWithinAbsoluteError (fast.bpm.value_or (0.0), 174.0, 1.0, "clicks at 174 bpm");

            // A short tuned hit has a note, not a key: a 0.5 s tone at A (220 Hz).
            std::vector<float> hit;
            for (int i = 0; i < static_cast<int> (0.5 * synth::rate); ++i)
                hit.push_back (static_cast<float> (0.8 * std::exp (-i / 6000.0) * std::sin (2 * juce::MathConstants<double>::pi * 220.0 * i / synth::rate)));
            expectEquals (run (hit).key, juce::String ("A"), "a one-shot shows its note");

            const auto snare = run (synth::noise (0.4));
            expect (snare.key.isEmpty() && ! snare.bpm.has_value(), "a noise burst: no key, no tempo");
            expectWithinAbsoluteError (snare.seconds, 0.4, 0.01);
        }

        beginTest ("Preview: plays at its own volume; a new sound replaces the old at once; stop is silent");
        {
            engine::SamplePreview preview;
            preview.prepare (48000.0);
            preview.setVolumeDb (0.0f);
            const auto first = writeSine (root.getChildFile ("a.wav"), 0.5, 44100.0, 0.5f);   // converted to 48 kHz

            const auto pull = [&] (int blocks)
            {
                juce::AudioBuffer<float> out (2, 256);
                float peak = 0.0f;

                for (int b = 0; b < blocks; ++b)
                {
                    out.clear();
                    preview.render (out.getWritePointer (0), out.getWritePointer (1), 256);
                    peak = std::max (peak, out.getMagnitude (0, 256));
                    juce::Thread::sleep (1);
                }

                return peak;
            };

            preview.play (first);
            juce::Thread::sleep (100);   // the reader fills the FIFO
            const auto heard = pull (40);
            expectWithinAbsoluteError (heard, 0.5f, 0.05f, "the sound comes through at full volume");
            expect (preview.isPlaying());

            preview.setVolumeDb (-12.0f);
            pull (2);   // the ramp
            expectWithinAbsoluteError (pull (10), 0.5f * juce::Decibels::decibelsToGain (-12.0f), 0.03f, "the preview's own volume");

            preview.stop();
            juce::Thread::sleep (50);
            pull (2);   // the flush is answered
            expectEquals (pull (10), 0.0f, "stop is silence");
            expect (! preview.isPlaying());

            preview.play (first);
            juce::Thread::sleep (100);
            pull (200);   // to the end: 0.5 s is about 94 blocks
            expect (! preview.isPlaying(), "a sound that played to its end is done");
        }
    }
};

static SampleTests sampleTests;
