#pragma once

#include <engine/platform/SharedMemory.h>
#include <engine/plughost/Link.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

namespace carthage::engine::plughost
{
    class RemotePlugin;

    /** One `carthage-plughost` process, from the app's side (5f, W65). Requests wait for the
        host's answer, so they are made off the audio thread (the message thread, or a worker). A
        host that goes away (a plugin crashed in it) leaves isRunning() false; its plugins then
        play silence. */
    class HostProcess final : private juce::ChildProcessCoordinator
    {
    public:
        explicit HostProcess (juce::File executable);
        ~HostProcess() override;

        juce::Result start (int timeoutMs = 10000);
        bool isRunning() const noexcept   { return running.load (std::memory_order_acquire); }

        /** Loads a plugin in the host and links it: the plugin's processing from then on is the
            returned object's process(). Null and `error` if it could not. The plugin must not
            outlive this host. */
        std::unique_ptr<RemotePlugin> load (const juce::PluginDescription&, double sampleRate, int maxBlock,
                                            juce::String& error, int timeoutMs = 30000);

    private:
        friend class RemotePlugin;

        struct Pending
        {
            juce::WaitableEvent done;
            juce::ValueTree reply;
        };

        juce::File executable;
        std::atomic<bool> running { false };
        std::atomic<int> nextID { 1 };
        std::mutex pendingLock;   // never the audio thread
        std::map<int, std::shared_ptr<Pending>> pending;

        /** Sends `request` and waits for its reply; an invalid tree if none came in time. */
        juce::ValueTree ask (juce::ValueTree request, int timeoutMs);

        void handleMessageFromWorker (const juce::MemoryBlock&) override;
        void handleConnectionLost() override;
        void failAll();
    };

    /** A plugin running in a host process. process() is the audio thread's: it never waits, and
        its output is latency() samples behind its input (the plugin's own latency and one block
        for the hop). */
    class RemotePlugin final
    {
    public:
        ~RemotePlugin();

        const juce::String& getName() const noexcept   { return name; }
        int numIns() const noexcept    { return link.numIns(); }
        int numOuts() const noexcept   { return link.numOuts(); }

        /** The plugin's own latency, as the host last reported it. */
        int pluginLatency() const noexcept   { return ownLatency; }

        /** The hop's latency: the device block size the link was made for. */
        int linkLatency() const noexcept   { return link.latency(); }

        int latency() const noexcept   { return ownLatency + link.latency(); }

        /** One block in place, as AudioProcessor::processBlock: real time, never waits. */
        void process (juce::AudioBuffer<float>&, const juce::MidiBuffer&) noexcept;

        /** The same block for export: waits (up to `timeoutMs`) for the host. False on timeout,
            with silence. */
        bool processWaiting (juce::AudioBuffer<float>&, const juce::MidiBuffer&, int timeoutMs);

        std::optional<juce::MemoryBlock> getState (int timeoutMs = 10000);
        bool setState (const juce::MemoryBlock&, int timeoutMs = 10000);

        /** Blocks whose output was not ready in time, since the plugin loaded. */
        std::uint32_t lateBlocks() const noexcept   { return link.state().late.load (std::memory_order_relaxed); }

        RemotePlugin (HostProcess&, int slot, juce::String name, int ownLatency,
                      std::unique_ptr<platform::SharedMemory>, std::unique_ptr<platform::WakeSignal>, Link);

    private:
        HostProcess& host;
        const int slot;
        const juce::String name;
        int ownLatency;
        std::unique_ptr<platform::SharedMemory> memory;
        std::unique_ptr<platform::WakeSignal> wake;
        Link link;

        JUCE_DECLARE_NON_COPYABLE (RemotePlugin)
    };
}
