#include "Paths.h"

namespace carthage::app
{
    juce::File Paths::settings()
    {
       #if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Application Support/Carthage");
       #else
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Carthage");
       #endif
    }

    juce::File Paths::pluginCache()     { return settings().getChildFile ("Plugins"); }
    juce::File Paths::autosave()        { return settings().getChildFile ("Autosave"); }
    juce::File Paths::shortcuts()       { return settings().getChildFile ("Shortcuts.xml"); }
    juce::File Paths::templates()       { return settings().getChildFile ("Templates"); }
    juce::File Paths::mixerChains()     { return settings().getChildFile ("Mixer chains"); }
    juce::File Paths::presets()         { return settings().getChildFile ("Presets"); }

    juce::File Paths::projects()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Carthage");
    }

    juce::File Paths::newUntitledProject (const juce::Array<juce::File>& inUse)
    {
        auto folder = projects().getChildFile ("Untitled");

        for (int n = 2; folder.exists() || inUse.contains (folder); ++n)
            folder = projects().getChildFile ("Untitled " + juce::String (n));

        return folder;
    }

    std::vector<juce::File> Paths::systemPluginFolders()
    {
       #if JUCE_MAC
        const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        return { juce::File ("/Library/Audio/Plug-Ins/Components"),
                 home.getChildFile ("Library/Audio/Plug-Ins/Components"),
                 juce::File ("/Library/Audio/Plug-Ins/VST3"),
                 home.getChildFile ("Library/Audio/Plug-Ins/VST3") };
       #elif JUCE_WINDOWS
        return { juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory)
                     .getChildFile ("Common Files/VST3") };
       #else
        return {};
       #endif
    }

    juce::File Paths::scanner()
    {
        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
       #if JUCE_WINDOWS
        return exe.getSiblingFile ("carthage-scan.exe");
       #else
        return exe.getSiblingFile ("carthage-scan");
       #endif
    }
}
