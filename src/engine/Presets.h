#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace carthage::presets
{
    /** One plugin's presets (F19, W64): its settings in a file of its own, to load onto the same
        plugin on any channel. Carthage's own files (".carthagepreset": which plugin, and its
        state) and the plugins' own formats: a VST3's ".vstpreset" and an Audio Unit's
        ".aupreset". Loading never writes into the running plugin: a fresh copy of it, built
        from the preset, takes its place in the Line, so a load is one undo step like any edit.
        Whole Lines are mixer chains (editing::chainOf, W63). Message thread. */
    inline constexpr const char* extension = ".carthagepreset";

    /** The files a preset can be: "*.carthagepreset;*.vstpreset;*.aupreset". */
    juce::String filePatterns();

    /** Which plugin a preset belongs to: the plugin's identifier (format, maker, name, id). */
    juce::String keyOf (tracktion::Plugin&);

    /** A plugin's folder of presets under `root`: one folder per plugin, by its name. */
    juce::File folderFor (const juce::File& root, tracktion::Plugin&);

    /** The presets in a plugin's folder that it can open, by name. */
    juce::Array<juce::File> listFor (const juce::File& root, tracktion::Plugin&);

    /** Whether `file` is a preset this plugin can open (by its kind, and for Carthage's own files,
        which plugin it was saved from). */
    bool canOpen (tracktion::Plugin&, const juce::File&);

    /** The same for a plugin not yet on a channel, by its description (the plugin browser). */
    juce::Array<juce::File> listFor (const juce::File& root, const juce::PluginDescription&);

    /** Saves the plugin's settings as a Carthage preset. */
    juce::Result save (tracktion::Plugin&, const juce::File&);

    /** Loads a preset onto the plugin: a new plugin made from it takes this one's place (the old
        one leaves the Line; one undo step, the caller's). Returns the new plugin, or null and why. */
    std::pair<tracktion::Plugin::Ptr, juce::Result> load (tracktion::Plugin&, const juce::File&);
}
