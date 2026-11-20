#pragma once

#include "Mixer.h"

#include <tracktion_engine/tracktion_engine.h>

#include <optional>
#include <vector>

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

    /** What happens to clips when the tempo changes (W38, asked each time unless remembered):
        they keep their place in seconds, move with the beat at their own speed, or stretch with
        the beat, keeping their pitch. Fitted loops (auto-tempo) always follow the beat. */
    enum class TempoFollow { leaveInTime, moveWithBeat, stretch };

    /** Sets the project's tempo with every clip left where it is in seconds (Tracktion's own
        setBpm moves them); followTempo then does what the person chose. */
    void setTempo (tracktion::Edit&, double bpm);

    /** Where every lane clip sits in beats, taken before the tempo changes. */
    struct BeatPlace { tracktion::EditItemID clip; double start = 0.0, end = 0.0; bool fitted = false; };
    std::vector<BeatPlace> placesInBeats (tracktion::Edit&);

    /** After a tempo change: puts the clips taken in `before` back on their beats, as `how`
        says. Returns how many clips changed. */
    int followTempo (tracktion::Edit&, const std::vector<BeatPlace>& before, TempoFollow how);

    /** Slides the content inside the clip; the clip stays put (Cmd/Ctrl drag). */
    void slip (tracktion::Clip&, double deltaBeats);

    /** Splits each clip that spans `beat` into two; returns the new right-hand parts. */
    Clips split (tracktion::Edit&, const Clips&, double beat);

    /** Copies the clips right after the last one, keeping their gaps; returns the copies. */
    Clips duplicate (const Clips&);

    /** A copy of `source` on `lane`, starting at `beat`, routed as its sample is (the draw and
        paint tools). */
    tracktion::Clip* placeCopy (const tracktion::Clip& source, const Lane& lane, double beat);

    /** A sound file as a new clip on `lane` starting at `beat`, as long as the file (a dropped
        sample, W30), playing to master until routed (W47). Null if the file can not be read. */
    tracktion::Clip* insertFile (tracktion::Edit&, const Lane& lane, const juce::File&, double beat);

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
        const tracktion::Edit* from = nullptr;   // pasted into the same edit, a clip keeps its route

        int count() const noexcept     { return static_cast<int> (clips.clips.size()); }
    };

    std::shared_ptr<const Copied> copy (const Clips&);

    /** Pastes into `edit` with the first clip at `atBeat`, on the lanes the clips were copied
        from (clamped to the lanes the edit has); returns the new clips. */
    Clips paste (tracktion::Edit&, const Copied&, double atBeat);

    /** The loop region (4g): a range of beats kept in the edit with undo, so setting, changing
        and clearing it are each one undo step. Tracktion's transport keeps its loop points outside
        undo, so it follows the region (syncLoop) rather than holding it. In beats, so a tempo
        change keeps the region on its bars. */
    struct LoopRegion
    {
        double from = 0.0, to = 0.0;
    };

    std::optional<LoopRegion> loopRegion (const tracktion::Edit&);

    /** Sets the region (either order; at least a tick long) and turns looping on. */
    void setLoopRegion (tracktion::Edit&, double fromBeat, double toBeat);

    /** Removes the region; looping turns off. */
    void clearLoopRegion (tracktion::Edit&);

    /** Looping on or off, kept with the region (outside undo, as the transport is). Without a
        region there is nothing to loop and it stays off. */
    void setLooping (tracktion::Edit&, bool);
    bool isLooping (const tracktion::Edit&);

    /** Puts the transport's loop points where the region is, after any edit or undo; a region
        that comes back (an undone clear) loops again. */
    void syncLoop (tracktion::Edit&);

    /** A project from before 4g keeps the loop points it had on its transport as its region. */
    void adoptTransportLoop (tracktion::Edit&);
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

    /** The project's notes (W38): plain text kept in the edit itself, so they are saved with the
        project and a change to them counts as an unsaved change. Not an undo step. */
    juce::String notes (tracktion::Edit&);
    void setNotes (tracktion::Edit&, const juce::String&);

}
