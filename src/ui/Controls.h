#pragma once

#include "Motion.h"
#include "theme/Theme.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace carthage::ui
{
    /** A square button that draws a glyph: transport and small actions. Round caps, no fill until
        hovered; `lit` draws it in the given signal colour (record armed, playing). */
    class IconButton final : public juce::Button
    {
    public:
        enum class Glyph { record, play, stop, toStart, settings, close, popOut, popIn, metronome, loop, mixer, punch };

        explicit IconButton (Glyph);

        void setGlyph (Glyph g)                 { glyph = g; repaint(); }
        // A right-click is for the "Copy tooltip" menu, never a press.
        void mouseDown (const juce::MouseEvent& e) override   { if (! e.mods.isPopupMenu()) juce::Button::mouseDown (e); }
        void mouseUp (const juce::MouseEvent& e) override     { if (! e.mods.isPopupMenu()) juce::Button::mouseUp (e); }
        void mouseDrag (const juce::MouseEvent& e) override   { if (! e.mods.isPopupMenu()) juce::Button::mouseDrag (e); }

        void setLit (juce::Colour c, bool on)   { litColour = c; lit = on; repaint(); }

        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        Glyph glyph;
        juce::Colour litColour = theme::accent;
        bool lit = false;
    };

    /** A tool in the toolbar: its icon, and the key that picks it in small figures beside it.
        The chosen tool is live, so it takes the accent (AGENTS.md, UI). */
    class ToolButton final : public juce::Button
    {
    public:
        /** The tools' icons, in the order of Timeline::Tool. */
        enum class Glyph { select, draw, paint, slice, erase, mute, zoom, stretch };

        explicit ToolButton (Glyph);

        void setKeyText (const juce::String& k)  { keyText = k; repaint(); }
        /** Chosen or not; a change cross-fades over theme::fast when motion is allowed (W45). */
        void setChosen (bool on);

        /** In a narrow toolbar the key is left to the tooltip. */
        void setShowsKey (bool on)               { showsKey = on; repaint(); }
        // A right-click is for the "Copy tooltip" menu, never a press.
        void mouseDown (const juce::MouseEvent& e) override   { if (! e.mods.isPopupMenu()) juce::Button::mouseDown (e); }
        void mouseUp (const juce::MouseEvent& e) override     { if (! e.mods.isPopupMenu()) juce::Button::mouseUp (e); }
        void mouseDrag (const juce::MouseEvent& e) override   { if (! e.mods.isPopupMenu()) juce::Button::mouseDrag (e); }


        static constexpr int width = 38, narrowWidth = 28, height = 28;

        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        Glyph glyph;
        juce::String keyText;
        bool chosen = false, showsKey = true;
        float chosenAmount = 0.0f;   // 0 to 1, follows `chosen` through the cross-fade
        motion::Tween tween { *this };
    };

    /** An on/off switch (W45): its thumb slides when it is clicked, over theme::fast, unless
        motion is not allowed; set from the model, it jumps. Drawn by the LookAndFeel, which reads
        where the thumb is from the "switchAt" property (0 off, 1 on). */
    class Switch final : public juce::ToggleButton
    {
    public:
        explicit Switch (const juce::String& text);

    private:
        motion::Tween tween { *this };
        void clicked() override;
    };

    /** The border of a panel, to drag (owner's list F22): resize it by dragging, double-click to
        fit its contents. It draws nothing until the pointer is over it. */
    class PanelEdge final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        PanelEdge();

        /** For a panel under something (the sample editor): the edge runs across, dragging it up
            makes the panel taller. */
        void setAlongTop (bool along)   { alongTop = along; setMouseCursor (along ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::LeftRightResizeCursor); }

        /** The panel's width now; a drag reports the width plus the distance dragged. */
        std::function<int()> getCurrentWidth;
        std::function<void (int newWidth)> onResize;
        std::function<void()> onFit;

        static constexpr int thickness = 6;

        void paint (juce::Graphics&) override;
        void mouseEnter (const juce::MouseEvent&) override   { repaint(); }
        void mouseExit (const juce::MouseEvent&) override    { repaint(); }
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override      { repaint(); }
        void mouseDoubleClick (const juce::MouseEvent&) override;

    private:
        int widthAtStart = 0;
        bool alongTop = false;
    };

    /** Peak meter with a held peak, in a recessed well: ok below -18 dBFS, warn to -6, clip above. */
    class LevelMeter final : public juce::Component
    {
    public:
        LevelMeter() = default;

        /** Feed the newest peak (linear) once per frame; the meter falls back at a fixed rate. */
        void push (float peak, double secondsSinceLastFrame);
        void paint (juce::Graphics&) override;

        bool isClipped() const noexcept     { return clipHold > 0.0; }

    private:
        float level = 0.0f, held = 0.0f;
        double holdTime = 0.0, clipHold = 0.0;
    };

    /** Every control that adjusts a value is one of these (DECISIONS W37): a double-click puts it
        back to its default, and the scroll wheel over it adjusts it, with Shift for fine steps (a
        tenth). Switches and menus are not, and ignore the wheel, so scrolling a panel never
        changes a choice by accident. */
    /** A scroll bar whose thumb can be pulled longer or shorter by its ends to zoom (owner): the
        thumb is the part in view, so dragging an end out shows more, in shows less. The middle
        scrolls as usual; small marks show the ends can be held. */
    class ZoomScrollBar final : public juce::ScrollBar
    {
    public:
        using juce::ScrollBar::ScrollBar;

        /** While an end is dragged: the range the view should now show. The owner zooms to it
            and sets the scroll bar's range again. */
        std::function<void (juce::Range<double>)> onZoom;
        std::function<void()> onZoomStart;   ///< when an end is taken hold of

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        enum class End { none, start, finish };
        juce::Range<float> thumb();   // along the bar, in pixels (non-const: the look asks for a ScrollBar&)
        End endAt (int position);
        int along (const juce::MouseEvent& e) const   { return isVertical() ? e.y : e.x; }

        End held = End::none, hovered = End::none;
        juce::Range<double> rangeAtDown;
        int downAt = 0;
    };

    class ValueSlider final : public juce::Slider
    {
    public:
        explicit ValueSlider (double defaultValue)
        {
            setDefault (defaultValue);
            setScrollWheelEnabled (true);
        }

        void setDefault (double v)   { setDoubleClickReturnValue (true, v); }

        /** A fader that only its cap takes hold of (owner): a click elsewhere on its track goes
            through to what is behind it (the mixer strip, which selects), and never jumps the
            value. The wheel is unaffected. */
        bool onlyThumbGrabs = false;

        bool hitTest (int x, int y) override
        {
            if (! onlyThumbGrabs || ! isVertical())
                return juce::Slider::hitTest (x, y);

            const auto thumb = getPositionOfValue (getValue());
            return std::abs (static_cast<float> (y) - thumb) <= 13.0f;
        }

        /** When set and false, the wheel passes on to the parent instead (the mixer: only the
            selected channel's controls take it; elsewhere it scrolls, W51). */
        std::function<bool()> acceptsWheel;

        /** Called when the wheel starts moving it after a pause: a wheel gesture is one undo step,
            as a drag is (onDragStart), not one per tick (computer-use test, 2026-11-20). */
        std::function<void()> onWheelStart;

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            // Cmd+wheel belongs to the parent (the mixer zooms) when the wheel is gated.
            if (acceptsWheel != nullptr && (! acceptsWheel() || e.mods.isCommandDown()))
            {
                if (auto* parent = getParentComponent())   // on to the parent: the mixer scrolls
                    parent->mouseWheelMove (e.getEventRelativeTo (parent), wheel);

                return;
            }

            if (const auto now = juce::Time::getMillisecondCounter(); now - lastWheel > 600)
                if (onWheelStart != nullptr)
                    onWheelStart();

            lastWheel = juce::Time::getMillisecondCounter();
            auto w = wheel;

            if (e.mods.isShiftDown())
            {
                // macOS sends a mouse wheel's turn with Shift as a sideways scroll, which JUCE's
                // slider reads reversed (-deltaX): take it as the wheel turn it was (4f wheel check).
                if (w.deltaY == 0.0f)
                    std::swap (w.deltaX, w.deltaY);

                w.deltaX *= 0.1f;
                w.deltaY *= 0.1f;
            }

            juce::Slider::mouseWheelMove (e, w);
        }

    private:
        juce::uint32 lastWheel = 0;
    };

    /** The audio-thread load: a thin bar and a figure. Turns warn above 70 %, clip above 90 %. */
    class LoadMeter final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        void setLoad (float averageFraction, float peakFraction);
        void paint (juce::Graphics&) override;

    private:
        float average = 0.0f, peak = 0.0f, shownPeak = 0.0f;
    };

    /** Copyable tooltips (W38): the text of the tooltip showing now (empty if none), and the
        right-click "Copy tooltip" offered on controls that have no menu of their own. */
    juce::String showingTooltip();
    void copyToClipboard (const juce::String&);

    class TooltipMenu final : public juce::MouseListener
    {
    public:
        void mouseDown (const juce::MouseEvent&) override;
    };

    /** Waveform overviews kept on disk (W38), so a project opened again draws at once instead of
        reading every file anew. Keyed by the file's path and modification time (JUCE's hash for
        a file source), so a changed file gets a new overview. The newest `maxFiles` are kept. */
    class DiskThumbnailCache final : public juce::AudioThumbnailCache
    {
    public:
        DiskThumbnailCache (int inMemory, juce::File folder);

        static constexpr int maxFiles = 4000;

    protected:
        void saveNewlyFinishedThumbnail (const juce::AudioThumbnailBase&, juce::int64 hash) override;
        bool loadNewThumb (juce::AudioThumbnailBase&, juce::int64 hash) override;

    private:
        juce::File folder;
        juce::File fileFor (juce::int64 hash) const;
    };

    /** Small helpers so every label is set the same way. */
    void styleLabel (juce::Label&, float size = theme::textMd, juce::Colour = theme::text, bool medium = false);
    void styleFigure (juce::Label&, float size = theme::textMd, juce::Colour = theme::text);

    /** The name of the view it is in, quiet at that view's edge (owner): "Main grid", "Mixer",
        "Sample editor". Drawn right-aligned in `area` on a patch of `behind`, so whatever runs
        under it (bar numbers) stops short of it instead of colliding. */
    void drawViewName (juce::Graphics&, juce::Rectangle<int> area, const juce::String& name, juce::Colour behind);
    int viewNameWidth (const juce::String& name);   ///< how much room drawViewName takes

    /** dBFS of a linear level, floored. */
    float toDecibels (float linear) noexcept;
}
