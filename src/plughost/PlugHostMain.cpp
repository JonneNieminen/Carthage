// carthage-plughost: runs plugins for the app in a process of its own (5f, W65).
//
// Started by the app (engine/plughost/HostProcess) through JUCE's child-process pipe; it answers
// the requests in engine/plughost/Protocol.h. Each plugin it loads is a slot with its own shared
// memory link (engine/plughost/Link.h) and its own real-time render thread, woken by the app's
// audio thread once a block. It quits when the app goes away. If a plugin crashes, this process
// dies with it and the app carries on.

#include <engine/platform/SharedMemory.h>
#include <engine/plughost/Link.h>
#include <engine/plughost/Protocol.h>
#include <engine/plughost/TestPlugins.h>

#include <map>

namespace
{
    using namespace carthage::engine;
    using namespace carthage::engine::plughost;

    /** One loaded plugin: its processor, link and render thread. */
    class Slot final : private juce::Thread
    {
    public:
        explicit Slot (std::unique_ptr<juce::AudioPluginInstance> p)
            : juce::Thread ("Carthage plugin host render"), processor (std::move (p)), parameters (processor->getParameters()) {}

        ~Slot() override   { stop(); }

        juce::AudioPluginInstance& plugin()   { return *processor; }

        /** Every parameter's value now. */
        juce::Array<float> values() const
        {
            juce::Array<float> v;

            for (auto* p : parameters)
                v.add (p->getValue());

            return v;
        }

        juce::String connect (const juce::String& memoryName, const juce::String& wakeName, size_t bytes, double rate)
        {
            stop();   // a new link (block size or rate) replaces the old one
            memory = platform::SharedMemory::open (memoryName, bytes);
            wake = platform::WakeSignal::open (wakeName);

            if (memory == nullptr || wake == nullptr)
                return "the host could not open the link";

            link = Link::attach (memory->data(), memory->size());

            if (! link.isValid())
                return "the link's memory does not hold a link";

            link.prepareHost (side);
            processor->setRateAndBufferSizeDetails (rate, link.maxBlock());
            processor->prepareToPlay (rate, link.maxBlock());

            if (! startRealtimeThread (juce::Thread::RealtimeOptions{}.withApproximateAudioProcessingTime (link.maxBlock(), rate)))
                return "the host could not start its render thread";

            return {};
        }

        void stop()
        {
            if (! isThreadRunning())
                return;

            signalThreadShouldExit();
            wake->post();
            stopThread (2000);
            processor->releaseResources();
        }

    private:
        std::unique_ptr<juce::AudioPluginInstance> processor;
        juce::Array<juce::AudioProcessorParameter*> parameters;
        std::unique_ptr<platform::SharedMemory> memory;
        std::unique_ptr<platform::WakeSignal> wake;
        Link link;
        HostSide side;

        void run() override
        {
            while (true)
            {
                wake->wait();

                if (threadShouldExit() || link.state().quit.load (std::memory_order_acquire) != 0)
                    return;

                link.serve (side,
                            [this] (juce::AudioBuffer<float>& a, juce::MidiBuffer& m) { processor->processBlock (a, m); },
                            [this] (int index, float value)
                            {
                                if (juce::isPositiveAndBelow (index, parameters.size()))
                                    parameters.getUnchecked (index)->setValue (value);
                            });
            }
        }
    };

    class Host final : public juce::ChildProcessWorker
    {
    public:
        Host()
        {
            juce::addDefaultFormatsToManager (formats);
        }

        ~Host() override   { slots.clear(); }

        void handleMessageFromCoordinator (const juce::MemoryBlock& block) override
        {
            // Plugins are made and spoken to on the message thread, as in the app.
            juce::MessageManager::callAsync ([this, tree = fromBlock (block)] { handle (tree); });
        }

        void handleConnectionLost() override
        {
            juce::MessageManager::callAsync ([this]
            {
                slots.clear();
                juce::JUCEApplicationBase::quit();
            });
        }

    private:
        juce::AudioPluginFormatManager formats;
        std::map<int, std::unique_ptr<Slot>> slots;
        int nextSlot = 1;

        void handle (const juce::ValueTree& request)
        {
            juce::ValueTree reply (msg::reply);
            reply.setProperty (msg::id, request[msg::id], nullptr);
            const auto error = answer (request, reply);
            reply.setProperty (msg::ok, error.isEmpty(), nullptr);

            if (error.isNotEmpty())
                reply.setProperty (msg::error, error, nullptr);

            sendMessageToCoordinator (toBlock (reply));
        }

        Slot* slotFor (const juce::ValueTree& request)
        {
            const auto found = slots.find (static_cast<int> (request[msg::slot]));
            return found != slots.end() ? found->second.get() : nullptr;
        }

        std::unique_ptr<juce::AudioPluginInstance> make (const juce::PluginDescription& d, double rate, int maxBlock, juce::String& error)
        {
            auto instance = d.pluginFormatName == testFormat ? createTestPlugin (d, error)
                                                             : formats.createPluginInstance (d, rate, maxBlock, error);

            if (instance != nullptr)
                instance->enableAllBuses();   // as Tracktion does with the plugins in the app

            return instance;
        }

        juce::String answer (const juce::ValueTree& request, juce::ValueTree& reply)
        {
            const auto type = request.getType();

            if (type == msg::load)
            {
                juce::PluginDescription d;
                const auto xml = juce::parseXML (request[msg::description].toString());

                if (xml == nullptr || ! d.loadFromXml (*xml))
                    return "the plugin's description could not be read";

                juce::String error;
                auto processor = make (d, request[msg::sampleRate], request[msg::maxBlock], error);

                if (processor == nullptr)
                    return error.isNotEmpty() ? error : "the plugin did not load";

                reply.setProperty (msg::slot, nextSlot, nullptr);
                describe (*processor, reply);
                slots[nextSlot++] = std::make_unique<Slot> (std::move (processor));
                return {};
            }

            auto* slot = slotFor (request);

            if (slot == nullptr)
                return "no such plugin in this host";

            if (type == msg::connect)
                return slot->connect (request[msg::memory], request[msg::wake],
                                      static_cast<size_t> (static_cast<juce::int64> (request[msg::bytes])),
                                      request[msg::sampleRate]);

            if (type == msg::getState)
            {
                juce::MemoryBlock data;
                slot->plugin().getStateInformation (data);
                reply.setProperty (msg::state, data.toBase64Encoding(), nullptr);
                reply.setProperty (msg::values, valuesToText (slot->values()), nullptr);
                return {};
            }

            if (type == msg::setState)
            {
                juce::MemoryBlock data;

                if (! data.fromBase64Encoding (request[msg::state].toString()))
                    return "the state could not be read";

                slot->plugin().setStateInformation (data.getData(), static_cast<int> (data.getSize()));
                reply.setProperty (msg::values, valuesToText (slot->values()), nullptr);
                reply.setProperty (msg::latency, slot->plugin().getLatencySamples(), nullptr);
                return {};
            }

            if (type == msg::unload)
            {
                slots.erase (static_cast<int> (request[msg::slot]));
                return {};
            }

            return "unknown request " + type.toString();
        }
    };
}

/** A JUCE application, so the host has the same message loop (and, later, windows) as the app. */
class PlugHostApplication final : public juce::JUCEApplicationBase
{
public:
    const juce::String getApplicationName() override      { return "carthage-plughost"; }
    const juce::String getApplicationVersion() override   { return "1"; }
    bool moreThanOneInstanceAllowed() override             { return true; }
    void anotherInstanceStarted (const juce::String&) override {}
    void suspended() override {}
    void resumed() override {}
    void systemRequestedQuit() override                    { quit(); }
    void unhandledException (const std::exception*, const juce::String&, int) override {}

    void initialise (const juce::String& commandLine) override
    {
        host = std::make_unique<Host>();

        if (! host->initialiseFromCommandLine (commandLine, commandLineID))
        {
            setApplicationReturnValue (3);   // not started by the app
            quit();
        }
    }

    void shutdown() override   { host.reset(); }

private:
    std::unique_ptr<Host> host;
};

JUCE_CREATE_APPLICATION_DEFINE (PlugHostApplication)

// Not START_JUCE_APPLICATION: it picks main (argc, argv) from this target's _CONSOLE, but the shared
// JUCE library is built without it, so on Windows only the main() that reads the command line
// itself is there.
int main (int argc, char* argv[])
{
    juce::JUCEApplicationBase::createInstance = &juce_CreateApplication;
   #if JUCE_WINDOWS
    juce::ignoreUnused (argc, argv);
    return juce::JUCEApplicationBase::main();
   #else
    return juce::JUCEApplicationBase::main (argc, const_cast<const char**> (argv));
   #endif
}
