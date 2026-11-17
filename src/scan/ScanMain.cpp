// carthage-scan: the out-of-process plugin scanner (F9).
//
//   carthage-scan --format <AudioUnit|VST3> --plugin <file or AU identifier>
//
// Loads the plugin the way the app would, prints the descriptions it found as one <PLUGINS> XML
// element on stdout and exits 0. Exits 2 if the plugin loaded nothing usable, 3 on bad arguments.
// If the plugin crashes or hangs, this process dies or is killed and the app blocks the plugin.

#include <engine/PluginScan.h>

#include <iostream>

int main (int argc, char* argv[])
{
    using namespace carthage::engine;

    juce::String formatName, plugin;

    for (int i = 1; i + 1 < argc; ++i)
    {
        const juce::String arg (argv[i]);

        if (arg == scan::formatArg)       formatName = juce::String::fromUTF8 (argv[++i]);
        else if (arg == scan::pluginArg)  plugin = juce::String::fromUTF8 (argv[++i]);
    }

    if (formatName.isEmpty() || plugin.isEmpty())
    {
        std::cerr << "usage: carthage-scan --format <AudioUnit|VST3> --plugin <file or identifier>\n";
        return scan::exitBadArguments;
    }

    // Plugins expect a message manager on the thread that creates them.
    const juce::ScopedJuceInitialiser_GUI juce;

    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);

    juce::AudioPluginFormat* format = nullptr;

    for (auto* f : formats.getFormats())
        if (f->getName() == formatName)
            format = f;

    if (format == nullptr)
    {
        std::cerr << "unknown plugin format: " << formatName << "\n";
        return scan::exitBadArguments;
    }

    juce::OwnedArray<juce::PluginDescription> found;
    format->findAllTypesForFile (found, plugin);

    if (found.isEmpty())
        return scan::exitNothingFound;

    juce::Array<juce::PluginDescription> descriptions;

    for (auto* d : found)
        descriptions.add (*d);

    // Plugins sometimes print to stdout themselves; the app looks for our element only.
    std::cout << "\n" << scan::toXml (descriptions) << "\n" << std::flush;
    return scan::exitFound;
}
