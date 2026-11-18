#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

namespace carthage::autosave
{
    /** Autosave and crash recovery (docs/ARCHITECTURE.md, "Phase 4a"; DECISIONS W30).

        Each open project has a folder `<settings>/Autosave/<project id>/` holding its newest
        autosaves (`<date time>.carthage`, the last 10) and, while it is open, a `running` marker
        naming the project file. A clean close removes the marker; a marker found at launch means
        Carthage did not quit cleanly. Tested headless in tests/ProjectTests.cpp. */

    /** When to autosave: after an edit, once the user has paused for `pauseSeconds`, and no more
        often than every `intervalSeconds`. Times in seconds on any steady clock. */
    struct Policy
    {
        double pauseSeconds = 2.0;
        double intervalSeconds = 120.0;
        int keep = 10;

        /** `changedSinceSave`: an edit happened after the last autosave. `lastSave` is empty until
            this project's first autosave. */
        bool isDue (double now, bool changedSinceSave, double lastChange, std::optional<double> lastSave) const noexcept
        {
            return changedSinceSave
                && now - lastChange >= pauseSeconds
                && (! lastSave.has_value() || now - *lastSave >= intervalSeconds);
        }
    };

    class Store final
    {
    public:
        explicit Store (juce::File root) : root (std::move (root)) {}

        juce::File folderFor (const juce::String& projectId) const;

        /** A new file to write the next autosave to (named by the time, so they sort). */
        juce::File nextFile (const juce::String& projectId) const;

        /** Deletes all but the newest `keep` autosaves. */
        void prune (const juce::String& projectId, int keep) const;

        /** The project is open: writes the marker. */
        void markOpen (const juce::String& projectId, const juce::File& projectFile) const;

        /** The project was closed cleanly: removes the marker. */
        void markClosed (const juce::String& projectId) const;

        /** A project Carthage did not close cleanly, and its newest autosave. */
        struct Recovery
        {
            juce::String projectId;
            juce::File projectFile;   ///< Where the project lives (it may never have been saved)
            juce::File autosave;
            juce::Time savedAt;
        };

        /** Projects whose marker was left behind, newest first. One whose project file was saved
            after its newest autosave has nothing to recover; its marker is removed. */
        std::vector<Recovery> findUnclosed() const;

        /** Newest first. */
        juce::Array<juce::File> autosavesOf (const juce::String& projectId) const;

    private:
        juce::File root;
    };
}
