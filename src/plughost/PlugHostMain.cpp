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

#include <map>

namespace
{
    using namespace carthage::engine;
    using namespace carthage::engine::plughost;

    /** "Half" (Protocol.h): the host's own test plugin. */
    class HalfProcessor final : public juce::AudioProcessor
    {
    public:
        HalfProcessor()
            : juce::AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo())
                                                     .withOutput ("Out", juce::AudioChannelSet::stereo()))
        {}

        const juce::String getName() const override   { return "Half"; }
        void prepareToPlay (double, int) override {}
        void releaseResources() override {}

        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
        {
            buffer.applyGain (0.5f);

            for (const auto m : midi)
                if (m.getMessage().isNoteOn() && buffer.getNumChannels() > 0)
                    buffer.addSample (0, m.samplePosition, 1.0f);
        }

        double getTailLengthSeconds() const override   { return 0.0; }
        bool acceptsMidi() const override              { return true; }
        bool producesMidi() const override             { return false; }
        juce::AudioProcessorEditor* createEditor() override   { return nullptr; }
        bool hasEditor() const override                { return false; }
        int getNumPrograms() override                  { return 1; }
        int getCurrentProgram() override               { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override   { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& data) override   { data = state; }
        void setStateInformation (const void* data, int size) override   { state.replaceAll (data, static_cast<size_t> (size)); }

    private:
        juce::MemoryBlock state;
    };

    /** One loaded plugin: its processor, link and render thread. */
    class Slot final : private juce::Thread
    {
    public:
        explicit Slot (std::unique_ptr<juce::AudioProcessor> p)
            : juce::Thread ("Carthage plugin host render"), processor (std::move (p)) {}

        ~Slot() override   { stop(); }

        juce::AudioProcessor& plugin()   { return *processor; }

        juce::String connect (const juce::String& memoryName, const juce::String& wakeName, size_t bytes, double rate)
        {
            memory = platform::SharedMemory::open (memoryName, bytes);
            wake = platform::WakeSignal::open (wakeName);

            if (memory == nullptr || wake == nullptr)
                return "the host could not open the link";

            link = Link::attach (memory->data(), memory->size());

            if (! link.isValid())
                return "the link's memory does not hold a link";

            link.prepareHost (audio, midi);
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
        std::unique_ptr<juce::AudioProcessor> processor;
        std::unique_ptr<platform::SharedMemory> memory;
        std::unique_ptr<platform::WakeSignal> wake;
        Link link;
        juce::AudioBuffer<float> audio;
        juce::MidiBuffer midi;

        void run() override
        {
            while (true)
            {
                wake->wait();

                if (threadShouldExit() || link.state().quit.load (std::memory_order_acquire) != 0)
                    return;

                link.serve (audio, midi, [this] (juce::AudioBuffer<float>& a, juce::MidiBuffer& m)
                {
                    processor->processBlock (a, m);
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

        std::unique_ptr<juce::AudioProcessor> make (const juce::PluginDescription& d, double rate, int maxBlock, juce::String& error)
        {
            if (d.pluginFormatName == testFormat)
            {
                if (d.name == "Half")
                    return std::make_unique<HalfProcessor>();

                error = "no test plugin called " + d.name;
                return {};
            }

            return formats.createPluginInstance (d, rate, maxBlock, error);
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
                reply.setProperty (msg::name, processor->getName(), nullptr);
                reply.setProperty (msg::numIns, processor->getTotalNumInputChannels(), nullptr);
                reply.setProperty (msg::numOuts, processor->getTotalNumOutputChannels(), nullptr);
                reply.setProperty (msg::latency, processor->getLatencySamples(), nullptr);
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
                return {};
            }

            if (type == msg::setState)
            {
                juce::MemoryBlock data;

                if (! data.fromBase64Encoding (request[msg::state].toString()))
                    return "the state could not be read";

                slot->plugin().setStateInformation (data.getData(), static_cast<int> (data.getSize()));
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
