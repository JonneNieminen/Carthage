#include <ui/Controls.h>

using namespace carthage;

class OverviewCacheTests final : public juce::UnitTest
{
public:
    OverviewCacheTests() : juce::UnitTest ("Waveform overviews on disk", "ui") {}

    void runTest() override
    {
        juce::TemporaryFile holder;
        const auto& folder = holder.getFile();
        folder.createDirectory();

        // One second of a tone at 48 kHz.
        const auto wavFile = folder.getChildFile ("tone.wav");
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (wavFile);
            auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
            juce::AudioBuffer<float> b (1, 48000);
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (0, i, 0.5f * std::sin (static_cast<float> (i) * 0.05f));
            writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        const auto cacheFolder = folder.getChildFile ("Overviews");

        beginTest ("A finished overview is written to disk");
        {
            ui::DiskThumbnailCache cache (4, cacheFolder);
            juce::AudioThumbnail thumb (256, formats, cache);
            thumb.setSource (new juce::FileInputSource (wavFile));

            for (int i = 0; i < 500 && ! thumb.isFullyLoaded(); ++i)
                juce::Thread::sleep (10);

            expect (thumb.isFullyLoaded(), "the overview was made");

            for (int i = 0; i < 100 && cacheFolder.findChildFiles (juce::File::findFiles, false, "*.overview").isEmpty(); ++i)
                juce::Thread::sleep (10);

            expectEquals (cacheFolder.findChildFiles (juce::File::findFiles, false, "*.overview").size(), 1);
        }

        beginTest ("A new cache reads it back at once");
        {
            ui::DiskThumbnailCache cache (4, cacheFolder);
            juce::AudioThumbnail thumb (256, formats, cache);
            thumb.setSource (new juce::FileInputSource (wavFile));
            expect (thumb.isFullyLoaded(), "loaded from disk, not read from the audio");
            expectWithinAbsoluteError (thumb.getTotalLength(), 1.0, 1.0e-3);
            expectWithinAbsoluteError (thumb.getApproximatePeak(), 0.5f, 0.01f);
        }

        beginTest ("A damaged file is dropped and made again");
        {
            const auto saved = cacheFolder.findChildFiles (juce::File::findFiles, false, "*.overview");
            saved.getFirst().replaceWithText ("damaged");
            ui::DiskThumbnailCache cache (4, cacheFolder);
            juce::AudioThumbnail thumb (256, formats, cache);
            thumb.setSource (new juce::FileInputSource (wavFile));

            for (int i = 0; i < 500 && ! thumb.isFullyLoaded(); ++i)
                juce::Thread::sleep (10);

            expectWithinAbsoluteError (thumb.getApproximatePeak(), 0.5f, 0.01f);
        }

        folder.deleteRecursively();
    }
};

static OverviewCacheTests overviewCacheTests;
