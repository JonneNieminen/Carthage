#include <engine/AudioFormats.h>
#include <engine/Editing.h>
#include <engine/Export.h>
#include <engine/Mixer.h>

using namespace carthage;

namespace
{
    struct Headless final : tracktion::EngineBehaviour
    {
        bool autoInitialiseDeviceManager() override   { return false; }
    };

    /** Clicks every 100 ms on top of a sweep: any misalignment between two paths shows. */
    juce::File makeSound (const juce::File& folder, const juce::String& name, float seed)
    {
        const auto file = folder.getChildFile (name + ".wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (file);
        auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
        juce::AudioBuffer<float> b (1, 96000);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const auto t = static_cast<float> (i) / 48000.0f;
            const auto click = (i % 4800) < 8 ? 0.4f : 0.0f;
            b.setSample (0, i, click + 0.2f * std::sin (juce::MathConstants<float>::twoPi * (seed + 300.0f * t) * t));
        }
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        return file;
    }

    juce::AudioBuffer<float> read (const juce::File& f)
    {
        juce::AudioFormatManager formats;
        audio::registerReadable (formats);
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
        juce::AudioBuffer<float> b (2, r != nullptr ? static_cast<int> (r->lengthInSamples) : 0);
        b.clear();
        if (r != nullptr)
            r->read (&b, 0, b.getNumSamples(), 0, true, true);
        return b;
    }

    /** How far below `a` the difference between them is, in dB, and the offset that lines them up best. */
    std::pair<double, int> compare (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int search = 1024, int skip = 0)
    {
        const auto n = std::min (a.getNumSamples(), b.getNumSamples());
        double best = 1.0e30, signal = 1.0e-20;
        int bestOffset = 0;

        for (int offset = -search; offset <= search; ++offset)
        {
            double sum = 0.0, s = 0.0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = std::max (skip, -offset); i < n - std::max (0, offset); ++i)
                {
                    const auto d = static_cast<double> (b.getSample (ch, i + offset)) - a.getSample (ch, i);
                    sum += d * d;
                    s += static_cast<double> (a.getSample (ch, i)) * a.getSample (ch, i);
                }

            if (sum < best)
            {
                best = sum;
                signal = std::max (s, 1.0e-20);
                bestOffset = offset;
            }

            if (search == 0)
                break;
        }

        return { juce::Decibels::gainToDecibels (std::sqrt (best / signal), -200.0), bestOffset };
    }
}

/** Plugin delay compensation (F8, phase 5 exit check): a plugin that delays its sound and says so
    (Tracktion's latency test plugin: a pure delay that reports it) must not move anything in the
    mix, wherever it sits in Carthage's routing. Each export must null against the same mix without
    it, sample for sample. */
class DelayCompensationTests final : public juce::UnitTest
{
public:
    DelayCompensationTests() : juce::UnitTest ("Plugin delay compensation", "engine") {}

    void runTest() override
    {
        juce::TemporaryFile holder;
        const auto& folder = holder.getFile();
        folder.createDirectory();

        tracktion::Engine engine ("CarthagePdcTests", nullptr, std::make_unique<Headless>());
        engine.getPluginManager().createBuiltInType<tracktion::LatencyPlugin>();   // tests only: never offered to users

        auto edit = tracktion::createEmptyEdit (engine, folder.getChildFile ("Pdc.tracktionedit"));
        editing::useConsistentFilePaths (*edit);
        editing::prepareMix (*edit, true);
        edit->clickTrackEnabled = false;
        const auto lanes = editing::lanes (*edit);
        const auto ch = editing::channels (*edit);

        // Two sounds on two channels; channel 3 also sends to channel 5; channel 4 plays into
        // channel 6, a bus. Everything else to master through the Pre-Master.
        auto* a = editing::insertFile (*edit, lanes[0], makeSound (folder, "A", 200.0f), 0.0);
        auto* b = editing::insertFile (*edit, lanes[1], makeSound (folder, "B", 500.0f), 0.0);
        expect (a != nullptr && b != nullptr);
        editing::routeSample (*a, ch[2]);
        editing::routeSample (*b, ch[3]);
        expect (editing::addSend (*ch[2], *ch[4], -6.0f).wasOk());
        expect (editing::setOutput (*ch[3], ch[5]).wasOk());
        const exporting::Settings settings { exporting::Format::wav32Float, 48000.0 };
        int renders = 0;

        // Twice: a range starting at zero, on the sounds' first click, and one starting mid-sound.
        for (const auto& [fromBeat, toBeat] : { std::pair { 0.0, 4.0 }, std::pair { 1.0, 5.0 } })
        {
            editing::setLoopRegion (*edit, fromBeat, toBeat);
            const auto range = exporting::rangeOf (*edit);
            expect (range.has_value());

            if (! range.has_value())
                return;

            const auto renderMix = [&]
            {
                const auto file = folder.getChildFile ("Mix " + juce::String (++renders) + ".wav");
                const auto r = exporting::render (*edit, *range, settings, file);
                expect (r.wasOk(), r.getErrorMessage());
                return read (file);
            };

            const auto reference = renderMix();
            const auto from = juce::String (range->time.getStart().inSeconds(), 1) + " s";

            // A latency plugin (10 ms, 480 samples) at each kind of place, one at a time.
            const auto withLatencyOn = [&] (const juce::String& where, tracktion::AudioTrack* channel)
            {
                beginTest ("A plugin that delays its sound on " + where + " moves nothing (export from " + from + ")");
                auto& list = editing::pluginListOf (*edit, channel);
                auto plugin = edit->getPluginCache().createNewPlugin (tracktion::LatencyPlugin::xmlTypeName, {});
                auto* latency = dynamic_cast<tracktion::LatencyPlugin*> (plugin.get());
                expect (latency != nullptr);

                if (latency == nullptr)
                    return;

                latency->latencyTimeSeconds = 0.01f;
                expectWithinAbsoluteError (editing::latencyOf (*latency), 0.01, 1.0e-6, "Carthage reads the latency the plugin reports");
                expect (! editing::hasMix (*latency), "a plugin that delays its sound has no wet/dry knob (W56)");
                editing::placeInsert (list, -1, plugin);
                edit->dispatchPendingUpdatesSynchronously();

                const auto mix = renderMix();
                const auto [db, offset] = compare (reference, mix);
                logMessage (where + ", from " + from + ": residual " + juce::String (db, 1) + " dB, offset " + juce::String (offset));
                expect (db < -90.0, where + ": the mix with the plugin nulls against the mix without it (" + juce::String (db, 1) + " dB)");
                expectEquals (offset, 0, where + ": nothing moved");

                editing::removeInsert (*plugin);
                edit->dispatchPendingUpdatesSynchronously();
            };

            withLatencyOn ("the channel a sound plays through", ch[2]);
            withLatencyOn ("a send's destination", ch[4]);
            withLatencyOn ("a bus another channel plays into", ch[5]);
            withLatencyOn ("the Pre-Master", editing::preMaster (*edit));
            withLatencyOn ("master", nullptr);

            beginTest ("Without the plugin again, the mix is as it was (export from " + from + ")");
            expect (compare (reference, renderMix(), 0).first < -150.0);
        }

        beginTest ("Playback is compensated too: the paths stay together, the whole mix later by the latency at most");
        {
            constexpr double rate = 48000.0;
            constexpr int block = 512;
            editing::setLoopRegion (*edit, 0.0, 4.0);
            const auto range = exporting::rangeOf (*edit);
            expect (range.has_value());

            auto plugin = edit->getPluginCache().createNewPlugin (tracktion::LatencyPlugin::xmlTypeName, {});
            if (auto* latency = dynamic_cast<tracktion::LatencyPlugin*> (plugin.get()))
                latency->latencyTimeSeconds = 0.01f;
            editing::placeInsert (editing::pluginListOf (*edit, ch[2]), -1, plugin);
            edit->dispatchPendingUpdatesSynchronously();

            const auto file = folder.getChildFile ("Playback reference.wav");
            if (range.has_value())
                expect (exporting::render (*edit, *range, { exporting::Format::wav32Float, rate }, file).wasOk());
            const auto exported = read (file);

            auto& dm = engine.getDeviceManager();
            dm.setCpuLimitBeforeMuting (1000.0);
            auto& io = dm.getHostedAudioDeviceInterface();
            tracktion::HostedAudioDeviceInterface::Parameters p;
            p.sampleRate = rate;
            p.blockSize = block;
            io.initialise (p);
            io.prepareToPlay (rate, block);
            dm.dispatchPendingUpdates();

            auto& transport = edit->getTransport();
            editing::setLooping (*edit, false);
            edit->dispatchPendingUpdatesSynchronously();
            transport.ensureContextAllocated();
            transport.setPosition (tracktion::TimePosition());
            transport.play (false);

            for (const auto* clip : { a, b })
                if (const auto* audio = dynamic_cast<const tracktion::AudioClipBase*> (clip))
                    for (int i = 0; i < 300 && ! engine.getAudioFileManager().cache.hasMappedReader (audio->getAudioFile(), 0); ++i)
                        juce::Thread::sleep (10);

            const auto total = exported.getNumSamples() + block * 4;   // room for the playback's own delay
            juce::AudioBuffer<float> played (2, total), buffer (2, block);
            juce::MidiBuffer midi;

            for (int done = 0; done < total; done += block)
            {
                buffer.clear();
                io.processBlock (buffer, midi);
                for (int c = 0; c < 2; ++c)
                    played.copyFrom (c, done, buffer, c, 0, std::min (block, total - done));
            }

            transport.stop (false, false);
            // The first 2 ms after Play are left out: playback fades its start in against a click,
            // as a DAW should; the export does not, and should not.
            const auto [db, offset] = compare (exported, played, 1024, 96);
            logMessage ("Playback with a 480-sample plugin against its export: residual " + juce::String (db, 1) + " dB, playback later by " + juce::String (offset) + " samples");
            expect (db < -90.0, "playback nulls against the export once lined up (" + juce::String (db, 1) + " dB)");
            expect (offset >= 0 && offset <= 480, "the whole mix is late by the plugin's latency at most: " + juce::String (offset));

            // Low-latency monitoring (W57) freezes such a plugin: playback no longer waits for it,
            // and an export still has it.
            const auto playOnce = [&]
            {
                transport.setPosition (tracktion::TimePosition());
                transport.play (false);
                juce::AudioBuffer<float> out (2, total);

                for (int done = 0; done < total; done += block)
                {
                    buffer.clear();
                    io.processBlock (buffer, midi);
                    for (int c = 0; c < 2; ++c)
                        out.copyFrom (c, done, buffer, c, 0, std::min (block, total - done));
                }

                transport.stop (false, false);
                return out;
            };

            const auto firstSound = [] (const juce::AudioBuffer<float>& buf)
            {
                for (int i = 0; i < buf.getNumSamples(); ++i)
                    if (std::abs (buf.getSample (0, i)) > 0.01f)
                        return i;
                return -1;
            };

            const auto delayed = firstSound (playOnce());
            plugin->setFrozen (true);
            transport.ensureContextAllocated (true);   // the app's restartPlayback() does this on its next timer tick
            const auto undelayed = firstSound (playOnce());
            logMessage ("First sound in playback: " + juce::String (delayed) + " samples with the plugin, " + juce::String (undelayed) + " with it frozen");
            expect (delayed - undelayed >= 470, "frozen, the plugin no longer delays playback");

            if (range.has_value())
                expect (exporting::render (*edit, *range, { exporting::Format::wav32Float, rate }, file).wasOk());
            expect (compare (exported, read (file), 0).first < -150.0, "an export still has the frozen plugin");
            plugin->setFrozen (false);

            dm.deviceManager.closeAudioDevice();
            dm.removeHostedAudioDeviceInterface();
        }

        edit.reset();
    }
};

static DelayCompensationTests delayCompensationTests;
