#pragma once

#include "Controls.h"
#include "theme/Theme.h"

#include <engine/Editing.h>
#include <engine/Session.h>
#include <engine/time/Grid.h>

#include <map>
#include <set>

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

        /** The tools (F7), in the command table's order (app::commands::tools()). */
        enum class Tool { select, draw, paint, slice, erase, mute, zoom, stretch };

        void setTool (Tool);
        Tool getTool() const noexcept   { return tool; }

        /** Called when a tool did something with the mouse (a held tool key then reverts on release). */
        std::function<void()> onToolUsed;

        /** A short message for the status line ("Click a clip first to choose what to draw"). */
        std::function<void (const juce::String&)> onStatus;

        /** Selection: clips by their ids, so it survives undo. */
        editing::Clips getSelectedClips() const;
        void selectOnly (const editing::Clips&);
        void clearSelection();
        std::function<void()> onSelectionChanged;

        /** A double-click on a clip: the window opens the sample editor for it (W42). */
        std::function<void (tracktion::Clip&)> onClipDoubleClicked;

        /** The edit operations the keyboard drives, each one named undo step. */
        void splitAtPlayhead();
        void duplicateSelection();
        void deleteSelection (bool cut = false);

        /** Copy and paste, also between projects: what was copied outlives a tab switch, because
            the window has one timeline whichever project is in front. */
        void copySelection();
        void paste();
        bool canPaste() const noexcept      { return copied != nullptr && copied->count() > 0; }
        void nudgeSelection (int steps);
        void addMarkerAtPlayhead();

        /** Adds `count` lanes below `after` (or at the end) and scrolls to the first. */
        void addLane (tracktion::AudioTrack* after = nullptr, int count = 1);

        /** The current lane: the one whose name was clicked last (highlighted). ⌥⌘N adds a lane
            below it, ⌘↑/⌘↓ move it (owner's list). */
        void addLaneBelowCurrent();
        void moveCurrentLane (int delta);
        void loopSelection();

        /** Looping turned on or off from outside (the loop button, L): the region's look follows. */
        void loopChanged();

        /** Once per frame: where the playhead is, and the start of the take in progress (if any). */
        void setPlayhead (double seconds, std::optional<double> recordingFrom);

        time::Grid& getGrid() noexcept   { return grid; }
        void gridChanged()               { repaintLanes(); }

        /** The session opened another project: forget the old edit's clips and follow the new one. */
        void projectChanged();

        /** Zoom, scroll and grid, saved in the project file (engine/Project.h carries it). */
        juce::ValueTree getViewState() const;
        void setViewState (const juce::ValueTree&);

        /** View: pixels per beat, and the beat at the left edge of the lanes. */
        double getPixelsPerBeat() const noexcept    { return pixelsPerBeat; }
        void setZoom (double newPixelsPerBeat, double aroundBeat);
        void setScrollBeat (double beat);

        /** What a plain or Shift wheel over the lanes does: scroll sideways (in time) or down, and by
            how much. Shift is sideways; macOS already delivers a mouse wheel's turn with Shift as
            deltaX, other systems in deltaY, and both count (owner, 4f wheel check). */
        struct WheelScroll { bool sideways; float delta; };
        static WheelScroll wheelScroll (bool shift, const juce::MouseWheelDetails&) noexcept;

        /** Beats and pixels, in this component's coordinates. */
        double beatAtX (float x) const noexcept;
        float xAtBeat (double beat) const noexcept;
        double beatsPerBar() const;

        void resized() override;
        void paint (juce::Graphics&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void mouseMagnify (const juce::MouseEvent&, float scale) override;

        static constexpr int rulerHeight = 28;

        /** The lane names' column (owner's list): drag its border, double-click fits the names. */
        static constexpr int defaultHeaderWidth = 168, minHeaderWidth = 120, maxHeaderWidth = 400;
        int getHeaderWidth() const noexcept   { return headerWidth; }
        void setHeaderWidth (int);

        /** Lane height (W38): Option + scroll and Option+Cmd+plus / minus change every lane, the
            default with them; a lane's own border sets that lane alone (kept in the project),
            and a double-click there returns it to the default. */
        static constexpr int defaultLaneHeight = 72, minLaneHeight = 40, maxLaneHeight = 240, laneHeightStep = 12;
        int getLaneHeight() const noexcept   { return laneHeight; }
        void setLaneHeight (int newHeight, std::optional<int> keepLaneAtY = {});
        static constexpr double minPixelsPerBeat = 0.5, maxPixelsPerBeat = 2000.0;

    private:
        class Ruler;
        class Headers;
        class Lanes;
        class Playhead;
        class RecordingRegion;

        engine::Session& session;
        juce::ValueTree editState;
        Tool tool = Tool::select;
        juce::Array<tracktion::EditItemID> selected;
        tracktion::EditItemID brush;   // the clip the draw and paint tools place
        time::Grid grid;
        double pixelsPerBeat = 24.0;
        double scrollBeat = 0.0;
        int scrollY = 0;
        int laneHeight = defaultLaneHeight;   // the default for lanes without a height of their own
        int headerWidth = defaultHeaderWidth;
        tracktion::EditItemID currentLane;
        tracktion::AudioTrack* findCurrentLane() const;

        // Lane geometry, in content pixels (before scrolling).
        int heightOfLane (int index) const;
        int laneTop (int index) const;
        int lanesHeight() const;
        int laneIndexAt (int contentY) const;   // -1 above, size() and up below the last lane

        // Each lane's top, and the end of the last, worked out once per change of the lanes
        // (repaintLanes clears it): painting asks for them per lane, per frame.
        mutable std::vector<int> tops;
        const std::vector<int>& laneTops() const;
        void setLaneOwnHeight (tracktion::AudioTrack&, std::optional<int>);

        std::shared_ptr<const editing::Copied> copied;

        juce::AudioFormatManager formats;
        DiskThumbnailCache thumbnailCache;   // overviews on disk (W38)
        std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> thumbnails;   // by file path
        std::map<juce::String, juce::File> resolvedFiles;   // see fileOf

        std::unique_ptr<Ruler> ruler;
        std::unique_ptr<Headers> headers;
        std::unique_ptr<Lanes> lanes;
        std::unique_ptr<Playhead> playhead;
        std::unique_ptr<RecordingRegion> recording;
        juce::ScrollBar hScroll { false }, vScroll { true };
        juce::TextButton addLaneButton { "Add lane" };   // in the corner above the lane headers
        PanelEdge headerEdge;                            // the border between lane names and the grid
        int fittingHeaderWidth() const;

        double lastPlayheadSeconds = -1.0;
        std::optional<double> lastRecordingFrom;

        juce::AudioThumbnail& thumbnailFor (const juce::File&);

        /** A clip's audio file. Resolving a path looks at the disk (editing::consistentFilePaths),
            which painting never does, so each source path is resolved once per project file and
            kept; a Save as changes the project file, so it resolves afresh. */
        juce::File fileOf (tracktion::WaveAudioClip&);

        /** A file's key and tempo for the clips on the grid (4e): the library's, else analysed once
            in the background and kept in the library, so the browser and the editor share it. */
        std::optional<engine::SampleLibrary::Analysis> analysisFor (const juce::File&);
        juce::ThreadPool analysisPool { juce::ThreadPoolOptions{}.withThreadName ("Carthage clip analysis").withNumberOfThreads (1) };
        std::set<juce::String> analysisAsked;
        std::map<juce::String, engine::SampleLibrary::Analysis> knownFacts;   // painting never touches the disk

        /** "Am  120", "F#", "120": a clip's facts, short enough for its name row. */
        static juce::String shortFacts (const engine::SampleLibrary::Analysis&);
        juce::Array<tracktion::AudioTrack*> laneTracks() const;
        juce::Colour laneColour (int laneIndex) const;

        /** A lane's colour (owner): one of the theme's lane colours, or none for the colour its
            place gives it. One undo step; kept in the project. */
        void setLaneColour (tracktion::AudioTrack&, std::optional<juce::Colour>);
        double secondsToBeats (double seconds) const;
        double beatsToSeconds (double beats) const;
        double playheadBeat() const;
        time::SnapTargets snapTargets (const editing::Clips& excluding) const;
        bool isSelected (const tracktion::Clip&) const;
        void repaintLanes();
        void updateScrollBars();
        juce::Rectangle<int> lanesArea() const;

        void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override             { repaintLanes(); }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override      { repaintLanes(); }
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    };
}
