#pragma once

#include "theme/Theme.h"

#include <engine/Session.h>
#include <engine/time/Grid.h>

#include <map>

namespace carthage::ui
{
    /** The timeline (brief, feature 2): lanes of clips under a bar and beat ruler, with the lane
        names on the left.

        It reads clips and tracks from the edit and listens to it; it keeps no copy of them
        (AGENTS.md, "Model and editing"). Positions are beats; the shared grid (engine/time/Grid)
        draws the grid lines. Only what is in view is drawn: clips are culled to the visible beats
        and lanes, and waveforms come from juce::AudioThumbnail, read on the thumbnail cache's own
        thread. The playhead and the take being recorded are separate children over the lanes, so
        moving them does not repaint the clips.

        Mouse: the wheel scrolls (Shift or a sideways swipe scrolls in time); Cmd/Ctrl + wheel or
        a trackpad pinch zooms in time around the pointer; clicking the ruler moves the playhead. */
    class Timeline final : public juce::Component, private juce::ValueTree::Listener, private juce::ScrollBar::Listener
    {
    public:
        explicit Timeline (engine::Session&);
        ~Timeline() override;

        /** Once per frame: where the playhead is, and the start of the take in progress (if any). */
        void setPlayhead (double seconds, std::optional<double> recordingFrom);

        time::Grid& getGrid() noexcept   { return grid; }
        void gridChanged()               { repaintLanes(); }

        /** View: pixels per beat, and the beat at the left edge of the lanes. */
        double getPixelsPerBeat() const noexcept    { return pixelsPerBeat; }
        void setZoom (double newPixelsPerBeat, double aroundBeat);
        void setScrollBeat (double beat);

        /** Beats and pixels, in this component's coordinates. */
        double beatAtX (float x) const noexcept;
        float xAtBeat (double beat) const noexcept;
        double beatsPerBar() const;

        void resized() override;
        void paint (juce::Graphics&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void mouseMagnify (const juce::MouseEvent&, float scale) override;

        static constexpr int rulerHeight = 28, headerWidth = 168, laneHeight = 72;
        static constexpr double minPixelsPerBeat = 0.5, maxPixelsPerBeat = 2000.0;

    private:
        class Ruler;
        class Headers;
        class Lanes;
        class Playhead;
        class RecordingRegion;

        engine::Session& session;
        juce::ValueTree editState;
        time::Grid grid;
        double pixelsPerBeat = 24.0;
        double scrollBeat = 0.0;
        int scrollY = 0;

        juce::AudioFormatManager formats;
        juce::AudioThumbnailCache thumbnailCache { 64 };
        std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> thumbnails;   // by file path

        std::unique_ptr<Ruler> ruler;
        std::unique_ptr<Headers> headers;
        std::unique_ptr<Lanes> lanes;
        std::unique_ptr<Playhead> playhead;
        std::unique_ptr<RecordingRegion> recording;
        juce::ScrollBar hScroll { false }, vScroll { true };

        double lastPlayheadSeconds = -1.0;
        std::optional<double> lastRecordingFrom;

        juce::AudioThumbnail& thumbnailFor (const juce::File&);
        juce::Array<tracktion::AudioTrack*> laneTracks() const;
        juce::Colour laneColour (int laneIndex) const;
        double secondsToBeats (double seconds) const;
        double beatsToSeconds (double beats) const;
        void repaintLanes();
        void updateScrollBars();
        juce::Rectangle<int> lanesArea() const;

        void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override             { repaintLanes(); }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override      { repaintLanes(); }
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { repaintLanes(); }
    };
}
