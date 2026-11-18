#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace carthage::engine
{
    /** The sample browser's data (F19, W30). It starts empty: the user adds folders, and every
        sound file under them (WAV, AIFF, FLAC, MP3, OGG; any depth) is listed, by folder and by
        search. Favourites and each file's analysis (key and tempo, filled in by the analyser)
        are kept in `Samples.xml` in the settings folder, versioned.

        Folders are read by an indexer thread; the finished index is handed to the message thread
        in one piece, so the browser never sees a half-built list and never waits on the disk.
        Everything here except the indexer: message thread. Tested in tests/SampleLibraryTests.cpp. */
    class SampleLibrary final : private juce::Thread
    {
    public:
        static constexpr int fileVersion = 1;

        struct Folder
        {
            juce::File file;
            juce::String name;
            int parent = -1, depth = 0;
            std::vector<int> folders;      ///< indices into getFolders(), by name
            std::vector<int> samples;      ///< indices into getSamples(), by name
        };

        struct Sample
        {
            juce::File file;
            juce::String name;             ///< file name without extension
            juce::String where;            ///< folder path under its root, for search ("Drums/Kicks")
            int folder = -1;
        };

        /** What the analyser found. Absent values were not found (silence, too short, no beat). */
        struct Analysis
        {
            std::optional<double> bpm;
            juce::String key;              ///< "A minor", "F# major"; empty when not found
            double seconds = 0.0;
        };

        explicit SampleLibrary (juce::File settingsFolder);
        ~SampleLibrary() override;

        //==========================================================================================
        // Folders
        const juce::Array<juce::File>& getRoots() const noexcept    { return roots; }
        void addRoot (const juce::File&);
        void removeRoot (const juce::File&);

        /** Reads the folders again in the background; onChange follows when done. */
        void rescan();
        bool isIndexing() const noexcept     { return indexing.load(); }

        /** Reads the folders on this thread and returns when done (tests and tools). */
        void indexNow();

        /** The index, depth first: each root (parent -1, in getRoots() order) followed by the
            folders under it. */
        const std::vector<Folder>& getFolders() const noexcept      { return folders; }
        const std::vector<Sample>& getSamples() const noexcept      { return samples; }

        /** After a new index, a favourite or an analysis. */
        std::function<void()> onChange;

        //==========================================================================================
        // Finding
        /** Indices into getSamples(): every word of `query` must match the name or folder
            (fuzzily, as the plugin search does); best match first. */
        std::vector<size_t> find (const juce::String& query, bool favouritesOnly) const;

        bool isFavourite (const juce::File&) const;
        void setFavourite (const juce::File&, bool);
        std::vector<size_t> favourites() const;

        //==========================================================================================
        // Analysis, kept while the file is unchanged (same size and date)
        std::optional<Analysis> getAnalysis (const juce::File&) const;
        void setAnalysis (const juce::File&, const Analysis&);

    private:
        juce::File storeFile;
        juce::Array<juce::File> roots;
        std::vector<Folder> folders;
        std::vector<Sample> samples;
        juce::StringArray favouritePaths;

        struct Stored { juce::int64 size = 0, modified = 0; Analysis analysis; };
        std::map<juce::String, Stored> analyses;

        std::atomic<bool> indexing { false };
        static void build (const juce::Array<juce::File>& roots, std::vector<Folder>&, std::vector<Sample>&, juce::Thread*);
        juce::Array<juce::File> rootsToIndex;   // the indexer's own copy, set before it starts
        int scanNumber = 0;
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);

        void load();
        void save() const;
        void changed();
        void run() override;
    };
}
