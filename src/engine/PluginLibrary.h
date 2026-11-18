#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace carthage::engine
{
    /** How well `query` matches `text`, case-insensitively: -1 when its letters do not all appear in
        order; otherwise higher is better. Word starts ("nam" in "Neural Amp Modeler"), runs of
        consecutive letters and an exact prefix score highest; gaps cost a little. Pure; tested in
        tests/PluginLibraryTests.cpp. */
    int fuzzyScore (const juce::String& query, const juce::String& text);

    /** The plugin browser's data (brief, feature 6): every confirmed plugin, with favourites and
        when each was last used, searched and filtered. Favourites and use times live in the plugin
        scan cache folder (Library.xml, versioned). Message thread. */
    class PluginLibrary
    {
    public:
        enum class Kind { any, instruments, effects };

        struct Filter
        {
            juce::String query;           ///< fuzzy; several words must all match name or vendor
            juce::String format;          ///< "" = any, "AudioUnit", "VST3"
            juce::String vendor;          ///< "" = any, else exact manufacturer name
            Kind kind = Kind::any;
            bool favouritesOnly = false;
        };

        struct Entry
        {
            juce::PluginDescription description;
            juce::String key;             ///< createIdentifierString()
            bool favourite = false;
            juce::int64 lastUsedMs = 0;   ///< 0 = never
        };

        explicit PluginLibrary (juce::File cacheFolder);

        /** Replaces the plugin list (after a scan); favourites and use times are kept by key. */
        void setPlugins (const juce::Array<juce::PluginDescription>&);

        const std::vector<Entry>& getEntries() const noexcept   { return entries; }

        /** Indices into getEntries(): with a query, best match first; without, by name. */
        std::vector<size_t> find (const Filter&) const;

        /** The most recently used, newest first. */
        std::vector<size_t> recent (size_t count) const;

        juce::StringArray getVendors() const;

        void setFavourite (const juce::String& key, bool);
        void markUsed (const juce::String& key);
        const Entry* findEntry (const juce::String& key) const;

        static constexpr int fileVersion = 1;

    private:
        juce::File file;
        std::vector<Entry> entries;
        std::vector<juce::String> lowerNames, lowerVendors;
        juce::StringArray favourites;                 // by key; kept for plugins not present now
        std::map<juce::String, juce::int64> lastUsed;

        void load();
        void save() const;
    };
}
