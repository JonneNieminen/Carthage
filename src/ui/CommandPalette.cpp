#include "CommandPalette.h"

#include <engine/PluginLibrary.h>   // fuzzyScore: the same matching as the browsers

namespace carthage::ui
{
    using namespace juce;

    namespace
    {
        constexpr int rowHeight = 36, width = 560, maxRows = 10;
    }

    CommandPalette::CommandPalette (ApplicationCommandManager& m) : manager (m)
    {
        search.setTextToShowWhenEmpty (String::fromUTF8 ("Find a command\xe2\x80\xa6"), theme::faint);
        search.setFont (theme::uiFont (theme::textLg));
        search.setIndents (theme::space3, 8);
        search.onTextChange = [this] { filter(); };
        search.onEscapeKey = [this] { close(); };
        search.onReturnKey = [this] { run (std::max (0, list.getSelectedRow())); };
        search.addKeyListener (this);
        addAndMakeVisible (search);

        list.setModel (this);
        list.setRowHeight (rowHeight);
        list.setColour (ListBox::backgroundColourId, theme::bg2);
        list.setOutlineThickness (0);
        addAndMakeVisible (list);
    }

    CommandPalette::~CommandPalette()
    {
        search.removeKeyListener (this);
        list.setModel (nullptr);
    }

    void CommandPalette::open()
    {
        if (auto* parent = getParentComponent())
        {
            const auto rows = maxRows;
            setBounds (Rectangle<int> (std::min (width, parent->getWidth() - 32), 52 + rows * rowHeight + 8)
                           .withCentre ({ parent->getWidth() / 2, 0 }).withY (88));
        }

        search.clear();
        filter();
        setVisible (true);
        toFront (true);
        search.grabKeyboardFocus();
    }

    void CommandPalette::close()
    {
        setVisible (false);

        if (auto* parent = getParentComponent())
            parent->grabKeyboardFocus();
    }

    void CommandPalette::focusOfChildComponentChanged (FocusChangeType)
    {
        if (isVisible() && ! hasKeyboardFocus (true))
            MessageManager::callAsync ([safe = SafePointer<CommandPalette> (this)]
            {
                if (safe != nullptr && ! safe->hasKeyboardFocus (true))
                    safe->setVisible (false);
            });
    }

    void CommandPalette::filter()
    {
        const auto query = search.getText().trim();
        std::vector<std::pair<int, CommandID>> scored;

        for (int i = 0; i < manager.getNumCommands(); ++i)
        {
            const auto* info = manager.getCommandForIndex (i);

            if (info == nullptr || info->commandID == StandardApplicationCommandIDs::quit || info->commandID == own)
                continue;

            ApplicationCommandInfo now (info->commandID);
            manager.getTargetForCommand (info->commandID, now);   // fills in its state now

            if ((now.flags & ApplicationCommandInfo::isDisabled) != 0 || now.shortName.isEmpty())
                continue;

            const auto score = query.isEmpty() ? 0 : std::max (engine::fuzzyScore (query, now.shortName),
                                                                engine::fuzzyScore (query, now.categoryName) / 3);

            if (score >= 0)
                scored.emplace_back (score, info->commandID);
        }

        std::stable_sort (scored.begin(), scored.end(), [this] (const auto& a, const auto& b)
        {
            if (a.first != b.first)
                return a.first > b.first;

            return manager.getNameOfCommand (a.second).compareNatural (manager.getNameOfCommand (b.second)) < 0;
        });

        shown.clearQuick();

        for (const auto& s : scored)
            shown.add (s.second);

        list.updateContent();
        list.selectRow (0);
        list.repaint();
    }

    void CommandPalette::run (int row)
    {
        if (row < 0 || row >= shown.size())
            return;

        const auto id = shown[row];
        close();
        manager.invokeDirectly (id, true);
    }

    bool CommandPalette::keyPressed (const KeyPress& k, Component*)
    {
        // The arrows move the choice while the search keeps the typing.
        if (k == KeyPress::downKey || k == KeyPress::upKey)
        {
            const auto row = jlimit (0, std::max (0, shown.size() - 1), list.getSelectedRow() + (k == KeyPress::downKey ? 1 : -1));
            list.selectRow (row);
            return true;
        }

        return false;
    }

    void CommandPalette::paintListBoxItem (int row, Graphics& g, int w, int h, bool selected)
    {
        if (row < 0 || row >= shown.size())
            return;

        const auto id = shown[row];
        ApplicationCommandInfo info (id);
        manager.getTargetForCommand (id, info);

        auto r = Rectangle<int> (0, 0, w, h).reduced (theme::space1, 1);

        if (selected)
        {
            g.setColour (theme::accent.withAlpha (0.12f));   // the choice is live
            g.fillRoundedRectangle (r.toFloat(), theme::radiusSm);
        }

        r.reduce (theme::space3, 0);

        // Its keys as keycaps, right.
        const auto keys = manager.getKeyMappings()->getKeyPressesAssignedToCommand (id);

        if (! keys.isEmpty())
        {
            const auto text = keys.getFirst().getTextDescriptionWithIcons();
            g.setFont (theme::timeFont (theme::textSm));
            const auto capW = GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text) + theme::space3;
            const auto cap = r.removeFromRight (capW).withSizeKeepingCentre (capW, 22).toFloat();
            g.setColour (theme::well);
            g.fillRoundedRectangle (cap, theme::radiusXs);
            g.setColour (theme::lineStrong);
            g.drawRoundedRectangle (cap.reduced (0.5f), theme::radiusXs, theme::stroke);
            g.setColour (theme::muted);
            g.drawText (text, cap, Justification::centred, false);
            r.removeFromRight (theme::space3);
        }

        g.setColour (theme::faint);
        g.setFont (theme::uiFont (theme::textSm));
        g.drawText (info.categoryName, r.removeFromRight (90), Justification::centredRight, false);

        g.setColour (theme::text);
        g.setFont (theme::uiFont (theme::textMd));
        g.drawText (info.shortName, r, Justification::centredLeft, true);
    }

    void CommandPalette::paint (Graphics& g)
    {
        g.setColour (theme::bg2);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), theme::radiusMd);
        g.setColour (theme::lineStrong);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), theme::radiusMd, theme::stroke);
    }

    void CommandPalette::resized()
    {
        auto r = getLocalBounds().reduced (theme::space2);
        search.setBounds (r.removeFromTop (40));
        r.removeFromTop (theme::space1);
        list.setBounds (r);
    }
}
