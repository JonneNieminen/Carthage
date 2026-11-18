#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace carthage::ui
{
    /** The shortcut editor (brief, feature 3; AGENTS.md: "all rebindable"). Every command in the
        command table, by menu, with its keys as keycaps. Click a keycap to change or remove that key,
        or + to add one; then press the new key. A key another command had moves here, and the
        editor says which command lost it. Escape cancels.

        It reads and writes the command manager's key mappings, which the menus, the keyboard and
        every tooltip read too; the app saves them (app/Main.cpp). It sizes itself to its rows, for
        the settings panel's scrolling body. */
    class ShortcutEditor final : public juce::Component, private juce::ChangeListener
    {
    public:
        /** `commands`: every command to list, in the order to list them. */
        ShortcutEditor (juce::ApplicationCommandManager&, juce::Array<juce::CommandID> commands);
        ~ShortcutEditor() override;

        /** The height its rows need at the width it has. */
        int getIdealHeight() const noexcept   { return idealHeight; }
        std::function<void()> onHeightChanged;

        /** The search text changed (after the rows were laid out again). */
        std::function<void()> onSearchChanged;

        void resized() override;

    private:
        class Keycap;
        class Row;

        juce::ApplicationCommandManager& manager;
        juce::Array<juce::CommandID> ids;
        juce::TextEditor search;
        juce::TextButton resetAll { "Reset all" };
        juce::Label note;
        juce::OwnedArray<juce::Label> headings;
        juce::OwnedArray<Row> rows;
        int idealHeight = 0;

        void rebuild();
        void layout();
        void changeListenerCallback (juce::ChangeBroadcaster*) override   { rebuild(); }

        /** Gives `key` to `id` (replacing `replacing` if it is valid), taking it from any other command. */
        void assign (juce::CommandID id, const juce::KeyPress& key, const juce::KeyPress& replacing);
        void removeKey (juce::CommandID id, const juce::KeyPress& key);
        void say (const juce::String& text, bool warning = false);
    };
}
