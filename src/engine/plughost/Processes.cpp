#include "Processes.h"

#include "PluginProxy.h"
#include "Protocol.h"

#include <engine/Log.h>

namespace carthage::engine::plughost
{
    using namespace tracktion;

    Processes::Processes (juce::File hostExecutable) : executable (std::move (hostExecutable))
    {
        startTimer (250);
    }

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
        for (auto& [maker, host] : hosts)
            if (host != nullptr && host->isRunning())
                host->setWindows (windowsOnTop, carthageInFront);
    }

    Processes::~Processes()
    {
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

    HostProcess& Processes::hostFor (const juce::PluginDescription& d)
    {
        const auto maker = d.manufacturerName.isNotEmpty() ? d.manufacturerName : juce::String ("(no maker)");
        auto& slot = hosts[maker];

        if (slot == nullptr || ! slot->isRunning())
        {
            slot = std::make_unique<HostProcess> (executable);

            if (const auto started = slot->start(); started.failed())
                log::warning ("Plugin process for " + maker + " did not start: " + started.getErrorMessage());
        }

        return *slot;
    }

    std::unique_ptr<juce::AudioPluginInstance> Processes::create (const juce::PluginDescription& d, double rate, int blockSize,
                                                                  juce::String& error)
    {
        if (! enabled || ! canRunInHost (d))
            return original (d, rate, blockSize, error);

        auto& host = hostFor (d);
        auto proxy = PluginProxy::create (d, rate, blockSize, host.isRunning() ? &host : nullptr, original, error);

        if (proxy != nullptr && ! proxy->isInHost())
            log::warning (d.name + " runs inside Carthage: its process could not load it (" + error + ")");

        return proxy;
    }

    void Processes::place (Edit& edit, const std::set<const AudioTrack*>& live)
    {
        for (auto* p : getAllPlugins (edit, false))
        {
            auto* ext = dynamic_cast<ExternalPlugin*> (p);
            auto* proxy = ext != nullptr ? dynamic_cast<PluginProxy*> (ext->getAudioPluginInstance()) : nullptr;

            if (proxy == nullptr)
                continue;

            // Master and the Pre-Master hear everything, so they are on any live path.
            auto* track = dynamic_cast<const AudioTrack*> (ext->getOwnerTrack());
            const auto onLivePath = track != nullptr ? live.contains (track) : ! live.empty();
            const auto toHost = enabled && ! onLivePath && proxy->getHost() != nullptr;

            if (proxy->isInHost() == toHost)
                continue;

            // Its editor belongs to the plugin here: it moves once the editor is closed.
            if (toHost && proxy->isEditorOpenHere())
                continue;

            juce::String error;

            if (proxy->moveToHost (toHost, error))
                log::info (ext->getName() + (toHost ? " moved to its own process" : " moved into Carthage"));
            else
                log::warning (ext->getName() + " could not move " + (toHost ? "to its own process: " : "into Carthage: ") + error);
        }
    }
}
