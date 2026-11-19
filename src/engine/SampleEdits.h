#pragma once

#include "Editing.h"

namespace carthage::editing
{
    /** The sample editor's model (phase 4e, W42). A sample is every audio clip of one file until
        Make unique gives a clip a sample of its own ("Kick (2)"); copies of that clip stay in it.
        The sound of a sample (gain, fades, reverse, pitch, which part of the file plays) is the
        same on every clip of it: an edit is applied to all of them, as one undo step. The file
        itself is never changed (W42). Message thread; tested in tests/SampleEditTests.cpp. */

    /** What makes clips one sample: a name given by Make unique, else the file. */
    juce::String sampleKey (const tracktion::Clip&);

    /** "Kick", or "Kick (2)" after Make unique. */
    juce::String sampleName (const tracktion::Clip&);

    /** Every audio clip on the lanes in the same sample as `clip` (itself included). */
    Clips clipsOfSample (const tracktion::Clip&);

    /** Gives `clip` a sample of its own, named after the file with the lowest free number:
        "Kick (2)", "Kick (3)"… One undo step. Returns the new name. */
    juce::String makeUnique (tracktion::Clip&);

    /** The sound of a sample, as one clip of it has it. Times are seconds on the timeline. */
    struct SampleSound
    {
        float gainDb = 0.0f;
        double fadeIn = 0.0, fadeOut = 0.0;
        bool reversed = false;
        float pitch = 0.0f;           ///< semitones, -24 to 24
        double start = 0.0;           ///< where in the file it starts playing
        double length = 0.0;          ///< how much of it plays
    };

    SampleSound soundOf (const tracktion::Clip&);

    /** Gives every clip of `clip`'s sample this sound. The caller opens the undo step (begin()),
        so a whole drag of a control undoes at once. */
    void setSound (const tracktion::Clip&, const SampleSound&);

    /** Fits every clip of the sample to the project tempo (W36, fitToTempo), from `bpm`. */
    void fitSampleToTempo (const tracktion::Clip&, double bpm);
}
