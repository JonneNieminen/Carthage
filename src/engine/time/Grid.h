#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

namespace carthage::time
{
    /** The one grid and snap system for the timeline and the piano roll (brief, features 2 and 7;
        AGENTS.md "Model and editing"). Positions are in beats; nothing here draws or knows about
        Tracktion. Tested in tests/GridTests.cpp. */

    /** A grid step. `bar` and `beat` follow the meter; the rest are fractions of a beat, so the
        default, a quarter beat, is four steps per beat. */
    enum class Division { bar, beat, half, quarter, eighth, sixteenth, thirtySecond, sixtyFourth };

    /** Straight, triplet (three in the space of two) or dotted (one and a half times as long). */
    enum class Feel { straight, triplet, dotted };

    struct Grid
    {
        Division division = Division::quarter;   ///< the brief's default: 1/4 beat
        Feel feel = Feel::straight;
        bool adaptive = false;   ///< follow the zoom: the step grows until lines are at least minPixels apart
        bool enabled = true;     ///< the grid toggle (shortcut and toolbar button)

        /** The step in beats, for a bar of `beatsPerBar` beats, as drawn at `pixelsPerBeat`. */
        double stepBeats (double beatsPerBar, double pixelsPerBeat) const;

        /** The nearest grid line to `beat` (ties go to the later line). */
        double nearestLine (double beat, double beatsPerBar, double pixelsPerBeat) const;

        /** Lines from `startBeat` to `endBeat` inclusive, for drawing; at most `maxLines`. */
        std::vector<double> lines (double startBeat, double endBeat, double beatsPerBar,
                                   double pixelsPerBeat, size_t maxLines = 4096) const;

        /** "1/4 beat", "1/8 beat triplet", "Bar", "Adaptive (1/4 beat)": what the grid menu shows. */
        juce::String describe (double beatsPerBar, double pixelsPerBeat) const;

        static constexpr double minPixels = 6.0;   ///< closest adaptive lines may be drawn
    };

    /** Everything a drag may snap to, besides the grid. All in beats. */
    struct SnapTargets
    {
        std::vector<double> clipEdges;
        std::vector<double> markers;
        std::optional<double> playhead;
    };

    struct SnapResult
    {
        double beat = 0.0;
        enum class To { nothing, grid, clipEdge, marker, playhead } to = To::nothing;
    };

    /** Where `beat` lands: the closest of the grid line, clip edges, markers and the playhead within
        `radiusPixels` on screen. Clip edges, markers and the playhead win a tie with the grid, so a
        clip can meet another exactly. `bypass` (Cmd/Ctrl held for this drag) returns `beat` as is. */
    SnapResult snap (double beat, const Grid&, const SnapTargets&, double beatsPerBar, double pixelsPerBeat,
                     bool bypass, double radiusPixels = 8.0);
}
