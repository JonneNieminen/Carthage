#include "ShortcutEditor.h"
#include "Controls.h"

namespace carthage::ui
{
    using namespace juce;

    namespace
    {
        constexpr int rowHeight = 28, capHeight = 22, capGap = theme::space1;

        /** "⌘S", "F8", "Home": JUCE names some keys in lower case ("home", "page up"). */
        String keyText (const KeyPress& k)
        {
            auto t = k.getTextDescriptionWithIcons();
            const auto last = t.getLastCharacters (t.length() - t.indexOfAnyOf ("abcdefghijklmnopqrstuvwxyz"));

            if (t.indexOfAnyOf ("abcdefghijklmnopqrstuvwxyz") >= 0 && last.length() > 1)
                t = t.dropLastCharacters (last.length()) + last.substring (0, 1).toUpperCase() + last.substring (1);

            return t;
        }

        /** Keys the system keeps for itself: binding them would never reach Carthage. */
        bool isSystemKey (const KeyPress& k)
        {
           #if JUCE_MAC
            const auto cmd = ModifierKeys::commandModifier;
            for (auto c : { 'q', 'h', 'm' })
                if (k == KeyPress (c, cmd, 0))
                    return true;
            return k == KeyPress (KeyPress::tabKey, cmd, 0) || k == KeyPress ('`', cmd, 0);
           #else
            return k == KeyPress (KeyPress::F4Key, ModifierKeys::altModifier, 0)
                || k == KeyPress (KeyPress::tabKey, ModifierKeys::altModifier, 0);
           #endif
        }

        /** Hands the keyboard back to the window's own keys (the first parent that takes focus),
            so a key just set can be tried at once. Left alone, JUCE would pass focus on to the
            search box beside the keycap, and the key would type there instead. */
        void returnKeyboardFocus (Component& c)
        {
            for (auto* p = c.getParentComponent(); p != nullptr; p = p->getParentComponent())
            {
                if (p->getWantsKeyboardFocus())
                {
                    p->grabKeyboardFocus();
                    return;
                }
            }

            c.giveAwayKeyboardFocus();
        }
    }

    //==============================================================================================
    /** One key as a keycap; or, with no key, the + that adds one. Click it, then press a key. */
    class ShortcutEditor::Keycap final : public Component, public SettableTooltipClient
    {
    public:
        Keycap (KeyPress k, String commandName) : key (std::move (k)), name (std::move (commandName))
        {
            setWantsKeyboardFocus (false);
            setMouseCursor (MouseCursor::PointingHandCursor);
            setTooltip (key.isValid() ? "Change or remove " + keyText (key) : "Add a key for " + name);
        }

        std::function<void (const KeyPress&)> onCaptured;
        std::function<void()> onRemove;

        int idealWidth() const
        {
            const auto text = capturing ? String ("Press a key") : key.isValid() ? keyText (key) : String ("+");
            return std::max (capHeight, GlyphArrangement::getStringWidthInt (theme::timeFont (theme::textSm), text) + theme::space3);
        }

        void paint (Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat().reduced (0.5f);
            const bool over = isMouseOver() && ! capturing;

            g.setColour (capturing ? theme::well : over ? theme::hover : key.isValid() ? theme::well : theme::bg2);
            g.fillRoundedRectangle (r, theme::radiusXs);
            g.setColour (capturing ? theme::accent : theme::lineStrong);
            g.drawRoundedRectangle (r, theme::radiusXs, theme::stroke);

            g.setFont (theme::timeFont (theme::textSm));
            g.setColour (capturing ? theme::accentText : key.isValid() ? theme::text : theme::muted);
            g.drawText (capturing ? String ("Press a key") : key.isValid() ? keyText (key) : String ("+"),
                        getLocalBounds(), Justification::centred, false);
        }

        void mouseEnter (const MouseEvent&) override   { repaint(); }
        void mouseExit (const MouseEvent&) override    { repaint(); }

        void mouseUp (const MouseEvent& e) override
        {
            if (! e.mouseWasClicked() || capturing)
                return;

            if (! key.isValid())
            {
                capture();
                return;
            }

            PopupMenu m;
            m.addItem ("Change", [this] { capture(); });
            m.addItem ("Remove", [this] { if (onRemove) onRemove(); });
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (this));
        }

        bool keyPressed (const KeyPress& k) override
        {
            if (! capturing)
                return false;

            stopCapture();
            returnKeyboardFocus (*this);

            if (k != KeyPress (KeyPress::escapeKey) && onCaptured)
                onCaptured (k);   // the editor rebuilds its rows later, not inside this call

            return true;
        }

        // While capturing, a held tool key must not switch the tool underneath.
        bool keyStateChanged (bool) override   { return capturing; }
        void focusLost (FocusChangeType) override    { stopCapture(); }

    private:
        KeyPress key;
        String name;
        bool capturing = false;

        void capture()
        {
            capturing = true;
            setWantsKeyboardFocus (true);
            grabKeyboardFocus();
            resizeToFit();
        }

        void stopCapture()
        {
            if (! capturing)
                return;

            capturing = false;
            setWantsKeyboardFocus (false);
            resizeToFit();
        }

        void resizeToFit()
        {
            if (auto* p = getParentComponent())
                p->resized();

            repaint();
        }
    };

    //==============================================================================================
    /** A command's name and its keycaps, then a + to add another. */
    class ShortcutEditor::Row final : public Component
    {
    public:
        Row (CommandID c, const ApplicationCommandInfo& info, const Array<KeyPress>& keys) : id (c)
        {
            styleLabel (name, theme::textMd, theme::text);
            name.setText (info.shortName, dontSendNotification);
            name.setTooltip (info.description);
            name.setMinimumHorizontalScale (0.8f);
            addAndMakeVisible (name);

            searchText = info.shortName + " " + info.categoryName;

            for (const auto& k : keys)
            {
                addAndMakeVisible (caps.add (new Keycap (k, info.shortName)));
                searchText << " " << keyText (k);
            }

            addAndMakeVisible (caps.add (new Keycap ({}, info.shortName)));
        }

        const CommandID id;
        String searchText;
        Label name;
        OwnedArray<Keycap> caps;

        void resized() override
        {
            auto r = getLocalBounds();
            const auto y = (rowHeight - capHeight) / 2;

            for (int i = caps.size(); --i >= 0;)
            {
                const auto w = caps[i]->idealWidth();
                caps[i]->setBounds (r.removeFromRight (w).withY (y).withHeight (capHeight));
                r.removeFromRight (capGap);
            }

            name.setBounds (r);
        }
    };

    //==============================================================================================
    ShortcutEditor::ShortcutEditor (ApplicationCommandManager& m, Array<CommandID> commands)
        : manager (m), ids (std::move (commands))
    {
        search.setTextToShowWhenEmpty ("Find a command or key", theme::faint);
        search.setFont (theme::uiFont (theme::textMd));
        search.onTextChange = [this] { layout(); if (onSearchChanged) onSearchChanged(); };
        search.onEscapeKey = [this] { search.clear(); layout(); if (onSearchChanged) onSearchChanged(); };
        addAndMakeVisible (search);

        resetAll.setTooltip ("Put every shortcut back to how Carthage came");
        resetAll.onClick = [this]
        {
            manager.getKeyMappings()->resetToDefaultMappings();
            say ("Every shortcut is back to its default.");
        };
        addAndMakeVisible (resetAll);

        styleLabel (note, theme::textSm, theme::muted);
        note.setJustificationType (Justification::topLeft);
        addAndMakeVisible (note);

        manager.getKeyMappings()->addChangeListener (this);
        rebuild();
    }

    ShortcutEditor::~ShortcutEditor()
    {
        manager.getKeyMappings()->removeChangeListener (this);
    }

    void ShortcutEditor::say (const String& text, bool warning)
    {
        note.setText (text, dontSendNotification);
        note.setColour (Label::textColourId, warning ? theme::warn : theme::muted);
        layout();
    }

    void ShortcutEditor::assign (CommandID id, const KeyPress& key, const KeyPress& replacing)
    {
        auto* mappings = manager.getKeyMappings();

        if (isSystemKey (key))
        {
            say (keyText (key) + " belongs to the system, so Carthage never sees it. Choose another key.", true);
            return;
        }

        const auto previous = mappings->findCommandForKeyPress (key);

        if (previous == id)
            return;   // already this command's

        if (previous != 0)
        {
            mappings->removeKeyPress (key);
            say (keyText (key) + " moved here from " + manager.getNameOfCommand (previous) + ".", true);
        }
        else
        {
            note.setText ({}, dontSendNotification);
        }

        if (replacing.isValid())
            mappings->removeKeyPress (replacing);

        mappings->addKeyPress (id, key);
    }

    void ShortcutEditor::removeKey (CommandID, const KeyPress& key)
    {
        manager.getKeyMappings()->removeKeyPress (key);
        note.setText ({}, dontSendNotification);
    }

    void ShortcutEditor::rebuild()
    {
        rows.clear();
        headings.clear();

        String category;

        for (auto id : ids)
        {
            const auto* info = manager.getCommandForID (id);

            if (info == nullptr)
                continue;

            if (info->categoryName != category)
            {
                category = info->categoryName;
                auto* h = headings.add (new Label());
                styleLabel (*h, theme::textSm, theme::muted, true);
                h->setText (category, dontSendNotification);
                h->getProperties().set ("index", rows.size());   // the first row under it
                addAndMakeVisible (h);
            }

            auto* row = rows.add (new Row (id, *info, manager.getKeyMappings()->getKeyPressesAssignedToCommand (id)));

            const auto keys = manager.getKeyMappings()->getKeyPressesAssignedToCommand (id);

            for (int i = 0; i < row->caps.size(); ++i)
            {
                const auto old = i < keys.size() ? keys[i] : KeyPress();
                row->caps[i]->onCaptured = [this, id, old] (const KeyPress& k) { assign (id, k, old); };
                row->caps[i]->onRemove = [this, id, old] { removeKey (id, old); };
            }

            addAndMakeVisible (row);
        }

        layout();
    }

    void ShortcutEditor::layout()
    {
        const auto query = search.getText().trim();
        auto r = getLocalBounds().withHeight (100000);

        auto top = r.removeFromTop (rowHeight);
        resetAll.setBounds (top.removeFromRight (88));
        top.removeFromRight (theme::space2);
        search.setBounds (top);

        if (note.getText().isNotEmpty())
        {
            r.removeFromTop (theme::space1);
            note.setBounds (r.removeFromTop (34));
            note.setVisible (true);
        }
        else
        {
            note.setVisible (false);
        }

        int headingIndex = 0;

        for (int i = 0; i < rows.size(); ++i)
        {
            // A heading goes above its first row, if any row under it is shown.
            while (headingIndex < headings.size() && static_cast<int> (headings[headingIndex]->getProperties()["index"]) == i)
            {
                auto* h = headings[headingIndex++];
                const auto next = headingIndex < headings.size() ? static_cast<int> (headings[headingIndex]->getProperties()["index"]) : rows.size();
                bool any = false;

                for (int j = i; j < next; ++j)
                    any = any || query.isEmpty() || rows[j]->searchText.containsIgnoreCase (query);

                h->setVisible (any);

                if (any)
                {
                    r.removeFromTop (theme::space3);
                    h->setBounds (r.removeFromTop (20));
                }
            }

            auto* row = rows[i];
            const bool shown = query.isEmpty() || row->searchText.containsIgnoreCase (query);
            row->setVisible (shown);

            if (shown)
                row->setBounds (r.removeFromTop (rowHeight));
        }

        const auto height = r.getY() + theme::space2;

        if (height != idealHeight)
        {
            idealHeight = height;

            if (onHeightChanged)
                onHeightChanged();
        }
    }

    void ShortcutEditor::resized()   { layout(); }
}
