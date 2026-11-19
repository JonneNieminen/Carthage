#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

namespace carthage::audio
{
    /** The formats Carthage reads, and only those (W30): WAV, AIFF, FLAC, Ogg Vorbis and MP3.
        Not JUCE's registerBasicFormats(): on macOS that adds the CoreAudio reader, which
        Carthage does not list, and which spins on some damaged files (found by fuzzing, phase 4d;
        tools/fuzz/seeds/audio/caf-spin is the input). Every reader in Carthage registers these. */
    inline void registerReadable (juce::AudioFormatManager& formats)
    {
        formats.registerFormat (new juce::WavAudioFormat(), true);
        formats.registerFormat (new juce::AiffAudioFormat(), false);
        formats.registerFormat (new juce::FlacAudioFormat(), false);
        formats.registerFormat (new juce::OggVorbisAudioFormat(), false);
       #if JUCE_USE_MP3AUDIOFORMAT
        formats.registerFormat (new juce::MP3AudioFormat(), false);
       #endif
    }
}
