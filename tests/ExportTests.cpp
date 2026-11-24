#include <engine/AudioFormats.h>
#include <engine/Editing.h>
#include <engine/Export.h>
#include <engine/Mixer.h>

#include <thread>

using namespace carthage;

namespace
{
    struct Headless final : tracktion::EngineBehaviour
    {
        bool autoInitialiseDeviceManager() override   { return false; }
    };

    /** A two-second tone that changes as it goes, so a misaligned comparison can not null. */
    juce::File makeTone (const juce::File& folder)
    {
        const auto file = folder.getChildFile ("Tone.wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (file);
        auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
        juce::AudioBuffer<float> b (1, 96000);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const auto t = static_cast<float> (i) / 48000.0f;
            b.setSample (0, i, 0.3f * std::sin (juce::MathConstants<float>::twoPi * (220.0f + 200.0f * t) * t));
        }
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        return file;
    }

    std::unique_ptr<juce::AudioFormatReader> open (const juce::File& f)
    {
        juce::AudioFormatManager formats;
        audio::registerReadable (formats);
        return std::unique_ptr<juce::AudioFormatReader> (formats.createReaderFor (f));
    }
}

/** Export (W43, phase 5 exit check): the range, the formats, and an export that nulls against
    the same range played back through the engine's own playback graph. */
class ExportTests final : public juce::UnitTest
{
public:
    ExportTests() : juce::UnitTest ("Export", "engine") {}

    void runTest() override
    {
        juce::TemporaryFile holder;
        const auto& folder = holder.getFile();
        folder.createDirectory();
        const auto tone = makeTone (folder);

        tracktion::Engine engine ("CarthageExportTests", nullptr, std::make_unique<Headless>());
        auto edit = tracktion::createEmptyEdit (engine, folder.getChildFile ("Export.tracktionedit"));
        editing::useConsistentFilePaths (*edit);
        editing::prepareMix (*edit, true);
        edit->clickTrackEnabled = false;
        const auto lanes = editing::lanes (*edit);
        const auto channels = editing::channels (*edit);

        beginTest ("Nothing to export in an empty project");
        expect (! exporting::rangeOf (*edit).has_value());

        // A sound on lane 2 at beat 2 (1 s at 120 bpm), through channel 3 into the mix, with a
        // send to channel 5 and the Pre-Master on: the export is the whole mixer's sound.
        auto* clip = editing::insertFile (*edit, lanes[1], tone, 2.0);
        expect (clip != nullptr);
        editing::routeSample (*clip, channels[2]);
        editing::faderOf (*channels[2])->setVolumeDb (-6.0f);
        editing::faderOf (*channels[2])->setPan (0.3f);
        expect (editing::addSend (*channels[2], *channels[4], -10.0f).wasOk());

        beginTest ("The whole project: from its first sound to its last");
        {
            const auto r = exporting::rangeOf (*edit);
            expect (r.has_value());

            if (r.has_value())
            {
                expect (! r->isLoopRegion);
                expectWithinAbsoluteError (r->time.getStart().inSeconds(), 1.0, 1.0e-6);
                expectWithinAbsoluteError (r->time.getEnd().inSeconds(), 3.0, 1.0e-6);
                expect (exporting::parametersFor (*edit, *r, {}, folder.getChildFile ("x.wav")).endAllowance.inSeconds() > 0.0,
                        "a whole-project export lets its sound ring out");
            }
        }

        beginTest ("The loop region, when there is one, exactly");
        editing::setLoopRegion (*edit, 0.0, 8.0);   // 0 to 4 s
        const auto found = exporting::rangeOf (*edit);
        expect (found.has_value());

        if (! found.has_value())
            return;

        const auto range = *found;
        expect (range.isLoopRegion);
        expectWithinAbsoluteError (range.time.getEnd().inSeconds(), 4.0, 1.0e-6);
        expect (exporting::parametersFor (*edit, range, {}, folder.getChildFile ("x.wav")).endAllowance.inSeconds() == 0.0);
        expect (exporting::parametersFor (*edit, range, {}, folder.getChildFile ("x.wav")).useMasterPlugins, "master's Line and fader are in the mix");

        beginTest ("Every format: WAV 16, 24, 32-bit float and FLAC 16, 24, at the chosen rate, stereo, exact length");
        for (const auto format : exporting::formats)
            for (const auto rate : { 44100.0, 96000.0 })
            {
                const auto file = folder.getChildFile ("Export " + juce::String (static_cast<int> (format)) + " " + juce::String (rate) + "." + exporting::extensionOf (format));
                const auto rendered = exporting::render (*edit, range, { format, rate }, file);
                expect (rendered.wasOk(), rendered.getErrorMessage());
                const auto reader = open (file);
                const auto label = exporting::describe (format) + " at " + juce::String (rate);

                expect (reader != nullptr, label + ": readable");

                if (reader == nullptr)
                    continue;

                expectEquals (static_cast<int> (reader->bitsPerSample), exporting::bitDepthOf (format), label);
                expect (reader->usesFloatingPointData == (format == exporting::Format::wav32Float), label + ": float only for 32-bit");
                expectEquals (reader->sampleRate, rate, label);
                expectEquals (static_cast<int> (reader->numChannels), 2, label + ": stereo");
                expectEquals (reader->lengthInSamples, static_cast<juce::int64> (std::llround (4.0 * rate)), label + ": the region's length");
            }

        // Playback through the engine's own graph on a hosted device, blocks pumped as fast as possible.
        constexpr double rate = 48000.0;
        constexpr int block = 512;
        auto& dm = engine.getDeviceManager();
        dm.setCpuLimitBeforeMuting (1000.0);
        auto& io = dm.getHostedAudioDeviceInterface();
        tracktion::HostedAudioDeviceInterface::Parameters p;
        p.sampleRate = rate;
        p.blockSize = block;
        p.inputChannels = 2;
        p.outputChannels = 2;
        io.initialise (p);
        io.prepareToPlay (rate, block);
        dm.dispatchPendingUpdates();
        auto& transport = edit->getTransport();
        juce::MidiBuffer midi;

        const auto startPlaying = [&] (double fromSeconds)
        {
            editing::setLooping (*edit, false);
            edit->dispatchPendingUpdatesSynchronously();
            transport.ensureContextAllocated();
            transport.setPosition (tracktion::TimePosition::fromSeconds (fromSeconds));
            transport.play (false);

            // The file is read through the cache once something plays it: wait until it is mapped.
            const tracktion::AudioFile af (engine, tone);
            for (int i = 0; i < 300 && ! engine.getAudioFileManager().cache.hasMappedReader (af, 0); ++i)
                std::this_thread::sleep_for (std::chrono::milliseconds (10));
        };

        const auto pump = [&] (juce::AudioBuffer<float>& into, int from, int count)
        {
            juce::AudioBuffer<float> io2 (2, block);

            for (int done = 0; done < count; done += block)
            {
                io2.clear();
                io.processBlock (io2, midi);
                const auto n = std::min (block, count - done);
                for (int ch = 0; ch < 2; ++ch)
                    into.copyFrom (ch, from + done, io2, ch, 0, n);
            }
        };

        /** Exports `r` and plays the same range; they must null, sample for sample. */
        const auto nullsAgainstPlayback = [&] (const exporting::Range& r, const juce::String& what)
        {
            const auto file = folder.getChildFile ("Null " + what + ".wav");
            const auto rendered = exporting::render (*edit, r, { exporting::Format::wav32Float, rate }, file);
            expect (rendered.wasOk(), rendered.getErrorMessage());
            const auto reader = open (file);
            expect (reader != nullptr, what + ": the file reads back");

            if (reader == nullptr)
                return;

            const auto total = static_cast<int> (reader->lengthInSamples);
            juce::AudioBuffer<float> exported (2, total);
            exported.clear();
            reader->read (&exported, 0, total, 0, true, true);

            startPlaying (r.time.getStart().inSeconds());
            juce::AudioBuffer<float> played (2, total);
            pump (played, 0, total);
            transport.stop (false, false);

            // Compare, allowing for a playback latency, and say what it was. Playback that starts
            // right on a sound fades it in over a few dozen samples (Tracktion, against a click at
            // the start); an export does not, nor should it. So the comparison starts after that.
            static constexpr int startRamp = 128;   // static: MSVC calls a constexpr local read only in a lambda unused (C4189)
            const auto residualAt = [&] (int offset)
            {
                double sum = 0.0, signal = 0.0;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = std::max (startRamp, -offset); i < total - std::max (0, offset); ++i)
                    {
                        const auto d = static_cast<double> (played.getSample (ch, i + offset)) - exported.getSample (ch, i);
                        sum += d * d;
                        signal += static_cast<double> (exported.getSample (ch, i)) * exported.getSample (ch, i);
                    }
                return std::pair { sum, signal };
            };

            int bestOffset = 0;
            double best = 1.0e30, signal = 0.0;
            for (int offset = -2048; offset <= 2048; ++offset)
                if (const auto [res, sig] = residualAt (offset); res < best)
                {
                    best = res;
                    signal = sig;
                    bestOffset = offset;
                }

            const auto ratioDb = juce::Decibels::gainToDecibels (std::sqrt (best / std::max (signal, 1.0e-20)), -200.0);
            logMessage (what + " against playback: residual " + juce::String (ratioDb, 1) + " dB below the signal, playback offset "
                        + juce::String (bestOffset) + " samples, " + juce::String (total) + " samples long");
            expect (signal > 1.0, what + ": the export has sound in it");
            expect (ratioDb < -90.0, what + ": the export nulls against playback: " + juce::String (ratioDb, 1) + " dB");
            expectEquals (bestOffset, 0, what + ": and lines up with it sample for sample");
        };

        beginTest ("An export of the loop region nulls against the same range played back (phase 5 exit check)");
        nullsAgainstPlayback (range, "The loop region");

        beginTest ("So does an export of the whole project, from its first sound to its last (phase 5 exit check)");
        {
            editing::clearLoopRegion (*edit);
            const auto whole = exporting::rangeOf (*edit);
            expect (whole.has_value() && ! whole->isLoopRegion, "no loop region: the whole project");

            if (whole.has_value())
                nullsAgainstPlayback (*whole, "The whole project");
        }

        beginTest ("A clip re-routed to another channel while playing keeps sounding, now through that channel (phase 5 exit check)");
        {
            // Into channel 3 at -6 dB (and a send), then moved to channel 7 at 0 dB mid-way.
            constexpr int half = static_cast<int> (0.5 * rate);
            const auto rms = [] (const juce::AudioBuffer<float>& b, int from, int count)
            {
                double sum = 0.0;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = from; i < from + count; ++i)
                        sum += static_cast<double> (b.getSample (ch, i)) * b.getSample (ch, i);
                return std::sqrt (sum / (2.0 * count));
            };

            startPlaying (1.2);
            juce::AudioBuffer<float> played (2, 2 * half);
            pump (played, 0, half);

            editing::routeSample (*clip, channels[6]);
            edit->dispatchPendingUpdatesSynchronously();
            transport.ensureContextAllocated (true);   // the app's restartPlayback() does this on its next timer tick
            pump (played, half, half);
            transport.stop (false, false);

            int silentBlocks = 0;
            for (int at = half; at + block <= 2 * half; at += block)
                silentBlocks += rms (played, at, block) < 1.0e-4 ? 1 : 0;

            const auto before = rms (played, half - 8 * block, 8 * block);
            const auto after = rms (played, 2 * half - 8 * block, 8 * block);
            logMessage ("Re-routed while playing: " + juce::String (silentBlocks) + " silent blocks after the change; level "
                        + juce::String (juce::Decibels::gainToDecibels (after / std::max (before, 1.0e-9)), 1) + " dB against before");
            expect (before > 1.0e-3, "it sounded before the change");
            expect (after > 1.0e-3, "and still sounds after it");
            expect (silentBlocks <= 2, "with at most a block or two of quiet while the graph is rebuilt: " + juce::String (silentBlocks));
            expect (after > before * 1.4, "louder now, through a channel at 0 dB instead of -6 dB");
            editing::routeSample (*clip, channels[2]);
        }

        dm.deviceManager.closeAudioDevice();
        dm.removeHostedAudioDeviceInterface();

        edit.reset();
    }
};

static ExportTests exportTests;
