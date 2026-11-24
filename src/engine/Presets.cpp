#include "Presets.h"

namespace carthage::presets
{
    using namespace tracktion;

    namespace
    {
        const juce::Identifier presetTag ("CARTHAGEPRESET");

        juce::String kindOf (const juce::File& f)   { return f.getFileExtension().toLowerCase(); }

        /** The plugin in its Line put back from `state`: a new plugin there, the old one gone. */
        Plugin::Ptr replaceWith (Plugin& plugin, juce::ValueTree state)
        {
            auto* list = plugin.getOwnerList();

            if (list == nullptr)
                return {};

            const auto index = list->indexOf (&plugin);
            auto& edit = plugin.edit;
            EditItemID::remapIDs (state, nullptr, edit);   // a plugin of its own, not the old one
            auto created = edit.getPluginCache().getOrCreatePluginFor (state);

            if (created == nullptr)
                return {};

            const Plugin::Ptr old (&plugin);   // held while it leaves the list
            old->deleteFromParent();
            list->insertPlugin (created, index, nullptr);
            return created;
        }
    }

    juce::String filePatterns()   { return juce::String ("*") + extension + ";*.vstpreset;*.aupreset"; }

    juce::String keyOf (Plugin& p)   { return p.getIdentifierString(); }

    juce::File folderFor (const juce::File& root, Plugin& p)
    {
        return root.getChildFile (juce::File::createLegalFileName (p.getName()));
    }

    namespace
    {
        /** Whether a file opens on a plugin of this key and format ("VST3", "AudioUnit", or "" for a
            built-in). Carthage's own files say which plugin they came from; the others say only
            their format here (the plugin itself checks the rest when it reads them). */
        bool opens (const juce::String& key, const juce::String& format, const juce::File& file)
        {
            const auto kind = kindOf (file);

            if (kind == ".vstpreset")   return format == "VST3";
            if (kind == ".aupreset")    return format == "AudioUnit";

            if (kind == extension)
                if (const auto xml = juce::parseXML (file); xml != nullptr && xml->hasTagName (presetTag.toString()))
                    return xml->getStringAttribute ("plugin") == key;

            return false;
        }

        juce::Array<juce::File> listIn (const juce::File& folder, const juce::String& key, const juce::String& format)
        {
            juce::Array<juce::File> found;

            for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, filePatterns()))
                if (opens (key, format, f))
                    found.add (f);

            found.sort();
            return found;
        }

        juce::String formatOf (Plugin& p)
        {
            auto* ext = dynamic_cast<ExternalPlugin*> (&p);
            return ext != nullptr ? ext->desc.pluginFormatName : juce::String();
        }
    }

    bool canOpen (Plugin& p, const juce::File& file)   { return opens (keyOf (p), formatOf (p), file); }

    juce::Array<juce::File> listFor (const juce::File& root, Plugin& p)
    {
        return listIn (folderFor (root, p), keyOf (p), formatOf (p));
    }

    juce::Array<juce::File> listFor (const juce::File& root, const juce::PluginDescription& d)
    {
        return listIn (root.getChildFile (juce::File::createLegalFileName (d.name)), createIdentifierString (d), d.pluginFormatName);
    }

    juce::Result save (Plugin& p, const juce::File& file)
    {
        p.flushPluginStateToValueTree();   // a plugin's own state is in its tree only once flushed
        juce::ValueTree preset (presetTag);
        preset.setProperty ("version", 1, nullptr);
        preset.setProperty ("plugin", keyOf (p), nullptr);
        preset.setProperty ("name", p.getName(), nullptr);
        preset.appendChild (p.state.createCopy(), nullptr);
        const auto xml = preset.createXml();

        if (xml == nullptr || ! file.getParentDirectory().createDirectory().wasOk() || ! xml->writeTo (file))
            return juce::Result::fail ("Carthage could not write " + file.getFullPathName());

        return juce::Result::ok();
    }

    std::pair<Plugin::Ptr, juce::Result> load (Plugin& p, const juce::File& file)
    {
        if (! canOpen (p, file))
            return { {}, juce::Result::fail (file.getFileName() + " is not a preset for " + p.getName()) };

        if (kindOf (file) == extension)
        {
            const auto xml = juce::parseXML (file);
            const auto preset = xml != nullptr ? juce::ValueTree::fromXml (*xml) : juce::ValueTree();
            const auto state = preset.getChild (0);

            if (! state.isValid())
                return { {}, juce::Result::fail (file.getFileName() + " has no settings in it") };

            auto created = replaceWith (p, state.createCopy());
            return { created, created != nullptr ? juce::Result::ok() : juce::Result::fail ("Carthage could not make " + p.getName() + " from " + file.getFileName()) };
        }

        // An Audio Unit's preset is the unit's whole state, as the host stores it: the new plugin
        // is made from the old one's tree with that state in it, the old one untouched.
        if (kindOf (file) == ".aupreset")
        {
            juce::MemoryBlock data;

            if (! file.loadFileAsData (data) || data.isEmpty())
                return { {}, juce::Result::fail ("Carthage could not read " + file.getFileName()) };

            p.flushPluginStateToValueTree();
            auto state = p.state.createCopy();
            state.setProperty (IDs::state, data.toBase64Encoding(), nullptr);
            state.removeProperty (IDs::parameters, nullptr);   // values the old settings left behind
            auto created = replaceWith (p, state);
            return { created, created != nullptr ? juce::Result::ok() : juce::Result::fail ("Carthage could not make " + p.getName() + " from " + file.getFileName()) };
        }

        // A VST3's preset: the plugin reads it, its new state is taken, and the plugin in the Line
        // goes back to how it was before (that is what an undo brings back).
        auto* ext = dynamic_cast<ExternalPlugin*> (&p);
        auto* instance = ext != nullptr ? ext->getAudioPluginInstance() : nullptr;
        juce::MemoryBlock data;

        if (instance == nullptr || ! file.loadFileAsData (data))
            return { {}, juce::Result::fail ("Carthage could not read " + file.getFileName() + " into " + p.getName()) };

        p.flushPluginStateToValueTree();
        const auto before = p.state.createCopy();
        bool read = false;

        if (auto* vst3 = instance->getVST3Client())
            read = vst3->setPreset (data);

        if (! read)
            return { {}, juce::Result::fail (p.getName() + " did not take " + file.getFileName()) };

        p.flushPluginStateToValueTree();
        auto after = p.state.createCopy();
        p.state.copyPropertiesAndChildrenFrom (before, nullptr);   // the old plugin as it was, for undo
        auto created = replaceWith (p, after);
        return { created, created != nullptr ? juce::Result::ok() : juce::Result::fail ("Carthage could not make " + p.getName() + " from " + file.getFileName()) };
    }
}
