#include "Processes.h"

#include "PluginProxy.h"
#include "Protocol.h"

#include <engine/Log.h>

namespace carthage::engine::plughost
{
    using namespace tracktion;

    namespace
    {
        const juce::Identifier processChoice ("carthageProcess");   // on an ExternalPlugin's state
        const juce::Identifier historyTag ("PLUGINCRASHES"), pluginTag ("PLUGIN"), idAttr ("id"), crashesAttr ("crashes");

        constexpr int crashesBeforeStopping = 3;   // in one run of Carthage

        juce::String keyOf (const juce::PluginDescription& d)   { return d.createIdentifierString(); }
    }

    Processes::Processes (juce::File hostExecutable, juce::File crashHistory)
        : executable (std::move (hostExecutable)), historyFile (std::move (crashHistory))
    {
        loadHistory();
        startTimer (250);
    }

    Processes::~Processes()
    {
        alive->store (false);
        stopTimer();
    }

    bool Processes::canRunInHost (const juce::PluginDescription& d)
    {
        return d.pluginFormatName == "VST3" || d.pluginFormatName == "AudioUnit" || d.pluginFormatName == testFormat;
    }

    void Processes::install (Engine& engine)
    {
        auto& manager = engine.getPluginManager();
        original = manager.createPluginInstance;
        manager.createPluginInstance = [this] (const juce::PluginDescription& d, double rate, int blockSize, juce::String& error)
        {
            return create (d, rate, blockSize, error);
        };
    }

    int Processes::numHosts() const noexcept
    {
        return static_cast<int> (std::count_if (hosts.begin(), hosts.end(), [] (const auto& h) { return h.second != nullptr && h.second->isRunning(); }));
    }

    //==============================================================================================
    Processes::Choice Processes::choiceOf (const ExternalPlugin& p)
    {
        const auto c = p.state[processChoice].toString();
        return c == "own" ? Choice::ownProcess : c == "carthage" ? Choice::inCarthage : Choice::withMaker;
    }

    void Processes::setChoice (ExternalPlugin& p, Choice c)
    {
        // Where it runs, not part of the mix: no undo step.
        p.state.setProperty (processChoice, c == Choice::ownProcess ? "own" : c == Choice::inCarthage ? "carthage" : "maker", nullptr);
    }

    HostProcess& Processes::hostFor (const juce::PluginDescription& d, bool ownProcess)
    {
        const auto maker = d.manufacturerName.isNotEmpty() ? d.manufacturerName : juce::String ("(no maker)");
        const auto key = ownProcess ? "own: " + keyOf (d) : "maker: " + maker;
        auto& slot = hosts[key];

        if (slot == nullptr || ! slot->isRunning())
        {
            if (slot != nullptr)
                retired.push_back (std::move (slot));   // its plugins still refer to it until they move

            slot = std::make_unique<HostProcess> (executable);
            slot->onLost = [this, token = alive]
            {
                juce::MessageManager::callAsync ([this, token] { if (token->load()) checkHosts(); });
            };

            if (const auto started = slot->start(); started.failed())
                log::warning ("Plugin process (" + key + ") did not start: " + started.getErrorMessage());
            else
                slot->setWindows (windowsOnTop, carthageInFront);
        }

        return *slot;
    }

    std::unique_ptr<juce::AudioPluginInstance> Processes::create (const juce::PluginDescription& d, double rate, int blockSize,
                                                                  juce::String& error)
    {
        if (! enabled || ! canRunInHost (d))
            return original (d, rate, blockSize, error);

        // Starts where automatic grouping puts it; place() moves it if the person chose otherwise.
        auto& host = hostFor (d, crashesOf (d) > 0);
        auto proxy = PluginProxy::create (d, rate, blockSize, host.isRunning() ? &host : nullptr, original, error);

        if (proxy == nullptr)
            return {};

        if (! proxy->isInHost())
            log::warning (d.name + " runs inside Carthage: its process could not load it (" + error + ")");

        proxies.insert (proxy.get());
        proxy->onDestroyed = [this, raw = proxy.get(), token = alive] { if (token->load()) proxies.erase (raw); };
        return proxy;
    }

    HostProcess* Processes::targetFor (ExternalPlugin& ext, bool onLivePath)
    {
        if (! enabled || onLivePath)
            return nullptr;

        if (! automatic)
        {
            switch (choiceOf (ext))
            {
                case Choice::inCarthage:  return nullptr;
                case Choice::ownProcess:  return &hostFor (ext.desc, true);
                case Choice::withMaker:   break;
            }
        }

        return &hostFor (ext.desc, automatic && crashesOf (ext.desc) > 0);
    }

    void Processes::place (Edit& edit, const std::set<const AudioTrack*>& live)
    {
        for (auto* p : getAllPlugins (edit, false))
        {
            auto* ext = dynamic_cast<ExternalPlugin*> (p);
            auto* proxy = ext != nullptr ? dynamic_cast<PluginProxy*> (ext->getAudioPluginInstance()) : nullptr;

            if (proxy == nullptr || proxy->isStopped())
                continue;

            // Master and the Pre-Master hear everything, so they are on any live path.
            auto* track = dynamic_cast<const AudioTrack*> (ext->getOwnerTrack());
            const auto onLivePath = track != nullptr ? live.contains (track) : ! live.empty();
            auto* from = proxy->runningIn();
            const auto stays = [&] (HostProcess* to) { return to == from && (to == nullptr || to->isRunning()); };

            // Asked without starting a process: a plugin that stays needs none.
            if (from == nullptr && (! enabled || onLivePath))
                continue;

            auto* target = targetFor (*ext, onLivePath);

            if (stays (target))
                continue;

            // Its editor belongs to the plugin here: it moves once the editor is closed.
            if (from == nullptr && proxy->isEditorOpenHere())
                continue;

            juce::String error;
            const auto restarting = from != nullptr && ! from->isRunning();

            if (proxy->moveTo (target, error))
                log::info (ext->getName() + (restarting ? " started again" : target != nullptr ? " moved to its own process" : " moved into Carthage")
                           + (restarting && target == nullptr ? ", in Carthage" : juce::String()));
            else
                log::warning (ext->getName() + " could not move: " + error);
        }
    }

    //==============================================================================================
    void Processes::checkHosts()
    {
        std::vector<HostProcess*> gone;

        for (auto& [key, host] : hosts)
            if (host != nullptr && host->hasStarted() && ! host->isRunning() && ! dealtWith.contains (host.get()))
                gone.push_back (host.get());

        for (auto& host : retired)
            if (host->hasStarted() && ! host->isRunning() && ! dealtWith.contains (host.get()))
                gone.push_back (host.get());

        for (auto* host : gone)
        {
            dealtWith.insert (host);
            hostLost (host);
        }
    }

    void Processes::hostLost (HostProcess* dead)
    {
        Crash crash;
        std::vector<PluginProxy*> in, blamed;

        for (auto* proxy : proxies)
            if (proxy->runningIn() == dead)
                in.push_back (proxy);

        for (auto* proxy : in)
            if (proxy->crashedItsHost() || (dead->slotAskedWhenLost() > 0 && proxy->slotInHost() == dead->slotAskedWhenLost()))
                blamed.push_back (proxy);

        // A plugin that took its process down while loading has no stand-in yet: it is to blame,
        // not the plugins already there.
        const auto loading = dead->pluginLoadingWhenLost();

        if (loading.isNotEmpty() && blamed.empty())
        {
            juce::PluginDescription d;

            if (const auto xml = juce::parseXML (loading); xml != nullptr && d.loadFromXml (*xml))
            {
                countCrash (d);
                crash.crashed.add (d.name);
            }
        }

        // The one plugin in it, if nothing else says.
        if (blamed.empty() && loading.isEmpty() && in.size() == 1)
            blamed = in;

        for (auto* proxy : blamed)
        {
            juce::PluginDescription d;
            proxy->fillInPluginDescription (d);
            countCrash (d);
            crash.crashed.add (d.name);

            if (crashesThisRun[keyOf (d)] >= crashesBeforeStopping)
            {
                proxy->stopForGood();
                crash.stoppedForGood.add (d.name);
            }
        }


        crash.others = static_cast<int> (in.size() - blamed.size());
        log::warning ("A plugin process stopped: " + (crash.crashed.isEmpty() ? juce::String ("no plugin to blame") : crash.crashed.joinIntoString (", "))
                      + (crash.others > 0 ? "; " + juce::String (crash.others) + " other plugin(s) in it start again" : juce::String()));

        if (onCrash)
            onCrash (crash);
    }

    //==============================================================================================
    int Processes::crashesOf (const juce::PluginDescription& d) const
    {
        const auto found = history.find (keyOf (d));
        return found != history.end() ? found->second : 0;
    }

    void Processes::countCrash (const juce::PluginDescription& d)
    {
        ++history[keyOf (d)];
        ++crashesThisRun[keyOf (d)];
        saveHistory();
    }

    void Processes::forgetCrashes()
    {
        history.clear();
        saveHistory();
    }

    void Processes::loadHistory()
    {
        if (historyFile == juce::File())
            return;

        if (const auto xml = juce::parseXML (historyFile); xml != nullptr && xml->hasTagName (historyTag.toString()))
            for (auto* p : xml->getChildWithTagNameIterator (pluginTag.toString()))
                history[p->getStringAttribute (idAttr.toString())] = p->getIntAttribute (crashesAttr.toString());
    }

    void Processes::saveHistory() const
    {
        if (historyFile == juce::File())
            return;

        juce::XmlElement xml (historyTag.toString());
        xml.setAttribute ("version", 1);

        for (const auto& [id, crashes] : history)
        {
            auto* p = xml.createNewChildElement (pluginTag.toString());
            p->setAttribute (idAttr.toString(), id);
            p->setAttribute (crashesAttr.toString(), crashes);
        }

        if (! xml.writeTo (historyFile))
            log::warning ("Carthage could not write the plugin crash history: " + historyFile.getFullPathName());
    }

    //==============================================================================================
    void Processes::setWindowsOnTop (bool on)
    {
        windowsOnTop = on;
        tellHosts();
    }

    void Processes::timerCallback()
    {
        if (const auto inFront = juce::Process::isForegroundProcess(); inFront != carthageInFront)
        {
            carthageInFront = inFront;
            tellHosts();
        }
    }

    void Processes::tellHosts()
    {
        for (auto& [key, host] : hosts)
            if (host != nullptr && host->isRunning())
                host->setWindows (windowsOnTop, carthageInFront);
    }
}
