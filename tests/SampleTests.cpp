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
