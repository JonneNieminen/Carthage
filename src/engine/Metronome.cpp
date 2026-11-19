#include "Metronome.h"
#include "AudioFormats.h"

#include <cmath>
#include <numbers>

namespace carthage::metronome
{
    namespace
    {
        constexpr double twoPi = 2.0 * std::numbers::pi;

        /** A wooden block: two damped partials that a wood block rings at, and a short knock. */
        float wood (double t, bool accent, juce::Random& noise)
        {
            const auto f = accent ? 2100.0 : 1650.0;
            const auto ring = std::exp (-t / 0.018) * (0.7 * std::sin (twoPi * f * t) + 0.3 * std::sin (twoPi * f * 2.76 * t));
            const auto knock = std::exp (-t / 0.0015) * (noise.nextDouble() * 2.0 - 1.0);
            return static_cast<float> (ring + 0.35 * knock);
        }

        /** A closed hi-hat: bright noise (differenced twice, which tilts it high), decaying fast. */
        struct HiHat
        {
            double last = 0.0, lastDiff = 0.0;

            float next (double t, bool accent, juce::Random& noise)
            {
                const auto n = noise.nextDouble() * 2.0 - 1.0;
                const auto d1 = n - last;
                const auto d2 = d1 - lastDiff;
                last = n;
                lastDiff = d1;
                return static_cast<float> (0.5 * d2 * std::exp (-t / (accent ? 0.045 : 0.03)));
            }
        };

        /** A cowbell: two clipped tones a fifth and a bit apart, as the classic drum machines make it. */
        float cowbell (double t, bool accent)
        {
            const auto lift = accent ? 1.06 : 1.0;
            const auto tones = std::tanh (3.0 * std::sin (twoPi * 587.0 * lift * t)) + std::tanh (3.0 * std::sin (twoPi * 845.0 * lift * t));
            const auto envelope = 0.7 * std::exp (-t / 0.02) + 0.3 * std::exp (-t / 0.11);
            return static_cast<float> (0.5 * tones * envelope);
        }

        /** What Tracktion's Click::setClickWaveFile does (its header is internal to the module):
            the click track reads these two engine settings when playback is prepared. */
        void pointClickAt (tracktion::Engine& engine, const juce::File& accent, const juce::File& beat)
        {
            auto& storage = engine.getPropertyStorage();
            storage.setProperty (tracktion::SettingID::clickTrackSampleBig, accent.getFullPathName());
            storage.setProperty (tracktion::SettingID::clickTrackSampleSmall, beat.getFullPathName());
        }

        void normalise (juce::AudioBuffer<float>& b, float peak)
        {
            if (const auto m = b.getMagnitude (0, b.getNumSamples()); m > 0.0f)
                b.applyGain (peak / m);
        }

        juce::Result writeWav (const juce::AudioBuffer<float>& b, double rate, const juce::File& file)
        {
            file.deleteFile();
            std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (file);
            juce::WavAudioFormat wav;
            auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (rate)
                                                                                    .withNumChannels (b.getNumChannels())
                                                                                    .withBitsPerSample (24));
            if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()))
                return juce::Result::fail ("Could not write " + file.getFullPathName());

            return juce::Result::ok();
        }
    }

    juce::AudioBuffer<float> synthesize (Sound sound, bool accent, double sampleRate)
    {
        const auto seconds = sound == Sound::cowbell ? 0.28 : sound == Sound::hiHat ? 0.2 : 0.12;
        juce::AudioBuffer<float> b (1, static_cast<int> (seconds * sampleRate));
        juce::Random noise (accent ? 7 : 3);
        HiHat hat;

        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const auto t = i / sampleRate;
            float v = 0.0f;

            switch (sound)
            {
                case Sound::wood:     v = wood (t, accent, noise); break;
                case Sound::hiHat:    v = hat.next (t, accent, noise); break;
                case Sound::cowbell:  v = cowbell (t, accent); break;
                case Sound::custom:   break;
            }

            b.setSample (0, i, v);
        }

        // The last few milliseconds fade out, so no sound ends on a step.
        const auto fade = std::min (b.getNumSamples(), static_cast<int> (0.005 * sampleRate));
        b.applyGainRamp (b.getNumSamples() - fade, fade, 1.0f, 0.0f);
        normalise (b, accent ? 0.85f : 0.55f);
        return b;
    }

    juce::Result use (tracktion::Engine& engine, Sound sound, const juce::File& customFile, const juce::File& folder)
    {
        if (! folder.createDirectory())
            return juce::Result::fail ("Could not make " + folder.getFullPathName());

        constexpr double rate = 48000.0;   // Tracktion converts it to the device's rate when it plays
        if (sound == Sound::custom)
        {
            juce::AudioFormatManager formats;
            carthage::audio::registerReadable (formats);
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (customFile));

            if (reader == nullptr)
                return juce::Result::fail ("Carthage can't read " + customFile.getFileName());

            // Up to a second of it, its own rate kept: the file is written as read.
            const auto length = static_cast<int> (std::min<juce::int64> (reader->lengthInSamples, static_cast<juce::int64> (reader->sampleRate)));

            if (length <= 0 || reader->numChannels == 0)
                return juce::Result::fail (customFile.getFileName() + " holds no sound");

            juce::AudioBuffer<float> own (reader->numChannels > 1 ? 2 : 1, length);
            reader->read (&own, 0, length, 0, true, own.getNumChannels() > 1);

            // The same sound for both: the accent first, then quieter for the other beats.
            const auto accentFile = folder.getChildFile ("Own accent.wav"), beatFile = folder.getChildFile ("Own beat.wav");
            normalise (own, 0.85f);
            if (auto r = writeWav (own, reader->sampleRate, accentFile); r.failed()) return r;
            own.applyGain (0.55f / 0.85f);
            if (auto r = writeWav (own, reader->sampleRate, beatFile); r.failed())   return r;

            pointClickAt (engine, accentFile, beatFile);
            return juce::Result::ok();
        }

        const auto name = sound == Sound::wood ? juce::String ("Wood") : sound == Sound::hiHat ? juce::String ("Hi-hat") : juce::String ("Cowbell");
        const auto accentFile = folder.getChildFile (name + " accent.wav"), beatFile = folder.getChildFile (name + " beat.wav");

        if (auto r = writeWav (synthesize (sound, true, rate), rate, accentFile); r.failed()) return r;
        if (auto r = writeWav (synthesize (sound, false, rate), rate, beatFile); r.failed())  return r;

        pointClickAt (engine, accentFile, beatFile);
        return juce::Result::ok();
    }
}
