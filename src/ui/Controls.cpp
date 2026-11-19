#include "Controls.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace carthage::ui
{
    using namespace juce;

    namespace
    {
        const PathStrokeType roundStroke (1.75f, PathStrokeType::curved, PathStrokeType::rounded);
    }

    float toDecibels (float linear) noexcept
    {
        return Decibels::gainToDecibels (linear, -60.0f);
    }

    void styleLabel (Label& l, float size, Colour c, bool medium)
    {
        l.setFont (theme::uiFont (size, medium));
        l.setColour (Label::textColourId, c);
        l.setBorderSize ({});
        l.setMinimumHorizontalScale (1.0f);
    }

    void styleFigure (Label& l, float size, Colour c)
    {
        l.setFont (theme::timeFont (size));
        l.setColour (Label::textColourId, c);
        l.setBorderSize ({});
        l.setMinimumHorizontalScale (1.0f);
    }

    //==============================================================================================
    PanelEdge::PanelEdge()
    {
        setMouseCursor (MouseCursor::LeftRightResizeCursor);
        setTooltip ("Drag to resize; double-click to fit the longest name");
    }

    void PanelEdge::paint (Graphics& g)
    {
        if (isMouseOverOrDragging())
        {
            g.setColour (isMouseButtonDown() ? theme::accent : theme::lineStrong);
            g.fillRect (getLocalBounds().withSizeKeepingCentre (2, getHeight()));
        }
    }

    void PanelEdge::mouseDown (const MouseEvent&)
    {
        widthAtStart = getCurrentWidth ? getCurrentWidth() : 0;
        repaint();
    }

    void PanelEdge::mouseDrag (const MouseEvent& e)
    {
        if (onResize)
            onResize (widthAtStart + e.getDistanceFromDragStartX());
    }

    void PanelEdge::mouseDoubleClick (const MouseEvent&)
    {
        if (onFit)
            onFit();
    }

    //==============================================================================================
    ToolButton::ToolButton (Glyph g) : Button ({}), glyph (g)
    {
        setWantsKeyboardFocus (false);   // the tools have keys; focus stays on the time line
        setMouseCursor (MouseCursor::PointingHandCursor);
    }

    namespace
    {
        /** Each tool's icon, drawn in a 16 x 16 box. */
        Path toolGlyph (ToolButton::Glyph glyph)
        {
            using G = ToolButton::Glyph;
            Path p;

            switch (glyph)
            {
                case G::select:   // an arrow pointer
                    p.startNewSubPath (4.0f, 2.0f);
                    p.lineTo (4.0f, 13.5f);
                    p.lineTo (7.0f, 10.5f);
                    p.lineTo (9.2f, 15.0f);
                    p.lineTo (11.0f, 14.2f);
                    p.lineTo (8.8f, 9.8f);
                    p.lineTo (13.0f, 9.8f);
                    p.closeSubPath();
                    break;

                case G::draw:     // a pencil
                    p.startNewSubPath (2.5f, 13.5f);
                    p.lineTo (3.3f, 10.2f);
                    p.lineTo (11.0f, 2.5f);
                    p.lineTo (13.5f, 5.0f);
                    p.lineTo (5.8f, 12.7f);
                    p.closeSubPath();
                    p.startNewSubPath (9.3f, 4.2f);
                    p.lineTo (11.8f, 6.7f);
                    break;

                case G::paint:    // a roller
                    p.addRoundedRectangle (2.5f, 2.0f, 9.5f, 4.0f, 1.0f);
                    p.startNewSubPath (12.0f, 4.0f);
                    p.lineTo (14.0f, 4.0f);
                    p.lineTo (14.0f, 8.5f);
                    p.lineTo (8.0f, 8.5f);
                    p.lineTo (8.0f, 14.0f);
                    break;

                case G::slice:    // scissors
                    p.addEllipse (2.0f, 10.0f, 4.0f, 4.0f);
                    p.addEllipse (10.0f, 10.0f, 4.0f, 4.0f);
                    p.startNewSubPath (5.4f, 10.4f);
                    p.lineTo (11.5f, 2.0f);
                    p.startNewSubPath (10.6f, 10.4f);
                    p.lineTo (4.5f, 2.0f);
                    break;

                case G::erase:    // an eraser on its line
                    p.startNewSubPath (6.0f, 13.5f);
                    p.lineTo (2.5f, 10.0f);
                    p.lineTo (9.0f, 3.5f);
                    p.lineTo (13.5f, 8.0f);
                    p.lineTo (8.0f, 13.5f);
                    p.closeSubPath();
                    p.startNewSubPath (5.2f, 6.3f);
                    p.lineTo (10.0f, 11.1f);
                    p.startNewSubPath (8.0f, 13.5f);
                    p.lineTo (14.0f, 13.5f);
                    break;

                case G::mute:     // a speaker, crossed out
                    p.startNewSubPath (2.0f, 6.0f);
                    p.lineTo (4.8f, 6.0f);
                    p.lineTo (8.5f, 3.0f);
                    p.lineTo (8.5f, 13.0f);
                    p.lineTo (4.8f, 10.0f);
                    p.lineTo (2.0f, 10.0f);
                    p.closeSubPath();
                    p.startNewSubPath (11.0f, 6.0f);
                    p.lineTo (14.5f, 9.5f);
                    p.startNewSubPath (14.5f, 6.0f);
                    p.lineTo (11.0f, 9.5f);
                    break;

                case G::zoom:     // a magnifier
                    p.addEllipse (2.0f, 2.0f, 9.0f, 9.0f);
                    p.startNewSubPath (10.0f, 10.0f);
                    p.lineTo (14.0f, 14.0f);
                    break;

                case G::stretch:  // two edges pulled apart
                    p.startNewSubPath (2.0f, 3.0f);
                    p.lineTo (2.0f, 13.0f);
                    p.startNewSubPath (14.0f, 3.0f);
                    p.lineTo (14.0f, 13.0f);
                    p.startNewSubPath (4.5f, 8.0f);
                    p.lineTo (11.5f, 8.0f);
                    p.startNewSubPath (6.5f, 6.0f);
                    p.lineTo (4.5f, 8.0f);
                    p.lineTo (6.5f, 10.0f);
                    p.startNewSubPath (9.5f, 6.0f);
                    p.lineTo (11.5f, 8.0f);
                    p.lineTo (9.5f, 10.0f);
                    break;
            }

            return p;
        }
    }

    void ToolButton::paintButton (Graphics& g, bool highlighted, bool down)
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);

        if (chosen || highlighted || down)
        {
            g.setColour (chosen ? theme::accent : down ? theme::hover.darker (0.15f) : theme::hover);
            g.fillRoundedRectangle (r, theme::radiusSm);
        }

        const auto ink = chosen ? theme::onAccent : highlighted ? theme::text : theme::mist;
        auto area = getLocalBounds().reduced (theme::space1, 0);

        // The icon on the left, the key on the right: the key is how the tool is picked.
        auto icon = toolGlyph (glyph);
        const auto box = (showsKey ? area.removeFromLeft (16) : area).withSizeKeepingCentre (16, 16).toFloat();
        icon.applyTransform (AffineTransform::translation (box.getX(), box.getY()));
        g.setColour (ink);
        g.strokePath (icon, PathStrokeType (1.4f, PathStrokeType::curved, PathStrokeType::rounded));

        if (! showsKey)
            return;

        g.setColour (chosen ? theme::onAccent : theme::muted);
        g.setFont (theme::timeFont (theme::textXs));
        g.drawText (keyText, area, Justification::centredRight, false);
    }

    //==============================================================================================
    IconButton::IconButton (Glyph g) : Button ({}), glyph (g)
    {
        setWantsKeyboardFocus (true);
        setMouseCursor (MouseCursor::PointingHandCursor);
    }

    void IconButton::paintButton (Graphics& g, bool highlighted, bool down)
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);

        if (highlighted || down)
        {
            g.setColour (down ? theme::hover.darker (0.15f) : theme::hover);
            g.fillRoundedRectangle (r, theme::radiusSm);
        }

        if (hasKeyboardFocus (true))
        {
            g.setColour (theme::accent);
            g.drawRoundedRectangle (r, theme::radiusSm, theme::stroke);
        }

        const auto ink = ! isEnabled() ? theme::faint : (lit ? litColour : theme::mist);
        const auto c = r.getCentre();
        const auto s = std::min (r.getWidth(), r.getHeight()) * 0.5f;   // half size of the button
        g.setColour (ink);

        switch (glyph)
        {
            case Glyph::record:
            {
                const auto d = s * 0.75f;
                if (lit) g.fillEllipse (c.x - d * 0.5f, c.y - d * 0.5f, d, d);
                else     g.drawEllipse (c.x - d * 0.5f, c.y - d * 0.5f, d, d, 1.75f);
                break;
            }
            case Glyph::play:
            {
                const auto h = s * 0.8f;
                Path p;
                p.addTriangle (c.x - h * 0.35f, c.y - h * 0.5f, c.x - h * 0.35f, c.y + h * 0.5f, c.x + h * 0.5f, c.y);
                g.fillPath (p.createPathWithRoundedCorners (1.5f));
                break;
            }
            case Glyph::stop:
            {
                const auto d = s * 0.62f;
                g.fillRoundedRectangle (c.x - d * 0.5f, c.y - d * 0.5f, d, d, theme::radiusXs * 0.5f);
                break;
            }
            case Glyph::toStart:
            {
                const auto h = s * 0.7f;
                Path bar;
                bar.startNewSubPath (c.x - h * 0.45f, c.y - h * 0.5f);
                bar.lineTo (c.x - h * 0.45f, c.y + h * 0.5f);
                g.strokePath (bar, roundStroke);
                Path p;
                p.addTriangle (c.x + h * 0.5f, c.y - h * 0.5f, c.x + h * 0.5f, c.y + h * 0.5f, c.x - h * 0.2f, c.y);
                g.fillPath (p.createPathWithRoundedCorners (1.5f));
                break;
            }
            case Glyph::settings:
            {
                // Three sliders: "settings" without a cog's fuss.
                const auto w = s * 0.8f;
                for (int i = -1; i <= 1; ++i)
                {
                    const auto y = c.y + static_cast<float> (i) * s * 0.33f;
                    Path p;
                    p.startNewSubPath (c.x - w * 0.5f, y);
                    p.lineTo (c.x + w * 0.5f, y);
                    g.strokePath (p, roundStroke);
                    const auto kx = c.x + static_cast<float> (i) * w * 0.22f;
                    g.fillEllipse (kx - 2.5f, y - 2.5f, 5.0f, 5.0f);
                }
                break;
            }
            case Glyph::popOut:
            case Glyph::popIn:
            {
                // A window (a rounded square open at its top-right corner) and an arrow leaving it,
                // or coming into it: "in its own window" and "back".
                const auto h = s * 0.42f;
                Path frame;
                frame.startNewSubPath (c.x + h * 0.15f, c.y - h);
                frame.lineTo (c.x - h, c.y - h);
                frame.lineTo (c.x - h, c.y + h);
                frame.lineTo (c.x + h, c.y + h);
                frame.lineTo (c.x + h, c.y - h * 0.15f);
                g.strokePath (frame, roundStroke);

                const bool out = glyph == Glyph::popOut;
                const Point<float> from = out ? Point<float> (c.x, c.y) : Point<float> (c.x + h * 1.05f, c.y - h * 1.05f);
                const Point<float> to = out ? Point<float> (c.x + h * 1.05f, c.y - h * 1.05f) : Point<float> (c.x, c.y);
                Path arrow;
                arrow.startNewSubPath (from);
                arrow.lineTo (to);
                arrow.startNewSubPath (to.x + (out ? -h * 0.55f : h * 0.55f), to.y);
                arrow.lineTo (to);
                arrow.lineTo (to.x, to.y + (out ? h * 0.55f : -h * 0.55f));
                g.strokePath (arrow, roundStroke);
                break;
            }
            case Glyph::close:
            {
                const auto h = s * 0.45f;
                Path p;
                p.startNewSubPath (c.x - h, c.y - h); p.lineTo (c.x + h, c.y + h);
                p.startNewSubPath (c.x + h, c.y - h); p.lineTo (c.x - h, c.y + h);
                g.strokePath (p, roundStroke);
                break;
            }
        }
    }

    //==============================================================================================
    void LevelMeter::push (float peak, double dt)
    {
        // Fall back at 24 dB/s; hold the peak for one second, a clip for three.
        const auto fallen = Decibels::decibelsToGain (toDecibels (level) - static_cast<float> (24.0 * dt), 0.0f);
        level = std::max (peak, fallen < 0.001f ? 0.0f : fallen);

        if (peak >= held) { held = peak; holdTime = 1.0; }
        else
        {
            holdTime -= dt;
            if (holdTime <= 0.0) held = level;
        }

        if (peak >= 1.0f) clipHold = 3.0;
        else clipHold = std::max (0.0, clipHold - dt);

        repaint();
    }

    void LevelMeter::paint (Graphics& g)
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (theme::well);
        g.fillRoundedRectangle (r, theme::radiusXs);

        // -60 dBFS .. 0 dBFS, horizontal.
        const auto position = [&] (float linear) { return jlimit (0.0f, 1.0f, (toDecibels (linear) + 60.0f) / 60.0f); };
        const auto x18 = r.getWidth() * position (Decibels::decibelsToGain (-18.0f));
        const auto x6 = r.getWidth() * position (Decibels::decibelsToGain (-6.0f));
        const auto lit = r.getWidth() * position (level);

        const auto bar = r.reduced (1.0f);
        g.saveState();
        g.reduceClipRegion (Rectangle<float> (bar.getX(), bar.getY(), lit, bar.getHeight()).toNearestInt());
        g.setColour (theme::ok);   g.fillRoundedRectangle (bar.withWidth (x18), theme::radiusXs);
        g.setColour (theme::warn); g.fillRect (bar.withX (bar.getX() + x18).withWidth (x6 - x18));
        g.setColour (theme::clip); g.fillRect (bar.withX (bar.getX() + x6).withWidth (bar.getWidth() - x6));
        g.restoreState();

        if (held > 0.0f)
        {
            const auto hx = bar.getX() + bar.getWidth() * position (held);
            g.setColour (held >= 1.0f || clipHold > 0.0 ? theme::clip : theme::mist);
            g.fillRect (hx - 1.0f, bar.getY(), 2.0f, bar.getHeight());
        }
    }

    //==============================================================================================
    void LoadMeter::setLoad (float a, float p)
    {
        average = a;
        // The figure shows the worst block of the last half second or so, so a spike is readable.
        shownPeak = std::max (p, shownPeak * 0.92f);
        peak = p;
        repaint();
    }

    void LoadMeter::paint (Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        const auto colourFor = [] (float v) { return v > 0.9f ? theme::clip : (v > 0.7f ? theme::warn : theme::mist); };

        auto text = r.removeFromRight (40.0f);
        g.setColour (colourFor (shownPeak));
        g.setFont (theme::timeFont (theme::textSm));
        g.drawText (String (roundToInt (shownPeak * 100.0f)) + "%", text, Justification::centredRight, false);

        const auto track = r.withSizeKeepingCentre (r.getWidth() - static_cast<float> (theme::space2), 4.0f).withX (r.getX());
        g.setColour (theme::well);
        g.fillRoundedRectangle (track, 2.0f);
        const auto filled = track.getWidth() * jlimit (0.0f, 1.0f, average);

        if (filled >= 4.0f)   // narrower than its own round caps, a bar reads as a stray dot
        {
            g.setColour (colourFor (average).withAlpha (0.9f));
            g.fillRoundedRectangle (track.withWidth (filled), 2.0f);
        }

        const auto px = track.getX() + track.getWidth() * jlimit (0.0f, 1.0f, shownPeak);
        g.setColour (colourFor (shownPeak));
        g.fillRoundedRectangle (px - 1.0f, track.getY() - 2.0f, 2.0f, track.getHeight() + 4.0f, 1.0f);
    }
}
