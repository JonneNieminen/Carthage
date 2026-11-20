#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace carthage::ui
{
    /** JUCE's stock controls, drawn in the Carthage theme: flat surfaces, 1 px lines, round caps,
        radii by nesting, the accent only for focus and what is live. */
    class LookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();

        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        juce::Font getLabelFont (juce::Label&) override;
        juce::Font getPopupMenuFont() override;

        /** A slider's value box shows a figure: set in the time face so it does not jitter as it changes. */
        juce::Label* createSliderTextBox (juce::Slider&) override;

        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                                   bool highlighted, bool down) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
        void drawComboBox (juce::Graphics&, int width, int height, bool down,
                           int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
        void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
        void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
        juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                               juce::Rectangle<int> parentArea) override;
        int getPopupMenuBorderSize() override   { return theme::space1; }

        /** Knobs are half rings (owner: a half doughnut): the arc runs across the top, filled from
            its left end, or from the top centre for a slider marked "bipolar" (pan). */
        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float position,
                               float startAngle, float endAngle, juce::Slider&) override;

        /** A slider marked "fader" (the mixer's) gets a larger, grippable cap (owner). */
        void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float position,
                               float minPosition, float maxPosition, juce::Slider::SliderStyle, juce::Slider&) override;
    };
}
