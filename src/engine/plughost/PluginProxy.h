#pragma once

#include <engine/plughost/HostProcess.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <memory>

namespace carthage::engine::plughost
{
    /** The plugin Tracktion sees when plugins run in their own processes (5f-2, W65): one plugin
        that runs in a carthage-plughost or in this process, and moves between the two with its
        settings. Tracktion keeps the same object, the same parameters and the same buses through
        a move; only the latency changes (one block more in a host), and Tracktion re-reads it.

        The audio thread never waits: a move switches over between blocks (the message thread
        waits for the block in flight). Export (non-real-time) waits for the host's every block. */
    class PluginProxy final : public juce::AudioPluginInstance, private juce::AudioProcessorListener
    {
    public:
        /** Makes the plugin in this process (the format manager's own way). */
        using MakeLocal = std::function<std::unique_ptr<juce::AudioPluginInstance> (const juce::PluginDescription&, double, int, juce::String&)>;

        /** Starts in `host` when it is given and can load the plugin, or else in this process.
            Null and `error` when neither can. `host` must outlive the proxy. */
        static std::unique_ptr<PluginProxy> create (const juce::PluginDescription&, double sampleRate, int samplesPerBlock,
                                                    HostProcess* host, MakeLocal makeLocal, juce::String& error);

        ~PluginProxy() override;

        bool isInHost() const noexcept   { return inHost.load(); }

        /** Moves the plugin into its host (true) or into this process (false), keeping its state.
            Message thread. False and `error` when it can not (it stays where it was). */
        bool moveToHost (bool toHost, juce::String& error);

        /** Moves the plugin into `target` (null: into Carthage), keeping its state; from a host
            that has gone, with the newest settings Carthage has (a restart, 5f-4). */
        bool moveTo (HostProcess* target, juce::String& error);

        /** The host it runs in now (null: in Carthage). */
        HostProcess* runningIn() const noexcept;

        /** Whether its host was inside its processing when that host went (it crashed it). */
        bool crashedItsHost() const noexcept   { return inHost.load() && remote != nullptr && remote->wasProcessing(); }

        /** Its slot in the host it runs in (0: none). */
        int slotInHost() const noexcept   { return inHost.load() && remote != nullptr ? remote->slotID() : 0; }

        /** A VST3's own preset file (.vstpreset), read by the plugin wherever it runs (W64). */
        bool loadVST3Preset (const juce::MemoryBlock&);

        /** Called as the proxy goes (its registry forgets it). */
        std::function<void()> onDestroyed;

        /** Silent from now on: it crashed its process too often to start again (5f-4). */
        void stopForGood() noexcept   { stopped.store (true); }
        bool isStopped() const noexcept   { return stopped.load(); }

        /** Blocks whose output the host did not have ready, while in the host it has now. */
        std::uint32_t lateBlocks() const noexcept   { return inHost.load() && remote != nullptr ? remote->lateBlocks() : 0; }

        // ---- its window while it runs in its host (5f-3, W68) ----

        /** Shows the plugin's window from its host, or brings it forward. False if it can not. */
        bool openWindowInHost (const juce::String& title, juce::Colour background, bool onTop);
        void closeWindowInHost();
        bool isWindowOpenInHost() const noexcept   { return windowInHost; }

        /** Whether its own editor is showing in Carthage (a window of the app's). */
        bool isEditorOpenHere() const noexcept   { return ! inHost.load() && local != nullptr && local->getActiveEditor() != nullptr; }

        /** The window in the host closed: by itself (false), or because the plugin moved into
            Carthage with it open (true: the app shows it again, its own way). Message thread. */
        std::function<void (bool reopenHere)> onWindowClosed;

        /** A key the window in the host did not use, for Carthage's commands. Message thread. */
        std::function<void (const juce::KeyPress&)> onWindowKey;

        /** The host this plugin goes to (null: none, it stays here). */
        HostProcess* getHost() const noexcept   { return host; }

        // ---- juce::AudioPluginInstance ----
        void fillInPluginDescription (juce::PluginDescription& d) const override   { d = description; }
        const juce::String getName() const override   { return description.name; }
        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        void setNonRealtime (bool) noexcept override;
        double getTailLengthSeconds() const override;
        bool acceptsMidi() const override    { return midiIn; }
        bool producesMidi() const override   { return midiOut; }
        bool isBusesLayoutSupported (const BusesLayout&) const override;
        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override;
        int getNumPrograms() override;
        int getCurrentProgram() override;
        void setCurrentProgram (int) override;
        const juce::String getProgramName (int) override;
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override;
        void setStateInformation (const void*, int) override;
        juce::AudioPluginExtensions::VST3Client* getVST3Client() override;
        juce::AudioPluginExtensions::AudioUnitClient* getAudioUnitClient() override;

        PluginProxy (const juce::PluginDescription&, const BusesProperties&, const juce::ValueTree& info, HostProcess*, MakeLocal);

    private:
        class Parameter;
        friend class Parameter;

        static BusesProperties busesFrom (const juce::ValueTree& info);

        const juce::PluginDescription description;
        HostProcess* const host;   // where it goes by default
        const MakeLocal makeLocal;
        bool midiIn = false, midiOut = false;
        double tail = 0.0;
        double rate = 44100.0;
        int block = 512;
        bool prepared = false;

        std::unique_ptr<juce::AudioPluginInstance> local;
        std::unique_ptr<RemotePlugin> remote;
        std::atomic<bool> inHost { false };
        std::atomic<int> inFlight { 0 };   // blocks and parameter calls using local or remote now
        juce::MemoryBlock lastState;       // the newest state seen, for a host that has gone
        juce::Array<Parameter*> parameters;
        bool windowInHost = false;
        juce::String windowTitle;
        juce::Colour windowColour;
        bool windowOnTop = true;
        std::atomic<bool> stopped { false };

        /** Listens to what the plugin in the host does by itself. */
        void hear (RemotePlugin&);
        void handleNotice (const juce::ValueTree&);
        std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);   // for calls posted to later

        float valueOf (int index) const noexcept;
        void setValueOf (int index, float value) noexcept;
        juce::String textOf (int index, float value, int maxLength) const;
        float valueForText (int index, const juce::String&) const;

        void adopt (std::unique_ptr<juce::AudioPluginInstance>);
        void waitForIdle() const noexcept;
        void updateLatency();
        void refreshValues (const juce::Array<float>&);

        void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float value) override;
        void audioProcessorChanged (juce::AudioProcessor*, const juce::AudioProcessorListener::ChangeDetails&) override;
        void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override;
        void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int index) override;

        JUCE_DECLARE_NON_COPYABLE (PluginProxy)
    };
}
