#include "Autosave.h"

namespace carthage::autosave
{
    namespace
    {
        constexpr const char* markerName = "running";
    }

    juce::File Store::folderFor (const juce::String& projectId) const
    {
        return root.getChildFile (juce::File::createLegalFileName (projectId));
    }

    juce::File Store::nextFile (const juce::String& projectId) const
    {
        const auto folder = folderFor (projectId);
        folder.createDirectory();
        return folder.getNonexistentChildFile (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S"), ".carthage", false);
    }

    juce::Array<juce::File> Store::autosavesOf (const juce::String& projectId) const
    {
        auto files = folderFor (projectId).findChildFiles (juce::File::findFiles, false, "*.carthage");

        // Names sort by time; the modification time breaks ties within one second.
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
        {
            const auto ta = a.getLastModificationTime(), tb = b.getLastModificationTime();
            return ta != tb ? ta > tb : a.getFileName() > b.getFileName();
        });

        return files;
    }

    void Store::prune (const juce::String& projectId, int keep) const
    {
        const auto files = autosavesOf (projectId);

        for (int i = std::max (0, keep); i < files.size(); ++i)
            files[i].deleteFile();
    }

    void Store::markOpen (const juce::String& projectId, const juce::File& projectFile) const
    {
        const auto folder = folderFor (projectId);
        folder.createDirectory();
        folder.getChildFile (markerName).replaceWithText (projectFile.getFullPathName());
    }

    void Store::markClosed (const juce::String& projectId) const
    {
        folderFor (projectId).getChildFile (markerName).deleteFile();
    }

    std::vector<Store::Recovery> Store::findUnclosed() const
    {
        std::vector<Recovery> found;

        for (const auto& entry : juce::RangedDirectoryIterator (root, false, "*", juce::File::findDirectories))
        {
            const auto marker = entry.getFile().getChildFile (markerName);

            if (! marker.existsAsFile())
                continue;

            const auto id = entry.getFile().getFileName();
            const auto projectFile = juce::File (marker.loadFileAsString().trim());
            const auto saves = autosavesOf (id);

            if (saves.isEmpty()
                 || (projectFile.existsAsFile() && projectFile.getLastModificationTime() >= saves[0].getLastModificationTime()))
            {
                marker.deleteFile();   // nothing newer than what is on disk
                continue;
            }

            found.push_back ({ id, projectFile, saves[0], saves[0].getLastModificationTime() });
        }

        std::sort (found.begin(), found.end(), [] (const Recovery& a, const Recovery& b) { return a.savedAt > b.savedAt; });
        return found;
    }
}
