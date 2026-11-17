#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace carthage::engine
{
    /** Out-of-process plugin scanning (F9, AGENTS.md "Plugins").

        The app never loads a plugin binary it has not first seen loaded successfully by
        `carthage-scan`, a separate executable that loads one plugin, prints its descriptions as XML on
        stdout and exits. A scanner that crashes, fails or hangs puts the plugin on the blocklist.

        Phase 1 scans one plugin at a time, on demand. Phase 3 builds the full manager on this.
    */
    namespace scan
    {
        /** Command-line protocol between the app and carthage-scan. */
        inline constexpr const char* formatArg = "--format";
        inline constexpr const char* pluginArg = "--plugin";
        inline constexpr int exitFound = 0, exitNothingFound = 2, exitBadArguments = 3;

        struct Outcome
        {
            enum class Kind { found, nothingFound, crashed, timedOut, couldNotStart };

            Kind kind = Kind::couldNotStart;
            juce::Array<juce::PluginDescription> descriptions;
            juce::String detail;

            bool shouldBlock() const noexcept   { return kind == Kind::crashed || kind == Kind::timedOut || kind == Kind::nothingFound; }
        };

        /** Runs the scanner on one plugin and waits for it. Call from a worker thread. */
        Outcome runScanner (const juce::File& scannerExecutable, const juce::String& formatName,
                            const juce::String& fileOrIdentifier, int timeoutMs);

        /** Serialises descriptions the way the scanner prints them, and parses them back. */
        juce::String toXml (const juce::Array<juce::PluginDescription>&);
        juce::Array<juce::PluginDescription> fromXml (const juce::String&);
    }

    /** One plugin the user could pick: found on disk or in the system's AU registry, not yet loaded. */
    struct PluginCandidate
    {
        juce::String name, formatName, fileOrIdentifier;
    };

    /** The plugins Carthage knows about: what the scanner confirmed, and what it refused. Both
        lists live in the plugin scan cache folder, with a version, so a newer Carthage reads an
        older cache. */
    class PluginCatalogue
    {
    public:
        PluginCatalogue (juce::KnownPluginList&, juce::AudioPluginFormatManager&, juce::File cacheFolder);

        /** Lists AU and VST3 plugins in the standard locations without loading any of them. */
        std::vector<PluginCandidate> findCandidates() const;

        bool isBlocked (const juce::String& fileOrIdentifier) const;
        void block (const juce::String& fileOrIdentifier, const juce::String& reason);

        /** Descriptions the scanner confirmed for this file or identifier, if any. */
        juce::Array<juce::PluginDescription> findScanned (const juce::String& fileOrIdentifier) const;
        void addScanned (const juce::Array<juce::PluginDescription>&);

        /** True if this exact plugin was confirmed by the scanner (the engine's load gate). */
        bool isScanned (const juce::PluginDescription&) const;

        juce::File getKnownPluginsFile() const     { return folder.getChildFile ("KnownPlugins.xml"); }
        juce::File getBlocklistFile() const        { return folder.getChildFile ("Blocklist.xml"); }

        static constexpr int cacheFormatVersion = 1;

    private:
        juce::KnownPluginList& known;
        juce::AudioPluginFormatManager& formats;
        juce::File folder;
        juce::StringPairArray blocked;   // fileOrIdentifier -> reason

        void load();
        void save() const;
    };
}
