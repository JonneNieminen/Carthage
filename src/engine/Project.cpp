#include "Project.h"
#include "Editing.h"

namespace carthage::project
{
    using namespace tracktion;

    namespace
    {
        const juce::Identifier carthageTag ("CARTHAGE"), viewTag ("VIEW"),
                               formatVersionProp ("formatVersion"), idProp ("id");

        juce::String newId()   { return juce::Uuid().toDashedString(); }
    }

    Info Info::createNew()
    {
        Info info;
        info.id = newId();
        return info;
    }

    juce::File fileIn (const juce::File& folder)        { return folder.getChildFile (folder.getFileName() + extension); }
    juce::File recordingsIn (const juce::File& folder)  { return folder.getChildFile ("Recordings"); }

    std::unique_ptr<Edit> createEdit (Engine& engine, const juce::File& projectFile)
    {
        auto edit = createEmptyEdit (engine, projectFile);
        editing::useConsistentFilePaths (*edit);
        return edit;
    }

    void relink (juce::ValueTree editState, const std::function<juce::File (const juce::String&)>& resolve,
                 const juce::File& destination, const juce::File& movingFrom)
    {
        const auto folder = destination.getParentDirectory();

        const auto visit = [&] (auto& self, juce::ValueTree tree) -> void
        {
            if (tree.hasType (IDs::AUDIOCLIP) && tree.hasProperty (IDs::source))
            {
                const auto path = tree[IDs::source].toString();

                if (auto file = resolve (path); file != juce::File() && file.existsAsFile())
                {
                    if (movingFrom != juce::File() && file.isAChildOf (movingFrom))
                        file = folder.getChildFile (file.getRelativePathFrom (movingFrom));

                    // W32: relative to the project file taken as a folder, as Tracktion writes it.
                    const auto written = file.isAChildOf (folder) ? file.getRelativePathFrom (destination)
                                                                  : file.getFullPathName();

                    if (written != path)
                        tree.setProperty (IDs::source, written, nullptr);
                }
            }

            for (auto child : tree)
                self (self, child);
        };

        visit (visit, editState);
    }

    juce::ValueTree snapshot (Edit& edit, const juce::File& destination, const Info& info, const juce::File& movingFrom)
    {
        edit.flushState();
        auto state = edit.state.createCopy();

        if (auto old = state.getChildWithName (carthageTag); old.isValid())
            state.removeChild (old, nullptr);

        relink (state, [&edit] (const juce::String& p) { return edit.filePathResolver (p); }, destination, movingFrom);

        juce::ValueTree carthage (carthageTag);
        carthage.setProperty (formatVersionProp, formatVersion, nullptr);
        carthage.setProperty (idProp, info.id, nullptr);

        if (info.view.isValid())
            carthage.appendChild (info.view.createCopy(), nullptr);

        state.appendChild (carthage, nullptr);
        return state;
    }

    juce::Result write (const juce::ValueTree& snapshot, const juce::File& file)
    {
        if (! file.getParentDirectory().createDirectory())
            return juce::Result::fail ("Carthage could not create the folder " + file.getParentDirectory().getFullPathName());

        const auto xml = snapshot.createXml();

        if (xml == nullptr)
            return juce::Result::fail ("The project could not be turned into a file");

        // TemporaryFile writes beside the target and renames it over: a crash mid-write leaves the
        // old file whole.
        juce::TemporaryFile temp (file);

        if (! xml->writeTo (temp.getFile()))
            return juce::Result::fail ("Carthage could not write " + file.getFullPathName() + ". Is the disk full?");

        if (! temp.overwriteTargetFileWithTemporary())
            return juce::Result::fail ("Carthage could not replace " + file.getFullPathName());

        return juce::Result::ok();
    }

    Opened open (Engine& engine, const juce::File& stateFile, const juce::File& projectFile)
    {
        Opened result;

        if (! stateFile.existsAsFile())
        {
            result.error = stateFile.getFullPathName() + " is not there any more";
            return result;
        }

        auto state = tracktion::loadEditFromFile (engine, stateFile, ProjectItemID{});

        if (! state.isValid() || ! state.hasType (IDs::EDIT))
        {
            result.error = stateFile.getFileName() + " is not a Carthage project, or it is damaged";
            return result;
        }

        if (auto carthage = state.getChildWithName (carthageTag); carthage.isValid())
        {
            result.info.formatVersion = carthage.getProperty (formatVersionProp, 1);
            result.info.id = carthage[idProp].toString();
            result.info.view = carthage.getChildWithName (viewTag).createCopy();
            state.removeChild (carthage, nullptr);
        }

        if (result.info.formatVersion > formatVersion)
        {
            result.error = stateFile.getFileName() + " was saved by a newer Carthage. Update Carthage to open it.";
            return result;
        }

        if (result.info.id.isEmpty())
            result.info.id = newId();

        // Older formats are upgraded here as they appear; version 1 is the first.

        auto id = ProjectItemID::createNewID (ProjectID{});
        state.setProperty (IDs::projectID, id.toString(), nullptr);

        Edit::Options options { engine, state, id };
        options.editFileRetriever = [projectFile] { return projectFile; };
        options.filePathResolver = editing::consistentFilePaths ([projectFile] { return projectFile; });   // while it loads
        options.numAudioTracks = 0;
        result.edit = Edit::createEdit (std::move (options));

        if (result.edit == nullptr)
        {
            result.error = "The engine could not open " + stateFile.getFileName();
            return result;
        }

        editing::useConsistentFilePaths (*result.edit);
        return result;
    }

    void moveTo (Edit& edit, const juce::File& newFile, const juce::File& oldFolder)
    {
        // Resolve with the old location, then point the edit at the new one before rewriting, so
        // each clip re-resolves against the new file. A path that reads the same from both (the
        // recordings, relative) is not rewritten, so every clip is told to reload.
        const auto oldFile = edit.editFileRetriever ? edit.editFileRetriever() : juce::File();
        const auto resolveOld = editing::consistentFilePaths ([oldFile] { return oldFile; });

        edit.editFileRetriever = [newFile] { return newFile; };
        relink (edit.state, resolveOld, newFile, oldFolder);

        for (auto* track : getClipTracks (edit))
            for (auto* clip : track->getClips())
                if (dynamic_cast<AudioClipBase*> (clip) != nullptr)
                    clip->sourceMediaChanged();
    }

    juce::File samplesIn (const juce::File& folder)   { return folder.getChildFile ("Samples"); }

    juce::File collectTarget (const juce::File& samplesFolder, const juce::File& source, const juce::Array<juce::File>& taken)
    {
        auto target = samplesFolder.getChildFile (source.getFileName());

        for (int n = 2;; ++n)
        {
            const bool sameFileThere = target.existsAsFile() && target.getSize() == source.getSize();

            if (! taken.contains (target) && (! target.existsAsFile() || sameFileThere))
                return target;

            target = samplesFolder.getChildFile (source.getFileNameWithoutExtension() + " " + juce::String (n) + source.getFileExtension());
        }
    }

    namespace
    {
        juce::Result copyFolder (const juce::File& from, const juce::File& to)
        {
            if (! from.isDirectory())
                return juce::Result::ok();

            if (! to.createDirectory())
                return juce::Result::fail ("Carthage could not create " + to.getFullPathName());

            for (const auto& entry : juce::RangedDirectoryIterator (from, true, "*", juce::File::findFiles))
            {
                const auto target = to.getChildFile (entry.getFile().getRelativePathFrom (from));

                if (target.existsAsFile() && target.getSize() == entry.getFileSize())
                    continue;

                target.getParentDirectory().createDirectory();

                if (! entry.getFile().copyFileTo (target))
                    return juce::Result::fail ("Carthage could not copy " + entry.getFile().getFileName() + " into the project");
            }

            return juce::Result::ok();
        }
    }

    juce::Result copyMedia (const juce::File& fromFolder, const juce::File& toFolder)
    {
        if (fromFolder == toFolder)
            return juce::Result::ok();

        if (auto r = copyFolder (recordingsIn (fromFolder), recordingsIn (toFolder)); r.failed())
            return r;

        return copyFolder (samplesIn (fromFolder), samplesIn (toFolder));
    }

    //==============================================================================================
    Writer::Writer() : pool (juce::ThreadPoolOptions{}.withThreadName ("Carthage writer").withNumberOfThreads (1)) {}

    Writer::~Writer()
    {
        *alive = false;
        waitUntilIdle();
    }

    void Writer::post (std::function<juce::Result()> work, std::function<void (juce::Result)> done)
    {
        pool.addJob ([work = std::move (work), done = std::move (done), alive = alive]
        {
            const auto result = work();

            if (done)
                juce::MessageManager::callAsync ([done, result, alive]
                {
                    if (*alive)
                        done (result);
                });
        });
    }

    void Writer::waitUntilIdle()
    {
        while (pool.getNumJobs() > 0)
            juce::Thread::sleep (2);
    }

    bool Writer::isBusy() const   { return pool.getNumJobs() > 0; }
}
