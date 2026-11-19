#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <functional>

namespace carthage::project
{
    /** Project files (docs/ARCHITECTURE.md, "Phase 4a"). A project is a folder holding
        `<Name>.carthage` and `Recordings/`. The file is the edit's XML with a `CARTHAGE` child that
        holds the format version, the project's id and the view (zoom, grid), which the UI owns and
        the engine only carries. Tested headless in tests/ProjectTests.cpp. */

    /** The newest format this Carthage writes. Every older one opens (AGENTS.md). */
    constexpr int formatVersion = 1;

    inline const juce::String extension { ".carthage" };

    /** What the `CARTHAGE` child holds. */
    struct Info
    {
        juce::String id;          ///< Stable for the project's life: names its autosave folder
        int formatVersion = project::formatVersion;
        juce::ValueTree view;     ///< The UI's own state; may be invalid

        static Info createNew();
    };

    /** `<folder>/<folder name>.carthage`. */
    juce::File fileIn (const juce::File& folder);

    /** The folder takes are recorded into. */
    juce::File recordingsIn (const juce::File& folder);

    /** Project versions (W38): each save first keeps the file it replaces in `<folder>/Versions`,
        named by when it was replaced; the newest `versionsKept` stay. A version keeps its paths
        relative to the project file, so it is opened as if it were there (open's `editFile`). */
    constexpr int versionsKept = 20;
    juce::File versionsIn (const juce::File& folder);
    void keepVersion (const juce::File& projectFile, const juce::String& note = {});
    juce::Array<juce::File> versionsOf (const juce::File& projectFile);   // newest first

    /** The folder samples are collected into (W30, "Collect samples into project"). */
    juce::File samplesIn (const juce::File& folder);

    /** Where `source` goes in `samplesFolder`: its own name, numbered if that name is taken by a
        different file on disk or by another target in `taken`. The same file already collected
        (same name and size) keeps its name. */
    juce::File collectTarget (const juce::File& samplesFolder, const juce::File& source, const juce::Array<juce::File>& taken);

    /** A new, empty edit whose file is `projectFile`, with Carthage's path rule (W32). */
    std::unique_ptr<tracktion::Edit> createEdit (tracktion::Engine&, const juce::File& projectFile);

    /** A copy of the edit, ready to write as `destination`: clip paths are rewritten so they
        resolve from there (see relink), and the `CARTHAGE` child is added. `movingFrom` is the
        project folder the edit lives in now when it is being saved into another one; files inside
        it are taken to be copied along. Message thread; quick (no disk access but a stat per clip). */
    juce::ValueTree snapshot (tracktion::Edit&, const juce::File& destination, const Info&,
                              const juce::File& movingFrom = {});

    /** Writes a snapshot. Writes a temporary file beside `file` and renames it over, so a crash or
        a full disk never leaves a half-written project. Any thread. */
    juce::Result write (const juce::ValueTree& snapshot, const juce::File& file);

    struct Opened
    {
        std::unique_ptr<tracktion::Edit> edit;
        Info info;
        juce::String error;   ///< Set when edit is null
    };

    /** Reads `stateFile` into a new edit that lives at `projectFile` (the same file, or for a
        recovered autosave, the project it belongs to). Message thread. */
    Opened open (tracktion::Engine&, const juce::File& stateFile, const juce::File& projectFile);

    /** Rewrites every audio clip's file path so it resolves from `destination`: a file inside the
        project folder is written relative to the project file the way Tracktion writes it (W32);
        anything else is written as an absolute path, so moving the project never breaks it. Files
        inside `movingFrom` are mapped to the same place inside the destination's folder. Paths that
        resolve to no file are left as they are. */
    void relink (juce::ValueTree editState, const std::function<juce::File (const juce::String&)>& resolve,
                 const juce::File& destination, const juce::File& movingFrom = {});

    /** After Save As: the open edit now lives at `newFile`. Its clips are relinked from where they
        resolved before (files inside `oldFolder` map to the new folder, see relink) and reload
        their audio from there. Message thread. */
    void moveTo (tracktion::Edit&, const juce::File& newFile, const juce::File& oldFolder);

    /** A project's arrangement in miniature, for the start screen (F20): each lane's clips as
        start and length in seconds, read from the file's XML without building an edit. Any
        thread; quick (a few milliseconds for hundreds of clips). */
    struct Miniature
    {
        double seconds = 0.0;   ///< where the last clip ends
        std::vector<std::vector<std::pair<float, float>>> lanes;
    };

    std::optional<Miniature> miniatureOf (const juce::File& projectFile);

    /** Copies `from`'s Recordings and Samples folders into `to` (Save as). Any thread. */
    juce::Result copyMedia (const juce::File& fromFolder, const juce::File& toFolder);

    /** Saves and copies off the message thread, one job at a time, and reports back on the
        message thread. Destroying it waits for the job in hand. */
    class Writer final
    {
    public:
        Writer();
        ~Writer();

        /** `work` runs on the writer's thread; `done` gets its result on the message thread
            (and is dropped if the Writer has gone). */
        void post (std::function<juce::Result()> work, std::function<void (juce::Result)> done = {});

        /** Blocks until everything posted so far is written (quitting). */
        void waitUntilIdle();

        bool isBusy() const;

    private:
        juce::ThreadPool pool;
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    };
}
