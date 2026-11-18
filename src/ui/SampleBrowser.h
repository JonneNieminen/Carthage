#pragma once

#include "Controls.h"

#include <set>

#include <engine/Session.h>

namespace carthage::ui
{
    /** The sample browser (F19, W30), on the left with the plugin browser. It starts empty with
        "Add folder"; the folders added are listed as a tree, and a search lists matching sounds
        from all of them. A click plays a sound to its end through the preview (or, with "Play
        while held" in Settings, while the mouse button, Enter or Space is held); the arrow keys
        move and play. Each sound shows its key and tempo once analysed, and a star. Sounds drag
        onto the timeline's lanes. It reads the session's SampleLibrary and keeps no copy. */
    class SampleBrowser final : public juce::Component,
                                private juce::ListBoxModel
    {
    public:
        explicit SampleBrowser (engine::Session&);
        ~SampleBrowser() override;

        /** The library changed (index, favourite, analysis). */
        void refresh();

        /** From the main component's frame: the playing row's progress. */
        void frame();

        void focusSearch();

        /** Settings: text size (12 to 16 px) and whether a sound plays only while held. */
        void setTextSize (float);
        void setPlayWhileHeld (bool held)   { playWhileHeld = held; }

        /** Asks for a folder and adds it. */
        void addFolder();

        /** For tools: type into the search, and open every folder. */
        void setSearch (const juce::String& text)   { search.setText (text, true); }
        void openAllFolders();

        static constexpr const char* dragPrefix = "carthage-sample:";

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        bool keyStateChanged (bool isKeyDown) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        struct Row
        {
            enum class Type { header, folder, sample };
            Type type = Type::header;
            int index = 0;          ///< folder or sample index in the library
            int depth = 0;
            juce::String text;      ///< header title
        };

        engine::Session& session;
        std::vector<Row> rows;
        std::set<juce::String> open;   // folders shown open, by path
        bool favouritesOnly = false, playWhileHeld = false, heldByKey = false;
        float textSize = 13.0f;
        int rowHeight = 28;
        juce::File playing;
        double lastProgress = -1.0;

        juce::Label status;
        juce::TextButton add { "Add folder" };
        juce::TextEditor search;
        juce::TextButton viewFolders { "Folders" }, viewFavourites { "Favourites" };
        juce::ListBox list;
        juce::Label volumeLabel;
        juce::Slider volume;
        std::unique_ptr<juce::FileChooser> chooser;

        const engine::SampleLibrary::Sample* sampleAt (int row) const;
        void play (int row);
        void toggleFolder (int row);
        void showFolderMenu (int row);
        void setView (bool favourites);
        juce::Rectangle<int> starArea (int width) const   { return { width - 30, 0, 30, rowHeight }; }

        // ListBoxModel
        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void selectedRowsChanged (int lastRowSelected) override;
        void returnKeyPressed (int row) override;
        juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
        juce::String getTooltipForRow (int row) override;

        bool selectingWithMouse = false;
    };
}
