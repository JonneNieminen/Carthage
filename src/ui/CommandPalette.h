#pragma once

#include "Controls.h"

namespace carthage::ui
{
    /** Cmd/Ctrl+K (F21): type to find any command in the command table and run it. Lists the
        commands that can run now, with their menu and their keys; the best match first. Enter or
        a click runs one; Escape closes. It reads the command manager, so a rebound key shows. */
    class CommandPalette final : public juce::Component, private juce::ListBoxModel, private juce::KeyListener
    {
    public:
        explicit CommandPalette (juce::ApplicationCommandManager&);
        ~CommandPalette() override;

        /** Opens with an empty search, over `area` of the parent. */
        void open();
        void close();

        /** The command that opens the palette: not listed in it. */
        void setOwnCommand (juce::CommandID id)   { own = id; }

        /** Lists only these commands (the views palette, Cmd/Ctrl+J, W41), with its own prompt. */
        void setOnly (juce::Array<juce::CommandID> ids, const juce::String& prompt)
        {
            only = std::move (ids);
            search.setTextToShowWhenEmpty (prompt, theme::faint);
        }

        void paint (juce::Graphics&) override;
        void resized() override;
        /** A click elsewhere takes the keyboard away from it: it closes. */
        void focusOfChildComponentChanged (FocusChangeType) override;
        using juce::Component::keyPressed;

    private:
        juce::ApplicationCommandManager& manager;
        juce::TextEditor search;
        juce::ListBox list;
        juce::Array<juce::CommandID> shown;
        juce::CommandID own = 0;
        juce::Array<juce::CommandID> only;

        void filter();
        void run (int row);

        int getNumRows() override   { return shown.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override   { run (row); }
        void returnKeyPressed (int row) override                             { run (row); }
        bool keyPressed (const juce::KeyPress&, juce::Component*) override;
    };
}
