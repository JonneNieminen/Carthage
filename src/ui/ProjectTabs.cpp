#include "ProjectTabs.h"
#include "theme/Theme.h"

namespace carthage::ui
{
    using namespace juce;

    namespace
    {
        constexpr int minTab = 112, maxTab = 220, padding = theme::space3, closeSize = 16, gap = 2;

        Font tabFont()   { return theme::uiFont (theme::textSm); }
    }

    ProjectTabs::ProjectTabs (engine::Session& s) : session (s)
    {
        setRepaintsOnMouseActivity (false);
        refresh();
    }

    void ProjectTabs::refresh()
    {
        tabs.clear();

        for (int i = 0; i < session.getNumProjects(); ++i)
            tabs.push_back ({ session.describeProject (i), {} });

        layout();
        repaint();
    }

    void ProjectTabs::layout()
    {
        auto r = getLocalBounds().withTrimmedTop (theme::space1);
        r.removeFromLeft (theme::space2);

        for (auto& t : tabs)
        {
            const auto text = GlyphArrangement::getStringWidthInt (tabFont(), t.summary.name);
            const auto w = std::clamp (text + padding * 2 + closeSize + theme::space1, minTab, maxTab);
            t.bounds = r.removeFromLeft (w);
            r.removeFromLeft (gap);
        }

        r.removeFromLeft (theme::space1);
        plus = r.removeFromLeft (24).withSizeKeepingCentre (24, 22).translated (0, -1);
    }

    int ProjectTabs::tabAt (Point<int> p) const
    {
        for (size_t i = 0; i < tabs.size(); ++i)
            if (tabs[i].bounds.contains (p))
                return static_cast<int> (i);

        return -1;
    }

    Rectangle<int> ProjectTabs::closeArea (int index) const
    {
        const auto& b = tabs[static_cast<size_t> (index)].bounds;
        return b.withLeft (b.getRight() - padding - closeSize + 4).withWidth (closeSize).withSizeKeepingCentre (closeSize, closeSize);
    }

    void ProjectTabs::paint (Graphics& g)
    {
        g.fillAll (theme::well);

        const auto front = session.getFrontIndex();

        for (size_t i = 0; i < tabs.size(); ++i)
        {
            const auto index = static_cast<int> (i);
            const auto& t = tabs[i];
            const auto isFront = index == front;
            const auto isHovered = index == hovered;
            auto b = t.bounds.toFloat();

            if (isFront || isHovered)
            {
                // Top corners round, the foot square: the front tab runs into the toolbar.
                Path shape;
                shape.addRoundedRectangle (b.getX(), b.getY(), b.getWidth(), b.getHeight() + theme::radiusSm,
                                           theme::radiusSm, theme::radiusSm, true, true, false, false);
                g.setColour (isFront ? theme::bg : theme::bg2.withAlpha (0.6f));
                g.fillPath (shape);
            }
            else if (index + 1 != front && i + 1 < tabs.size() && index + 1 != hovered)
            {
                g.setColour (theme::line);   // a hairline between two tabs at the back
                g.fillRect (t.bounds.getRight() + gap / 2, t.bounds.getY() + 8, 1, t.bounds.getHeight() - 14);
            }

            const auto text = t.bounds.reduced (padding, 0).withTrimmedRight (closeSize);
            g.setColour (isFront ? theme::text : theme::muted);
            g.setFont (tabFont());
            g.drawText (t.summary.name, text, Justification::centredLeft, true);

            // The close spot: a dot when unsaved, the cross under the pointer and on the front tab.
            const auto c = closeArea (index).toFloat();
            const bool showCross = (isHovered && (overClose || ! t.summary.unsaved)) || (isFront && ! t.summary.unsaved);

            if (showCross)
            {
                if (isHovered && overClose)
                {
                    g.setColour (theme::hover);
                    g.fillRoundedRectangle (c, theme::radiusXs);
                }

                g.setColour (isHovered && overClose ? theme::text : theme::faint);
                const auto x = c.reduced (5.0f);
                g.drawLine ({ x.getTopLeft(), x.getBottomRight() }, 1.4f);
                g.drawLine ({ x.getTopRight(), x.getBottomLeft() }, 1.4f);
            }
            else if (t.summary.unsaved)
            {
                g.setColour (isFront ? theme::text : theme::muted);
                g.fillEllipse (c.withSizeKeepingCentre (6.0f, 6.0f));
            }
        }

        if (pressed >= 0 && dragTo >= 0 && dragTo != pressed)
        {
            // Where the dragged tab would land.
            const auto& target = tabs[static_cast<size_t> (dragTo)].bounds;
            const auto x = dragTo > pressed ? target.getRight() : target.getX() - gap;
            g.setColour (theme::accent);
            g.fillRect (x, target.getY() + 6, 2, target.getHeight() - 10);
        }

        // New project
        if (overPlus)
        {
            g.setColour (theme::bg2);
            g.fillRoundedRectangle (plus.toFloat(), theme::radiusXs);
        }

        g.setColour (overPlus ? theme::text : theme::muted);
        const auto p = plus.toFloat().getCentre();
        g.drawLine (p.x - 5.0f, p.y, p.x + 5.0f, p.y, 1.4f);
        g.drawLine (p.x, p.y - 5.0f, p.x, p.y + 5.0f, 1.4f);
    }

    void ProjectTabs::updateHover (Point<int> p)
    {
        const auto tab = tabAt (p);
        const auto close = tab >= 0 && closeArea (tab).contains (p);
        const auto onPlus = plus.contains (p);

        if (tab != hovered || close != overClose || onPlus != overPlus)
        {
            hovered = tab;
            overClose = close;
            overPlus = onPlus;

            if (onPlus)
                setTooltip ("New project");
            else if (close)
                setTooltip ("Close this project");
            else if (tab >= 0)
            {
                const auto& s = tabs[static_cast<size_t> (tab)].summary;
                setTooltip (s.untitled ? String ("Not saved yet") : s.file.getParentDirectory().getFullPathName());
            }
            else
                setTooltip ({});

            repaint();
        }
    }

    void ProjectTabs::mouseMove (const MouseEvent& e)   { updateHover (e.getPosition()); }

    void ProjectTabs::mouseExit (const MouseEvent&)
    {
        hovered = -1;
        overClose = overPlus = false;
        repaint();
    }

    void ProjectTabs::mouseDown (const MouseEvent& e)
    {
        pressed = tabAt (e.getPosition());
        dragTo = -1;

        // A tab comes forward on press, as tabs do; closing waits for the release.
        if (pressed >= 0 && e.mods.isLeftButtonDown() && ! closeArea (pressed).contains (e.getPosition()) && onSelect)
            onSelect (pressed);
    }

    void ProjectTabs::mouseDrag (const MouseEvent& e)
    {
        if (pressed < 0 || e.getDistanceFromDragStart() < 6)
            return;

        auto to = tabAt ({ e.x, tabs[static_cast<size_t> (pressed)].bounds.getCentreY() });

        if (to < 0)
            to = e.x < tabs.front().bounds.getX() ? 0 : static_cast<int> (tabs.size()) - 1;

        if (to != dragTo)
        {
            dragTo = to;
            repaint();
        }
    }

    void ProjectTabs::mouseUp (const MouseEvent& e)
    {
        const auto from = pressed, to = dragTo;
        pressed = dragTo = -1;
        repaint();

        if (from >= 0 && to >= 0 && to != from)
        {
            session.moveProject (from, to);
            return;
        }

        if (! e.mouseWasClicked())
            return;

        if (plus.contains (e.getPosition()))
        {
            if (onNew) onNew();
            return;
        }

        const auto tab = tabAt (e.getPosition());

        if (tab >= 0 && tab == from && onClose
             && (e.mods.isMiddleButtonDown() || closeArea (tab).contains (e.getPosition())))
            onClose (tab);
    }
}
