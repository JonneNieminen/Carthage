// Fuzzes the audio readers the sample browser, the analysis and the preview use (W30, W38): any
// bytes must be read or refused, never crash or read out of bounds. Built by the `fuzz` preset.
#include <juce_audio_formats/juce_audio_formats.h>
#include <engine/AudioFormats.h>

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput (const std::uint8_t* data, std::size_t size)
{
    static juce::AudioFormatManager formats;

    if (formats.getNumKnownFormats() == 0)
        carthage::audio::registerReadable (formats);

    // Each format directly, as the extension decides which one Carthage asks.
    for (int i = 0; i < formats.getNumKnownFormats(); ++i)
    {
        auto* stream = new juce::MemoryInputStream (data, size, false);
        std::unique_ptr<juce::AudioFormatReader> reader (formats.getKnownFormat (i)->createReaderFor (stream, true));

        if (reader == nullptr)
            continue;

        // A bounded read: a header may claim anything.
        const auto channels = static_cast<int> (std::min (8u, reader->numChannels));
        const auto length = static_cast<int> (std::clamp<juce::int64> (reader->lengthInSamples, 0, 4096));

        if (channels > 0 && length > 0)
        {
            juce::AudioBuffer<float> buffer (channels, length);
            reader->read (&buffer, 0, length, 0, true, channels > 1);
        }
    }

    return 0;
}
