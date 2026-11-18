#pragma once

#include "theme/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace carthage::ui
{
    /** A square button that draws a glyph: transport and small actions. Round caps, no fill until
        hovered; `lit` draws it in the given signal colour (record armed, playing). */
    class IconButton final : public juce::Button
    {
    public:
        enum class Glyph { record, play, stop, toStart, settings, close };

        explicit IconButton (Glyph);

        void setGlyph (Glyph g)                 { glyph = g; repaint(); }
        void setLit (juce::Colour c, bool on)   { litColour = c; lit = on; repaint(); }

        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        Glyph glyph;
        juce::Colour litColour = theme::accent;
        bool lit = false;
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

    /** The audio-thread load: a thin bar and a figure. Turns warn above 70 %, clip above 90 %. */
    /** Every control that adjusts a value is one of these (DECISIONS W37): a double-click puts it
        back to its default, and the scroll wheel over it adjusts it, with Shift for fine steps (a
        tenth). Switches and menus are not, and ignore the wheel, so scrolling a panel never
        changes a choice by accident. */
    class ValueSlider final : public juce::Slider
    {
    public:
        explicit ValueSlider (double defaultValue)
        {
            setDefault (defaultValue);
            setScrollWheelEnabled (true);
        }

        void setDefault (double v)   { setDoubleClickReturnValue (true, v); }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            auto w = wheel;

            if (e.mods.isShiftDown())
            {
                w.deltaX *= 0.1f;
                w.deltaY *= 0.1f;
            }

            juce::Slider::mouseWheelMove (e, w);
        }
    };

    class LoadMeter final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        void setLoad (float averageFraction, float peakFraction);
        void paint (juce::Graphics&) override;

    private:
        float average = 0.0f, peak = 0.0f, shownPeak = 0.0f;
    };

    /** Small helpers so every label is set the same way. */
    void styleLabel (juce::Label&, float size = theme::textMd, juce::Colour = theme::text, bool medium = false);
    void styleFigure (juce::Label&, float size = theme::textMd, juce::Colour = theme::text);

    /** dBFS of a linear level, floored. */
    float toDecibels (float linear) noexcept;
}
