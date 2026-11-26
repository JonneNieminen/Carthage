#include "HostProcess.h"

#include "Protocol.h"

namespace carthage::engine::plughost
{
    HostProcess::HostProcess (juce::File exe) : executable (std::move (exe)) {}

    HostProcess::~HostProcess()
    {
        running.store (false, std::memory_order_release);
        killWorkerProcess();
        failAll();
    }

    juce::Result HostProcess::start (int timeoutMs)
    {
        if (! executable.existsAsFile())
            return juce::Result::fail ("carthage-plughost is missing: " + executable.getFullPathName());

        if (! launchWorkerProcess (executable, commandLineID, timeoutMs))
            return juce::Result::fail ("carthage-plughost did not start");

        running.store (true, std::memory_order_release);
        return juce::Result::ok();
    }

    juce::ValueTree HostProcess::ask (juce::ValueTree request, int timeoutMs)
    {
        if (! isRunning())
            return {};

        const auto id = nextID.fetch_add (1);
        request.setProperty (msg::id, id, nullptr);
        auto waiting = std::make_shared<Pending>();

        {
            const std::scoped_lock lock (pendingLock);
            pending[id] = waiting;
        }

        const auto sent = sendMessageToWorker (toBlock (request));
        const auto answered = sent && waiting->done.wait (timeoutMs);

        const std::scoped_lock lock (pendingLock);
        pending.erase (id);
        return answered ? waiting->reply : juce::ValueTree();
    }

    void HostProcess::handleMessageFromWorker (const juce::MemoryBlock& block)
    {
        const auto reply = fromBlock (block);

        if (! reply.hasType (msg::reply))
            return;

        const std::scoped_lock lock (pendingLock);

        if (const auto found = pending.find (static_cast<int> (reply[msg::id])); found != pending.end())
        {
            found->second->reply = reply;
            found->second->done.signal();
        }
    }

    void HostProcess::handleConnectionLost()
    {
        running.store (false, std::memory_order_release);
        failAll();
    }

    void HostProcess::failAll()
    {
        const std::scoped_lock lock (pendingLock);

        for (auto& [id, waiting] : pending)
            waiting->done.signal();   // no reply: the caller sees an invalid tree
    }

    namespace
    {
        juce::String errorIn (const juce::ValueTree& reply, const juce::String& what)
        {
            if (! reply.isValid())
                return "carthage-plughost did not answer (" + what + ")";

            return reply[msg::error].toString().isNotEmpty() ? reply[msg::error].toString() : "carthage-plughost refused " + what;
        }
    }

    std::unique_ptr<RemotePlugin> HostProcess::load (const juce::PluginDescription& d, double sampleRate, int maxBlock,
                                                     juce::String& error, int timeoutMs)
    {
        const auto description = d.createXml();
        juce::ValueTree request (msg::load);
        request.setProperty (msg::description, description != nullptr ? description->toString() : juce::String(), nullptr);
        request.setProperty (msg::sampleRate, sampleRate, nullptr);
        request.setProperty (msg::maxBlock, maxBlock, nullptr);
        const auto loaded = ask (request, timeoutMs);

        if (! static_cast<bool> (loaded[msg::ok]))
        {
            error = errorIn (loaded, "loading " + d.name);
            return {};
        }

        const int slot = loaded[msg::slot];
        const int ins = loaded[msg::numIns], outs = loaded[msg::numOuts];
        const auto bytes = Link::bytesFor (ins, outs, Link::capacityFor (maxBlock));
        const auto memoryName = platform::uniqueName ('m'), wakeName = platform::uniqueName ('w');
        auto memory = platform::SharedMemory::create (memoryName, bytes);
        auto wake = platform::WakeSignal::create (wakeName);
        const auto link = memory != nullptr ? Link::create (memory->data(), memory->size(), ins, outs, maxBlock) : Link();

        auto forget = [&]
        {
            juce::ValueTree unload (msg::unload);
            unload.setProperty (msg::slot, slot, nullptr);
            ask (unload, 2000);
        };

        if (wake == nullptr || ! link.isValid())
        {
            error = "Carthage could not make the link to " + d.name;
            forget();
            return {};
        }

        juce::ValueTree connect (msg::connect);
        connect.setProperty (msg::slot, slot, nullptr);
        connect.setProperty (msg::memory, memoryName, nullptr);
        connect.setProperty (msg::wake, wakeName, nullptr);
        connect.setProperty (msg::bytes, static_cast<juce::int64> (bytes), nullptr);
        connect.setProperty (msg::sampleRate, sampleRate, nullptr);
        const auto connected = ask (connect, timeoutMs);

        // Both sides have them open now (or never will): the names can go.
        memory->removeName();
        wake->removeName();

        if (! static_cast<bool> (connected[msg::ok]))
        {
            error = errorIn (connected, "linking " + d.name);
            forget();
            return {};
        }

        return std::make_unique<RemotePlugin> (*this, slot, loaded[msg::name].toString(), static_cast<int> (loaded[msg::latency]),
                                               std::move (memory), std::move (wake), link);
    }

    RemotePlugin::RemotePlugin (HostProcess& h, int s, juce::String n, int latencyOfItsOwn,
                                std::unique_ptr<platform::SharedMemory> m, std::unique_ptr<platform::WakeSignal> w, Link l)
        : host (h), slot (s), name (std::move (n)), ownLatency (latencyOfItsOwn),
          memory (std::move (m)), wake (std::move (w)), link (l)
    {}

    RemotePlugin::~RemotePlugin()
    {
        link.state().quit.store (1, std::memory_order_release);
        wake->post();

        if (host.isRunning())
        {
            juce::ValueTree unload (msg::unload);
            unload.setProperty (msg::slot, slot, nullptr);
            host.ask (unload, 5000);
        }
    }

    void RemotePlugin::process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi) noexcept
    {
        const auto first = link.give (buffer, midi);
        wake->post();
        link.take (buffer, first);
    }

    bool RemotePlugin::processWaiting (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, int timeoutMs)
    {
        const auto first = link.give (buffer, midi);
        wake->post();
        const auto until = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);

        while (! link.caughtUp())
        {
            if (! host.isRunning() || juce::Time::getMillisecondCounter() > until)
            {
                buffer.clear();
                return false;
            }

            juce::Thread::yield();
        }

        link.take (buffer, first);
        return true;
    }

    std::optional<juce::MemoryBlock> RemotePlugin::getState (int timeoutMs)
    {
        juce::ValueTree request (msg::getState);
        request.setProperty (msg::slot, slot, nullptr);
        const auto reply = host.ask (request, timeoutMs);
        juce::MemoryBlock data;

        if (! static_cast<bool> (reply[msg::ok]) || ! data.fromBase64Encoding (reply[msg::state].toString()))
            return std::nullopt;

        return data;
    }

    bool RemotePlugin::setState (const juce::MemoryBlock& data, int timeoutMs)
    {
        juce::ValueTree request (msg::setState);
        request.setProperty (msg::slot, slot, nullptr);
        request.setProperty (msg::state, data.toBase64Encoding(), nullptr);
        return static_cast<bool> (host.ask (request, timeoutMs)[msg::ok]);
    }
}
