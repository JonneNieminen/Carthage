#include "PluginBrowser.h"

namespace carthage::ui
{
    using namespace juce;
    using Kind = engine::PluginLibrary::Kind;

    namespace
    {
        constexpr int rowHeight = 40, pad = theme::space3, recentCount = 5;

        /** A five-point star for favourites: filled when set, an outline otherwise. */
        Path star (Rectangle<float> r)
        {
            Path p;
            p.addStar (r.getCentre(), 5, r.getWidth() * 0.22f, r.getWidth() * 0.5f, 0.0f);   // one point straight up
            return p;
        }

        Rectangle<int> starArea (int width, int height)   { return { width - 30, 0, 30, height }; }

        String formatShort (const String& f)   { return f == "AudioUnit" ? "AU" : f; }
    }

    PluginBrowser::PluginBrowser (engine::Session& s) : session (s)
    {
        styleLabel (title, theme::textLg, theme::text, true);
        title.setText ("Plugins", dontSendNotification);
        styleLabel (status, theme::textSm, theme::muted);

        rescan.setTooltip ("Check every plugin again, including ones that could not be used");
        rescan.onClick = [this] { session.getPluginScanner().start (true); updateStatus(); };

        search.setTextToShowWhenEmpty ("Search plugins", theme::faint);
        search.setFont (theme::uiFont());
        search.setIndents (theme::space2, 6);
        search.setTooltip ("Type part of a name or maker; letters can be spread out (\"nam\" finds Neural Amp Modeler)");
        search.onTextChange = [this] { filter.query = search.getText(); refresh(); };
        search.onReturnKey = [this]
        {
            for (int r = 0; r < getNumRows(); ++r)
                if (entryAt (r) != nullptr) { insertRow (r); return; }
        };
        search.onEscapeKey = [this] { search.clear(); filter.query = {}; refresh(); };

        for (auto [b, kind, tip] : { std::tuple { &kindAll, Kind::any, "Every plugin" },
                                     { &kindInstruments, Kind::instruments, "Plugins that make sound from notes" },
                                     { &kindEffects, Kind::effects, "Plugins that change a sound: amps, EQ, reverb" } })
        {
            b->setClickingTogglesState (false);
            b->setTooltip (tip);
            b->onClick = [this, kind] { setKind (kind); };
            addAndMakeVisible (*b);
        }

        format.addItem ("All formats", 1);
        format.addItem ("Audio Units", 2);
        format.addItem ("VST3", 3);
        format.setSelectedId (1, dontSendNotification);
        format.setTooltip ("Show one plugin format");
        format.onChange = [this]
        {
            const auto id = format.getSelectedId();
            filter.format = id == 2 ? "AudioUnit" : id == 3 ? "VST3" : String();
            refresh();
        };

        vendor.setTooltip ("Show one maker's plugins");
        vendor.onChange = [this]
        {
            filter.vendor = vendor.getSelectedId() > 1 ? vendor.getText() : String();
            refresh();
        };

        list.setModel (this);
        list.setRowHeight (rowHeight);
        list.setColour (ListBox::backgroundColourId, theme::bg2);
        list.setOutlineThickness (0);

        for (auto* c : std::initializer_list<Component*> { &status, &rescan, &search, &format, &vendor, &list })
            addAndMakeVisible (c);

        setKind (Kind::any);
    }

    PluginBrowser::~PluginBrowser()
    {
        list.setModel (nullptr);
    }

    void PluginBrowser::focusSearch()
    {
        search.grabKeyboardFocus();
        search.selectAll();
    }

    void PluginBrowser::setKind (Kind kind)
    {
        filter.kind = kind;

        for (auto [b, k] : { std::pair { &kindAll, Kind::any }, { &kindInstruments, Kind::instruments }, { &kindEffects, Kind::effects } })
        {
            b->setToggleState (k == kind, dontSendNotification);
            b->setColour (TextButton::buttonColourId, k == kind ? theme::hover : theme::bg2);
            b->setColour (TextButton::textColourOffId, k == kind ? theme::text : theme::muted);
        }

        refresh();
    }

    void PluginBrowser::refresh()
    {
        auto& library = session.getPluginLibrary();
        auto& catalogue = session.getPlugins();

        // The maker list follows the plugins; keep the choice if that maker is still there.
        const auto chosen = filter.vendor;
        vendor.clear (dontSendNotification);
        vendor.addItem ("Any maker", 1);
        const auto vendors = library.getVendors();

        for (int i = 0; i < vendors.size(); ++i)
            vendor.addItem (vendors[i], i + 2);

        vendor.setSelectedId (chosen.isEmpty() || ! vendors.contains (chosen) ? 1 : vendors.indexOf (chosen) + 2, dontSendNotification);

        if (! vendors.contains (chosen))
            filter.vendor = {};

        rows.clear();
        const auto add = [this] (Row::Type t, const String& text, size_t entry = 0, const String& id = {})
        {
            rows.push_back ({ t, text, entry, id });
        };

        const auto matches = library.find (filter);
        const bool searching = filter.query.trim().isNotEmpty();
        const bool narrowed = filter.kind != Kind::any || filter.format.isNotEmpty() || filter.vendor.isNotEmpty();

        if (searching)
        {
            add (Row::Type::header, matches.empty() ? "No plugin matches" : "Best matches");

            for (auto i : matches)
                add (Row::Type::plugin, {}, i);
        }
        else
        {
            if (! narrowed)
            {
                const auto recent = library.recent (recentCount);

                if (! recent.empty())
                {
                    add (Row::Type::header, "Recent");
                    for (auto i : recent) add (Row::Type::plugin, {}, i);
                }

                bool any = false;
                for (auto i : matches)
                    if (library.getEntries()[i].favourite)
                    {
                        if (! any) { add (Row::Type::header, "Favourites"); any = true; }
                        add (Row::Type::plugin, {}, i);
                    }
            }

            add (Row::Type::header, matches.empty() ? "No plugins here yet" : (narrowed ? "Matching" : "All plugins") + String (" (") + String (static_cast<int> (matches.size())) + ")");

            for (auto i : matches)
                add (Row::Type::plugin, {}, i);
        }

        // Plugins that can not be used, last, folded unless opened.
        const auto blocked = catalogue.getBlocked();

        if (! blocked.isEmpty() && ! searching)
        {
            add (Row::Type::header, String (showUnavailable ? "Hide" : "Show") + " the " + String (blocked.size())
                                        + (blocked.size() == 1 ? " plugin" : " plugins") + " Carthage can not use", 0, "unavailable-toggle");

            if (showUnavailable)
                for (const auto& id : blocked)
                {
                    const auto name = id.startsWith ("AudioUnit:") ? id.fromFirstOccurrenceOf (":", false, false) : File (id).getFileNameWithoutExtension();
                    add (Row::Type::unavailable, name, 0, id);
                }
        }

        list.updateContent();
        list.repaint();
        updateStatus();
    }

    void PluginBrowser::frame()
    {
        const auto p = session.getPluginScanner().getProgress();

        if (p.running != lastProgress.running || p.done != lastProgress.done || p.current != lastProgress.current)
        {
            lastProgress = p;
            updateStatus();
            repaint (0, 0, getWidth(), 64);
        }
    }

    void PluginBrowser::updateStatus()
    {
        const auto p = session.getPluginScanner().getProgress();
        const auto count = static_cast<int> (session.getPluginLibrary().getEntries().size());

        if (p.running)
            status.setText (p.total == 0 ? "Checking plugins" : "Checking " + String (p.done) + " of " + String (p.total)
                                + (p.current.isEmpty() ? String() : ": " + p.current[0]), dontSendNotification);
        else
            status.setText (String (count) + (count == 1 ? " plugin" : " plugins"), dontSendNotification);

        rescan.setEnabled (! p.running);
    }

    const engine::PluginLibrary::Entry* PluginBrowser::entryAt (int row) const
    {
        if (row < 0 || row >= static_cast<int> (rows.size()) || rows[static_cast<size_t> (row)].type != Row::Type::plugin)
            return nullptr;

        return &session.getPluginLibrary().getEntries()[rows[static_cast<size_t> (row)].entry];
    }

    void PluginBrowser::insertRow (int row)
    {
        if (const auto* e = entryAt (row))
            if (onInsert)
                onInsert (e->description);
    }

    int PluginBrowser::getNumRows()   { return static_cast<int> (rows.size()); }

    int PluginBrowser::idealWidth() const
    {
        int widest = 0;

        for (const auto& r : rows)
        {
            if (r.type == Row::Type::header)
                continue;

            if (r.type == Row::Type::unavailable)
            {
                widest = std::max (widest, GlyphArrangement::getStringWidthInt (theme::uiFont (theme::textMd, true), r.text) + 72);
                continue;
            }

            const auto& d = session.getPluginLibrary().getEntries()[r.entry].description;
            const auto maker = d.manufacturerName + (d.isInstrument ? ", instrument" : String());
            widest = std::max ({ widest,
                                 GlyphArrangement::getStringWidthInt (theme::uiFont (theme::textMd), d.name) + 36 + 24,
                                 GlyphArrangement::getStringWidthInt (theme::uiFont (theme::textSm), maker) });
        }

        return widest + 2 * pad + theme::space4;
    }

    void PluginBrowser::paintListBoxItem (int row, Graphics& g, int width, int height, bool selected)
    {
        if (row < 0 || row >= getNumRows())
            return;

        const auto& r = rows[static_cast<size_t> (row)];
        auto area = Rectangle<int> (0, 0, width, height).reduced (pad, 0);

        if (r.type == Row::Type::header)
        {
            const bool toggle = r.id == "unavailable-toggle";
            g.setColour (toggle ? theme::accentText : theme::muted);
            g.setFont (theme::uiFont (theme::textSm, ! toggle));
            g.drawText (r.text, area.withTrimmedTop (height / 3), Justification::centredLeft, true);
            return;
        }

        if (selected)
        {
            // Selection is live: the accent, faint.
            g.setColour (theme::accent.withAlpha (0.16f));
            g.fillRoundedRectangle (Rectangle<int> (theme::space1, 1, width - 2 * theme::space1, height - 2).toFloat(), theme::radiusSm);
        }

        if (r.type == Row::Type::unavailable)
        {
            g.setColour (theme::muted);
            g.setFont (theme::uiFont (theme::textMd, true));
            g.drawText (r.text, area.withHeight (height / 2 + 2).withTrimmedTop (4), Justification::bottomLeft, true);
            g.setColour (theme::faint);
            g.setFont (theme::uiFont (theme::textSm));
            g.drawText (session.getPlugins().getBlockReason (r.id), area.withTrimmedTop (height / 2 + 2).withTrimmedRight (72), Justification::topLeft, true);
            g.setColour (theme::accentText);
            g.drawText ("Try again", area.removeFromRight (68), Justification::centredRight, false);
            return;
        }

        const auto& e = session.getPluginLibrary().getEntries()[r.entry];
        const auto& d = e.description;
        area.removeFromRight (24);   // the star

        g.setColour (theme::faint);
        g.setFont (theme::uiFont (theme::textXs));
        const auto tag = area.removeFromRight (36);
        g.drawText (formatShort (d.pluginFormatName), tag, Justification::centredRight, false);

        g.setColour (theme::text);
        g.setFont (theme::uiFont (theme::textMd));
        g.drawText (d.name, area.withHeight (height / 2 + 2), Justification::bottomLeft, true);

        g.setColour (theme::muted);
        g.setFont (theme::uiFont (theme::textSm));
        g.drawText (d.manufacturerName + (d.isInstrument ? ", instrument" : String()),
                    area.withTrimmedTop (height / 2 + 2), Justification::topLeft, true);

        const auto s = starArea (width, height).toFloat().withSizeKeepingCentre (14.0f, 14.0f);
        const auto p = star (s);

        if (e.favourite)
        {
            g.setColour (theme::text);
            g.fillPath (p);
        }
        else
        {
            g.setColour (theme::faint);
            g.strokePath (p, PathStrokeType (theme::stroke, PathStrokeType::curved, PathStrokeType::rounded));
        }
    }

    void PluginBrowser::listBoxItemClicked (int row, const MouseEvent& e)
    {
        if (row < 0 || row >= getNumRows())
            return;

        const auto& r = rows[static_cast<size_t> (row)];

        if (r.id == "unavailable-toggle")
        {
            showUnavailable = ! showUnavailable;
            refresh();
            return;
        }

        if (r.type == Row::Type::unavailable && e.x > list.getVisibleRowWidth() - 80)
        {
            // Try again: off the blocklist, then the scanner checks it like a new plugin.
            session.getPlugins().unblock (r.id);
            session.getPluginScanner().start (false);
            refresh();
            return;
        }

        if (const auto* entry = entryAt (row); entry != nullptr && starArea (list.getVisibleRowWidth(), rowHeight).contains (e.x, e.y))
        {
            session.getPluginLibrary().setFavourite (entry->key, ! entry->favourite);
            refresh();
        }
    }

    void PluginBrowser::listBoxItemDoubleClicked (int row, const MouseEvent&)   { insertRow (row); }
    void PluginBrowser::returnKeyPressed (int row)                               { insertRow (row); }

    var PluginBrowser::getDragSourceDescription (const SparseSet<int>& selected)
    {
        if (selected.size() == 1)
            if (const auto* e = entryAt (selected[0]))
                return String (dragPrefix) + e->key;

        return {};
    }

    String PluginBrowser::getTooltipForRow (int row)
    {
        if (const auto* e = entryAt (row))
            return e->description.name + " by " + e->description.manufacturerName + ", version " + e->description.version
                   + ". Double-click or drag onto the plugin card to use it; the star keeps it in Favourites.";

        if (row >= 0 && row < getNumRows() && rows[static_cast<size_t> (row)].type == Row::Type::unavailable)
            return session.getPlugins().getBlockReason (rows[static_cast<size_t> (row)].id);

        return {};
    }

    void PluginBrowser::paint (Graphics& g)
    {
        g.fillAll (theme::bg2);
        g.setColour (theme::line);
        g.fillRect (getWidth() - 1, 0, 1, getHeight());

        // The scan's progress as a hairline under the status.
        const auto p = session.getPluginScanner().getProgress();

        if (p.running && p.total > 0)
        {
            const auto bar = Rectangle<float> (static_cast<float> (pad), static_cast<float> (status.getBottom() + 2),
                                               static_cast<float> (getWidth() - 2 * pad), 2.0f);
            g.setColour (theme::lineStrong);
            g.fillRect (bar);
            g.setColour (theme::muted);
            g.fillRect (bar.withWidth (bar.getWidth() * static_cast<float> (p.done) / static_cast<float> (p.total)));
        }
    }

    void PluginBrowser::resized()
    {
        auto r = getLocalBounds().reduced (pad, 0).withTrimmedRight (1);
        r.removeFromTop (theme::space2);

        // The left panel's switch names the panel; the status and Rescan share the top line.
        auto top = r.removeFromTop (26);
        const auto w = rescan.getBestWidthForHeight (24) + theme::space4;
        rescan.setBounds (top.removeFromRight (w).withSizeKeepingCentre (w, 24));
        status.setBounds (top);
        r.removeFromTop (theme::space2);

        search.setBounds (r.removeFromTop (30));
        r.removeFromTop (theme::space2);

        auto kinds = r.removeFromTop (26);
        const auto third = kinds.getWidth() / 3;
        kindAll.setBounds (kinds.removeFromLeft (third - 2));
        kinds.removeFromLeft (3);
        kindInstruments.setBounds (kinds.removeFromLeft (third - 2));
        kinds.removeFromLeft (3);
        kindEffects.setBounds (kinds);
        r.removeFromTop (theme::space2);

        auto menus = r.removeFromTop (28);
        format.setBounds (menus.removeFromLeft (menus.getWidth() / 2 - 2));
        menus.removeFromLeft (4);
        vendor.setBounds (menus);
        r.removeFromTop (theme::space2);

        list.setBounds (getLocalBounds().withTrimmedTop (r.getY()).withTrimmedRight (1));
    }
}
