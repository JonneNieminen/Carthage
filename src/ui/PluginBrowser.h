#pragma once

#include "Controls.h"

#include <engine/Session.h>

namespace carthage::ui
{
    /** The manufacturer filter: looks like the other menus, but opens a list with its own search,
        so one manufacturer among many is a few letters away (owner). */
    class ManufacturerBox final : public juce::ComboBox
    {
    public:
        std::function<void()> onPopup;
        void showPopup() override
        {
            if (onPopup)
                onPopup();

            // JUCE marks the box open until its own menu closes; this one shows no JUCE menu, so
            // clear the mark at once or later clicks are ignored (owner: the filter "locked").
            hidePopup();
        }
    };

    /** The plugin browser (brief, feature 6): a panel on the left of the window. The scan's state
        and Rescan at the top; a search field and filters for type, format and manufacturer; the list,
        which without a search shows Recent, Favourites and every plugin A to Z, and with one shows
        the best matches first. A plugin goes into the insert by double-click, Enter, or a drag onto
        the plugin card. Plugins that can not be used are listed last, each with its reason and
        "Try again". It reads the session's PluginLibrary and catalogue and keeps no copy of them. */
    class PluginBrowser final : public juce::Component,
                                private juce::ListBoxModel
    {
    public:
        explicit PluginBrowser (engine::Session&);
        ~PluginBrowser() override;

        /** Rebuilds the list (the library changed, or a filter did). */
        void refresh();

        /** About eight times a second from the main component: the scan's progress. */
        void frame();

        void focusSearch();

        /** For tools: type into the search field, and open the list of plugins that can not be used. */
        void setSearch (const juce::String& text)   { search.setText (text, true); }
        void setShowUnavailable (bool show)         { showUnavailable = show; refresh(); }

        /** Insert this plugin into the signal path. */
        std::function<void (const juce::PluginDescription&)> onInsert;

        /** Presets under each plugin (5e): what a plugin has, and adding it with one of them. */
        std::function<juce::Array<juce::File> (const juce::PluginDescription&)> presetsFor;
        std::function<void (const juce::PluginDescription&, const juce::File&)> onInsertWithPreset;

        static constexpr int preferredWidth = 300;

        /** The width that shows the longest name in the list whole (double-click on the edge). */
        int idealWidth() const;
        static constexpr const char* dragPrefix = "carthage-plugin:";

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        struct Row
        {
            enum class Type { header, plugin, unavailable };
            Type type = Type::header;
            juce::String text;       ///< header title, or the unavailable plugin's name
            size_t entry = 0;        ///< index into the library's entries (plugin rows)
            juce::String id;         ///< fileOrIdentifier (unavailable rows)
        };

        engine::Session& session;
        std::vector<Row> rows;
        engine::PluginLibrary::Filter filter;
        bool showUnavailable = false;
        engine::PluginScanner::Progress lastProgress;

        juce::Label title, status;
        juce::TextButton rescan { "Rescan" };
        juce::TextEditor search;
        juce::TextButton kindAll { "All" }, kindInstruments { "Instruments" }, kindEffects { "Effects" };
        juce::ComboBox format;
        ManufacturerBox vendor;
        void showManufacturers();
        juce::ListBox list;

        const engine::PluginLibrary::Entry* entryAt (int row) const;
        void insertRow (int row);
        void setKind (engine::PluginLibrary::Kind);
        void updateStatus();

        // ListBoxModel
        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
        void returnKeyPressed (int row) override;
        juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
        juce::String getTooltipForRow (int row) override;
    };
}
