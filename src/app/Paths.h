#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace carthage::app
{
    /** Every folder Carthage uses, in one place (AGENTS.md: "Paths to user folders come from one
        place in app/"). The help page (phase 7) lists them from here; nothing else builds a path to
        a user folder. docs/ARCHITECTURE.md, "Files and folders", has the same table. */
    struct Paths
    {
        /** Settings: audio device, shortcuts, update preferences.
            macOS ~/Library/Application Support/Carthage, Windows %APPDATA%\Carthage */
        static juce::File settings();

        /** Plugin scan cache: known plugins and the blocklist. <settings>/Plugins */
        static juce::File pluginCache();

        /** Default home for projects. ~/Music/Carthage (macOS), Music\Carthage (Windows) */
        static juce::File projects();

        /** A folder for a new, untitled project: <projects>/Untitled, or Untitled 2, 3… if taken.
            It is created when something is recorded into it. */
        static juce::File newUntitledProject();

        /** Shortcuts the user changed (only the changes). <settings>/Shortcuts.xml */
        static juce::File shortcuts();

        /** Autosaves and the crash marker (phase 4). <settings>/Autosave */
        static juce::File autosave();

        /** The system plugin folders, which Carthage only reads. */
        static std::vector<juce::File> systemPluginFolders();

        /** carthage-scan, shipped next to the app's executable. */
        static juce::File scanner();
    };
}
