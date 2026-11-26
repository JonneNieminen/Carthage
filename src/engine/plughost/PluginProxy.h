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

        /** Blocks whose output the host did not have ready, while in the host it has now. */
        std::uint32_t lateBlocks() const noexcept   { return inHost.load() && remote != nullptr ? remote->lateBlocks() : 0; }

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
        HostProcess* const host;
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
