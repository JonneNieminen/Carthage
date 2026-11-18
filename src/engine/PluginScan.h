#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <memory>

namespace carthage::engine
{
    /** Out-of-process plugin scanning (F9, AGENTS.md "Plugins").

        The app never loads a plugin binary it has not first seen loaded successfully by
        `carthage-scan`, a separate executable that loads one plugin, prints its descriptions as XML on
        stdout and exits. A scanner that crashes, fails or hangs puts the plugin on the blocklist.

        PluginScanner (below) checks every plugin in the background, several scanners at a time.
    */
    namespace scan
    {
        /** Command-line protocol between the app and carthage-scan. */
        inline constexpr const char* formatArg = "--format";
        inline constexpr const char* pluginArg = "--plugin";
        inline constexpr int exitFound = 0, exitNothingFound = 2, exitBadArguments = 3, exitIncompatible = 4;

        /** The CPU architectures in a Mach-O binary ("arm64", "x86_64", "i386"), thin or universal;
            empty if the file is not Mach-O (Windows DLLs, a missing file). Reads only the header. */
        juce::StringArray machOArchitectures (const juce::File& binary);

        /** The architecture this build of Carthage runs as. */
        juce::String hostArchitecture();

        /** For a VST3 bundle on macOS: the binary's architectures, read without loading it. */
        juce::StringArray vst3Architectures (const juce::File& bundle);

        struct Outcome
        {
            enum class Kind { found, nothingFound, incompatible, crashed, timedOut, couldNotStart };

            Kind kind = Kind::couldNotStart;
            juce::Array<juce::PluginDescription> descriptions;
            juce::String detail;

            bool shouldBlock() const noexcept
            {
                return kind == Kind::crashed || kind == Kind::timedOut || kind == Kind::nothingFound || kind == Kind::incompatible;
            }

            /** How the blocklist records it: "incompatible", "crashed", "hung" or "unusable". */
            juce::String blockKind() const;
        };

        /** Runs the scanner on one plugin and waits for it. Call from a worker thread. If `cancel`
            becomes true, the scanner is killed and the outcome is couldNotStart ("cancelled"). */
        Outcome runScanner (const juce::File& scannerExecutable, const juce::String& formatName,
                            const juce::String& fileOrIdentifier, int timeoutMs,
                            const std::atomic<bool>* cancel = nullptr);

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

        /** Lists AU and VST3 plugins in the standard locations, and VST3s in the extra folders,
            without loading any of them. */
        std::vector<PluginCandidate> findCandidates() const;

        /** Folders searched for VST3 plugins besides the system ones (a setting later; tests now). */
        void setExtraFolders (const juce::Array<juce::File>& folders)     { extraFolders = folders; }

        bool isBlocked (const juce::String& fileOrIdentifier) const;
        void block (const juce::String& fileOrIdentifier, const juce::String& reason, const juce::String& kind = "crashed");
        juce::String getBlockKind (const juce::String& fileOrIdentifier) const      { return blockedKind[fileOrIdentifier]; }
        void unblock (const juce::String& fileOrIdentifier);
        juce::String getBlockReason (const juce::String& fileOrIdentifier) const   { return blocked[fileOrIdentifier]; }
        juce::StringArray getBlocked() const                                         { return blocked.getAllKeys(); }

        /** True when this candidate has never been confirmed, or its file changed since. Blocked
            plugins count as checked: only a full rescan tries them again. */
        bool needsScan (const PluginCandidate&) const;

        /** Every confirmed plugin, in name order. */
        juce::Array<juce::PluginDescription> getScanned() const;

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
        juce::StringPairArray blocked;       // fileOrIdentifier -> reason
        juce::StringPairArray blockedKind;   // fileOrIdentifier -> kind (Outcome::blockKind)
        juce::Array<juce::File> extraFolders;

        void load();
        void save() const;
    };

    /** Checks plugins in the background: every candidate that needs it (or, for a rescan, all of
        them), a few carthage-scan processes at a time, so a crashing or hanging plugin only costs
        its own scanner. Results reach the catalogue on the message thread; a crash, a hang or a
        plugin with nothing usable is blocked, and a later success unblocks it.

        start(), cancel(), getProgress() and onChange: message thread. */
    class PluginScanner final
    {
    public:
        struct Progress
        {
            bool running = false;
            int total = 0, done = 0, found = 0, blocked = 0;
            juce::StringArray current;      ///< names being checked right now
            double seconds = 0.0;           ///< since start (or the whole run, once finished)
            juce::StringArray newlyBlocked; ///< names blocked during this run
        };

        PluginScanner (PluginCatalogue&, juce::File scannerExecutable);
        ~PluginScanner();

        /** Checks what needs checking, or everything when `everything` is true. Ignored while running. */
        void start (bool everything);
        void cancel();
        Progress getProgress() const;

        /** After each plugin and at the end. */
        std::function<void()> onChange;

        int maxParallel = 4;
        int timeoutMs = 60000;

    private:
        struct State;
        PluginCatalogue& catalogue;
        juce::File scanner;
        std::shared_ptr<State> state;
    };
}
