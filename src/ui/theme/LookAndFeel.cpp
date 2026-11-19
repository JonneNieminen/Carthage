#include "LookAndFeel.h"

namespace carthage::ui
{
    using namespace juce;

    LookAndFeel::LookAndFeel()
    {
        setDefaultSansSerifTypeface (theme::defaultTypeface());   // JUCE's own text in Barlow too (W45)
        auto scheme = getDarkColourScheme();
        using CS = LookAndFeel_V4::ColourScheme;
        scheme.setUIColour (CS::windowBackground, theme::bg);
        scheme.setUIColour (CS::widgetBackground, theme::card);
        scheme.setUIColour (CS::menuBackground, theme::card);
        scheme.setUIColour (CS::outline, theme::lineStrong);
        scheme.setUIColour (CS::defaultText, theme::text);
        scheme.setUIColour (CS::defaultFill, theme::accent);
        scheme.setUIColour (CS::highlightedText, theme::text);
        scheme.setUIColour (CS::highlightedFill, theme::hover);
        scheme.setUIColour (CS::menuText, theme::text);
        setColourScheme (scheme);

        setColour (ResizableWindow::backgroundColourId, theme::bg);
        setColour (DocumentWindow::textColourId, theme::text);

        setColour (TextButton::buttonColourId, theme::card);
        setColour (TextButton::buttonOnColourId, theme::card);
        setColour (TextButton::textColourOffId, theme::text);
        setColour (TextButton::textColourOnId, theme::text);

        setColour (ComboBox::backgroundColourId, theme::card);
        setColour (ComboBox::textColourId, theme::text);
        setColour (ComboBox::outlineColourId, theme::line);
        setColour (ComboBox::arrowColourId, theme::muted);
        setColour (ComboBox::focusedOutlineColourId, theme::accent);

        setColour (PopupMenu::backgroundColourId, theme::card);
        setColour (PopupMenu::textColourId, theme::text);
        setColour (PopupMenu::headerTextColourId, theme::muted);
        setColour (PopupMenu::highlightedBackgroundColourId, theme::hover);
        setColour (PopupMenu::highlightedTextColourId, theme::text);

        setColour (Label::textColourId, theme::text);
        setColour (ToggleButton::textColourId, theme::text);
        setColour (ToggleButton::tickColourId, theme::onAccent);
        setColour (ToggleButton::tickDisabledColourId, theme::faint);

        setColour (TooltipWindow::backgroundColourId, theme::card);
        setColour (TooltipWindow::textColourId, theme::text);
        setColour (TooltipWindow::outlineColourId, theme::line);

        setColour (ScrollBar::thumbColourId, theme::lineStrong);
        setColour (CaretComponent::caretColourId, theme::accent);
        setColour (TextEditor::backgroundColourId, theme::card);
        setColour (TextEditor::outlineColourId, theme::line);
        setColour (TextEditor::focusedOutlineColourId, theme::accent);
        setColour (TextEditor::textColourId, theme::text);
        setColour (AlertWindow::backgroundColourId, theme::bg2);
        setColour (AlertWindow::textColourId, theme::text);
        setColour (AlertWindow::outlineColourId, theme::line);
    }

    Font LookAndFeel::getTextButtonFont (TextButton&, int)  { return theme::uiFont (theme::textMd, true); }
    Font LookAndFeel::getComboBoxFont (ComboBox&)           { return theme::uiFont(); }
    Font LookAndFeel::getLabelFont (Label& l)               { return l.getFont(); }
    Font LookAndFeel::getPopupMenuFont()                    { return theme::uiFont(); }

    Label* LookAndFeel::createSliderTextBox (Slider& slider)
    {
        auto* box = LookAndFeel_V4::createSliderTextBox (slider);   // JUCE's Slider owns it
        box->setFont (theme::timeFont (theme::textSm));
        return box;
    }

    void LookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour& background, bool highlighted, bool down)
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto fill = background;

        // A light (primary) button brightens on hover; the usual dark ones step to the hover tone.
        const auto light = background.getPerceivedBrightness() > 0.5f;

        if (! b.isEnabled())       fill = fill.withMultipliedAlpha (0.5f);
        else if (down)             fill = light ? theme::mist.darker (0.1f) : theme::hover.darker (0.15f);
        else if (highlighted)      fill = light ? theme::ice : theme::hover;

        g.setColour (fill);
        g.fillRoundedRectangle (r, theme::radiusSm);

        if (b.hasKeyboardFocus (true))
        {
            g.setColour (theme::accent);
            g.drawRoundedRectangle (r, theme::radiusSm, theme::stroke);
        }
    }

    void LookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool highlighted, bool)
    {
        // A switch: a track with a round thumb. On = accent, because an on switch makes something live.
        const auto h = static_cast<float> (theme::space4);
        const auto w = h * 1.75f;
        const auto area = b.getLocalBounds().toFloat();
        const Rectangle<float> track (area.getX(), area.getCentreY() - h * 0.5f, w, h);
        const bool on = b.getToggleState();

        // Where the thumb is, 0 to 1: a ui::Switch sets it while it slides (W45).
        const auto* moving = b.getProperties().getVarPointer ("switchAt");
        const auto at = moving != nullptr ? static_cast<float> (*moving) : (on ? 1.0f : 0.0f);
        const auto off = highlighted ? theme::hover : theme::card;

        g.setColour (off.interpolatedWith (theme::accent, at));
        g.fillRoundedRectangle (track, h * 0.5f);
        g.setColour (theme::lineStrong.withMultipliedAlpha (1.0f - at));
        g.drawRoundedRectangle (track.reduced (0.5f), h * 0.5f, theme::stroke);

        const auto thumb = h - 4.0f;
        g.setColour (theme::muted.interpolatedWith (theme::onAccent, at));
        g.fillEllipse (track.getX() + 2.0f + at * (w - thumb - 4.0f), track.getY() + 2.0f, thumb, thumb);

        if (b.hasKeyboardFocus (true))
        {
            g.setColour (theme::accent);
            g.drawRoundedRectangle (track.expanded (2.0f), h * 0.5f + 2.0f, theme::stroke);
        }

        g.setColour (b.isEnabled() ? theme::text : theme::faint);
        g.setFont (theme::uiFont());
        g.drawFittedText (b.getButtonText(),
                          area.withTrimmedLeft (w + static_cast<float> (theme::space2)).toNearestInt(),
                          Justification::centredLeft, 1);
    }

    void LookAndFeel::drawComboBox (Graphics& g, int width, int height, bool, int, int, int, int, ComboBox& box)
    {
        const Rectangle<float> r (0.5f, 0.5f, static_cast<float> (width) - 1.0f, static_cast<float> (height) - 1.0f);
        g.setColour (box.isMouseOver (true) && box.isEnabled() ? theme::hover : theme::card);
        g.fillRoundedRectangle (r, theme::radiusSm);
        g.setColour (box.hasKeyboardFocus (true) ? theme::accent : theme::line);
        g.drawRoundedRectangle (r, theme::radiusSm, theme::stroke);

        // A small chevron with round caps.
        const auto cx = static_cast<float> (width - theme::space3);
        const auto cy = static_cast<float> (height) * 0.5f;
        Path chevron;
        chevron.startNewSubPath (cx - 3.5f, cy - 1.75f);
        chevron.lineTo (cx, cy + 1.75f);
        chevron.lineTo (cx + 3.5f, cy - 1.75f);
        g.setColour (box.isEnabled() ? theme::muted : theme::faint);
        g.strokePath (chevron, PathStrokeType (1.5f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    void LookAndFeel::positionComboBoxText (ComboBox& box, Label& label)
    {
        label.setBounds (theme::space1, 0, box.getWidth() - theme::space6, box.getHeight());
        label.setFont (getComboBoxFont (box));
    }

    void LookAndFeel::drawPopupMenuBackground (Graphics& g, int width, int height)
    {
        // A popup floats, so it is one of the few things allowed a shadow; JUCE's window gives it one.
        g.fillAll (theme::card);
        g.setColour (theme::line);
        g.drawRect (0, 0, width, height, 1);
    }

    Rectangle<int> LookAndFeel::getTooltipBounds (const String& tipText, Point<int> screenPos, Rectangle<int> parentArea)
    {
        const TextLayout layout = [&]
        {
            AttributedString s;
            s.setJustification (Justification::centredLeft);
            s.append (tipText, theme::uiFont (theme::textSm), theme::text);
            TextLayout tl;
            tl.createLayoutWithBalancedLineLengths (s, 280.0f);
            return tl;
        }();

        const auto w = static_cast<int> (layout.getWidth() + static_cast<float> (theme::space4));
        const auto h = static_cast<int> (layout.getHeight() + static_cast<float> (theme::space3));

        return Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                               screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                               w, h).constrainedWithin (parentArea);
    }

    void LookAndFeel::drawTooltip (Graphics& g, const String& text, int width, int height)
    {
        const Rectangle<float> r (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height));
        g.setColour (theme::card);
        g.fillRoundedRectangle (r, theme::radiusSm);
        g.setColour (theme::line);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusSm, theme::stroke);

        AttributedString s;
        s.setJustification (Justification::centredLeft);
        s.append (text, theme::uiFont (theme::textSm), theme::text);
        TextLayout tl;
        tl.createLayoutWithBalancedLineLengths (s, 280.0f);
        tl.draw (g, r.reduced (static_cast<float> (theme::space2), static_cast<float> (theme::space1) + 2.0f));
    }
}
