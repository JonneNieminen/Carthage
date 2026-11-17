#include "PluginScan.h"

namespace carthage::engine
{
    namespace scan
    {
        juce::String toXml (const juce::Array<juce::PluginDescription>& descriptions)
        {
            juce::XmlElement root ("PLUGINS");

            for (const auto& d : descriptions)
                root.addChildElement (d.createXml().release());

            return root.toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
        }

        juce::Array<juce::PluginDescription> fromXml (const juce::String& text)
        {
            juce::Array<juce::PluginDescription> result;

            // The scanner may print other things (a plugin's own logging); find our element.
            const auto start = text.indexOf ("<PLUGINS");
            const auto end = text.lastIndexOf ("</PLUGINS>");

            if (start < 0)
                return result;

            const auto xmlText = end > start ? text.substring (start, end + 10) : text.substring (start);

            if (auto xml = juce::parseXML (xmlText))
            {
                for (auto* child : xml->getChildIterator())
                {
                    juce::PluginDescription d;

                    if (d.loadFromXml (*child))
                        result.add (d);
                }
            }

            return result;
        }

        Outcome runScanner (const juce::File& scanner, const juce::String& formatName,
                            const juce::String& fileOrIdentifier, int timeoutMs)
        {
            Outcome outcome;

            if (! scanner.existsAsFile())
            {
                outcome.detail = "The plugin scanner is missing: " + scanner.getFullPathName();
                return outcome;
            }

            juce::ChildProcess process;
            const juce::StringArray args { scanner.getFullPathName(), formatArg, formatName, pluginArg, fileOrIdentifier };

            if (! process.start (args, juce::ChildProcess::wantStdOut))
            {
                outcome.detail = "Could not start the plugin scanner";
                return outcome;
            }

            // Read while waiting, so a chatty plugin can not fill the pipe and stall the scanner.
            juce::MemoryOutputStream output;
            const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);
            char buffer[4096];

            for (;;)
            {
                const auto n = process.readProcessOutput (buffer, sizeof (buffer));

                if (n > 0)
                    output.write (buffer, static_cast<size_t> (n));
                else if (! process.isRunning())
                    break;
                else if (juce::Time::getMillisecondCounter() > deadline)
                {
                    process.kill();
                    outcome.kind = Outcome::Kind::timedOut;
                    outcome.detail = "The plugin did not finish loading within " + juce::String (timeoutMs / 1000) + " s";
                    return outcome;
                }
                else
                {
                    juce::Thread::sleep (5);
                }
            }

            const auto exitCode = static_cast<int> (process.getExitCode());
            const auto text = output.toString();

            if (exitCode == exitFound)
            {
                outcome.descriptions = fromXml (text);
                outcome.kind = outcome.descriptions.isEmpty() ? Outcome::Kind::nothingFound : Outcome::Kind::found;
            }
            else if (exitCode == exitNothingFound)
            {
                outcome.kind = Outcome::Kind::nothingFound;
                outcome.detail = "The plugin loaded no usable types";
            }
            else
            {
                outcome.kind = Outcome::Kind::crashed;
                outcome.detail = "The plugin crashed the scanner (exit code " + juce::String (exitCode) + ")";
            }

            return outcome;
        }
    }

    //==============================================================================================
    PluginCatalogue::PluginCatalogue (juce::KnownPluginList& k, juce::AudioPluginFormatManager& f, juce::File cacheFolder)
        : known (k), formats (f), folder (std::move (cacheFolder))
    {
        folder.createDirectory();
        load();
    }

    std::vector<PluginCandidate> PluginCatalogue::findCandidates() const
    {
        std::vector<PluginCandidate> result;

        for (auto* format : formats.getFormats())
        {
            const auto name = format->getName();

            if (name != "AudioUnit" && name != "VST3")
                continue;

            for (const auto& id : format->searchPathsForPlugins (format->getDefaultLocationsToSearch(), true, false))
                result.push_back ({ format->getNameOfPluginFromIdentifier (id), name, id });
        }

        std::sort (result.begin(), result.end(), [] (const auto& a, const auto& b)
        {
            const auto c = a.name.compareNatural (b.name);
            return c != 0 ? c < 0 : a.formatName < b.formatName;
        });

        return result;
    }

    bool PluginCatalogue::isBlocked (const juce::String& id) const
    {
        return blocked.containsKey (id);
    }

    void PluginCatalogue::block (const juce::String& id, const juce::String& reason)
    {
        blocked.set (id, reason);
        save();
    }

    juce::Array<juce::PluginDescription> PluginCatalogue::findScanned (const juce::String& id) const
    {
        juce::Array<juce::PluginDescription> result;

        for (const auto& d : known.getTypes())
            if (d.fileOrIdentifier == id)
                result.add (d);

        return result;
    }

    void PluginCatalogue::addScanned (const juce::Array<juce::PluginDescription>& descriptions)
    {
        for (const auto& d : descriptions)
            known.addType (d);

        save();
    }

    bool PluginCatalogue::isScanned (const juce::PluginDescription& d) const
    {
        return known.getTypeForIdentifierString (d.createIdentifierString()) != nullptr;
    }

    void PluginCatalogue::load()
    {
        if (auto xml = juce::parseXML (getKnownPluginsFile()))
            if (xml->getIntAttribute ("version") <= cacheFormatVersion)
                if (auto* list = xml->getChildByName ("KNOWNPLUGINS"))
                    known.recreateFromXml (*list);

        if (auto xml = juce::parseXML (getBlocklistFile()))
            for (auto* e : xml->getChildWithTagNameIterator ("PLUGIN"))
                blocked.set (e->getStringAttribute ("id"), e->getStringAttribute ("reason"));
    }

    void PluginCatalogue::save() const
    {
        {
            juce::XmlElement root ("CARTHAGEPLUGINCACHE");
            root.setAttribute ("version", cacheFormatVersion);

            if (auto list = known.createXml())
                root.addChildElement (list.release());

            root.writeTo (getKnownPluginsFile());
        }

        {
            juce::XmlElement root ("CARTHAGEBLOCKLIST");
            root.setAttribute ("version", cacheFormatVersion);

            for (const auto& key : blocked.getAllKeys())
            {
                auto* e = root.createNewChildElement ("PLUGIN");
                e->setAttribute ("id", key);
                e->setAttribute ("reason", blocked[key]);
            }

            root.writeTo (getBlocklistFile());
        }
    }
}
