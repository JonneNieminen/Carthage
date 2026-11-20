#pragma once

#include <engine/PluginLibrary.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <tracktion_engine/tracktion_engine.h>

#include <map>

namespace carthage::ui
{
    /** The windows of the plugins on every channel's Line (5b). Any number open at once, one per
        plugin; opening one that is open brings it forward. A window holds its plugin, so the
        plugin (and its editor) live as long as the window; a plugin removed from its Line has its
        window closed (closeMissing). Message thread. */
    class PluginWindows
    {
    public:
        PluginWindows();
        ~PluginWindows();

        void open (tracktion::Plugin&);
        void close (const tracktion::Plugin&);
        bool isOpen (const tracktion::Plugin&) const;
        void closeAll();

        /** Closes the windows of plugins that are no longer in any Line (deleted, undone, moved). */
        void closeMissing();

        /** Cmd/Ctrl+W: closes `front` if it is one of these windows. */
        bool closeIfFront (const juce::Component* front);

        void setAlwaysOnTop (bool);

        /** Above Carthage's own windows unless switched off (F21); asked when a window opens. */
        std::function<bool()> keepOnTop;

        class Window;

    private:
        std::map<tracktion::EditItemID, std::unique_ptr<Window>> windows;
        std::shared_ptr<int> alive = std::make_shared<int> (0);   // a closing window's async step checks it
    };

    /** A search field and the scanned effects, best match first; a click (or Return) chooses.
        Shown in a call-out from an empty slot (owner: double-click an empty slot). */
    class PluginChooser final : public juce::Component, private juce::ListBoxModel
    {
    public:
        PluginChooser (engine::PluginLibrary&, std::function<void (const juce::PluginDescription&)> chosen);

        void resized() override;

    private:
        engine::PluginLibrary& library;
        std::function<void (const juce::PluginDescription&)> onChosen;
        juce::TextEditor search;
        juce::ListBox list { {}, this };
        std::vector<size_t> shown;

        void refresh();
        void choose (int row);

        int getNumRows() override   { return static_cast<int> (shown.size()); }
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override   { choose (row); }
        void returnKeyPressed (int row) override                             { choose (row); }
    };
}
