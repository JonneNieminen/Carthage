#pragma once

#include <engine/plughost/HostProcess.h>

#include <tracktion_engine/tracktion_engine.h>

#include <map>
#include <memory>
#include <set>

namespace carthage::engine::plughost
{
    /** Which process each plugin runs in (5f, W65). Installed into Tracktion's plugin manager,
        it hands Tracktion a PluginProxy for every AU and VST3 while it is on: the plugin starts
        in its maker's carthage-plughost (one process per manufacturer) and place() moves the
        plugins on a live path (an input being heard, and everything it plays into) into this
        process and the others back out. While off, plugins load in this process as before, and
        place() brings any proxy home. Message thread. Must outlive the engine: Tracktion lets plugin
        instances go as late as its own destructor. */
    class Processes final
    {
    public:
        explicit Processes (juce::File hostExecutable);
        ~Processes();

        /** Takes over `engine`'s plugin creation (keeping its own for plugins that stay here). */
        void install (tracktion::Engine&);

        void setEnabled (bool on)          { enabled = on; }
        bool isEnabled() const noexcept    { return enabled; }

        /** Puts every plugin of `edit` where it belongs: on a track in `live` (or on master or
            the Pre-Master while anything is live) in this process, the rest in their hosts. A
            plugin whose window is open there stays until it closes. */
        void place (tracktion::Edit&, const std::set<const tracktion::AudioTrack*>& live);

        /** The process a plugin's maker gets, started when first needed. */
        HostProcess& hostFor (const juce::PluginDescription&);

        /** Whether a plugin of this format can run in a host (AU, VST3, and the test format). */
        static bool canRunInHost (const juce::PluginDescription&);

        int numHosts() const noexcept   { return static_cast<int> (hosts.size()); }

    private:
        juce::File executable;
        bool enabled = false;
        std::map<juce::String, std::unique_ptr<HostProcess>> hosts;
        std::function<std::unique_ptr<juce::AudioPluginInstance> (const juce::PluginDescription&, double, int, juce::String&)> original;

        std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription&, double, int, juce::String&);
    };
}
