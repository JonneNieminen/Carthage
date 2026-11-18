#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace carthage::editing
{
    /** Every timeline edit, as plain functions on the edit model (AGENTS.md, "Model and editing").
        Positions are beats. Each function changes the edit through Tracktion's undo manager; the
        caller opens one named transaction per user action (begin()), so a whole drag, which calls
        these many times, undoes in one step. Tested headless in tests/EditingTests.cpp.

        Message thread. */

    using Clips = juce::Array<tracktion::Clip*>;

    /** Starts one undoable step with a name the Edit menu shows ("Move clip", "Trim 3 clips"). */
    void begin (tracktion::Edit&, const juce::String& name);

    /** "Move clip" or "Move 3 clips": names follow the count. */
    juce::String named (const juce::String& verb, int count, const juce::String& noun = "clip");

    double startBeat (const tracktion::Clip&);
    double endBeat (const tracktion::Clip&);

    /** Moves clips by `deltaBeats` (never before 0) and by `laneDelta` lanes, keeping their order. */
    void move (const Clips&, double deltaBeats, int laneDelta);

    /** Trims without touching the audio: the start moves and the content stays where it is in time. */
    void trimStart (tracktion::Clip&, double newStartBeat);
    void trimEnd (tracktion::Clip&, double newEndBeat);

    /** Changes the clip's length by time-stretching its content to fill it (Option/Alt drag, S tool). */
    void stretchEnd (tracktion::Clip&, double newEndBeat);

    /** Makes an audio clip of a loop at `bpm` follow the project's tempo (W30's option): its
        content is time-stretched, keeping its pitch, and it keeps its length in beats. */
    void fitToTempo (tracktion::Clip&, double bpm);

    /** Slides the content inside the clip; the clip stays put (Cmd/Ctrl drag). */
    void slip (tracktion::Clip&, double deltaBeats);

    /** Splits each clip that spans `beat` into two; returns the new right-hand parts. */
    Clips split (tracktion::Edit&, const Clips&, double beat);

    /** Copies the clips right after the last one, keeping their gaps; returns the copies. */
    Clips duplicate (const Clips&);

    /** A copy of `source` on `lane`, starting at `beat` (the draw and paint tools). */
    tracktion::Clip* placeCopy (const tracktion::Clip& source, tracktion::AudioTrack& lane, double beat);

    /** A sound file as a new clip on `lane` starting at `beat`, as long as the file (a dropped
        sample, W30). Null if the file can not be read. */
    tracktion::Clip* insertFile (tracktion::AudioTrack& lane, const juce::File&, double beat);

    void remove (const Clips&);

    /** Mutes every clip if any is playing, otherwise unmutes them all (the T tool, and its key). */
    void toggleMute (const Clips&);

    /** What Copy holds (W30: copy and paste between tabs from the start). Each clip keeps
        everything Tracktion stores about it, its audio file written as an absolute path so it
        pastes into any project. Positions are seconds from the first clip, since an audio clip's
        content is fixed in time whatever the tempo; lanes count from the topmost clip's lane. */
    struct Copied
    {
        tracktion::Clipboard::Clips clips;
        int firstLane = 0;

        int count() const noexcept     { return static_cast<int> (clips.clips.size()); }
    };

    std::shared_ptr<const Copied> copy (const Clips&);

    /** Pastes into `edit` with the first clip at `atBeat`, on the lanes the clips were copied
        from (clamped to the lanes the edit has); returns the new clips. */
    Clips paste (tracktion::Edit&, const Copied&, double atBeat);

    void setLoopRange (tracktion::Edit&, double startBeat, double endBeat);
    tracktion::MarkerClip* addMarker (tracktion::Edit&, double beat);

    /** Makes the edit find clip files the way Tracktion writes their paths (DECISIONS W32). The
        pinned Tracktion writes a relative clip path with getRelativePathFrom (editFile), which
        counts the edit file as a folder ("../Recordings/Take.wav" for a take beside it), but its
        default resolver reads it as a sibling of the edit file, one folder too high: clips beside
        the edit could not find their audio. Call once on every edit Carthage creates or loads. */
    void useConsistentFilePaths (tracktion::Edit&);

    /** The same rule as a resolver for an edit not built yet (Edit::Options::filePathResolver),
        so clips find their files while the edit loads. `editFile` is asked each time. */
    tracktion::Edit::FilePathResolver consistentFilePaths (std::function<juce::File()> editFile);

    /** Lanes in order: the edit's audio tracks. */
    juce::Array<tracktion::AudioTrack*> lanes (tracktion::Edit&);
    int laneOf (const tracktion::Clip&);
}
