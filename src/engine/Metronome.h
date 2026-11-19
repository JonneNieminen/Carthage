#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace carthage::metronome
{
    /** The metronome's sounds (owner's list F22, W38): three made by Carthage, or the person's own.
        Tracktion's click track plays them: it reads two WAV files, one for the first beat of a
        bar (the accent) and one for the other beats, so Carthage writes its sounds as WAV files
        into its own folder and points the click track at them. */
    enum class Sound { wood, hiHat, cowbell, custom };

    /** A click made from nothing but maths, so it needs no bundled files: short (under 0.3 s),
        mono, the accent louder and a little brighter. The same every time (fixed noise seed). */
    juce::AudioBuffer<float> synthesize (Sound, bool accent, double sampleRate);

    /** Writes the two click files for `sound` into `folder` and points the engine's click track
        at them. For Sound::custom, `customFile` (any format Carthage reads) is the sound, the
        other beats at a lower level. Fails, changing nothing, when the custom file can't be read. */
    juce::Result use (tracktion::Engine&, Sound, const juce::File& customFile, const juce::File& folder);
}
