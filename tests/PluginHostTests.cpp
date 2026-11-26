// Plugins in their own processes (5f, W65): the shared-memory link, and carthage-plughost with its
// own test plugin ("Half": half the level, a note-on adds 1 at its sample).
#include <engine/RealtimeCheck.h>
#include <engine/plughost/HostProcess.h>
#include <engine/plughost/Link.h>
#include <engine/plughost/Protocol.h>

#include <memory>

using namespace carthage::engine;
using namespace carthage::engine::plughost;

namespace
{
    /** A link in this process's own memory, with the host's side run by hand. */
    struct LocalLink
    {
        explicit LocalLink (int maxBlock, int channels = 2)
            : bytes (Link::bytesFor (channels, channels, Link::capacityFor (maxBlock))),
              storage (bytes + 64, true)
        {
            void* aligned = storage.getData();
            auto space = bytes + 64;
            std::align (64, bytes, aligned, space);
            link = Link::create (aligned, bytes, channels, channels, maxBlock);
            host = Link::attach (aligned, bytes);
            host.prepareHost (audio, midi);
        }

        /** The host catches up: "Half" by hand. */
        void serve()
        {
            host.serve (audio, midi, [] (juce::AudioBuffer<float>& a, juce::MidiBuffer& m)
            {
                a.applyGain (0.5f);

                for (const auto e : m)
                    if (e.getMessage().isNoteOn())
                        a.addSample (0, e.samplePosition, 1.0f);
            });
        }

        size_t bytes;
        juce::HeapBlock<char> storage;
        Link link, host;
        juce::AudioBuffer<float> audio;
        juce::MidiBuffer midi;
    };

    /** Input frame f of the test signal: a ramp that never repeats within a test. */
    float signal (std::uint64_t f, int channel)   { return static_cast<float> (f % 1000) / 1000.0f + 0.25f * static_cast<float> (channel); }

    void fill (juce::AudioBuffer<float>& b, std::uint64_t first)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (c, i, signal (first + static_cast<std::uint64_t> (i), c));
    }

    /** Output frame f is input frame f - latency at half the level, or silence before it. */
    float expected (std::uint64_t f, int channel, int latency)
    {
        return f < static_cast<std::uint64_t> (latency) ? 0.0f : 0.5f * signal (f - static_cast<std::uint64_t> (latency), channel);
    }
}

class PluginHostTests final : public juce::UnitTest
{
public:
    PluginHostTests() : juce::UnitTest ("Plugins in their own processes", "plughost") {}

    void runTest() override
    {
        constexpr int block = 64;

        beginTest ("The link's output is its input one block later, exactly");
        {
            LocalLink l (block);
            juce::AudioBuffer<float> buffer (2, block);
            const juce::MidiBuffer none;
            bool exact = true;

            for (std::uint64_t first = 0; first < 20 * block; first += block)
            {
                fill (buffer, first);
                const auto at = l.link.give (buffer, none);
                l.serve();
                l.link.take (buffer, at);

                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < block; ++i)
                        exact = exact && buffer.getSample (c, i) == expected (first + static_cast<std::uint64_t> (i), c, block);
            }

            expect (exact);
            expectEquals (l.link.latency(), block);
            expectEquals (static_cast<int> (l.link.state().late.load()), 0);
        }

        beginTest ("Shorter blocks than the device's keep the same latency");
        {
            LocalLink l (block);
            const juce::MidiBuffer none;
            bool exact = true;
            std::uint64_t first = 0;

            for (const int n : { 64, 17, 33, 64, 1, 63, 64, 40, 24 })
            {
                juce::AudioBuffer<float> buffer (2, n);
                fill (buffer, first);
                l.link.exchange (buffer, none);   // reads what is ready before this block is served
                l.serve();

                for (int i = 0; i < n; ++i)
                    exact = exact && buffer.getSample (1, i) == expected (first + static_cast<std::uint64_t> (i), 1, block);

                first += static_cast<std::uint64_t> (n);
            }

            expect (exact);
            expectEquals (static_cast<int> (l.link.state().late.load()), 0);
        }

        beginTest ("A host that falls behind gives silence, counts it, and the timing holds after");
        {
            LocalLink l (block);
            juce::AudioBuffer<float> buffer (2, block);
            const juce::MidiBuffer none;

            fill (buffer, 0);
            l.link.exchange (buffer, none);   // block 0: the link's own silence
            fill (buffer, block);
            l.link.exchange (buffer, none);   // block 1 needs block 0 served: it was not
            expectEquals (buffer.getMagnitude (0, block), 0.0f);
            expectEquals (static_cast<int> (l.link.state().late.load()), 1);

            l.serve();                        // the host catches up on both
            fill (buffer, 2 * block);
            l.link.exchange (buffer, none);   // block 2 holds block 1's input, on time
            expectEquals (buffer.getSample (0, 5), expected (2 * block + 5, 0, block));
        }

        beginTest ("A host a whole ring behind starts again from the newest input");
        {
            LocalLink l (block);
            juce::AudioBuffer<float> buffer (2, block);
            const juce::MidiBuffer none;

            for (int b = 0; b < 12; ++b)
            {
                fill (buffer, static_cast<std::uint64_t> (b) * block);
                l.link.exchange (buffer, none);
            }

            l.serve();
            expectEquals (static_cast<int> (l.link.state().skipped.load()), 1);
            fill (buffer, 12 * block);
            l.link.exchange (buffer, none);
            expectEquals (buffer.getSample (0, 7), expected (12 * block + 7, 0, block));
        }

        beginTest ("MIDI arrives at its sample, one block later like the audio");
        {
            LocalLink l (block);
            juce::AudioBuffer<float> buffer (2, block);
            juce::MidiBuffer notes;
            notes.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 5);
            const std::uint8_t sysex[] = { 0xf0, 0x7d, 1, 2, 3, 4, 5, 0xf7 };
            notes.addEvent (sysex, 8, 6);   // system exclusive: not carried yet, counted

            buffer.clear();
            const auto at = l.link.give (buffer, notes);
            l.serve();
            l.link.take (buffer, at);
            buffer.clear();
            const auto next = l.link.give (buffer, {});
            l.serve();
            l.link.take (buffer, next);
            expectEquals (buffer.getSample (0, 5), 1.0f);
            expectEquals (buffer.getSample (0, 4), 0.0f);
            expectEquals (static_cast<int> (l.link.state().midiDropped.load()), 1);
        }

        beginTest ("A link that is not one is refused");
        {
            juce::HeapBlock<char> junk (4096, true);
            expect (! Link::attach (junk.getData(), 4096).isValid());
        }

       #ifdef CARTHAGE_PLUGHOST_PATH
        const juce::File executable (CARTHAGE_PLUGHOST_PATH);

        beginTest ("carthage-plughost runs a plugin: the same sound one block later");
        {
            HostProcess host (executable);
            const auto started = host.start();
            expect (started.wasOk(), started.getErrorMessage());
            juce::String error;
            auto plugin = host.load (testPlugin ("Half"), 48000.0, block, error);
            expect (plugin != nullptr, error);

            if (plugin != nullptr)
            {
                expectEquals (plugin->getName(), juce::String ("Half"));
                expectEquals (plugin->numIns(), 2);
                expectEquals (plugin->numOuts(), 2);
                expectEquals (plugin->latency(), block);

                juce::AudioBuffer<float> buffer (2, block);
                const juce::MidiBuffer none;
                bool exact = true, answered = true;

                for (std::uint64_t first = 0; first < 50 * block; first += block)
                {
                    fill (buffer, first);
                    answered = answered && plugin->processWaiting (buffer, none, 5000);

                    for (int i = 0; i < block; ++i)
                        exact = exact && buffer.getSample (0, i) == expected (first + static_cast<std::uint64_t> (i), 0, block);
                }

                expect (answered);
                expect (exact);
            }
        }

        beginTest ("In real time the host keeps up, and the audio thread never waits");
        {
            HostProcess host (executable);
            expect (host.start().wasOk());
            juce::String error;
            auto plugin = host.load (testPlugin ("Half"), 48000.0, block, error);
            expect (plugin != nullptr, error);

            if (plugin != nullptr)
            {
                juce::AudioBuffer<float> buffer (2, block);
                const juce::MidiBuffer none;
                double slowest = 0.0;
                bool exact = true;

                for (std::uint64_t first = 0; first < 100 * block; first += block)
                {
                    fill (buffer, first);
                    const auto t0 = juce::Time::getMillisecondCounterHiRes();

                    {
                        const carthage::rt::ScopedRealtime realtime;   // checked by RTSan: no allocation, lock or wait
                        plugin->process (buffer, none);
                    }

                    slowest = juce::jmax (slowest, juce::Time::getMillisecondCounterHiRes() - t0);

                    for (int i = 0; i < block; ++i)
                        exact = exact && buffer.getSample (1, i) == expected (first + static_cast<std::uint64_t> (i), 1, block);

                    juce::Thread::sleep (10);   // a generous block period, so a loaded test machine keeps up
                }

                expect (exact, "late blocks: " + juce::String (plugin->lateBlocks()));
                expectEquals (static_cast<int> (plugin->lateBlocks()), 0);
                expect (slowest < 5.0, "slowest process() call took " + juce::String (slowest, 2) + " ms");
            }
        }

        beginTest ("A plugin's state goes there and back");
        {
            HostProcess host (executable);
            expect (host.start().wasOk());
            juce::String error;
            auto plugin = host.load (testPlugin ("Half"), 44100.0, 256, error);
            expect (plugin != nullptr, error);

            if (plugin != nullptr)
            {
                juce::MemoryBlock state ("settings of the plugin", 22);
                expect (plugin->setState (state));
                const auto back = plugin->getState();
                expect (back.has_value() && *back == state);
            }
        }

        beginTest ("A plugin the host can not load says why");
        {
            HostProcess host (executable);
            expect (host.start().wasOk());
            juce::String error;
            expect (host.load (testPlugin ("Nothing"), 48000.0, block, error) == nullptr);
            expect (error.contains ("Nothing"), error);
        }

        beginTest ("A missing host is reported");
        {
            HostProcess host (juce::File ("/nonexistent/carthage-plughost"));
            expect (host.start().failed());
            juce::String error;
            expect (host.load (testPlugin ("Half"), 48000.0, block, error) == nullptr);
        }
       #endif
    }
};

static PluginHostTests pluginHostTests;
