#include "RoutingView.h"

#include "theme/Theme.h"

#include <algorithm>
#include <map>

namespace carthage::ui::routing
{
    using namespace juce;

    namespace
    {
        using Kind = Node::Kind;

        const Node& node (const Model& m, int i)   { return m.nodes[static_cast<size_t> (i)]; }
        int count (const Model& m)                 { return static_cast<int> (m.nodes.size()); }
        bool valid (const Model& m, int i)         { return i >= 0 && i < count (m); }

        String dbText (float db)
        {
            if (std::abs (db) < 0.05f)
                return "0.0 dB";   // not "-0.0"

            return db <= -59.95f ? String::fromUTF8 ("\xe2\x88\x92\xe2\x88\x9e dB")
                                 : String (db, 1).replace ("-", String::fromUTF8 ("\xe2\x88\x92")) + " dB";
        }

        /** Whether `from` plays into `to` by its main output, and the send's level if it sends there. */
        bool feeds (const Node& from, int to)   { return from.out == to; }

        std::optional<float> sendTo (const Node& from, int to)
        {
            for (const auto& s : from.sends)
                if (s.to == to)
                    return s.db;

            return std::nullopt;
        }

        /** Everything that plays into `to` (main output or send), in mixer order. */
        std::vector<int> into (const Model& m, int to)
        {
            std::vector<int> v;

            for (int i = 0; i < count (m); ++i)
                if (i != to && (feeds (node (m, i), to) || sendTo (node (m, i), to)))
                    v.push_back (i);

            return v;
        }

        /** The selected channel's way out: its main output, that one's, and so on to master. */
        std::vector<int> chainFrom (const Model& m, int from)
        {
            std::vector<int> v;

            for (auto n = valid (m, from) ? node (m, from).out : -1; valid (m, n) && v.size() < 8; n = node (m, n).out)
                v.push_back (n);

            return v;
        }

        /** Whether a node is lit: the selected one, what plays straight into it, and its way out. */
        bool isLit (const Model& m, int i)
        {
            if (! valid (m, m.selected))
                return false;

            if (i == m.selected)
                return true;

            const auto in = into (m, m.selected);
            const auto out = chainFrom (m, m.selected);
            return std::find (in.begin(), in.end(), i) != in.end() || std::find (out.begin(), out.end(), i) != out.end()
                || sendTo (node (m, m.selected), i).has_value();
        }

        bool linkLit (const Model& m, int from, int to)
        {
            if (from == m.selected || to == m.selected)
                return true;

            const auto out = chainFrom (m, m.selected);
            return std::find (out.begin(), out.end(), from) != out.end() && node (m, from).out == to;
        }

        Colour inkOf (const Node& n)   { return n.pinned() ? theme::text : n.colour; }

        void text (Graphics& g, const String& s, Rectangle<float> r, const Font& f, Colour c, Justification j = Justification::centredLeft)
        {
            g.setFont (f);
            g.setColour (c);
            g.drawText (s, r, j, true);
        }

        float widthOf (const Font& f, const String& s)   { return GlyphArrangement::getStringWidth (f, s); }

        void chip (Graphics& g, Point<float> centre, const String& s, Colour edge, bool lit)
        {
            const auto f = theme::timeFont (theme::textXs);
            const auto r = Rectangle<float> (widthOf (f, s) + 12.0f, 16.0f).withCentre (centre);
            g.setColour (theme::bg2);
            g.fillRoundedRectangle (r, theme::radiusXs);
            g.setColour (lit ? edge : theme::faint);
            g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusXs, theme::stroke);
            text (g, s, r, f, lit ? edge : theme::faint, Justification::centred);
        }

        void stroke (Graphics& g, const Path& p, Colour c, float width, bool dashed)
        {
            g.setColour (c);

            if (! dashed)
            {
                g.strokePath (p, PathStrokeType (width, PathStrokeType::curved, PathStrokeType::rounded));
                return;
            }

            Path d;
            const float dashes[] = { 5.0f, 4.0f };
            PathStrokeType (width, PathStrokeType::curved, PathStrokeType::rounded).createDashedStroke (d, p, dashes, 2);
            g.fillPath (d);
        }

        void arrowRight (Graphics& g, Point<float> tip, Colour c)
        {
            Path p;
            p.addTriangle (tip.x - 7.0f, tip.y - 4.5f, tip.x, tip.y, tip.x - 7.0f, tip.y + 4.5f);
            g.setColour (c);
            g.fillPath (p);
        }

        void arrowUp (Graphics& g, Point<float> tip, Colour c)
        {
            Path p;
            p.addTriangle (tip.x - 4.5f, tip.y + 7.0f, tip.x, tip.y, tip.x + 4.5f, tip.y + 7.0f);
            g.setColour (c);
            g.fillPath (p);
        }

        /** A rounded label for a node: its colour dot and name. */
        void pill (Graphics& g, Rectangle<float> r, const Node& n, bool selected, bool lit)
        {
            g.setColour (selected ? theme::accent : theme::card);
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            g.setColour (lit ? inkOf (n) : theme::line);
            g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, lit ? 1.5f : theme::stroke);
            auto inner = r.reduced (r.getHeight() * 0.45f, 0.0f);

            if (! n.pinned())
            {
                g.setColour (selected ? theme::onAccent : n.colour);
                g.fillEllipse (inner.removeFromLeft (8.0f).withSizeKeepingCentre (8.0f, 8.0f));
                inner.removeFromLeft (6.0f);
            }

            text (g, n.name + (n.muted ? " (muted)" : ""), inner, theme::denseFont (theme::textSm, true),
                  selected ? theme::onAccent : lit ? theme::text : theme::muted, n.pinned() ? Justification::centred : Justification::centredLeft);
        }

        //==========================================================================================
        // D: the selected channel's story.
        void paintStory (Graphics& g, Rectangle<float> area, const Model& m)
        {
            if (! valid (m, m.selected))
            {
                text (g, "Select a channel to see where its sound goes and what plays into it.", area.reduced (16.0f, 0.0f),
                      theme::uiFont (theme::textMd), theme::muted);
                return;
            }

            const auto& sel = node (m, m.selected);
            auto r = area.reduced (16.0f, 6.0f);
            text (g, sentence (m), r.removeFromTop (22.0f), theme::uiFont (theme::textMd), theme::text);
            r.removeFromTop (10.0f);

            const auto in = into (m, m.selected);
            const auto chain = chainFrom (m, m.selected);
            auto left = r.removeFromLeft (r.getWidth() * 0.27f);
            auto middle = r.removeFromLeft (r.getWidth() * 0.42f);
            auto right = r;
            const auto labelFont = theme::uiFont (theme::textXs);

            // What plays into it, each linked to the card in its own colour.
            text (g, "Plays into it", left.removeFromTop (18.0f), labelFont, theme::faint);
            const auto card = middle.withTrimmedTop (18.0f).reduced (28.0f, 0.0f).withHeight (std::min (112.0f, middle.getHeight() - 18.0f));

            if (in.empty())
            {
                text (g, "Nothing yet", left.removeFromTop (28.0f), theme::uiFont (theme::textSm), theme::muted);
            }
            else
            {
                const auto shown = std::min<size_t> (in.size(), static_cast<size_t> (std::max (1.0f, (left.getHeight() + 6.0f) / 34.0f)));

                for (size_t k = 0; k < shown; ++k)
                {
                    const auto& n = node (m, in[k]);
                    const auto send = sendTo (n, m.selected);
                    const auto last = k + 1 == shown && shown < in.size();
                    auto row = left.removeFromTop (28.0f).withTrimmedRight (8.0f);
                    left.removeFromTop (6.0f);
                    Node more;   // the rest, counted
                    more.name = "+" + String (in.size() - k) + " more";
                    more.kind = Kind::master;
                    pill (g, row, last ? more : n, false, true);

                    if (send && ! last)
                        text (g, dbText (*send), row.withTrimmedRight (12.0f), theme::timeFont (theme::textXs), theme::muted, Justification::centredRight);

                    Path p;
                    const auto from = Point<float> (row.getRight(), row.getCentreY());
                    const auto to = Point<float> (card.getX() - 8.0f, card.getCentreY());
                    p.startNewSubPath (from);
                    p.cubicTo ((from.x + to.x) * 0.5f, from.y, (from.x + to.x) * 0.5f, to.y, to.x, to.y);
                    stroke (g, p, last ? theme::faint : n.colour, send ? 1.6f : 2.2f, send.has_value());
                    arrowRight (g, to.translated (7.0f, 0.0f), last ? theme::faint : n.colour);
                }
            }

            // The channel itself.
            g.setColour (theme::card);
            g.fillRoundedRectangle (card, theme::radiusMd);
            g.setColour (theme::accent);
            g.drawRoundedRectangle (card.reduced (0.75f), theme::radiusMd, 1.5f);
            auto c = card.reduced (14.0f, 10.0f);

            if (! sel.pinned())
            {
                g.setColour (sel.colour);
                g.fillRoundedRectangle (c.removeFromTop (3.0f), 1.5f);
            }

            c.removeFromTop (4.0f);
            text (g, sel.name, c.removeFromTop (22.0f), theme::uiFont (theme::textLg, true), theme::text);

            const auto fact = [&] (const String& label, const String& value, bool figure)
            {
                auto line = c.removeFromTop (19.0f);
                text (g, label, line, theme::uiFont (theme::textSm), theme::muted);
                text (g, value, line, figure ? theme::timeFont (theme::textSm) : theme::uiFont (theme::textSm), theme::text, Justification::centredRight);
            };

            if (sel.kind != Kind::master && sel.kind != Kind::preMaster)
                fact ("Input", sel.input.isEmpty() ? String ("none") : sel.input, false);

            fact ("Plugins", sel.plugins == 0 ? String ("none yet") : String (sel.plugins), false);
            fact ("Level", dbText (sel.levelDb), true);

            // Where it goes: its way to master, then its sends.
            text (g, "It plays into", right.removeFromTop (18.0f), labelFont, theme::faint);
            auto x = right.getX();
            const auto y = card.getCentreY();
            Path lead;
            lead.startNewSubPath (card.getRight() + 2.0f, y);
            lead.lineTo (x + 10.0f, y);

            if (chain.empty())
            {
                const auto words = sel.kind == Kind::master ? "Your interface: " + (m.masterOutput.isEmpty() ? String ("its outputs") : m.masterOutput)
                                                            : String ("Nowhere: its main output is off");
                stroke (g, lead, theme::accent, 2.2f, false);
                arrowRight (g, { x + 17.0f, y }, theme::accent);
                text (g, words, Rectangle<float> (x + 24.0f, y - 12.0f, right.getRight() - x - 24.0f, 24.0f), theme::uiFont (theme::textSm), theme::text);
            }
            else
            {
                stroke (g, lead, theme::accent, 2.2f, false);
                arrowRight (g, { x + 17.0f, y }, theme::accent);
                x += 20.0f;
                const auto f = theme::denseFont (theme::textSm, true);

                for (size_t k = 0; k < chain.size(); ++k)
                {
                    const auto& n = node (m, chain[k]);
                    const auto w = std::min (widthOf (f, n.name) + 34.0f, 150.0f);

                    if (x + w > right.getRight())
                        break;

                    pill (g, { x, y - 14.0f, w, 28.0f }, n, false, true);
                    x += w;

                    if (k + 1 < chain.size() && x + 40.0f < right.getRight())
                    {
                        Path p;
                        p.startNewSubPath (x + 2.0f, y);
                        p.lineTo (x + 18.0f, y);
                        stroke (g, p, theme::text, 2.0f, false);
                        arrowRight (g, { x + 25.0f, y }, theme::text);
                        x += 28.0f;
                    }
                }

                if (node (m, chain.back()).kind == Kind::master && m.masterOutput.isNotEmpty())
                    text (g, String::fromUTF8 ("\xe2\x86\x92 ") + m.masterOutput, Rectangle<float> (right.getX() + 20.0f, y + 18.0f, right.getWidth() - 20.0f, 16.0f),
                          theme::uiFont (theme::textXs), theme::muted, Justification::centredRight);
            }

            auto sendsArea = Rectangle<float> (right.getX() + 20.0f, y + 36.0f, right.getWidth() - 20.0f, 18.0f);

            for (const auto& s : sel.sends)
                if (valid (m, s.to) && sendsArea.getBottom() <= area.getBottom())
                {
                    text (g, "Also sends to " + node (m, s.to).name + " at " + dbText (s.db), sendsArea, theme::uiFont (theme::textSm), theme::text);
                    sendsArea.translate (0.0f, 18.0f);
                }
        }

        //==========================================================================================
        // A: a line per destination, under the strips.
        std::vector<int> destinations (const Model& m)
        {
            std::vector<int> d;

            for (int i = 0; i < count (m); ++i)
                if (! node (m, i).pinned() && ! into (m, i).empty())
                    d.push_back (i);

            for (int i = 0; i < count (m); ++i)
                if (node (m, i).kind == Kind::preMaster && ! into (m, i).empty())
                    d.push_back (i);

            for (int i = 0; i < count (m); ++i)
                if (node (m, i).kind == Kind::master)
                    d.push_back (i);

            return d;
        }

        constexpr float lineRow = 28.0f, lineTop = 18.0f;

        void paintLines (Graphics& g, Rectangle<float> area, const Model& m)
        {
            const auto dests = destinations (m);
            const auto top = area.getY();

            for (size_t row = 0; row < dests.size(); ++row)
            {
                const auto d = dests[row];
                const auto& dn = node (m, d);
                const auto y = top + lineTop + static_cast<float> (row) * lineRow;
                const auto users = into (m, d);

                if (users.empty())
                    continue;

                const auto lit = d == m.selected || std::any_of (users.begin(), users.end(), [&m] (int u) { return u == m.selected; });
                auto x0 = dn.x, x1 = dn.x;

                for (auto u : users)
                {
                    x0 = std::min (x0, node (m, u).x);
                    x1 = std::max (x1, node (m, u).x + (sendTo (node (m, u), d) ? 9.0f : 0.0f));
                }

                const auto lineColour = lit ? theme::accent : theme::faint;
                const auto allSends = std::all_of (users.begin(), users.end(), [&] (int u) { return ! feeds (node (m, u), d); });
                Path along;
                along.startNewSubPath (x0, y);
                along.lineTo (x1, y);
                stroke (g, along, lineColour, lit ? 2.4f : 1.8f, allSends);

                // Back up into the destination's strip.
                Path rise;
                rise.startNewSubPath (dn.x, y);
                rise.lineTo (dn.x, top + 7.0f);
                stroke (g, rise, lineColour, lit ? 2.4f : 1.8f, false);
                arrowUp (g, { dn.x, top + 1.0f }, lineColour);

                for (auto u : users)
                {
                    const auto& un = node (m, u);
                    const auto send = sendTo (un, d);
                    const auto mine = u == m.selected || d == m.selected;
                    const auto colour = mine ? inkOf (un) : theme::faint.withMultipliedAlpha (0.8f);
                    const auto x = un.x + (send ? 9.0f : 0.0f);
                    Path drop;
                    drop.startNewSubPath (x, top);
                    drop.lineTo (x, y);
                    stroke (g, drop, colour, send ? 1.5f : (mine ? 2.4f : 1.8f), send.has_value());
                    g.setColour (colour);
                    g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre ({ x, y }));

                    if (send && mine)
                        chip (g, { x + 28.0f, y - 10.0f }, dbText (*send), colour, true);
                }

                const auto label = (allSends ? "sends to " : "into ") + dn.name;
                const auto labelFont = theme::uiFont (theme::textXs, lit);
                const auto box = Rectangle<float> (widthOf (labelFont, label) + 12.0f, 18.0f)
                                     .withCentre ({ area.getRight() - 8.0f - (widthOf (labelFont, label) + 12.0f) * 0.5f, y });
                g.setColour (theme::bg2);
                g.fillRoundedRectangle (box, theme::radiusXs);
                text (g, label, box, labelFont, lit ? theme::text : theme::muted, Justification::centred);
            }
        }

        //==========================================================================================
        // B: signal flow, left to right, in columns by how far each is from master.
        std::vector<std::vector<int>> flowColumns (const Model& m)
        {
            std::vector<int> depth (m.nodes.size(), 0);

            for (int i = 0; i < count (m); ++i)
            {
                int d = 0;

                for (auto n = node (m, i).out; valid (m, n) && d < 16; n = node (m, n).out)
                    ++d;

                depth[static_cast<size_t> (i)] = node (m, i).kind == Kind::master ? 0 : std::max (1, d);
            }

            const auto deepest = *std::max_element (depth.begin(), depth.end());
            std::vector<std::vector<int>> columns (static_cast<size_t> (deepest + 1));

            for (int i = 0; i < count (m); ++i)
                columns[static_cast<size_t> (deepest - depth[static_cast<size_t> (i)])].push_back (i);

            return columns;
        }

        constexpr float flowRow = 34.0f;

        void paintFlow (Graphics& g, Rectangle<float> area, const Model& m)
        {
            const auto columns = flowColumns (m);
            const auto n = static_cast<float> (columns.size());
            const auto colWidth = (area.getWidth() - 32.0f) / std::max (1.0f, n);
            const auto pillWidth = std::min (150.0f, colWidth - 56.0f);
            std::map<int, Rectangle<float>> at;

            for (size_t c = 0; c < columns.size(); ++c)
            {
                const auto& col = columns[c];
                const auto height = static_cast<float> (col.size()) * flowRow;
                auto y = area.getY() + std::max (8.0f, (area.getHeight() - height) * 0.5f);
                const auto cx = area.getX() + 16.0f + colWidth * (static_cast<float> (c) + 0.5f);

                for (auto i : col)
                {
                    at[i] = Rectangle<float> (pillWidth, 26.0f).withCentre ({ cx, y + flowRow * 0.5f });
                    y += flowRow;
                }
            }

            // Links first, unlit under lit.
            for (int pass = 0; pass < 2; ++pass)
                for (int i = 0; i < count (m); ++i)
                {
                    const auto& from = node (m, i);
                    std::vector<std::pair<int, std::optional<float>>> links;

                    if (valid (m, from.out)) links.emplace_back (from.out, std::nullopt);
                    for (const auto& s : from.sends) if (valid (m, s.to)) links.emplace_back (s.to, s.db);

                    for (const auto& [to, db] : links)
                    {
                        const auto lit = linkLit (m, i, to);

                        if (lit != (pass == 1))
                            continue;

                        const auto a = Point<float> (at[i].getRight(), at[i].getCentreY());
                        const auto b = Point<float> (at[to].getX() - 8.0f, at[to].getCentreY());
                        const auto colour = lit ? inkOf (from) : theme::faint;
                        Path p;
                        const auto mid = (a.x + b.x) * 0.5f;
                        p.startNewSubPath (a);

                        if (b.x > a.x)
                            p.cubicTo (mid, a.y, mid, b.y, b.x, b.y);
                        else   // a link back to the left (a send into an earlier column): around beneath
                            p.cubicTo (a.x + 40.0f, a.y + 30.0f, b.x - 40.0f, b.y + 30.0f, b.x, b.y);

                        stroke (g, p, colour.withMultipliedAlpha (lit ? 1.0f : 0.6f), db ? 1.5f : (lit ? 2.4f : 1.8f), db.has_value());
                        arrowRight (g, b.translated (7.0f, 0.0f), colour);

                        if (db && lit)
                            chip (g, p.getPointAlongPath (p.getLength() * 0.5f), dbText (*db), colour, true);
                    }
                }

            for (const auto& [i, r] : at)
                pill (g, r, node (m, i), i == m.selected, isLit (m, i));
        }

        //==========================================================================================
        // C: the patch grid.
        struct Grid
        {
            std::vector<int> rows, cols;
            float nameWidth = 0.0f, colWidth = 0.0f, top = 0.0f;
        };

        constexpr float gridRow = 24.0f, gridHead = 26.0f;

        Grid gridOf (Rectangle<float> area, const Model& m)
        {
            Grid grid;

            for (int i = 0; i < count (m); ++i)
                if (node (m, i).kind != Kind::master)
                    grid.rows.push_back (i);

            for (int i = 0; i < count (m); ++i)
                if (node (m, i).kind == Kind::master || ! into (m, i).empty() || node (m, i).kind == Kind::bus)
                    grid.cols.push_back (i);

            // Channels and buses first, then the Pre-Master, then master: the way sound goes.
            const auto order = [&m] (int i) { const auto k = node (m, i).kind; return k == Kind::master ? 2 : k == Kind::preMaster ? 1 : 0; };
            std::stable_sort (grid.cols.begin(), grid.cols.end(), [&order] (int a, int b) { return order (a) < order (b); });
            grid.nameWidth = std::min (220.0f, area.getWidth() * 0.3f);
            grid.colWidth = std::clamp ((area.getWidth() - grid.nameWidth - 16.0f) / std::max (1.0f, static_cast<float> (grid.cols.size())), 60.0f, 140.0f);
            grid.top = area.getY() + gridHead;
            return grid;
        }

        void paintGrid (Graphics& g, Rectangle<float> area, const Model& m)
        {
            const auto grid = gridOf (area, m);
            const auto left = area.getX() + 16.0f;
            const auto colX = [&] (size_t c) { return left + grid.nameWidth + grid.colWidth * (static_cast<float> (c) + 0.5f); };
            const auto right = left + grid.nameWidth + grid.colWidth * static_cast<float> (grid.cols.size());

            for (size_t c = 0; c < grid.cols.size(); ++c)
            {
                const auto d = grid.cols[c];

                if (d == m.selected)
                {
                    g.setColour (theme::accent.withAlpha (0.08f));
                    g.fillRoundedRectangle (Rectangle<float> (colX (c) - grid.colWidth * 0.5f, area.getY(), grid.colWidth, gridHead + gridRow * static_cast<float> (grid.rows.size())), theme::radiusSm);
                }

                text (g, node (m, d).name, Rectangle<float> (colX (c) - grid.colWidth * 0.5f, area.getY(), grid.colWidth, gridHead - 4.0f),
                      theme::denseFont (theme::textSm, true), d == m.selected ? theme::accent : theme::muted, Justification::centred);
            }

            for (size_t r = 0; r < grid.rows.size(); ++r)
            {
                const auto i = grid.rows[r];
                const auto& n = node (m, i);
                const auto y = grid.top + gridRow * static_cast<float> (r);
                const auto row = Rectangle<float> (left, y, right - left, gridRow);

                if (i == m.selected)
                {
                    g.setColour (theme::accent.withAlpha (0.08f));
                    g.fillRoundedRectangle (row, theme::radiusSm);
                }

                g.setColour (theme::line);
                g.fillRect (row.withTop (row.getBottom() - 1.0f));
                auto name = Rectangle<float> (left + 4.0f, y, grid.nameWidth - 8.0f, gridRow);

                if (! n.pinned())
                {
                    g.setColour (n.colour);
                    g.fillEllipse (name.removeFromLeft (9.0f).withSizeKeepingCentre (9.0f, 9.0f));
                    name.removeFromLeft (8.0f);
                }

                const auto saysBus = n.name.containsIgnoreCase ("bus");
                const auto extra = String (n.kind == Kind::bus && ! saysBus ? "  bus" : "") + (n.muted ? "  muted" : "");
                text (g, n.name + extra, name, theme::denseFont (theme::textSm, i == m.selected), i == m.selected ? theme::text : theme::muted);

                if (n.input.isNotEmpty())
                    text (g, String::fromUTF8 ("\xe2\x86\x90 ") + n.input, name, theme::uiFont (theme::textXs), theme::faint, Justification::centredRight);

                for (size_t c = 0; c < grid.cols.size(); ++c)
                {
                    const auto d = grid.cols[c];
                    const auto centre = Point<float> (colX (c), y + gridRow * 0.5f);
                    const auto mine = i == m.selected || d == m.selected;
                    const auto colour = mine ? inkOf (n) : theme::muted;

                    if (d == i)
                    {
                        g.setColour (theme::faint);
                        g.fillRect (Rectangle<float> (14.0f, 1.0f).withCentre (centre));
                    }
                    else if (feeds (n, d))
                    {
                        g.setColour (colour);
                        g.fillEllipse (Rectangle<float> (13.0f, 13.0f).withCentre (centre));
                    }
                    else if (const auto send = sendTo (n, d))
                    {
                        g.setColour (colour);
                        g.drawEllipse (Rectangle<float> (11.0f, 11.0f).withCentre (centre.translated (-22.0f, 0.0f)), 1.6f);
                        text (g, dbText (*send), Rectangle<float> (centre.x - 14.0f, centre.y - 8.0f, 60.0f, 16.0f), theme::timeFont (theme::textXs), colour);
                    }
                    else
                    {
                        g.setColour (theme::hover);
                        g.fillEllipse (Rectangle<float> (5.0f, 5.0f).withCentre (centre));
                    }
                }
            }
        }

        //==========================================================================================
        // E: a tree from master.
        struct TreeRow
        {
            int node = -1, depth = 0;
            std::optional<float> send;   ///< a send into its parent, at this level
        };

        void addChildren (const Model& m, int parent, int depth, std::vector<TreeRow>& rows)
        {
            if (depth > 12)
                return;

            for (int i = 0; i < count (m); ++i)
                if (i != parent && feeds (node (m, i), parent))
                {
                    rows.push_back ({ i, depth, std::nullopt });
                    addChildren (m, i, depth + 1, rows);
                }

            for (int i = 0; i < count (m); ++i)
                if (i != parent)
                    if (const auto s = sendTo (node (m, i), parent))
                        rows.push_back ({ i, depth, s });
        }

        std::vector<TreeRow> treeOf (const Model& m)
        {
            std::vector<TreeRow> rows;

            for (int i = 0; i < count (m); ++i)
                if (node (m, i).kind == Kind::master)
                {
                    rows.push_back ({ i, 0, std::nullopt });
                    addChildren (m, i, 1, rows);
                }

            // Channels whose main output is off hang from nothing: listed at the end.
            for (int i = 0; i < count (m); ++i)
                if (! node (m, i).pinned() && node (m, i).out < 0)
                    rows.push_back ({ i, 0, std::nullopt });

            return rows;
        }

        constexpr float treeRow = 22.0f;

        void paintTree (Graphics& g, Rectangle<float> area, const Model& m)
        {
            const auto rows = treeOf (m);
            const auto x0 = area.getX() + 24.0f, indent = 28.0f;
            const auto columns = std::max (1, static_cast<int> ((area.getHeight() - 8.0f) / treeRow));
            const auto colWidth = 420.0f;

            for (size_t k = 0; k < rows.size(); ++k)
            {
                const auto& r = rows[k];
                const auto& n = node (m, r.node);
                const auto column = std::floor (static_cast<float> (k) / static_cast<float> (columns));
                const auto y = area.getY() + 4.0f + treeRow * (static_cast<float> (static_cast<int> (k) % columns) + 0.5f);
                const auto x = x0 + column * colWidth + indent * static_cast<float> (r.depth);
                const auto lit = isLit (m, r.node) && ! r.send.has_value();

                if (x > area.getRight() - 60.0f)
                    break;

                // An elbow up to the parent's row (in the same column).
                if (r.depth > 0)
                {
                    auto p = static_cast<int> (k) - 1;
                    while (p >= 0 && rows[static_cast<size_t> (p)].depth >= r.depth) --p;

                    if (p >= 0 && p / columns == static_cast<int> (k) / columns)
                    {
                        const auto py = area.getY() + 4.0f + treeRow * (static_cast<float> (p % columns) + 0.5f) + 7.0f;
                        const auto px = x - indent + 6.0f;
                        Path elbow;
                        elbow.startNewSubPath (px, py);
                        elbow.lineTo (px, y);
                        elbow.lineTo (x - 6.0f, y);
                        const auto chainLit = lit && isLit (m, rows[static_cast<size_t> (p)].node);
                        stroke (g, elbow, chainLit ? theme::accent : theme::faint, chainLit ? 2.0f : 1.3f, r.send.has_value());
                    }
                }

                auto label = Rectangle<float> (x, y - treeRow * 0.5f, colWidth - (x - x0 - column * colWidth) - 8.0f, treeRow);

                if (r.node == m.selected && ! r.send)
                {
                    g.setColour (theme::accent);
                    g.fillRoundedRectangle (label.withWidth (widthOf (theme::denseFont (theme::textSm, true), n.name) + 40.0f).translated (-6.0f, 0.0f).reduced (0.0f, 1.0f), theme::radiusSm);
                }

                if (! n.pinned())
                {
                    g.setColour (r.node == m.selected && ! r.send ? theme::onAccent : n.colour);
                    g.fillEllipse (label.removeFromLeft (9.0f).withSizeKeepingCentre (9.0f, 9.0f));
                    label.removeFromLeft (8.0f);
                }

                const auto nameFont = theme::denseFont (theme::textSm, true);
                const auto ink = r.node == m.selected && ! r.send ? theme::onAccent : lit ? theme::text : theme::muted;
                text (g, n.name, label, nameFont, ink);
                label.removeFromLeft (widthOf (nameFont, n.name) + 12.0f);

                String note;
                if (r.send)                             note = "send, " + dbText (*r.send);
                else if (n.kind == Kind::bus)           note = "bus";
                else if (n.muted)                       note = "muted";
                else if (n.input.isNotEmpty())          note = String::fromUTF8 ("\xe2\x86\x90 ") + n.input;
                else if (n.kind == Kind::master)        note = String::fromUTF8 ("\xe2\x86\x92 ") + m.masterOutput;
                else if (n.out < 0)                     note = "plays nowhere: its main output is off";

                text (g, note, label, r.send ? theme::timeFont (theme::textXs) : theme::uiFont (theme::textXs), theme::faint);
            }
        }
    }

    //==============================================================================================
    String nameOf (Style s)
    {
        switch (s)
        {
            case Style::story: return "The selected channel";
            case Style::lines: return "Lines per destination";
            case Style::flow:  return "Signal flow";
            case Style::grid:  return "Patch grid";
            case Style::tree:  return "Tree from master";
        }

        return {};
    }

    String describe (Style s)
    {
        switch (s)
        {
            case Style::story: return "Only the selected channel, in a sentence: what plays into it, the channel, and where it goes on to master.";
            case Style::lines: return "A labelled line for each destination; every channel drops from its strip onto the line of where it plays.";
            case Style::flow:  return "The whole mix in columns, left to right: channels, buses, the Pre-Master, master.";
            case Style::grid:  return "Channels down the side, destinations across the top; click a cell in the selected channel's row to route it.";
            case Style::tree:  return "Master at the top, everything that feeds it nested below; sends marked with their level.";
        }

        return {};
    }

    String idOf (Style s)
    {
        switch (s)
        {
            case Style::story: return "story";
            case Style::lines: return "lines";
            case Style::flow:  return "flow";
            case Style::grid:  return "grid";
            case Style::tree:  return "tree";
        }

        return "story";
    }

    Style styleFromId (const String& id)
    {
        for (auto s : styles)
            if (idOf (s) == id)
                return s;

        return Style::story;
    }

    Model example (float width)
    {
        Model m;
        const auto add = [&m] (String name, Colour colour, Kind kind, int out)
        {
            Node n;
            n.name = std::move (name);
            n.colour = colour;
            n.kind = kind;
            n.out = out;
            m.nodes.push_back (std::move (n));
            return static_cast<int> (m.nodes.size()) - 1;
        };

        const auto master = add ("Master", theme::text, Kind::master, -1);
        const auto pre = add ("Pre-Master", theme::text, Kind::preMaster, master);
        const auto one = add ("1", theme::laneSky, Kind::channel, pre);
        add ("2", theme::laneTide, Kind::channel, pre);
        const auto kick = add ("Kick", theme::laneKelp, Kind::channel, -1);
        const auto snare = add ("Snare", theme::laneSand, Kind::channel, -1);
        const auto bus = add ("Drum bus", theme::laneCoral, Kind::bus, pre);
        add ("6", theme::laneMurex, Kind::channel, pre);
        const auto seven = add ("7", theme::laneRose, Kind::channel, pre);
        const auto reverb = add ("Reverb", theme::laneSlate, Kind::channel, pre);

        auto& nodes = m.nodes;
        nodes[static_cast<size_t> (one)].input = "Analogue 1";
        nodes[static_cast<size_t> (kick)].out = bus;
        nodes[static_cast<size_t> (snare)].out = bus;
        nodes[static_cast<size_t> (kick)].sends = { { reverb, -12.0f } };
        nodes[static_cast<size_t> (snare)].sends = { { reverb, -8.0f } };
        nodes[static_cast<size_t> (kick)].levelDb = -4.5f;
        nodes[static_cast<size_t> (snare)].levelDb = -9.0f;
        nodes[static_cast<size_t> (seven)].muted = true;
        nodes[static_cast<size_t> (bus)].plugins = 2;
        m.selected = bus;
        m.masterOutput = "Analogue 1 + 2";

        // Strips as the mixer lays them: master and the Pre-Master, then the channels.
        auto x = 14.0f;
        const auto unit = width / 1000.0f;

        for (auto& n : nodes)
        {
            const auto w = (n.pinned() ? 78.0f : n.kind == Kind::bus ? 108.0f : 72.0f) * unit;
            n.x = x + w * 0.5f;
            x += w + (n.kind == Kind::preMaster ? 18.0f : n.kind == Kind::master ? 6.0f : 4.0f) * unit;
        }

        return m;
    }

    String sentence (const Model& m)
    {
        if (! valid (m, m.selected))
            return "Select a channel to see where its sound goes and what plays into it.";

        const auto& sel = node (m, m.selected);
        StringArray from;

        for (auto i : into (m, m.selected))
            from.add (node (m, i).name);

        String s;

        if (from.size() == 1)       s << from[0] << " plays into " << sel.name;
        else if (from.size() > 1)   s << from.joinIntoString (", ", 0, from.size() - 1) << " and " << from[from.size() - 1] << " play into " << sel.name;
        else                        s << "Nothing plays into " << sel.name << " yet";

        const auto chain = chainFrom (m, m.selected);

        if (sel.kind == Kind::master)
        {
            s << (from.isEmpty() ? String (", and it") : String (", which")) << " plays out through "
              << (m.masterOutput.isEmpty() ? String ("your interface") : m.masterOutput);
        }
        else if (chain.empty())
        {
            s << (from.isEmpty() ? String (", and its") : String (", but its")) << " main output is off";
        }
        else
        {
            s << (from.isEmpty() ? String (". It plays into ") : String (", which plays into ")) << node (m, chain[0]).name;

            if (chain.size() > 1)
                s << " and on to " << node (m, chain.back()).name;
        }

        if (! sel.sends.empty())
        {
            StringArray sends;

            for (const auto& x : sel.sends)
                if (valid (m, x.to))
                    sends.add (node (m, x.to).name + " (" + dbText (x.db) + ")");

            s << ". It also sends to " << sends.joinIntoString (", ");
        }

        return s + ".";
    }

    int heightFor (const Model& m, Style s)
    {
        switch (s)
        {
            case Style::story: return 168;
            case Style::lines: return static_cast<int> (lineTop + lineRow * static_cast<float> (destinations (m).size()));
            case Style::flow:
            {
                size_t tallest = 1;
                for (const auto& c : flowColumns (m)) tallest = std::max (tallest, c.size());
                return static_cast<int> (flowRow * static_cast<float> (tallest)) + 20;
            }
            case Style::grid:  return static_cast<int> (gridHead + gridRow * static_cast<float> (std::max (1, count (m) - 1))) + 8;
            case Style::tree:  return static_cast<int> (treeRow * static_cast<float> (treeOf (m).size())) + 12;
        }

        return 160;
    }

    void paint (Graphics& g, Rectangle<float> area, const Model& m, Style s)
    {
        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (area.toNearestInt());

        switch (s)
        {
            case Style::story: paintStory (g, area, m); break;
            case Style::lines: paintLines (g, area, m); break;
            case Style::flow:  paintFlow (g, area, m); break;
            case Style::grid:  paintGrid (g, area, m); break;
            case Style::tree:  paintTree (g, area, m); break;
        }
    }

    std::optional<std::pair<int, int>> gridCellAt (Rectangle<float> area, const Model& m, Point<float> p)
    {
        const auto grid = gridOf (area, m);
        const auto left = area.getX() + 16.0f + grid.nameWidth;
        const auto row = static_cast<int> (std::floor ((p.y - grid.top) / gridRow));
        const auto col = static_cast<int> (std::floor ((p.x - left) / grid.colWidth));

        if (p.y < grid.top || p.x < left || row >= static_cast<int> (grid.rows.size()) || col >= static_cast<int> (grid.cols.size()))
            return std::nullopt;

        return std::pair { grid.rows[static_cast<size_t> (row)], grid.cols[static_cast<size_t> (col)] };
    }

    //==============================================================================================
    void Section::setModel (Model m)
    {
        model = std::move (m);
        scrollBy (0.0f);   // a shorter drawing may need less scrolling
        repaint();
    }

    void Section::setStyle (Style s)
    {
        style = s;
        scroll = 0.0f;
        repaint();
    }

    Rectangle<float> Section::drawing() const
    {
        // As tall as the drawing wants; when that is more than the section has, it scrolls.
        const auto shown = getLocalBounds().toFloat().withTrimmedTop (static_cast<float> (headingHeight));
        return shown.withY (shown.getY() - scroll).withHeight (std::max (shown.getHeight(), static_cast<float> (heightFor (model, style))));
    }

    void Section::scrollBy (float pixels)
    {
        const auto room = static_cast<float> (heightFor (model, style)) - static_cast<float> (getHeight() - headingHeight);
        const auto next = std::clamp (scroll + pixels, 0.0f, std::max (0.0f, room));

        if (! approximatelyEqual (next, scroll))
        {
            scroll = next;
            repaint();
        }
    }

    void Section::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
    {
        if (static_cast<float> (heightFor (model, style)) <= static_cast<float> (getHeight() - headingHeight))
        {
            Component::mouseWheelMove (e, wheel);   // nothing to scroll here: let the mixer have it
            return;
        }

        scrollBy (-wheel.deltaY * 120.0f);
    }

    void Section::paint (Graphics& g)
    {
        g.fillAll (theme::bg2);
        g.setColour (theme::line);
        g.fillRect (0, 0, getWidth(), 1);

        auto head = getLocalBounds().removeFromTop (headingHeight).reduced (16, 0).toFloat();
        text (g, "Routing", head, theme::uiFont (theme::textMd, true), theme::text);
        text (g, nameOf (style) + String::fromUTF8 (" \xc2\xb7 ") + "Settings > Mixer changes how it is drawn", head,
              theme::uiFont (theme::textXs), theme::faint, Justification::centredRight);

        {
            Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (getLocalBounds().withTrimmedTop (headingHeight));
            routing::paint (g, drawing(), model, style);
        }

        // More below or above: a quiet line says so.
        const auto room = static_cast<float> (heightFor (model, style)) - static_cast<float> (getHeight() - headingHeight);

        if (room > 0.0f)
        {
            g.setColour (theme::faint);
            g.setFont (theme::uiFont (theme::textXs));
            g.drawText (scroll < room ? String ("Scroll for more") : String ("Scroll back up"), getLocalBounds().removeFromBottom (18).reduced (16, 0),
                        Justification::centredRight, false);
        }
    }

    void Section::mouseMove (const MouseEvent& e)
    {
        const auto cell = style == Style::grid ? gridCellAt (drawing(), model, e.position) : std::nullopt;
        setMouseCursor (cell && cell->first == model.selected && cell->second != model.selected ? MouseCursor::PointingHandCursor
                                                                                                 : MouseCursor::NormalCursor);
    }

    void Section::mouseUp (const MouseEvent& e)
    {
        if (style != Style::grid || ! onRoute || e.mouseWasDraggedSinceMouseDown())
            return;

        if (const auto cell = gridCellAt (drawing(), model, e.position); cell && cell->first == model.selected && cell->second != model.selected)
            onRoute (cell->second);
    }

    String Section::getTooltip()
    {
        if (style == Style::grid)
            if (const auto cell = gridCellAt (drawing(), model, getMouseXYRelative().toFloat()))
            {
                const auto& from = node (model, cell->first);
                const auto& to = node (model, cell->second);
                const auto send = sendTo (from, cell->second);
                const auto state = feeds (from, cell->second) ? from.name + " plays into " + to.name + " (its main output)."
                                 : send ? from.name + " sends to " + to.name + " at " + dbText (send.value_or (0.0f)) + "."
                                        : from.name + " does not play into " + to.name + ".";
                return cell->first == model.selected ? state + " Click to change that, as the route button at " + to.name + "'s strip does."
                                                     : state + " Select " + from.name + " to route it here.";
            }

        return sentence (model);
    }

    //==============================================================================================
    Preview::Preview (Style s) : Button (nameOf (s)), shown (s)
    {
        setClickingTogglesState (false);
        setTooltip (nameOf (s) + ": " + describe (s));
    }

    void Preview::paintButton (Graphics& g, bool highlighted, bool down)
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const auto chosen = getToggleState();

        g.setColour (down ? theme::hover : theme::bg2);
        g.fillRoundedRectangle (r, theme::radiusSm);
        g.setColour (chosen ? theme::accent : highlighted ? theme::lineStrong : theme::line);
        g.drawRoundedRectangle (r, theme::radiusSm, chosen ? 2.0f : theme::stroke);

        auto label = r.removeFromBottom (24.0f).reduced (10.0f, 0.0f);
        routing::text (g, nameOf (shown), label, theme::uiFont (theme::textSm, chosen), chosen ? theme::accent : theme::text);

        // The example drawn at full size, then scaled into the card.
        const auto picture = r.reduced (6.0f);
        const auto stripRow = shown == Style::lines ? 26.0f : 0.0f;   // lines hang from the strips: name them
        const auto logical = Rectangle<float> (0.0f, 0.0f, logicalWidth, static_cast<float> (heightFor (sample, shown)) + stripRow);
        const auto scale = std::min (picture.getWidth() / logical.getWidth(), picture.getHeight() / logical.getHeight());
        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (picture.toNearestInt());
        g.addTransform (AffineTransform::scale (scale).translated (picture.getX() + (picture.getWidth() - logical.getWidth() * scale) * 0.5f, picture.getY()));
        for (const auto& n : sample.nodes)
            if (stripRow > 0.0f)
                routing::text (g, n.name, Rectangle<float> (64.0f, stripRow - 4.0f).withCentre ({ n.x, stripRow * 0.5f }),
                               theme::denseFont (theme::textSm, true), &n == &sample.nodes[static_cast<size_t> (sample.selected)] ? theme::accent : theme::muted,
                               Justification::centred);

        routing::paint (g, logical.withTrimmedTop (stripRow), sample, shown);
    }
}
