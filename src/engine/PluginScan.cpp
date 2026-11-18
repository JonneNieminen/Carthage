#include "PluginScan.h"

#include <mutex>
#include <thread>

namespace carthage::engine
{
    namespace scan
    {
        juce::String Outcome::blockKind() const
        {
            switch (kind)
            {
                case Kind::incompatible: return "incompatible";
                case Kind::timedOut:     return "hung";
                case Kind::nothingFound: return "unusable";
                default:                 return "crashed";
            }
        }

        juce::StringArray machOArchitectures (const juce::File& binary)
        {
            juce::StringArray archs;
            juce::FileInputStream in (binary);

            if (! in.openedOk())
                return archs;

            const auto name = [] (juce::uint32 cpu) -> juce::String
            {
                switch (cpu)
                {
                    case 0x0100000c: return "arm64";
                    case 0x01000007: return "x86_64";
                    case 0x00000007: return "i386";
                    default:         return "cpu-" + juce::String::toHexString (static_cast<int> (cpu));
                }
            };

            const auto magic = static_cast<juce::uint32> (in.readIntBigEndian());

            if (magic == 0xcafebabe || magic == 0xcafebabf)   // universal binary, big-endian header
            {
                const auto count = static_cast<juce::uint32> (in.readIntBigEndian());
                const auto entrySize = magic == 0xcafebabe ? 20 : 32;

                for (juce::uint32 i = 0; i < std::min<juce::uint32> (count, 16); ++i)
                {
                    in.setPosition (8 + static_cast<juce::int64> (i) * entrySize);
                    archs.add (name (static_cast<juce::uint32> (in.readIntBigEndian())));
                }
            }
            else if (magic == 0xcffaedfe || magic == 0xcefaedfe)   // thin, little-endian
            {
                archs.add (name (static_cast<juce::uint32> (in.readInt())));
            }

            return archs;
        }

        juce::String hostArchitecture()
        {
           #if defined (__aarch64__) || defined (_M_ARM64)
            return "arm64";
           #else
            return "x86_64";
           #endif
        }

        juce::StringArray vst3Architectures (const juce::File& bundle)
        {
            const auto macos = bundle.getChildFile ("Contents").getChildFile ("MacOS");
            const auto binaries = macos.findChildFiles (juce::File::findFiles, false);
            return binaries.isEmpty() ? juce::StringArray() : machOArchitectures (binaries.getFirst());
        }

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
                            const juce::String& fileOrIdentifier, int timeoutMs, const std::atomic<bool>* cancel)
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
                else if (cancel != nullptr && cancel->load())
                {
                    process.kill();
                    outcome.detail = "cancelled";
                    return outcome;
                }
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
                outcome.detail = formatName == "AudioUnit"
                    ? "It loaded nothing Carthage can use. It may be built for Intel Macs only; check for a newer version"
                    : "It loaded nothing Carthage can use";
            }
            else if (exitCode == exitIncompatible)
            {
                outcome.kind = Outcome::Kind::incompatible;
                outcome.detail = text.trim().isNotEmpty() ? text.trim() : juce::String ("It is not built for this computer");
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

    bool PluginCatalogue::needsScan (const PluginCandidate& c) const
    {
        if (isBlocked (c.fileOrIdentifier))
            return false;

        const auto scanned = findScanned (c.fileOrIdentifier);

        if (scanned.isEmpty())
            return true;

        // A changed file (an update installed over it) is checked again before it is loaded.
        for (auto* format : formats.getFormats())
            if (format->getName() == c.formatName)
                return format->pluginNeedsRescanning (scanned.getReference (0));

        return true;
    }

    juce::Array<juce::PluginDescription> PluginCatalogue::getScanned() const
    {
        auto list = known.getTypes();
        std::sort (list.begin(), list.end(), [] (const auto& a, const auto& b) { return a.name.compareNatural (b.name) < 0; });
        return list;
    }

    void PluginCatalogue::unblock (const juce::String& id)
    {
        if (blocked.containsKey (id))
        {
            blocked.remove (juce::StringRef (id));
            blockedKind.remove (juce::StringRef (id));
            save();
        }
    }

    bool PluginCatalogue::isBlocked (const juce::String& id) const
    {
        return blocked.containsKey (id);
    }

    void PluginCatalogue::block (const juce::String& id, const juce::String& reason, const juce::String& kind)
    {
        blocked.set (id, reason);
        blockedKind.set (id, kind);
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
            {
                blocked.set (e->getStringAttribute ("id"), e->getStringAttribute ("reason"));
                blockedKind.set (e->getStringAttribute ("id"), e->getStringAttribute ("kind", "crashed"));
            }
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
                e->setAttribute ("kind", blockedKind[key]);
            }

            root.writeTo (getBlocklistFile());
        }
    }

    //==============================================================================================
    struct PluginScanner::State
    {
        mutable std::mutex lock;
        Progress progress;
        juce::uint32 startedMs = 0;
        std::atomic<bool> cancelled { false }, destroyed { false };
        std::thread manager;
    };

    PluginScanner::PluginScanner (PluginCatalogue& c, juce::File exe)
        : catalogue (c), scanner (std::move (exe)), state (std::make_shared<State>())
    {
    }

    PluginScanner::~PluginScanner()
    {
        state->cancelled = true;
        state->destroyed = true;   // results still on their way to the message thread are dropped

        if (state->manager.joinable())
            state->manager.join();
    }

    PluginScanner::Progress PluginScanner::getProgress() const
    {
        const std::lock_guard l (state->lock);
        auto p = state->progress;

        if (p.running)
            p.seconds = (juce::Time::getMillisecondCounter() - state->startedMs) / 1000.0;

        return p;
    }

    void PluginScanner::cancel()
    {
        state->cancelled = true;
    }

    void PluginScanner::start (bool everything)
    {
        if (getProgress().running)
            return;

        if (state->manager.joinable())
            state->manager.join();   // the previous run has finished

        std::vector<PluginCandidate> todo;

        for (auto& c : catalogue.findCandidates())
            if (everything || catalogue.needsScan (c))
                todo.push_back (std::move (c));

        {
            const std::lock_guard l (state->lock);
            state->progress = {};
            state->progress.running = true;
            state->progress.total = static_cast<int> (todo.size());
            state->startedMs = juce::Time::getMillisecondCounter();
        }

        state->cancelled = false;

        auto s = state;
        auto* cat = &catalogue;
        const auto exe = scanner;
        const auto parallel = std::max (1, maxParallel);
        const auto timeout = timeoutMs;
        auto changed = [s, this] { if (! s->destroyed && onChange) onChange(); };

        state->manager = std::thread ([s, cat, exe, parallel, timeout, todo = std::move (todo), changed]
        {
            std::atomic<size_t> next { 0 };
            std::vector<std::thread> workers;

            for (int w = 0; w < parallel; ++w)
                workers.emplace_back ([&]
                {
                    for (;;)
                    {
                        const auto i = next++;

                        if (i >= todo.size() || s->cancelled)
                            return;

                        const auto& c = todo[i];
                        { const std::lock_guard l (s->lock); s->progress.current.add (c.name); }

                        auto outcome = scan::runScanner (exe, c.formatName, c.fileOrIdentifier, timeout, &s->cancelled);
                        const auto wasCancelled = outcome.detail == "cancelled";

                        {
                            const std::lock_guard l (s->lock);
                            s->progress.current.removeString (c.name);

                            if (! wasCancelled)
                            {
                                ++s->progress.done;

                                if (outcome.kind == scan::Outcome::Kind::found)   ++s->progress.found;
                                else if (outcome.shouldBlock())                     { ++s->progress.blocked; s->progress.newlyBlocked.add (c.name); }
                            }
                        }

                        if (wasCancelled)
                            return;

                        // The catalogue belongs to the message thread.
                        juce::MessageManager::callAsync ([s, cat, c, outcome = std::move (outcome), changed]
                        {
                            if (s->destroyed)
                                return;

                            if (outcome.kind == scan::Outcome::Kind::found)
                            {
                                cat->addScanned (outcome.descriptions);
                                cat->unblock (c.fileOrIdentifier);
                            }
                            else if (outcome.shouldBlock())
                            {
                                cat->block (c.fileOrIdentifier, outcome.detail, outcome.blockKind());
                            }

                            changed();
                        });
                    }
                });

            for (auto& w : workers)
                w.join();

            juce::MessageManager::callAsync ([s, changed]
            {
                {
                    const std::lock_guard l (s->lock);
                    s->progress.running = false;
                    s->progress.current.clear();
                    s->progress.seconds = (juce::Time::getMillisecondCounter() - s->startedMs) / 1000.0;
                }

                changed();
            });
        });
    }
}
