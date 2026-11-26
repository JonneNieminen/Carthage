// Plugins in their own processes (5f, W65): the shared-memory link, and carthage-plughost with its
// own test plugin ("Half": half the level, a note-on adds 1 at its sample).
#include <engine/RealtimeCheck.h>
#include <engine/plughost/HostProcess.h>
#include <engine/plughost/Link.h>
#include <engine/plughost/PluginProxy.h>
#include <engine/plughost/Processes.h>
#include <engine/plughost/Protocol.h>
#include <engine/plughost/TestPlugins.h>
#include <engine/AudioFormats.h>
#include <engine/Editing.h>
#include <engine/Export.h>
#include <engine/Mixer.h>

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
            host.prepareHost (side);
        }

        /** The host catches up: "Half" by hand. */
        void serve()
        {
            host.serve (side, [] (juce::AudioBuffer<float>& a, juce::MidiBuffer& m)
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
        HostSide side;
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

        beginTest ("A parameter set here reaches the plugin there, and its state goes there and back");
        {
            HostProcess host (executable);
            expect (host.start().wasOk());
            juce::String error;
            auto plugin = host.load (testPlugin ("Half"), 44100.0, 256, error);
            expect (plugin != nullptr, error);

            if (plugin != nullptr)
            {
                expectEquals (plugin->lastValues().size(), 1);
                expectEquals (plugin->getInfo().getChildWithName (msg::parameter)[msg::paramID].toString(), juce::String ("gain"));

                juce::AudioBuffer<float> buffer (2, 256);
                const juce::MidiBuffer none;
                const auto levelAfter = [&] (float gain)
                {
                    plugin->setParameter (0, gain);

                    for (int b = 0; b < 3; ++b)   // the change, then a block's latency
                    {
                        for (int c = 0; c < 2; ++c)
                            juce::FloatVectorOperations::fill (buffer.getWritePointer (c), 1.0f, 256);

                        plugin->processWaiting (buffer, none, 5000);
                    }

                    return buffer.getSample (0, 100);
                };

                expectWithinAbsoluteError (levelAfter (0.3f), 0.3f, 1.0e-6f);
                const auto saved = plugin->getState();
                expect (saved.has_value());
                expectWithinAbsoluteError (plugin->lastValues()[0], 0.3f, 1.0e-6f);

                expectWithinAbsoluteError (levelAfter (0.9f), 0.9f, 1.0e-6f);
                expect (saved.has_value() && plugin->setState (*saved));
                expectWithinAbsoluteError (plugin->lastValues()[0], 0.3f, 1.0e-6f, "the values after setState come back with it");
                const auto again = plugin->getState();
                expect (again.has_value() && saved.has_value() && *again == *saved);
            }
        }

        beginTest ("A new block size links again, with the new latency");
        {
            HostProcess host (executable);
            expect (host.start().wasOk());
            juce::String error;
            auto plugin = host.load (testPlugin ("Half"), 48000.0, 64, error);
            expect (plugin != nullptr, error);

            if (plugin != nullptr)
            {
                expect (plugin->relink (44100.0, 256, error), error);
                expectEquals (plugin->latency(), 256);
                juce::AudioBuffer<float> buffer (2, 256);
                const juce::MidiBuffer none;
                bool exact = true;

                for (std::uint64_t first = 0; first < 10 * 256; first += 256)
                {
                    fill (buffer, first);
                    expect (plugin->processWaiting (buffer, none, 5000));

                    for (int i = 0; i < 256; ++i)
                        exact = exact && buffer.getSample (0, i) == expected (first + static_cast<std::uint64_t> (i), 0, 256);
                }

                expect (exact);
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

namespace
{
    struct Headless final : tracktion::EngineBehaviour
    {
        bool autoInitialiseDeviceManager() override   { return false; }
    };

    /** Clicks every 100 ms on a sweep: anything out of time shows. */
    juce::File makeSound (const juce::File& folder)
    {
        const auto file = folder.getChildFile ("Sound.wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (file);
        auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
        juce::AudioBuffer<float> b (1, 96000);

        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const auto t = static_cast<float> (i) / 48000.0f;
            b.setSample (0, i, ((i % 4800) < 8 ? 0.4f : 0.0f) + 0.2f * std::sin (juce::MathConstants<float>::twoPi * (200.0f + 300.0f * t) * t));
        }

        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        return file;
    }

    juce::AudioBuffer<float> readBack (const juce::File& f)
    {
        juce::AudioFormatManager formats;
        carthage::audio::registerReadable (formats);
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
        juce::AudioBuffer<float> b (2, r != nullptr ? static_cast<int> (r->lengthInSamples) : 0);
        b.clear();

        if (r != nullptr)
            r->read (&b, 0, b.getNumSamples(), 0, true, true);

        return b;
    }

    /** The largest difference between two renders, in dB below full scale. */
    double largestDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        float worst = a.getNumSamples() == b.getNumSamples() ? 0.0f : 1.0f;

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < juce::jmin (a.getNumSamples(), b.getNumSamples()); ++i)
                worst = juce::jmax (worst, std::abs (a.getSample (c, i) - b.getSample (c, i)));

        return juce::Decibels::gainToDecibels (static_cast<double> (worst), -200.0);
    }
}

/** 5f-2: the plugins of a project through the stand-in, in Tracktion. */
class PluginProxyTests final : public juce::UnitTest
{
public:
    PluginProxyTests() : juce::UnitTest ("Plugins in their own processes, in a project", "plughost") {}

    void runTest() override
    {
       #ifdef CARTHAGE_PLUGHOST_PATH
        using namespace carthage;
        juce::TemporaryFile holder;
        const auto& folder = holder.getFile();
        folder.createDirectory();

        tracktion::Engine engine ("CarthageProxyTests", nullptr, std::make_unique<Headless>());
        auto& manager = engine.getPluginManager();
        manager.pluginFormatManager.addFormat (std::make_unique<TestPluginFormat>());   // "Half" here too, tests only
        manager.knownPluginList.addType (testPlugin ("Half"));
        Processes processes { juce::File (CARTHAGE_PLUGHOST_PATH) };
        processes.install (engine);

        auto edit = tracktion::createEmptyEdit (engine, folder.getChildFile ("Proxy.tracktionedit"));
        editing::useConsistentFilePaths (*edit);
        editing::prepareMix (*edit, true);
        edit->clickTrackEnabled = false;
        const auto lanes = editing::lanes (*edit);
        const auto channels = editing::channels (*edit);
        auto* clip = editing::insertFile (*edit, lanes[0], makeSound (folder), 0.0);
        expect (clip != nullptr);

        if (clip == nullptr)
            return;

        editing::routeSample (*clip, channels[0]);
        editing::setLoopRegion (*edit, 0.0, 4.0);
        const auto range = exporting::rangeOf (*edit);
        const exporting::Settings settings { exporting::Format::wav32Float, 48000.0 };
        int renders = 0;

        const auto render = [&]
        {
            const auto file = folder.getChildFile ("Mix " + juce::String (++renders) + ".wav");
            const auto r = range.has_value() ? exporting::render (*edit, *range, settings, file) : juce::Result::fail ("no range");
            expect (r.wasOk(), r.getErrorMessage());
            return readBack (file);
        };

        const auto insertHalf = [&] (tracktion::AudioTrack* channel) -> tracktion::ExternalPlugin*
        {
            auto plugin = edit->getPluginCache().createNewPlugin (tracktion::ExternalPlugin::create (engine, testPlugin ("Half")));
            editing::placeInsert (editing::pluginListOf (*edit, channel), -1, plugin);
            edit->dispatchPendingUpdatesSynchronously();
            return dynamic_cast<tracktion::ExternalPlugin*> (plugin.get());
        };

        const auto proxyOf = [] (tracktion::ExternalPlugin* p)
        {
            return p != nullptr ? dynamic_cast<PluginProxy*> (p->getAudioPluginInstance()) : nullptr;
        };

        beginTest ("While off, plugins load in Carthage as before");
        auto* here = insertHalf (channels[0]);
        expect (here != nullptr && here->getAudioPluginInstance() != nullptr, here != nullptr ? "load error: " + here->getLoadError() : juce::String ("no plugin"));
        expect (proxyOf (here) == nullptr);
        const auto reference = render();
        expect (reference.getMagnitude (0, reference.getNumSamples()) > 0.05f, "the mix has sound");
        editing::removeInsert (*here);
        edit->dispatchPendingUpdatesSynchronously();

        beginTest ("While on, a plugin runs in its maker's process, behind a stand-in");
        processes.setEnabled (true);
        auto* there = insertHalf (channels[0]);
        auto* proxy = proxyOf (there);
        expect (proxy != nullptr && proxy->isInHost());
        expectEquals (processes.numHosts(), 1);

        if (proxy == nullptr)
            return;

        expect (proxy->getLatencySamples() > 0, "the hop's block of latency is reported");
        expect (proxy->getParameters().size() == 1);

        beginTest ("Its export nulls against the same plugin in Carthage: compensated, nothing lost");
        {
            const auto mix = render();
            const auto db = largestDifference (reference, mix);
            logMessage ("in a host against in Carthage: largest difference " + juce::String (db, 1) + " dB");
            expect (db < -120.0, "the export through the host nulls (" + juce::String (db, 1) + " dB)");
        }

        beginTest ("A parameter set through Tracktion reaches the plugin in its process");
        {
            tracktion::AutomatableParameter* gain = nullptr;

            for (auto* param : there->getAutomatableParameters())
                if (param->paramName == "Gain")   // Tracktion keys a plugin's parameters by index
                    gain = param;

            expect (gain != nullptr, "Tracktion sees the plugin's parameter");

            if (gain != nullptr)
                gain->setParameter (0.25f, juce::sendNotificationSync);

            const auto mix = render();
            const auto ratio = mix.getMagnitude (0, 0, mix.getNumSamples()) / juce::jmax (1.0e-9f, reference.getMagnitude (0, 0, reference.getNumSamples()));
            expectWithinAbsoluteError (ratio, 0.5f, 1.0e-3f, "a quarter instead of a half: half the level");
        }

        beginTest ("A live channel's plugin moves into Carthage with its settings, and back out");
        {
            processes.place (*edit, { channels[0] });
            expect (! proxy->isInHost(), "on the live path: in Carthage");
            expectEquals (proxy->getLatencySamples(), 0, "no hop: no latency");
            expectWithinAbsoluteError (proxy->getParameters()[0]->getValue(), 0.25f, 1.0e-6f, "its setting came along");
            const auto inHere = render();

            processes.place (*edit, {});
            expect (proxy->isInHost(), "off the live path: back in its process");
            expectWithinAbsoluteError (proxy->getParameters()[0]->getValue(), 0.25f, 1.0e-6f);
            const auto inThere = render();
            expect (largestDifference (inHere, inThere) < -120.0, "the same mix either way");
        }

        beginTest ("Master's plugins are on every live path");
        {
            auto* onMaster = insertHalf (nullptr);
            auto* masterProxy = proxyOf (onMaster);
            expect (masterProxy != nullptr && masterProxy->isInHost());
            processes.place (*edit, { channels[3] });
            expect (masterProxy != nullptr && ! masterProxy->isInHost());
            expect (proxy->isInHost(), "a channel not on the live path stays out");
            processes.place (*edit, {});
            expect (masterProxy != nullptr && masterProxy->isInHost());
            editing::removeInsert (*onMaster);
        }

        beginTest ("The project saves and reopens with the plugin's settings");
        {
            there->flushPluginStateToValueTree();
            expect (there->state.hasProperty (tracktion::IDs::state));
            juce::MemoryBlock saved;
            saved.fromBase64Encoding (there->state[tracktion::IDs::state].toString());
            float gain = 0.0f;

            if (saved.getSize() >= sizeof (float))
                std::memcpy (&gain, saved.getData(), sizeof (gain));

            expectWithinAbsoluteError (gain, 0.25f, 1.0e-6f, "the state in the project is the plugin's, from its process");
        }

        beginTest ("Turned off, every plugin comes home");
        processes.setEnabled (false);
        processes.place (*edit, {});
        expect (! proxy->isInHost());
        edit.reset();
       #endif
    }
};

static PluginProxyTests pluginProxyTests;
