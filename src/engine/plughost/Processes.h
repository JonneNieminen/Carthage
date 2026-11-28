#pragma once

#include <engine/plughost/HostProcess.h>

#include <tracktion_engine/tracktion_engine.h>

#include <map>
#include <memory>
#include <set>

namespace carthage::engine::plughost
{
    class PluginProxy;

    /** Which process each plugin runs in (5f, W65). Installed into Tracktion's plugin manager,
        it hands Tracktion a PluginProxy for every AU and VST3 while it is on, and place() puts
        each where it belongs:
        - on a live path (an input being heard, and everything it plays into): in Carthage (F8);
        - otherwise, grouped automatically: with the other plugins by the same maker, one process
          each, or in a process of its own once it has ever crashed one (its crash history);
        - otherwise, grouped by hand: where the person chose for it (with its maker, on its own,
          or in Carthage).
        While off, plugins load in Carthage as before, and place() brings any proxy home.

        When a process goes (a plugin crashed it), its plugins play silence; the one that was
        running counts a crash; onCrash says so; and the next place() starts them again, each
        with its newest settings, the one to blame in a process of its own (5f-4, W69). A plugin
        that crashes three times in a session stays silent until the project is opened again.

        Message thread. Must outlive the engine: Tracktion lets plugin instances go as late as
        its own destructor. */
    class Processes final : private juce::Timer
    {
    public:
        /** `crashHistory`: the file the crash counts are kept in (none: kept for this run only). */
        explicit Processes (juce::File hostExecutable, juce::File crashHistory = {});
        ~Processes() override;

        /** Takes over `engine`'s plugin creation (keeping its own for plugins that stay here). */
        void install (tracktion::Engine&);

        void setEnabled (bool on)          { enabled = on; }
        bool isEnabled() const noexcept    { return enabled; }

        /** Automatic grouping (on by default, W38): by maker and crash history. Off: each
            plugin's own choice (choiceOf). */
        void setAutomaticGrouping (bool on)   { automatic = on; }
        bool isAutomaticGrouping() const noexcept   { return automatic; }

        /** Where a plugin goes while grouping by hand, kept with the plugin in the project. */
        enum class Choice { withMaker, ownProcess, inCarthage };
        static Choice choiceOf (const tracktion::ExternalPlugin&);
        static void setChoice (tracktion::ExternalPlugin&, Choice);

        /** Puts every plugin of `edit` where it belongs (see above). A plugin whose editor is
            open in Carthage stays until it closes; one whose window is open in its host moves all
            the same and its window opens again where it went. */
        void place (tracktion::Edit&, const std::set<const tracktion::AudioTrack*>& live);

        /** What a crash did, for the person (the banner). */
        struct Crash
        {
            juce::StringArray crashed;        ///< the plugins that were running (names)
            juce::StringArray stoppedForGood; ///< crashed too often this session: silent until reopened
            int others = 0;                   ///< other plugins in the same process, started again too
        };

        /** After a process goes; the plugins start again at the next place(). Message thread. */
        std::function<void (const Crash&)> onCrash;

        /** Looks for processes that have gone and deals with each once (onCrash). Done by itself
            soon after one goes; the tests, which have no message loop, call it. */
        void checkHosts();

        /** How many times a plugin has crashed its process, ever (its crash history). */
        int crashesOf (const juce::PluginDescription&) const;
        void forgetCrashes();

        /** The process a plugin gets: its maker's, or one of its own. Started when first needed. */
        HostProcess& hostFor (const juce::PluginDescription&, bool ownProcess = false);

        /** Whether a plugin of this format can run in a host (AU, VST3, and the test format). */
        static bool canRunInHost (const juce::PluginDescription&);

        int numHosts() const noexcept;

        /** The hosts' plugin windows: above other windows or not (Carthage's setting, F21). */
        void setWindowsOnTop (bool);

    private:
        juce::File executable, historyFile;
        bool enabled = false, automatic = true;
        bool windowsOnTop = true, carthageInFront = true;
        std::map<juce::String, std::unique_ptr<HostProcess>> hosts;
        std::vector<std::unique_ptr<HostProcess>> retired;   // gone, still referred to by their plugins
        std::set<PluginProxy*> proxies;
        std::set<HostProcess*> dealtWith;
        std::map<juce::String, int> history;           // plugin identifier -> crashes, ever
        std::map<juce::String, int> crashesThisRun;
        std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
        std::function<std::unique_ptr<juce::AudioPluginInstance> (const juce::PluginDescription&, double, int, juce::String&)> original;

        std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription&, double, int, juce::String&);
        HostProcess* targetFor (tracktion::ExternalPlugin&, bool onLivePath);
        void hostLost (HostProcess*);
        void countCrash (const juce::PluginDescription&);
        void loadHistory();
        void saveHistory() const;

        void timerCallback() override;   // follows which app is in front, for the hosts' windows
        void tellHosts();
    };
}
