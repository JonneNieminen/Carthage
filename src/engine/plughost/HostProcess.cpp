#include "HostProcess.h"

#include "Protocol.h"

#include <algorithm>

namespace carthage::engine::plughost
{
    HostProcess::HostProcess (juce::File exe) : executable (std::move (exe)) {}

    HostProcess::~HostProcess()
    {
        closing.store (true);
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
        started.store (true);
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
            askingSlot.store (request.hasProperty (msg::slot) ? static_cast<int> (request[msg::slot]) : -1);

            if (request.hasType (msg::load))
                loading = request[msg::description].toString();
        }

        const auto sent = sendMessageToWorker (toBlock (request));
        const auto answered = sent && waiting->done.wait (timeoutMs);

        const std::scoped_lock lock (pendingLock);
        pending.erase (id);
        askingSlot.store (0);
        loading.clear();
        return answered ? waiting->reply : juce::ValueTree();
    }

    void HostProcess::handleMessageFromWorker (const juce::MemoryBlock& block)
    {
        const auto reply = fromBlock (block);

        if (reply.hasType (msg::notice))
        {
            const std::scoped_lock lock (listenersLock);

            if (const auto found = listeners.find (static_cast<int> (reply[msg::slot])); found != listeners.end())
                if (found->second->onNotice)
                    found->second->onNotice (reply);

            return;
        }

        if (! reply.hasType (msg::reply))
            return;

        if (reply.hasProperty (msg::pid))
            pid.store (reply[msg::pid]);

        const std::scoped_lock lock (pendingLock);

        if (const auto found = pending.find (static_cast<int> (reply[msg::id])); found != pending.end())
        {
            found->second->reply = reply;
            found->second->done.signal();
        }
    }

    void HostProcess::listen (int slot, RemotePlugin* plugin)
    {
        const std::scoped_lock lock (listenersLock);

        if (plugin != nullptr)
            listeners[slot] = plugin;
        else
            listeners.erase (slot);
    }

    void HostProcess::setWindows (bool onTop, bool carthageInFront)
    {
        juce::ValueTree request (msg::windows);
        request.setProperty (msg::onTop, onTop, nullptr);
        request.setProperty (msg::foreground, carthageInFront, nullptr);

        // Not waited for: a host busy loading a plugin must not hold up the message thread. Its
        // reply carries id 0, which no request waits on.
        if (isRunning())
            sendMessageToWorker (toBlock (request));
    }

    void HostProcess::handleConnectionLost()
    {
        {
            const std::scoped_lock lock (pendingLock);
            lostSlot.store (askingSlot.load());
            loadingWhenLost = loading;
        }

        running.store (false, std::memory_order_release);
        failAll();

        if (! closing.load() && onLost)
            onLost();
    }

    juce::String HostProcess::pluginLoadingWhenLost() const
    {
        const std::scoped_lock lock (pendingLock);
        return loadingWhenLost;
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

    HostProcess::Linked HostProcess::linkUp (int slot, int ins, int outs, int params, double sampleRate, int maxBlock,
                                             juce::String& error, int timeoutMs)
    {
        Linked made;
        const auto bytes = Link::bytesFor (ins, outs, Link::capacityFor (maxBlock), params);
        const auto memoryName = platform::uniqueName ('m'), wakeName = platform::uniqueName ('w');
        made.memory = platform::SharedMemory::create (memoryName, bytes);
        made.wake = platform::WakeSignal::create (wakeName);
        made.link = made.memory != nullptr ? Link::create (made.memory->data(), made.memory->size(), ins, outs, maxBlock, params) : Link();

        if (made.wake == nullptr || ! made.link.isValid())
        {
            error = "Carthage could not make the link to the plugin";
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
        made.memory->removeName();
        made.wake->removeName();

        if (! static_cast<bool> (connected[msg::ok]))
        {
            error = errorIn (connected, "linking the plugin");
            return {};
        }

        return made;
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
        const auto params = loaded.getNumChildren() > 0 ? static_cast<int> (std::count_if (loaded.begin(), loaded.end(),
                                                              [] (const auto& c) { return c.hasType (msg::parameter); }))
                                                        : 0;
        auto linked = linkUp (slot, loaded[msg::numIns], loaded[msg::numOuts], juce::jmin (params, maxLinkParameters),
                              sampleRate, maxBlock, error, timeoutMs);

        if (! linked.link.isValid())
        {
            juce::ValueTree unload (msg::unload);
            unload.setProperty (msg::slot, slot, nullptr);
            ask (unload, 2000);
            error = d.name + ": " + error;
            return {};
        }

        return std::make_unique<RemotePlugin> (*this, slot, loaded, sampleRate, std::move (linked));
    }

    RemotePlugin::RemotePlugin (HostProcess& h, int s, juce::ValueTree i, double sampleRate, HostProcess::Linked linked)
        : host (h), slot (s), info (std::move (i)), name (info[msg::name].toString()),
          ownLatency (info[msg::latency]), rate (sampleRate),
          memory (std::move (linked.memory)), wake (std::move (linked.wake)), link (linked.link)
    {
        for (const auto& c : info)
            if (c.hasType (msg::parameter))
                values.add (static_cast<float> (c[msg::value]));

        host.listen (slot, this);
    }

    void RemotePlugin::setNoticeHandler (std::function<void (const juce::ValueTree&)> handler)
    {
        const std::scoped_lock lock (host.listenersLock);
        onNotice = std::move (handler);
    }

    bool RemotePlugin::openEditor (const juce::String& title, juce::Colour background, bool onTop)
    {
        platform::letComeForward (host.processID());   // Carthage is in front: it lets the window come forward

        juce::ValueTree request (msg::openEditor);
        request.setProperty (msg::slot, slot, nullptr);
        request.setProperty (msg::name, title, nullptr);
        request.setProperty (msg::colour, static_cast<juce::int64> (background.getARGB()), nullptr);
        request.setProperty (msg::onTop, onTop, nullptr);
        return static_cast<bool> (host.ask (request, 10000)[msg::ok]);
    }

    void RemotePlugin::closeEditor()
    {
        juce::ValueTree request (msg::closeEditor);
        request.setProperty (msg::slot, slot, nullptr);
        host.ask (request, 5000);
    }

    bool RemotePlugin::relink (double sampleRate, int maxBlock, juce::String& error)
    {
        auto linked = host.linkUp (slot, link.numIns(), link.numOuts(), link.numParams(), sampleRate, maxBlock, error, 10000);

        if (! linked.link.isValid())
            return false;

        // The host has let go of the old link (it stops before attaching to a new one).
        memory = std::move (linked.memory);
        wake = std::move (linked.wake);
        link = linked.link;
        rate = sampleRate;
        return true;
    }

    void RemotePlugin::takeValues (const juce::ValueTree& reply)
    {
        if (reply.hasProperty (msg::values))
            values = valuesFromText (reply[msg::values].toString());

        if (reply.hasProperty (msg::latency))
            ownLatency = reply[msg::latency];
    }

    RemotePlugin::~RemotePlugin()
    {
        host.listen (slot, nullptr);   // no notice reaches this plugin from here on
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

        takeValues (reply);
        return data;
    }

    bool RemotePlugin::loadVST3Preset (const juce::MemoryBlock& data, int timeoutMs)
    {
        juce::ValueTree request (msg::vst3Preset);
        request.setProperty (msg::slot, slot, nullptr);
        request.setProperty (msg::state, data.toBase64Encoding(), nullptr);
        const auto reply = host.ask (request, timeoutMs);

        if (! static_cast<bool> (reply[msg::ok]))
            return false;

        takeValues (reply);
        return true;
    }

    bool RemotePlugin::setState (const juce::MemoryBlock& data, int timeoutMs)
    {
        juce::ValueTree request (msg::setState);
        request.setProperty (msg::slot, slot, nullptr);
        request.setProperty (msg::state, data.toBase64Encoding(), nullptr);
        const auto reply = host.ask (request, timeoutMs);

        if (! static_cast<bool> (reply[msg::ok]))
            return false;

        takeValues (reply);
        return true;
    }
}
