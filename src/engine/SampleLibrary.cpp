#include "SampleLibrary.h"
#include "PluginLibrary.h"   // fuzzyScore
#include "SamplePreview.h"   // extensions

#include <algorithm>

namespace carthage::engine
{
    namespace
    {
        struct Index
        {
            std::vector<SampleLibrary::Folder> folders;
            std::vector<SampleLibrary::Sample> samples;
        };

        /** Walks one folder depth-first, children by name, so the tree reads like the disk. */
        void walk (Index& index, const juce::File& root, const juce::File& folder, int parent, int depth,
                   const juce::StringArray& extensions, juce::Thread* thread)
        {
            const auto self = static_cast<int> (index.folders.size());
            index.folders.push_back ({ folder, folder.getFileName(), parent, depth, {}, {} });

            if (parent >= 0)
                index.folders[static_cast<size_t> (parent)].folders.push_back (self);

            auto children = folder.findChildFiles (juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles, false);
            std::sort (children.begin(), children.end(), [] (const juce::File& a, const juce::File& b)
            {
                return a.getFileName().compareNatural (b.getFileName()) < 0;
            });

            for (const auto& child : children)
            {
                if (thread != nullptr && thread->threadShouldExit())
                    return;

                // Dot files are hidden on every system here, Windows too: macOS leaves "._Kick.wav"
                // companions on some drives, and they are not sounds.
                if (child.getFileName().startsWithChar ('.'))
                    continue;

                if (child.isDirectory())
                {
                    walk (index, root, child, self, depth + 1, extensions, thread);
                }
                else if (extensions.contains (child.getFileExtension().toLowerCase()))
                {
                    index.folders[static_cast<size_t> (self)].samples.push_back (static_cast<int> (index.samples.size()));
                    index.samples.push_back ({ child, child.getFileNameWithoutExtension(),
                                               folder.getRelativePathFrom (root.getParentDirectory()).replaceCharacter ('\\', '/'), self });
                }
            }
        }
    }

    SampleLibrary::SampleLibrary (juce::File settingsFolder)
        : juce::Thread ("Carthage sample index"), storeFile (settingsFolder.getChildFile ("Samples.xml"))
    {
        load();
        rescan();
    }

    SampleLibrary::~SampleLibrary()
    {
        *alive = false;
        stopThread (4000);
    }

    void SampleLibrary::addRoot (const juce::File& folder)
    {
        if (! folder.isDirectory() || roots.contains (folder))
            return;

        roots.add (folder);
        save();
        rescan();
    }

    void SampleLibrary::removeRoot (const juce::File& folder)
    {
        roots.removeFirstMatchingValue (folder);
        save();
        rescan();
    }

    void SampleLibrary::rescan()
    {
        stopThread (4000);   // a scan in hand gives way; its result is dropped (scanNumber)
        rootsToIndex = roots;
        ++scanNumber;
        indexing.store (true);
        startThread (juce::Thread::Priority::low);
    }

    void SampleLibrary::run()
    {
        const auto toIndex = rootsToIndex;   // set by rescan() before the thread started
        const auto number = scanNumber;
        auto index = std::make_shared<Index>();
        build (toIndex, index->folders, index->samples, this);

        if (threadShouldExit())
            return;

        juce::MessageManager::callAsync ([this, index, number, token = std::weak_ptr<bool> (alive)]
        {
            if (token.expired() || number != scanNumber)
                return;   // gone, or a newer scan is on its way

            folders = std::move (index->folders);
            samples = std::move (index->samples);
            indexing.store (false);
            changed();
        });
    }

    void SampleLibrary::build (const juce::Array<juce::File>& toIndex, std::vector<Folder>& outFolders,
                               std::vector<Sample>& outSamples, juce::Thread* thread)
    {
        Index index;
        const auto extensions = SamplePreview::extensions();

        for (const auto& root : toIndex)
            if (root.isDirectory())
                walk (index, root, root, -1, 0, extensions, thread);

        outFolders = std::move (index.folders);
        outSamples = std::move (index.samples);
    }

    void SampleLibrary::indexNow()
    {
        stopThread (4000);
        ++scanNumber;   // anything still on its way is dropped
        build (roots, folders, samples, nullptr);
        indexing.store (false);
        changed();
    }

    std::vector<size_t> SampleLibrary::find (const juce::String& query, bool favouritesOnly) const
    {
        const auto words = juce::StringArray::fromTokens (query.trim(), " ", {});
        std::vector<std::pair<int, size_t>> scored;

        for (size_t i = 0; i < samples.size(); ++i)
        {
            const auto& s = samples[i];

            if (favouritesOnly && ! favouritePaths.contains (s.file.getFullPathName()))
                continue;

            int total = 0;
            bool all = true;

            for (const auto& w : words)
            {
                // A word in the folder counts, at half the weight of one in the name.
                const auto inName = fuzzyScore (w, s.name);
                const auto inFolder = fuzzyScore (w, s.where);
                const auto score = std::max (inName, inFolder >= 0 ? inFolder / 2 : -1);

                if (score < 0)
                {
                    all = false;
                    break;
                }

                total += score;
            }

            if (all)
                scored.emplace_back (total, i);
        }

        std::stable_sort (scored.begin(), scored.end(), [this, hasQuery = ! words.isEmpty()] (const auto& a, const auto& b)
        {
            if (hasQuery && a.first != b.first)
                return a.first > b.first;

            return samples[a.second].name.compareNatural (samples[b.second].name) < 0;
        });

        std::vector<size_t> result;
        result.reserve (scored.size());

        for (const auto& s : scored)
            result.push_back (s.second);

        return result;
    }

    bool SampleLibrary::isFavourite (const juce::File& f) const
    {
        return favouritePaths.contains (f.getFullPathName());
    }

    void SampleLibrary::setFavourite (const juce::File& f, bool on)
    {
        if (on == isFavourite (f))
            return;

        if (on)
            favouritePaths.add (f.getFullPathName());
        else
            favouritePaths.removeString (f.getFullPathName());

        save();
        changed();
    }

    std::vector<size_t> SampleLibrary::favourites() const
    {
        return find ({}, true);
    }

    std::optional<SampleLibrary::Analysis> SampleLibrary::getAnalysis (const juce::File& f) const
    {
        const auto found = analyses.find (f.getFullPathName());

        if (found == analyses.end() || found->second.size != f.getSize()
             || found->second.modified != f.getLastModificationTime().toMilliseconds())
            return std::nullopt;

        return found->second.analysis;
    }

    void SampleLibrary::setAnalysis (const juce::File& f, const Analysis& a)
    {
        analyses[f.getFullPathName()] = { f.getSize(), f.getLastModificationTime().toMilliseconds(), a };
        save();
        changed();
    }

    void SampleLibrary::changed()
    {
        if (onChange)
            onChange();
    }

    void SampleLibrary::load()
    {
        const auto xml = juce::parseXML (storeFile);

        if (xml == nullptr || ! xml->hasTagName ("SAMPLES") || xml->getIntAttribute ("version") > fileVersion)
            return;

        for (auto* e : xml->getChildWithTagNameIterator ("ROOT"))
            roots.addIfNotAlreadyThere (juce::File (e->getStringAttribute ("path")));

        for (auto* e : xml->getChildWithTagNameIterator ("FAVOURITE"))
            favouritePaths.addIfNotAlreadyThere (e->getStringAttribute ("path"));

        for (auto* e : xml->getChildWithTagNameIterator ("ANALYSIS"))
        {
            Stored s;
            s.size = e->getStringAttribute ("size").getLargeIntValue();
            s.modified = e->getStringAttribute ("modified").getLargeIntValue();
            s.analysis.key = e->getStringAttribute ("key");
            s.analysis.seconds = e->getDoubleAttribute ("seconds");

            if (e->hasAttribute ("bpm"))
                s.analysis.bpm = e->getDoubleAttribute ("bpm");

            analyses[e->getStringAttribute ("path")] = s;
        }
    }

    void SampleLibrary::save() const
    {
        juce::XmlElement xml ("SAMPLES");
        xml.setAttribute ("version", fileVersion);

        for (const auto& r : roots)
            xml.createNewChildElement ("ROOT")->setAttribute ("path", r.getFullPathName());

        for (const auto& f : favouritePaths)
            xml.createNewChildElement ("FAVOURITE")->setAttribute ("path", f);

        for (const auto& [path, s] : analyses)
        {
            auto* e = xml.createNewChildElement ("ANALYSIS");
            e->setAttribute ("path", path);
            e->setAttribute ("size", juce::String (s.size));
            e->setAttribute ("modified", juce::String (s.modified));
            e->setAttribute ("seconds", s.analysis.seconds);

            if (s.analysis.key.isNotEmpty())
                e->setAttribute ("key", s.analysis.key);

            if (s.analysis.bpm)
                e->setAttribute ("bpm", *s.analysis.bpm);
        }

        storeFile.getParentDirectory().createDirectory();
        xml.writeTo (storeFile);
    }
}
