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

    /** Renames the sample (owner, 4e): every clip of it takes the name, as one undo step. Refused,
        changing nothing, when the name is empty or another sample has it (they would merge). */
    juce::Result renameSample (const tracktion::Clip&, const juce::String& newName);

    /** The sound of a sample, as one clip of it has it. Times are seconds on the timeline. */
    struct SampleSound
    {
        float gainDb = 0.0f;
        double fadeIn = 0.0, fadeOut = 0.0;

        /** The fades' shapes (owner, 4e): Tracktion's own curves. */
        tracktion::AudioFadeCurve::Type fadeInShape = tracktion::AudioFadeCurve::linear,
                                        fadeOutShape = tracktion::AudioFadeCurve::linear;
        bool reversed = false;
        float pitch = 0.0f;           ///< semitones, -24 to 24
        float formant = 0.0f;         ///< semitones, -12 to 12: the colour, not the pitch (FormantPlugin)
        double start = 0.0;           ///< where in the file it starts playing
        double length = 0.0;          ///< how much of it plays
    };

    SampleSound soundOf (const tracktion::Clip&);

    /** Gives every clip of `clip`'s sample this sound. The caller opens the undo step (begin()),
        so a whole drag of a control undoes at once. */
    void setSound (const tracktion::Clip&, const SampleSound&);

    /** The sample's volume envelope (owner, 4e): points drawn on its waveform, FL-style. Times are
        seconds into the file; the gain is in dB (-60 is silence); `bend` (-1 to 1) curves the
        line to the next point. Kept in each clip's own state, so it saves and undoes with the
        project. It plays through a volume plugin on the clip whose curve, in timeline time
        (checked by render), is worked out from these points: syncEnvelope. */
    struct EnvelopePoint
    {
        double time = 0.0;
        float gainDb = 0.0f;
        float bend = 0.0f;
    };

    std::vector<EnvelopePoint> envelopeOf (const tracktion::Clip&);

    /** Gives every clip of the sample this envelope (empty: none). The caller opens the undo step. */
    void setEnvelope (const tracktion::Clip&, std::vector<EnvelopePoint>);

    /** Makes the clip's playing curve match its envelope where the clip is now (moved, trimmed,
        stretched, reversed, undone). Cheap when nothing changed. Not an undo step: the curve is
        worked out from the envelope, which is. */
    void syncEnvelope (tracktion::Clip&);

    /** The envelope's gain (linear, 0 to 1) at `fileTime` on this clip, read from the curve that
        plays, so the editor draws exactly what is heard; 1 with no envelope. */
    float envelopeGainAt (tracktion::Clip&, double fileTime);
    void syncEnvelopes (tracktion::Edit&);

    /** Fits every clip of the sample to the project tempo (W36, fitToTempo), from `bpm`. */
    void fitSampleToTempo (const tracktion::Clip&, double bpm);
}
