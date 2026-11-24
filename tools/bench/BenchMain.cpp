// carthage-bench: measures the engine on real hardware, through the same Session the app uses.
//
// It is how the phase reports' numbers are produced (docs/TESTING.md, "Measuring"), so that every
// figure can be reproduced with one command. It never touches the user's settings or projects: it
// works in a temporary folder unless --keep is given.
//
//   carthage-bench devices
//   carthage-bench plugins [--name SUBSTRING]
//   (every command takes --output-device NAME to listen on a different device than --device)
//   carthage-bench latency --device NAME --rate HZ --buffers 32,64,128 --out N --in N [--repeat 5]
//   carthage-bench soak    --device NAME --rate HZ --buffer N --input N [--stereo]
//                          [--plugin-format VST3|AudioUnit --plugin FILE_OR_ID] [--seconds 600] [--keep]
//   carthage-bench scanall [--parallel 4] [--timeout-s 60] [--folder DIR]
//                          (checks every AU and VST3 plugin out of process, as the app's first scan
//                          does, in a fresh cache; reports counts, time and what was blocked)
//   carthage-bench wgprobe --device NAME --rate HZ --buffer N
//                          (is the device's workgroup valid and joinable, before and after a restart)
//   carthage-bench takes   --device NAME --rate HZ --buffer N [--count 6] [--seconds 2] [--gap-ms 500]
//                          [--plugin-format VST3|AudioUnit --plugin FILE_OR_ID] [--while-playing]
//                          (records takes back to back the way a player does: R, play, R, R, ...;
//                          every other one after returning to the start; reports any that ended early)
//   carthage-bench presets --plugin-format VST3|AudioUnit --plugin FILE_OR_ID [--parameter N]
//                          (a real plugin's presets, W64: a setting changed, saved as a Carthage preset and
//                          in the plugin's own format (.vstpreset or .aupreset), changed again, then each
//                          loaded back through Carthage; the setting must return, and undo take it away)
//   carthage-bench projects
//                          (through the app's Session: save as, reopen, save, autosave after a pause, a
//                          crash left behind and its recovery; needs no particular device)
//   carthage-bench demo    --out FOLDER [--clips 300]
//                          (writes a project folder with that many clips over the eight lanes, for
//                          measuring the timeline's frame time; open it with File > Open or
//                          `open Carthage.app --args FOLDER/NAME.carthage`)
//   carthage-bench align   --device NAME --rate HZ --buffer N --in N [--seconds 10] [--click-out N] [--settle-ms N]
//                          [--measured-out N]
//
// Every command that opens the device prints how many threads have joined an audio workgroup,
// counted at os_workgroup_join (WorkgroupJoins.cpp).
//
// Channels are numbered from 1, as on the interface. `latency` and `align` need a cable from an
// output to an input. `align` plays Tracktion Engine's metronome on outputs 1-2, or on the output pair
// holding --click-out.

#include "WorkgroupJoins.h"
#include <engine/AudioFormats.h>

#include <engine/Editing.h>
#include <engine/Presets.h>
#include <engine/Session.h>
#include <engine/SettingsTest.h>
#include <engine/SampleEdits.h>

#include <iostream>
#include <map>

using namespace carthage;
using namespace juce;

namespace
{
    struct Args
    {
        String command;
        std::map<String, String> values;
        StringArray flags;

        String get (const String& key, const String& fallback = {}) const
        {
            const auto it = values.find (key);
            return it != values.end() ? it->second : fallback;
        }

        int getInt (const String& key, int fallback) const     { return values.contains (key) ? get (key).getIntValue() : fallback; }
        bool has (const String& key) const                     { return flags.contains (key) || values.contains (key); }
    };

    Args parse (const StringArray& argv)
    {
        Args a;
        a.command = argv[0];

        for (int i = 1; i < argv.size(); ++i)
        {
            if (! argv[i].startsWith ("--"))
                continue;

            const auto key = argv[i].substring (2);

            if (i + 1 < argv.size() && ! argv[i + 1].startsWith ("--"))
                a.values[key] = argv[++i];
            else
                a.flags.add (key);
        }

        return a;
    }

    void say (const String& s)    { std::cout << s << '\n'; }

    template <typename F>
    // callSync is empty only if the message loop has stopped; then .value() throwing is right.
    auto onMessageThread (F&& f)  { return MessageManager::callSync (std::forward<F> (f)).value(); }   // NOLINT(bugprone-unchecked-optional-access)

    template <typename F>
    void onMessageThreadVoid (F&& f) { MessageManager::callSync (std::forward<F> (f)); }

    /** Waits on the worker thread while the message loop runs. */
    void waitMs (int ms)   { Thread::sleep (ms); }

    //==============================================================================================
    class Bench
    {
    public:
        explicit Bench (Args a) : args (std::move (a)) {}

        int run()
        {
            folder = File::getSpecialLocation (File::tempDirectory).getChildFile ("carthage-bench")
                         .getNonexistentChildFile ("run", {}, false);

            onMessageThreadVoid ([this]
            {
                engine::Folders f;
                f.settings = folder.getChildFile ("Settings");
                f.pluginCache = folder.getChildFile ("Plugins");
                f.project = folder.getChildFile ("Project");
                f.autosave = folder.getChildFile ("Autosave");
                session = std::make_unique<engine::Session> (f, File::getSpecialLocation (File::currentExecutableFile)
                                                                     .getSiblingFile ("carthage-scan"));
            });

            waitMs (500);
            int result = 2;

            if (args.command == "devices")      result = devices();
            else if (args.command == "plugins") result = plugins();
            else if (args.command == "latency") result = latency();
            else if (args.command == "soak")    result = soak();
            else if (args.command == "align")   result = align();
            else if (args.command == "takes")   result = takes();
            else if (args.command == "wgprobe") result = wgprobe();
            else if (args.command == "scanall") result = scanall();
            else if (args.command == "projects") result = projects();
            else if (args.command == "demo")     result = demo();
            else if (args.command == "samples")  result = samples();
            else if (args.command == "settings-test") result = settingsTest();
            else if (args.command == "clip-envelope") result = clipEnvelope();
            else if (args.command == "clip-formant") result = clipFormant();
            else if (args.command == "overlap") result = overlap();
            else if (args.command == "input-undo") result = inputUndo();
            else if (args.command == "plugin-window") result = pluginWindow();
            else if (args.command == "carrier") result = carrier();
            else if (args.command == "idle-mixer") result = idleMixer();
            else if (args.command == "presets") result = presetRoundTrip();
            else say ("unknown command: " + args.command);

            onMessageThreadVoid ([this] { session.reset(); });

            if (! args.has ("keep"))
                folder.deleteRecursively();
            else
                say ("kept: " + folder.getFullPathName());

            return result;
        }

    private:
        Args args;
        File folder;
        std::unique_ptr<engine::Session> session;

        //==========================================================================================
        int checksFailed = 0;

        void check (bool ok, const String& what)
        {
            say ((ok ? "  ok    " : "  FAIL  ") + what);
            checksFailed += ok ? 0 : 1;
        }

        /** Waits for an async result posted to the message thread. */
        template <typename Start>
        Result await (Start&& start)
        {
            std::optional<Result> result;
            WaitableEvent done;
            onMessageThreadVoid ([&] { start ([&] (const Result& r) { result = r; done.signal(); }); });
            done.wait (10000);
            return result.value_or (Result::fail ("timed out"));
        }

        int clipCount()
        {
            return onMessageThread ([this]
            {
                return carthage::editing::allClips (session->getEdit()).size();
            });
        }

        /** samples --folder FOLDER: indexes a sample library, searches it, analyses some of it and
            starts the preview, timing each (phase 4b's exit check). */
        int samples()
        {
            const File libraryFolder (File::getCurrentWorkingDirectory().getChildFile (args.get ("folder", "samples")));

            if (! libraryFolder.isDirectory())
            {
                say ("no folder " + libraryFolder.getFullPathName());
                return 1;
            }

            auto& library = session->getSampleLibrary();
            auto t0 = Time::getMillisecondCounterHiRes();
            onMessageThreadVoid ([&] { library.addRoot (libraryFolder); library.indexNow(); });
            const auto count = onMessageThread ([&] { return static_cast<int> (library.getSamples().size()); });
            say ("indexed " + String (count) + " sounds in " + String (Time::getMillisecondCounterHiRes() - t0, 1) + " ms");
            check (count > 0, "sounds found");

            double slowest = 0.0;
            for (auto q : { "kick", "snare 12", "loop 128", "pck", "pack 7 bass", "zzz", "fx", "lead loop" })
            {
                const auto ms = onMessageThread ([&]
                {
                    const auto s = Time::getMillisecondCounterHiRes();
                    [[maybe_unused]] auto found = library.find (q, false);
                    return Time::getMillisecondCounterHiRes() - s;
                });
                slowest = std::max (slowest, ms);
            }
            say ("slowest search: " + String (slowest, 2) + " ms");
            check (slowest < 16.0, "every search within a frame (16 ms)");

            // Analysis, as the background analyser does it, on a spread of files.
            const auto all = onMessageThread ([&] { std::vector<File> f; for (auto& s : library.getSamples()) f.push_back (s.file); return f; });
            const auto n = std::min<size_t> (all.size(), 60);
            t0 = Time::getMillisecondCounterHiRes();
            int withTempo = 0, withKey = 0;
            for (size_t i = 0; i < n; ++i)
                if (auto a = carthage::engine::analysis::analyseFile (all[i * all.size() / n]))
                {
                    withTempo += a->bpm ? 1 : 0;
                    withKey += a->key.isNotEmpty() ? 1 : 0;
                }
            const auto perFile = (Time::getMillisecondCounterHiRes() - t0) / static_cast<double> (n);
            say ("analysis: " + String (perFile, 1) + " ms a file (" + String (static_cast<int> (n)) + " files, " + String (withTempo)
                 + " with a tempo, " + String (withKey) + " with a key or note); all " + String (count) + " in about "
                 + String (roundToInt (perFile * count / 1000.0)) + " s in the background");

            // The preview: from play() until the reader has the sound queued.
            t0 = Time::getMillisecondCounterHiRes();
            onMessageThreadVoid ([&] { session->getPreview().play (all.front()); });
            while (! onMessageThread ([this] { return session->getPreview().isPlaying(); }) && Time::getMillisecondCounterHiRes() - t0 < 2000.0)
                Thread::sleep (1);
            say ("preview ready in " + String (Time::getMillisecondCounterHiRes() - t0, 1) + " ms");
            check (onMessageThread ([this] { return session->getPreview().isPlaying(); }), "the preview plays");
            onMessageThreadVoid ([this] { session->getPreview().stop(); });

            say (checksFailed == 0 ? "PASSED" : "FAILED: " + String (checksFailed));
            return checksFailed == 0 ? 0 : 1;
        }

        int demo()
        {
            const File out (File::getCurrentWorkingDirectory().getChildFile (args.get ("out", "Demo")));
            const auto count = args.getInt ("clips", 300);

            if (out.exists() && ! out.isDirectory())
            {
                say (out.getFullPathName() + " is a file");
                return 1;
            }

            // A few takes of different lengths: a decaying chord, so the waveforms show shape.
            Array<File> takes;
            for (int t = 0; t < 4; ++t)
            {
                const auto file = carthage::project::recordingsIn (out).getChildFile ("Demo take " + String (t + 1) + ".wav");
                file.getParentDirectory().createDirectory();
                WavAudioFormat wav;
                std::unique_ptr<OutputStream> stream = std::make_unique<FileOutputStream> (file);
                auto w = wav.createWriterFor (stream, AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (16));
                const auto seconds = 1.0 + t * 0.75;
                AudioBuffer<float> b (1, static_cast<int> (48000 * seconds));

                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const auto x = static_cast<float> (i) / 48000.0f;
                    const auto env = std::exp (-x * (2.0f + static_cast<float> (t)));
                    b.setSample (0, i, 0.5f * env * (std::sin (x * 2.0f * MathConstants<float>::pi * 110.0f * static_cast<float> (t + 1))
                                                     + 0.5f * std::sin (x * 2.0f * MathConstants<float>::pi * 165.0f)));
                }

                w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
                takes.add (file);
            }

            const auto r = onMessageThread ([&]
            {
                const auto file = carthage::project::fileIn (out);
                auto edit = carthage::project::createEdit (session->getEngine(), file);
                const auto lanes = carthage::editing::lanes (*edit);
                const auto channels = carthage::editing::channels (*edit);
                Random random (42);
                std::array<double, 8> next {};

                for (int i = 0; i < count; ++i)
                {
                    const auto lane = i % 8;
                    const auto& take = takes[random.nextInt (takes.size())];
                    const auto length = 1.0 + random.nextInt (3) * 0.5;
                    next[static_cast<size_t> (lane)] += random.nextInt (3) * 0.5;
                    // Lane n plays through channel n, so the mixer has something to show.
                    tracktion::insertWaveClip (*carthage::editing::carrierFor (*edit, lanes[lane], channels[lane]), "Clip " + String (i + 1), take,
                                               { { tracktion::TimePosition::fromSeconds (next[static_cast<size_t> (lane)]), tracktion::TimeDuration::fromSeconds (length) } },
                                               tracktion::DeleteExistingClips::no);
                    next[static_cast<size_t> (lane)] += length;
                }

                carthage::editing::renameLane (*edit, lanes[0], "Guitar");

                return carthage::project::write (carthage::project::snapshot (*edit, file, carthage::project::Info::createNew()), file);
            });

            say (r.wasOk() ? "wrote " + carthage::project::fileIn (out).getFullPathName() + " with " + String (count) + " clips"
                           : r.getErrorMessage());
            return r.wasOk() ? 0 : 1;
        }

        /** The settings test (W41) on this computer's interface, as the app runs it: silent. */
        int settingsTest()
        {
            say ("Settings test on " + onMessageThread ([this] { return session->describeAudio(); }));
            WaitableEvent finished;
            engine::SettingsTest::Recommendation result;
            std::unique_ptr<engine::SettingsTest> test;

            onMessageThreadVoid ([&]
            {
                test = std::make_unique<engine::SettingsTest> (*session);
                test->start ([] (const String& line) { say ("  " + line); },
                             [&] (const engine::SettingsTest::Recommendation& r) { result = r; finished.signal(); });
            });

            finished.wait (120000);

            for (const auto& s : result.steps)
                say ("  " + String (s.bufferSize).paddedLeft (' ', 5) + " samples: " + String (s.dropouts) + " dropouts, worst block "
                     + String (roundToInt (s.worstLoad * 100.0f)) + " %" + (s.clean ? " clean" : ""));

            check (result.valid, "a recommendation: " + (result.valid ? String (result.bufferSize) + " samples, " + String (result.sampleRate, 0) + " Hz, "
                                                                          + String (result.parallelChecks) + " checks at once" : result.reason));
            check (onMessageThread ([this] { return session->describeAudio(); }).isNotEmpty(), "the device is back as it was: "
                   + onMessageThread ([this] { return session->describeAudio(); }));
            onMessageThreadVoid ([&] { test.reset(); });
            say (checksFailed == 0 ? "PASSED" : "FAILED: " + String (checksFailed));
            return checksFailed == 0 ? 0 : 1;
        }

        /** The sample editor's envelope (4e), rendered: a 1 s tone at 4 s with an envelope at unity
            for its first half and silent after; then the same clip moved by a second. The render
            runs on this thread while the message loop runs (Tracktion's renderer needs it). */
        int clipEnvelope()
        {
            const auto tone = folder.getChildFile ("tone.wav");
            {
                WavAudioFormat wav;
                std::unique_ptr<OutputStream> out = std::make_unique<FileOutputStream> (tone);
                auto writer = wav.createWriterFor (out, AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
                AudioBuffer<float> b (1, 48000);
                for (int i = 0; i < b.getNumSamples(); ++i)
                    b.setSample (0, i, 0.25f * std::sin (static_cast<float> (i) * 0.05f));
                writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }

            tracktion::Clip* clip = nullptr;
            BigInteger mask;
            onMessageThreadVoid ([&]
            {
                auto& edit = session->getEdit();
                clip = carthage::editing::insertFile (edit, carthage::editing::lanes (edit)[1], tone, 8.0);   // beat 8 at 120 bpm: 4 s
                mask.setRange (0, tracktion::getAllTracks (edit).size(), true);   // the whole mix: lanes play through channels and the Pre-Master
            });

            const auto render = [&] (const String& name)
            {
                const auto out = folder.getChildFile (name + ".wav");
                waitMs (300);
                tracktion::Renderer::renderToFile (name, out, session->getEdit(), { tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (7.0) },
                                                   mask, true, true, {}, false);
                AudioFormatManager formats;
                carthage::audio::registerReadable (formats);
                std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (out));
                return [r = std::shared_ptr<AudioFormatReader> (std::move (reader))] (double at)
                {
                    if (r == nullptr) return -1.0f;
                    AudioBuffer<float> b (1, 4800);
                    r->read (&b, 0, 4800, static_cast<int64> (at * r->sampleRate), true, false);
                    return b.getRMSLevel (0, 0, 4800);
                };
            };

            const auto plain = render ("plain");
            onMessageThreadVoid ([&]
            {
                carthage::editing::begin (session->getEdit(), "Envelope");
                carthage::editing::setEnvelope (*clip, { { 0.0, 0.0f, 0.0f }, { 0.5, 0.0f, 0.0f }, { 0.51, -60.0f, 0.0f } });
            });
            const auto shaped = render ("shaped");
            say ("RMS without envelope at 4.25 s " + String (plain (4.25), 4) + "; with it at 4.25 s " + String (shaped (4.25), 4)
                 + " and 4.75 s " + String (shaped (4.75), 4));
            check (std::abs (shaped (4.25) - plain (4.25)) < 0.01f, "at unity it is as loud as without an envelope");
            check (shaped (4.75) < 0.001f, "silent after the envelope drops");

            onMessageThreadVoid ([&]
            {
                carthage::editing::begin (session->getEdit(), "Move clip");
                carthage::editing::move ({ clip }, 2.0, 0);   // one second later; the session re-syncs it
                session->getEdit().getUndoManager().dispatchPendingMessages();
            });
            const auto moved = render ("moved");
            say ("Moved by 1 s: RMS at 5.25 s " + String (moved (5.25), 4) + ", at 5.75 s " + String (moved (5.75), 4));
            check (moved (5.25) > 0.05f && moved (5.75) < 0.001f, "the envelope moves with the clip");
            say (checksFailed == 0 ? "PASSED" : "FAILED: " + String (checksFailed));
            return checksFailed == 0 ? 0 : 1;
        }

        /** Which input calls write into the undo history (computer-use test: unnamed "Change" steps). */
        int inputUndo()
        {
            const auto probe = [this] (const String& what, std::function<void()> f)
            {
                const auto before = onMessageThread ([this] { auto& um = session->getEdit().getUndoManager(); um.clearUndoHistory(); return um.canUndo(); });
                onMessageThreadVoid ([&] { f(); });
                waitMs (300);
                const auto after = onMessageThread ([this] { auto& um = session->getEdit().getUndoManager(); return um.canUndo() ? "UNDO STEP '" + um.getUndoDescription() + "'" : String ("no undo step"); });
                say (what + ": " + after + (before ? " (history was not empty)" : ""));
            };

            auto* ch = onMessageThread ([this] { return carthage::editing::channels (session->getEdit())[3]; });
            probe ("setChannelInput", [this, ch] { session->setChannelInput (*ch, { 0, false }); });
            probe ("setChannelMonitoring on", [this, ch] { session->setChannelMonitoring (*ch, true); });
            probe ("setRecordChannel", [this, ch] { session->setRecordChannel (*ch); });
            probe ("setChannelInput none", [this, ch] { session->setChannelInput (*ch, {}); });
            probe ("syncMix", [this] { carthage::editing::syncMix (session->getEdit(), &session->getTrack()); });
            return 0;
        }

        /** A plugin's window stays open (owner, 2026-11-20: "it flashes and disappears"): inserts
            the plugin as the app does, opens its editor in a window, and checks for a few seconds
            that the plugin instance and its editor are still the same ones. */
        int pluginWindow()
        {
            if (insertPlugin() != 0)
                return 1;

            // --old-project: the same, but in a project saved before 5a, where the input plugin sat
            // on the first lane's track and opening moves it to channel 1.
            if (args.has ("old-project"))
            {
                const auto r = onMessageThread ([this]
                {
                    const auto file = carthage::project::fileIn (folder.getChildFile ("Old"));
                    auto old = tracktion::createEmptyEdit (session->getEngine(), file);
                    old->ensureNumberOfAudioTracks (2);
                    auto* first = tracktion::getAudioTracks (*old).getFirst();

                    if (auto* plugin = session->getInsertedPlugin())
                        first->pluginList.insertPlugin (plugin->state.createCopy(), 0);

                    return carthage::project::write (carthage::project::snapshot (*old, file, carthage::project::Info::createNew()), file);
                });
                check (r.wasOk(), "an old project written");
                const auto opened = onMessageThread ([this] { return session->openProject (carthage::project::fileIn (folder.getChildFile ("Old"))); });
                check (opened.wasOk(), "and opened: " + opened.getErrorMessage());
                waitMs (2000);
                say ("old project open; its plugin: " + onMessageThread ([this] { auto* p = session->getInsertedPlugin(); return p != nullptr ? p->getName() : String ("none"); }));
            }

            std::unique_ptr<DocumentWindow> window;
            AudioPluginInstance* first = nullptr;
            onMessageThreadVoid ([&]
            {
                if (auto* ext = dynamic_cast<tracktion::ExternalPlugin*> (session->getInsertedPlugin()))
                    first = ext->getAudioPluginInstance();

                if (first != nullptr)
                    {
                        window = std::make_unique<DocumentWindow> ("bench", Colours::black, 0);
                        window->setContentOwned (first->createEditorAndMakeActive(), true);
                        window->setVisible (true);
                    }
            });
            check (first != nullptr, "the plugin has an instance to open");

            for (int i = 0; i < 6; ++i)
            {
                waitMs (500);
                const auto state = onMessageThread ([&]
                {
                    auto* ext = dynamic_cast<tracktion::ExternalPlugin*> (session->getInsertedPlugin());
                    auto* now = ext != nullptr ? ext->getAudioPluginInstance() : nullptr;
                    return String (now == first ? "same instance" : now == nullptr ? "no instance" : "a NEW instance")
                         + ", editor " + (window != nullptr && window->getContentComponent() != nullptr ? "open" : "gone")
                         + ", channel processing " + (session->getTrack().isProcessing (false) ? "on" : "off")
                         + ", plugin processing " + (ext != nullptr && ext->isProcessingEnabled() ? "on" : "off");
                });
                say ("  " + String ((i + 1) * 0.5, 1) + " s: " + state);
                check (state.startsWith ("same instance"), "the plugin is not reloaded under its window");
            }

            onMessageThreadVoid ([&] { window.reset(); });
            say (checksFailed == 0 ? "PASSED" : "FAILED: " + String (checksFailed));
            return checksFailed == 0 ? 0 : 1;
        }

        /** Phase 5a's footing (W47): once lanes are only where clips are drawn, clips of different
            lanes share a channel's track and may overlap on it. Two identical tones at the same
            time on one track must both play: the render is twice as loud as one. */
        int overlap()
        {
            const auto tone = folder.getChildFile ("tone.wav");
            {
                WavAudioFormat wav;
                std::unique_ptr<OutputStream> out = std::make_unique<FileOutputStream> (tone);
                auto writer = wav.createWriterFor (out, AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
                AudioBuffer<float> b (1, 48000);
                for (int i = 0; i < b.getNumSamples(); ++i)
                    b.setSample (0, i, 0.2f * std::sin (static_cast<float> (i) * 0.05f));
                writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }

            BigInteger mask;
            tracktion::AudioTrack* track = nullptr;
            onMessageThreadVoid ([&]
            {
                auto& edit = session->getEdit();
                track = dynamic_cast<tracktion::AudioTrack*> (carthage::editing::insertFile (edit, carthage::editing::lanes (edit)[1], tone, 8.0)->getTrack());   // 4 s at 120 bpm
                mask.setRange (0, tracktion::getAllTracks (edit).size(), true);   // the whole mix: lanes play through channels and the Pre-Master
            });

            const auto render = [&] (const String& name)
            {
                const auto out = folder.getChildFile (name + ".wav");
                waitMs (300);
                tracktion::Renderer::renderToFile (name, out, session->getEdit(), { tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (6.0) },
                                                   mask, true, true, {}, false);
                AudioFormatManager formats;
                carthage::audio::registerReadable (formats);
                std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (out));
                AudioBuffer<float> b (1, 4800);
                if (reader != nullptr)
                    reader->read (&b, 0, 4800, static_cast<int64> (4.5 * reader->sampleRate), true, false);
                return reader != nullptr ? b.getRMSLevel (0, 0, 4800) : -1.0f;
            };

            const auto one = render ("one");
            onMessageThreadVoid ([&] { carthage::editing::insertFile (session->getEdit(), carthage::editing::lanes (session->getEdit())[1], tone, 8.0); });   // the same tone, the same time, the same track
            const auto clips = onMessageThread ([&] { return track->getClips().size(); });
            const auto two = render ("two");
            say ("RMS at 4.5 s: one clip " + String (one, 4) + ", two overlapping clips on one track " + String (two, 4)
                 + " (" + String (clips) + " clips on the track)");
            check (clips == 2, "both clips are on the track");
            check (one > 0.05f && std::abs (two / one - 2.0f) < 0.05f, "both play: twice as loud");
            say (checksFailed == 0 ? "PASSED" : "FAILED: " + String (checksFailed));
            return checksFailed == 0 ? 0 : 1;
        }

        /** Phase 5a's routing (W47): a lane's clips sit on a hidden carrier track that outputs into
            its channel's track, where the channel's fader works. A tone through a carrier into a
            channel at -6.02 dB renders at half the level it has straight to master. */
        int carrier()
        {
            const auto tone = folder.getChildFile ("tone.wav");
            {
                WavAudioFormat wav;
                std::unique_ptr<OutputStream> out = std::make_unique<FileOutputStream> (tone);
                auto writer = wav.createWriterFor (out, AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
                AudioBuffer<float> b (1, 48000);
                for (int i = 0; i < b.getNumSamples(); ++i)
                    b.setSample (0, i, 0.2f * std::sin (static_cast<float> (i) * 0.05f));
                writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }

            tracktion::AudioTrack* carrierTrack = nullptr;
            tracktion::AudioTrack* channel = nullptr;
            onMessageThreadVoid ([&]
            {
                auto& edit = session->getEdit();
                carrierTrack = dynamic_cast<tracktion::AudioTrack*> (carthage::editing::insertFile (edit, carthage::editing::lanes (edit)[1], tone, 8.0)->getTrack());
                channel = carthage::editing::channels (edit)[2];
                carthage::editing::syncMix (edit, nullptr);
            });

            const auto render = [&] (const String& name)
            {
                const auto out = folder.getChildFile (name + ".wav");
                waitMs (300);
                BigInteger mask;
                onMessageThreadVoid ([&] { mask.setRange (0, tracktion::getAllTracks (session->getEdit()).size(), true); });
                tracktion::Renderer::renderToFile (name, out, session->getEdit(), { tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (6.0) },
                                                   mask, true, true, {}, false);
                AudioFormatManager formats;
                carthage::audio::registerReadable (formats);
                std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (out));
                AudioBuffer<float> b (2, 4800);
                if (reader != nullptr)
                    reader->read (&b, 0, 4800, static_cast<int64> (4.5 * reader->sampleRate), true, true);
                return reader != nullptr ? b.getRMSLevel (0, 0, 4800) : -1.0f;
            };

            const auto direct = render ("direct");
            onMessageThreadVoid ([&]
            {
                carrierTrack->getOutput().setOutputToTrack (channel);
                carthage::editing::faderOf (*channel)->setVolumeDb (-6.0206f);
                carthage::editing::syncMix (session->getEdit(), nullptr);   // the channel now has something to play
            });
            const auto routed = render ("routed");
            const auto feeds = onMessageThread ([&] { return carrierTrack->getOutput().feedsInto (channel); });
            say ("RMS at 4.5 s: straight to master " + String (direct, 4) + ", through a channel at -6.02 dB " + String (routed, 4));
            check (feeds, "the carrier feeds the channel");
            check (direct > 0.05f && std::abs (routed / direct - 0.5f) < 0.02f, "the channel's fader acts on the carrier's clips");

            // Sends (W51): the channel also plays into another, after its fader.
            tracktion::AudioTrack* other = nullptr;
            const auto sent = onMessageThread ([&]
            {
                other = carthage::editing::channels (session->getEdit())[5];
                carthage::editing::begin (session->getEdit(), "Send");
                const auto r = carthage::editing::addSend (*channel, *other, 0.0f);
                carthage::editing::syncMix (session->getEdit(), nullptr);
                return r.wasOk();
            });
            const auto both = render ("send");
            onMessageThreadVoid ([&]
            {
                carthage::editing::setPlaysToOutput (*channel, false);
                carthage::editing::syncMix (session->getEdit(), nullptr);
            });
            const auto onlySend = render ("send-only");
            say ("With a send to another channel: " + String (both, 4) + "; main output off, the send alone: " + String (onlySend, 4));
            check (sent, "the send is made");
            check (std::abs (both / routed - 2.0f) < 0.05f, "the send adds a second copy after the fader");
            check (std::abs (onlySend / routed - 1.0f) < 0.05f, "with the main output off, only the send plays");
            check (! carthage::editing::addSend (*other, *channel).wasOk(), "a send back would loop: refused");
            say (checksFailed == 0 ? "PASSED" : "FAILED: " + String (checksFailed));
            return checksFailed == 0 ? 0 : 1;
        }

        /** A real plugin's presets (W64): save, change, load back, undo. Needs no audio device. */
        int presetRoundTrip()
        {
            // A VST3 hands a changed setting to its audio side only while it processes audio: with
            // --device the plugin runs, as it does in the app, and its saved state is current.
            if (args.has ("device") && ! openDevice (args.getInt ("buffer", 256)))
                return 1;

            if (insertPlugin() != 0)
                return 1;

            if (args.has ("play"))   // the plugin processed all along, as while a project plays
                onMessageThreadVoid ([this] { session->getEdit().getTransport().play (false); });

            const auto index = args.getInt ("parameter", 0);
            int failures = 0;
            const auto check = [&] (bool ok, const String& what)
            {
                say (String (ok ? "PASS " : "FAIL ") + what);
                failures += ok ? 0 : 1;
            };

            // The plugin's parameter `index`, read and written through Tracktion's parameter for it,
            // as the host does: set behind Tracktion's back, an Audio Unit's value was pushed back
            // to Tracktion's and its saved state never held it; read from JUCE's, an AU gives a
            // cached value, not the unit's.
            const auto paramOf = [index] (tracktion::Plugin* p) -> tracktion::AutomatableParameter*
            {
                auto* ext = dynamic_cast<tracktion::ExternalPlugin*> (p);
                auto* instance = ext != nullptr ? ext->getAudioPluginInstance() : nullptr;

                if (instance == nullptr || ! isPositiveAndBelow (index, instance->getParameters().size()))
                    return nullptr;

                auto* hosted = dynamic_cast<HostedAudioProcessorParameter*> (instance->getParameters()[index]);

                for (auto* param : ext->getAutomatableParameters())
                    if (hosted != nullptr && param->paramID == hosted->getParameterID())
                        return param;

                return nullptr;
            };
            const auto valueOf = [paramOf] (tracktion::Plugin* p) -> float
            {
                auto* param = paramOf (p);
                return param != nullptr ? param->getCurrentValue() : -1.0f;
            };
            const auto setValue = [paramOf] (tracktion::Plugin* p, float v)
            {
                if (auto* param = paramOf (p))
                    param->setParameter (v, sendNotification);
            };

            auto* first = onMessageThread ([this] { return session->getInsertedPlugin(); });
            auto* list = first != nullptr ? first->getOwnerList() : nullptr;
            auto* ext = dynamic_cast<tracktion::ExternalPlugin*> (first);

            if (ext == nullptr || list == nullptr || ext->getAudioPluginInstance() == nullptr)
            {
                say ("FAIL no plugin instance to try presets on");
                return 1;
            }

            const auto name = onMessageThread ([&] { return ext->getAudioPluginInstance()->getParameters()[index]->getName (64); });
            say ("parameter " + String (index) + ": " + name);

            const auto saved = 0.23f, changed = 0.81f;
            Array<File> files;

            onMessageThreadVoid ([&] { setValue (first, saved); });
            waitMs (500);   // processed, if the device runs

            onMessageThreadVoid ([&]
            {
                auto* instance = ext->getAudioPluginInstance();
                const auto own = folder.getChildFile (String ("Saved") + presets::extension);
                check (presets::save (*first, own).wasOk(), "saved as a Carthage preset");
                files.add (own);

                // In the plugin's own format, as the plugin itself writes it.
                if (ext->isVST3())
                {
                    if (auto* vst3 = instance->getVST3Client())
                    {
                        const auto f = folder.getChildFile ("Saved.vstpreset");
                        const auto data = vst3->getPreset();
                        check (! data.isEmpty() && f.replaceWithData (data.getData(), data.getSize()), "the plugin wrote a .vstpreset");
                        files.add (f);
                    }
                }
                else if (ext->isAU())
                {
                    MemoryBlock data;
                    instance->getStateInformation (data);   // an AU's state is its class info: an .aupreset's content
                    const auto f = folder.getChildFile ("Saved.aupreset");
                    check (! data.isEmpty() && f.replaceWithData (data.getData(), data.getSize()), "the plugin wrote its state as an .aupreset");
                    files.add (f);
                }
            });

            for (const auto& f : files)
            {
                onMessageThreadVoid ([&] { setValue (editing::insertsOf (*list).getLast(), changed); });
                waitMs (500);

                // Load, then read after a moment, undo, then read again: an Audio Unit's parameter
                // values reach JUCE's host through the unit's notifications, a little later.
                const auto r = onMessageThread ([&]
                {
                    auto* current = editing::insertsOf (*list).getLast();
                    editing::begin (session->getEdit(), "Load preset");
                    return presets::load (*current, f).second;
                });
                waitMs (500);
                const auto after = onMessageThread ([&] { return valueOf (editing::insertsOf (*list).getLast()); });

                // For an Audio Unit, what the unit itself holds, against the .aupreset written from
                // the setting (Tracktion's view of an AU's parameter can lag the unit, see above).
                if (const auto au = folder.getChildFile ("Saved.aupreset"); au.existsAsFile())
                {
                    const auto holds = onMessageThread ([&]
                    {
                        MemoryBlock now, saved;
                        auto* e = dynamic_cast<tracktion::ExternalPlugin*> (editing::insertsOf (*list).getLast());
                        if (e != nullptr && e->getAudioPluginInstance() != nullptr)
                            e->getAudioPluginInstance()->getStateInformation (now);
                        au.loadFileAsData (saved);
                        return now == saved;
                    });
                    check (holds, f.getFileName() + ": the unit itself holds the saved setting");
                }
                onMessageThreadVoid ([&] { editing::undo (session->getEdit()); });
                waitMs (500);
                const auto undone = onMessageThread ([&] { return valueOf (editing::insertsOf (*list).getLast()); });
                check (r.wasOk(), f.getFileName() + " loaded" + (r.failed() ? ": " + r.getErrorMessage() : String()));
                // An Audio Unit takes a state whole without reporting each parameter, so Tracktion's
                // view of it lags (W64); what counts is what the unit holds, checked above.
                if (ext->isAU())
                    say ("  " + f.getFileName() + ": Tracktion's view of the setting reads " + String (after, 3) + " (an AU does not report a whole-state load)");
                else
                    check (std::abs (after - saved) < 0.01f, f.getFileName() + ": the setting is back to " + String (saved, 2) + " (" + String (after, 3) + ")");
                check (std::abs (undone - changed) < 0.01f, f.getFileName() + ": undo takes it back to " + String (changed, 2) + " (" + String (undone, 3) + ")");
            }

            say ("RESULT presets: " + String (failures) + " failed");
            return failures == 0 ? 0 : 1;
        }

        /** What 50 empty channels cost (5a exit check): the audio thread's load playing an empty
            project with its 8 tracks, then with 58 (the default mixer's 50 plus the lanes'). */
        int idleMixer()
        {
            if (! openDevice (args.getInt ("buffer", 128)))
                return 1;

            const auto measure = [this] (const String& what)
            {
                onMessageThreadVoid ([this] { session->getEdit().getTransport().play (false); });
                waitMs (1500);
                onMessageThreadVoid ([this] { session->getCallback().takeLoad(); });
                double sum = 0.0;
                float worst = 0.0f;
                for (int i = 0; i < 10; ++i)
                {
                    waitMs (1000);
                    const auto load = onMessageThread ([this] { return session->getCallback().takeLoad(); });
                    sum += load.average;
                    worst = std::max (worst, load.peak);
                }
                onMessageThreadVoid ([this] { session->getEdit().getTransport().stop (false, false); });
                say (what + ": audio-thread load " + String (sum / 10.0 * 100.0, 2) + "% average, " + String (worst * 100.0f, 2) + "% worst block");
                return sum / 10.0;
            };

            const auto tracks = onMessageThread ([this] { return tracktion::getAudioTracks (session->getEdit()).size(); });
            const auto before = measure (String (tracks) + " tracks, playing, empty");
            onMessageThreadVoid ([this, tracks] { session->getEdit().ensureNumberOfAudioTracks (tracks + 50); });
            waitMs (1000);
            const auto after = measure (String (tracks + 50) + " tracks, playing, empty");
            // 5a's rule: a channel with nothing that could sound through it is left out of the graph.
            onMessageThreadVoid ([this, tracks]
            {
                const auto all = tracktion::getAudioTracks (session->getEdit());
                for (int i = tracks; i < all.size(); ++i)
                    all[i]->setProcessing (false);
            });
            waitMs (1000);
            const auto skipped = measure (String (tracks + 50) + " tracks, the 50 empty ones left out of the graph");
            say ("RESULT idle-mixer: 50 more empty tracks add " + String ((after - before) * 100.0, 2) + " points of audio-thread load; left out of the graph, "
                 + String ((skipped - before) * 100.0, 2) + " points");
            return 0;
        }

        /** The sample editor's formant shift (4e), rendered: the same tone plain and shifted 6
            semitones up. The pitch must not move, and the sound must still start with the clip
            (the plugin's latency compensated). */
        int clipFormant()
        {
            const auto tone = folder.getChildFile ("tone.wav");
            {
                WavAudioFormat wav;
                std::unique_ptr<OutputStream> out = std::make_unique<FileOutputStream> (tone);
                auto writer = wav.createWriterFor (out, AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (24));
                AudioBuffer<float> b (1, 96000);
                for (int i = 0; i < b.getNumSamples(); ++i)   // a sawtooth-ish tone at 220 Hz: harmonics to colour
                    b.setSample (0, i, 0.2f * static_cast<float> (std::sin (i * 2.0 * MathConstants<double>::pi * 220.0 / 48000.0)
                                                                + 0.5 * std::sin (i * 2.0 * MathConstants<double>::pi * 440.0 / 48000.0)
                                                                + 0.33 * std::sin (i * 2.0 * MathConstants<double>::pi * 660.0 / 48000.0)));
                writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }

            tracktion::Clip* clip = nullptr;
            BigInteger mask;
            onMessageThreadVoid ([&]
            {
                auto& edit = session->getEdit();
                clip = carthage::editing::insertFile (edit, carthage::editing::lanes (edit)[1], tone, 8.0);   // 4 s
                mask.setRange (0, tracktion::getAllTracks (edit).size(), true);   // the whole mix: lanes play through channels and the Pre-Master
            });

            const auto render = [&] (const String& name)
            {
                const auto out = folder.getChildFile (name + ".wav");
                waitMs (300);
                tracktion::Renderer::renderToFile (name, out, session->getEdit(), { tracktion::TimePosition(), tracktion::TimePosition::fromSeconds (7.0) },
                                                   mask, true, true, {}, false);
                AudioFormatManager formats;
                carthage::audio::registerReadable (formats);
                std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (out));
                AudioBuffer<float> all (1, reader != nullptr ? static_cast<int> (reader->lengthInSamples) : 0);
                if (reader != nullptr)
                    reader->read (&all, 0, all.getNumSamples(), 0, true, false);
                const auto sampleRate = reader != nullptr ? reader->sampleRate : 48000.0;
                return std::make_pair (std::move (all), sampleRate);
            };

            // Where it starts (first sample above a fraction of the peak) and its pitch (zero
            // crossings in half a second of its middle).
            const auto measure = [] (const AudioBuffer<float>& b, double rate)
            {
                const auto* d = b.getReadPointer (0);
                const auto peak = b.getMagnitude (0, 0, b.getNumSamples());
                int onset = -1;
                for (int i = 0; i < b.getNumSamples() && onset < 0; ++i)
                    if (std::abs (d[i]) > peak * 0.1f) onset = i;

                const auto from = static_cast<int> (4.5 * rate), to = static_cast<int> (5.0 * rate);
                int crossings = 0;
                for (int i = from + 1; i < to && i < b.getNumSamples(); ++i)
                    if ((d[i - 1] < 0.0f) != (d[i] < 0.0f)) ++crossings;

                return std::make_pair (onset / rate, crossings / 2.0 / 0.5);
            };

            const auto [plain, plainRate] = render ("plain");
            onMessageThreadVoid ([&]
            {
                carthage::editing::begin (session->getEdit(), "Formant");
                auto s = carthage::editing::soundOf (*clip);
                s.formant = 6.0f;
                carthage::editing::setSound (*clip, s);
            });
            const auto [shifted, shiftedRate] = render ("shifted");

            const auto [plainStart, plainHz] = measure (plain, plainRate);
            const auto [shiftedStart, shiftedHz] = measure (shifted, shiftedRate);
            say ("Plain: starts " + String (plainStart, 4) + " s, " + String (plainHz, 1) + " Hz by crossings; shifted: starts "
                 + String (shiftedStart, 4) + " s, " + String (shiftedHz, 1) + " Hz");
            check (std::abs (shiftedStart - plainStart) < 0.005, "starts with the clip (latency compensated, within 5 ms)");
            check (std::abs (shiftedHz - plainHz) / plainHz < 0.03, "the pitch does not move (within 3 %)");

            // And it changed the sound: the shifted render differs from the plain one.
            double diff = 0.0;
            for (int i = static_cast<int> (4.4 * plainRate); i < static_cast<int> (4.6 * plainRate) && i < shifted.getNumSamples(); ++i)
                diff += std::abs (shifted.getSample (0, i) - plain.getSample (0, i));
            check (diff > 1.0, "and the colour changed (" + String (diff, 1) + ")");
            say (checksFailed == 0 ? "PASSED" : "FAILED: " + String (checksFailed));
            return checksFailed == 0 ? 0 : 1;
        }

        int projects()
        {
            const auto tone = folder.getChildFile ("Elsewhere/tone.wav");
            onMessageThreadVoid ([&]
            {
                tone.getParentDirectory().createDirectory();
                WavAudioFormat wav;
                std::unique_ptr<OutputStream> stream = std::make_unique<FileOutputStream> (tone);
                auto w = wav.createWriterFor (stream, AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (16));
                AudioBuffer<float> b (1, 48000);
                b.clear();
                w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            });

            say ("A new session is untitled and unchanged");
            check (onMessageThread ([this] { return session->isUntitled() && ! session->hasUnsavedChanges(); }), "untitled, nothing unsaved");
            waitMs (600);   // Tracktion clears its changed flag after loading
            check (! onMessageThread ([this] { return session->hasUnsavedChanges(); }), "still nothing unsaved after the engine settles");

            say ("An edit makes it unsaved; Save as writes the folder and clears it");
            onMessageThreadVoid ([&]
            {
                auto& edit = session->getEdit();
                carthage::editing::begin (edit, "Add clip");
                tracktion::insertWaveClip (*tracktion::getAudioTracks (edit)[2], "tone", tone,
                                           { { tracktion::TimePosition::fromSeconds (2.0), tracktion::TimeDuration::fromSeconds (1.0) } },
                                           tracktion::DeleteExistingClips::no);
                edit.getUndoManager().dispatchPendingMessages();
            });
            check (onMessageThread ([this] { return session->hasUnsavedChanges(); }), "unsaved after an edit");

            const auto song = folder.getChildFile ("Projects/Song");
            auto r = await ([&] (auto done) { session->saveAs (song, done); });
            check (r.wasOk(), "save as: " + (r.wasOk() ? String ("written") : r.getErrorMessage()));
            check (song.getChildFile ("Song.carthage").existsAsFile(), "Song/Song.carthage exists");
            check (onMessageThread ([this] { return ! session->isUntitled() && ! session->hasUnsavedChanges() && session->getProjectName() == "Song"; }),
                   "now named Song, nothing unsaved");
            check (onMessageThread ([this, song] { return session->getRecentProjects().contains (carthage::project::fileIn (song)); }),
                   "Song is a recent project (the start screen)");

            say ("Open it again: the clip comes back");
            onMessageThreadVoid ([this] { session->newProject (folder.getChildFile ("Projects/Untitled")); });
            check (onMessageThread ([this] { return session->getNumProjects(); }) == 2, "a new project opens in a second tab");
            check (clipCount() == 0, "a new project is empty");
            check (onMessageThread ([&] { return session->openProject (song).wasOk() && session->getFrontIndex() == 0; }),
                   "opening Song again brings its tab forward");
            onMessageThreadVoid ([this] { session->closeProject(); });
            say ("Tabs: switching is quick and is not an edit; only the front one has a graph");
            waitMs (600);
            onMessageThreadVoid ([&] { session->openProject (song); });
            waitMs (600);
            onMessageThreadVoid ([this] { session->newProject (folder.getChildFile ("Projects/Other")); });
            waitMs (600);
            check (onMessageThread ([this] { return session->getNumProjects() == 2 && session->getFrontIndex() == 1; }),
                   "Song and a new project, side by side");
            double slowest = 0.0;

            for (int i = 0; i < 10; ++i)
            {
                const auto ms = onMessageThread ([this, i]
                {
                    const auto t0 = Time::getMillisecondCounterHiRes();
                    session->showProject (i % 2);
                    return Time::getMillisecondCounterHiRes() - t0;
                });
                slowest = std::max (slowest, ms);
            }

            say ("  slowest of 10 switches: " + String (slowest, 1) + " ms");
            check (slowest < 100.0, "each switch under 100 ms");
            waitMs (600);
            check (onMessageThread ([this]
                   {
                       auto background = [this] (int i) { return session->describeProject (i); };
                       auto& front = session->getEdit();
                       return ! background (0).unsaved && ! background (1).unsaved && ! front.getUndoManager().canUndo()
                               && front.getTransport().getCurrentPlaybackContext() != nullptr;
                   }),
                   "nothing unsaved or undoable after switching; the front one has a graph");

            say ("Copy in one tab, paste in another");
            const auto copied = onMessageThread ([this]
            {
                session->showProject (0);   // Song
                editing::Clips all;
                for (auto* c : editing::allClips (session->getEdit()))
                    all.add (c);
                return editing::copy (all);
            });
            check (copied->count() == 1, "Song's clip copied");
            const auto pasted = onMessageThread ([this, copied]
            {
                session->showProject (1);   // Other
                auto& edit = session->getEdit();
                editing::begin (edit, "Paste clip");
                const auto added = editing::paste (edit, *copied, 4.0);
                edit.getUndoManager().dispatchPendingMessages();
                return added.size() == 1 && dynamic_cast<tracktion::AudioClipBase*> (added[0])->getAudioFile().getFile().existsAsFile()
                        && editing::laneOf (*added[0]) == 2;
            });
            check (pasted, "it lands in Other on the same lane and finds its audio in Song's folder");
            check (onMessageThread ([this] { return session->describeProject (1).unsaved && ! session->describeProject (0).unsaved; }),
                   "Other is unsaved, Song untouched");

            say ("Collect samples into Other");
            const auto collected = await ([this] (auto done)
            {
                session->collectSamples ([done] (const Result& r, int n)
                {
                    done (r.failed() ? r : n == 1 ? Result::ok() : Result::fail ("copied " + String (n)));
                });
            });
            check (collected.wasOk(), "one sample copied: " + (collected.wasOk() ? String ("yes") : collected.getErrorMessage()));
            check (onMessageThread ([this]
                   {
                       const auto samples = carthage::project::samplesIn (session->getProjectFolder());
                       for (auto* c : editing::allClips (session->getEdit()))
                               if (auto* audio = dynamic_cast<tracktion::AudioClipBase*> (c))
                                   return audio->getOriginalFile().isAChildOf (samples) && audio->getOriginalFile().existsAsFile();
                       return false;
                   }),
                   "the clip now plays the copy in Other's Samples folder");
            onMessageThreadVoid ([this] { auto& um = session->getEdit().getUndoManager(); um.undo(); um.undo(); });
            onMessageThreadVoid ([this] { session->showProject (1); session->closeProject(); });   // Other
            onMessageThreadVoid ([this] { session->newProject (folder.getChildFile ("Projects/Untitled 2")); });
            onMessageThreadVoid ([this] { session->showProject (0); session->closeProject(); });   // Song
            check (onMessageThread ([this] { return session->getNumProjects() == 1 && session->isUntitled(); }),
                   "closing Song leaves the new project");
            waitMs (600);
            check (onMessageThread ([&] { return session->openProject (song).wasOk(); }), "open the folder");
            check (onMessageThread ([this] { return session->getNumProjects(); }) == 1, "it takes the untouched new tab's place");
            check (clipCount() == 1, "one clip");
            waitMs (600);   // as a person would: Tracktion settles the new edit first
            check (! onMessageThread ([this] { return session->hasUnsavedChanges() || session->getEdit().getUndoManager().canUndo(); }),
                   "opening is not an edit: nothing unsaved, nothing to undo");

            say ("Rename from the tab: the folder and file take the name, everything in it comes along");
            song.getChildFile ("Notes.txt").replaceWithText ("kept");
            const auto renamed = song.getSiblingFile ("Song renamed");
            auto rr = await ([&] (auto done) { session->renameProject ("Song renamed", done); });
            check (rr.wasOk(), "rename: " + (rr.wasOk() ? String ("done") : rr.getErrorMessage()));
            check (renamed.getChildFile ("Song renamed.carthage").existsAsFile() && ! renamed.getChildFile ("Song.carthage").exists(),
                   "Song renamed/Song renamed.carthage, and no old file beside it");
            check (renamed.getChildFile ("Notes.txt").loadFileAsString() == "kept", "other files come along");
            check (! song.exists(), "the old folder is gone (to the bin)");
            check (onMessageThread ([this] { return session->getProjectName() == "Song renamed" && ! session->hasUnsavedChanges(); }),
                   "the tab shows the new name, nothing unsaved");
            check (clipCount() == 1, "the clip is still there");
            renamed.getSiblingFile ("Taken").createDirectory();
            auto taken = await ([&] (auto done) { session->renameProject ("Taken", done); });
            check (taken.failed() && renamed.isDirectory(), "a name in use is refused, nothing moves");
            auto back = await ([&] (auto done) { session->renameProject ("Song", done); });
            check (back.wasOk() && song.getChildFile ("Song.carthage").existsAsFile(), "renamed back to Song");

            say ("Versions: a save keeps the file it replaces; restoring one brings it back in its tab");
            const auto startOf = [this]
            {
                return onMessageThread ([this]
                {
                    auto* c = tracktion::getAudioTracks (session->getEdit())[2]->getClips().getFirst();
                    return c != nullptr ? carthage::editing::startBeat (*c) : -1.0;
                });
            };
            const auto before = startOf();
            onMessageThreadVoid ([this]
            {
                auto& edit = session->getEdit();
                carthage::editing::begin (edit, "Move clip");
                carthage::editing::move ({ tracktion::getAudioTracks (edit)[2]->getClips().getFirst() }, 4.0, 0);
            });
            onMessageThreadVoid ([this] { session->takePicture = [] { return MemoryBlock ("picture", 7); }; });   // the app's is the windows
            auto saved = await ([&] (auto done) { session->save (done); });
            check (saved.wasOk() && std::abs (startOf() - before - 4.0) < 1.0e-6, "moved by 4 beats and saved");
            check (onMessageThread ([this] { return carthage::project::previewOf (session->getProjectFile()).loadFileAsString(); }) == "picture",
                   "the save wrote its picture beside the project");
            const auto versions = onMessageThread ([this] { return carthage::project::versionsOf (session->getProjectFile()); });
            check (! versions.isEmpty(), "a version was kept: " + String (versions.size()));
            const auto tabsBefore = onMessageThread ([this] { return session->getNumProjects(); });
            const auto restored = onMessageThread ([this, v = versions.getFirst()] { return session->restoreVersion (v); });
            check (restored.wasOk(), "restore: " + (restored.wasOk() ? String ("done") : restored.getErrorMessage()));
            waitMs (600);   // as a person would: Tracktion settles the newly loaded edit first
            check (std::abs (startOf() - before) < 1.0e-6, "the clip is back where it was");
            check (onMessageThread ([this] { return session->hasUnsavedChanges(); }), "unsaved until saved");
            check (onMessageThread ([this] { return session->getNumProjects(); }) == tabsBefore, "in the same tab, no new one");
            check (clipCount() == 1, "its clip finds its place and file");
            const auto kept = onMessageThread ([this] { return carthage::project::versionsOf (session->getProjectFile()); });
            check (kept.size() == versions.size() + 1 && kept.getFirst().getFileName().contains ("before restoring"),
                   "the moved state was kept as a version first");
            check (carthage::project::pictureOfVersion (kept.getFirst()).existsAsFile(), "with a picture");
            auto resaved = await ([&] (auto done) { session->save (done); });
            check (resaved.wasOk() && ! onMessageThread ([this] { return session->hasUnsavedChanges(); }), "saved again");

            say ("Autosave: after a pause, written to the project's autosave folder");
            onMessageThreadVoid ([this] { session->setAutosavePolicy ({ 0.3, 0.0, 10 }); });
            onMessageThreadVoid ([this]
            {
                auto& edit = session->getEdit();
                carthage::editing::begin (edit, "Move clip");
                auto* c = tracktion::getAudioTracks (edit)[2]->getClips().getFirst();
                carthage::editing::move ({ c }, 4.0, 0);
                edit.getUndoManager().dispatchPendingMessages();
            });
            waitMs (1500);
            const auto unclosed = onMessageThread ([this] { return carthage::autosave::Store (folder.getChildFile ("Autosave")).findUnclosed(); });
            check (unclosed.size() == 1, "an open project with an autosave looks unclosed (as after a crash): " + String (unclosed.size()));

            if (unclosed.size() == 1)
            {
                say ("Recover it, as after a crash: the moved clip is there and the project is unsaved");
                const auto moved = onMessageThread ([this]
                {
                    auto* c = tracktion::getAudioTracks (session->getEdit())[2]->getClips().getFirst();
                    return c->getPosition().getStart().inSeconds();
                });
                check (onMessageThread ([&] { return session->recover (unclosed.front()).wasOk(); }), "recover");
                check (clipCount() == 1, "one clip");
                const auto recovered = onMessageThread ([this]
                {
                    auto* c = tracktion::getAudioTracks (session->getEdit())[2]->getClips().getFirst();
                    auto* a = dynamic_cast<tracktion::AudioClipBase*> (c);
                    return std::pair { c->getPosition().getStart().inSeconds(), a != nullptr && a->getAudioFile().getFile().existsAsFile() };
                });
                check (std::abs (recovered.first - moved) < 1.0e-6, "at its moved position");
                check (recovered.second, "its audio is found");
                check (onMessageThread ([this] { return session->hasUnsavedChanges(); }), "unsaved until saved");

                r = await ([&] (auto done) { session->save (done); });
                check (r.wasOk(), "save");
                check (! onMessageThread ([this] { return session->hasUnsavedChanges(); }), "saved");
            }

            say ("A clean close leaves no marker");
            onMessageThreadVoid ([this] { session->close(); });
            check (carthage::autosave::Store (folder.getChildFile ("Autosave")).findUnclosed().empty(), "nothing to recover");

            say (checksFailed == 0 ? "PASSED" : "FAILED: " + String (checksFailed));
            return checksFailed == 0 ? 0 : 1;
        }

        //------------------------------------------------------------------------------------------
        String describeDevice()
        {
            return onMessageThread ([this]
            {
                auto* d = session->getDeviceManager().getCurrentAudioDevice();
                if (d == nullptr) return String ("no device");
                const auto l = session->getReportedLatency();
                return d->getTypeName() + " / " + d->getName() + ", " + String (d->getCurrentSampleRate()) + " Hz, buffer "
                       + String (d->getCurrentBufferSizeSamples()) + ", inputs " + String (d->getActiveInputChannels().countNumberOfSetBits())
                       + ", outputs " + String (d->getActiveOutputChannels().countNumberOfSetBits())
                       + ", driver latency in " + String (l.inputSamples) + " + out " + String (l.outputSamples)
                       + " = " + String (l.roundTripSamples()) + " samples (" + String (l.roundTripMs(), 2) + " ms)";
            });
        }

        bool openDevice (int bufferSize)
        {
            const auto name = args.get ("device");
            const auto rate = args.get ("rate", "48000").getDoubleValue();

            const auto error = onMessageThread ([&]
            {
                auto& adm = session->getDeviceManager();
                auto setup = adm.getAudioDeviceSetup();

                if (name.isNotEmpty())
                {
                    setup.inputDeviceName = name;
                    setup.outputDeviceName = args.get ("output-device", name);
                }

                setup.sampleRate = rate;
                setup.bufferSize = bufferSize;
                setup.useDefaultInputChannels = true;
                setup.useDefaultOutputChannels = true;
                return adm.setAudioDeviceSetup (setup, true);
            });

            waitMs (1500);   // the engine prepares asynchronously after a device restart

            if (error.isNotEmpty())
            {
                say ("device error: " + error);
                return false;
            }

            const auto actual = onMessageThread ([this]
            {
                auto* d = session->getDeviceManager().getCurrentAudioDevice();
                return d != nullptr ? d->getCurrentBufferSizeSamples() : 0;
            });

            if (actual != bufferSize)
                say ("note: the interface chose a buffer of " + String (actual) + " instead of " + String (bufferSize));

            say ("device: " + describeDevice());
            say ("audio workgroup: " + onMessageThread ([this]
                 {
                     const auto wg = session->getDeviceManager().getDeviceAudioWorkgroup();
                     return wg ? "the device provides one, for up to " + String (static_cast<int> (wg.getMaxParallelThreadCount()))
                                     + " threads with its own; the engine joins its render threads to it"
                               : String ("none from this device");
                 }));
            sayWorkgroupJoins();
            return true;
        }

        int xruns()
        {
            return onMessageThread ([this]
            {
                auto* d = session->getDeviceManager().getCurrentAudioDevice();
                return d != nullptr ? d->getXRunCount() : -1;
            });
        }

        //------------------------------------------------------------------------------------------
        int devices()
        {
            onMessageThreadVoid ([this]
            {
                auto& adm = session->getDeviceManager();
                for (auto* type : adm.getAvailableDeviceTypes())
                {
                    type->scanForDevices();
                    say (type->getTypeName() + " inputs:  " + type->getDeviceNames (true).joinIntoString (" | "));
                    say (type->getTypeName() + " outputs: " + type->getDeviceNames (false).joinIntoString (" | "));
                }
            });
            say ("current: " + describeDevice());
            return 0;
        }

        //------------------------------------------------------------------------------------------
        int plugins()
        {
            const auto filter = args.get ("name").toLowerCase();
            const auto candidates = onMessageThread ([this] { return session->getPlugins().findCandidates(); });

            for (const auto& c : candidates)
                if (filter.isEmpty() || c.name.toLowerCase().contains (filter))
                    say (c.formatName + "\t" + c.name + "\t" + c.fileOrIdentifier);

            say (String (static_cast<int> (candidates.size())) + " plugins found (not loaded)");
            return 0;
        }

        //------------------------------------------------------------------------------------------
        int latency()
        {
            const auto out = args.getInt ("out", 1) - 1;
            const auto in = args.getInt ("in", 1) - 1;
            const auto repeat = args.getInt ("repeat", 5);
            auto buffers = StringArray::fromTokens (args.get ("buffers", "64,128,256"), ",", {});
            int failures = 0;

            onMessageThreadVoid ([this] { session->setMonitoring (false); });

            for (const auto& b : buffers)
            {
                if (! openDevice (b.getIntValue()))
                    return 1;

                const auto reported = onMessageThread ([this] { return session->getReportedLatency(); });
                Array<int> results;

                for (int i = 0; i < repeat; ++i)
                {
                    onMessageThreadVoid ([&] { session->getProbe().start (out, in); });
                    std::optional<engine::LatencyProbe::Measurement> m;

                    for (int t = 0; t < 400 && ! m; ++t)
                    {
                        waitMs (10);
                        m = onMessageThread ([this] { return session->getProbe().takeResult(); });
                    }

                    if (! m || m->confidence < engine::LatencyProbe::minimumConfidence)
                    {
                        say ("  run " + String (i + 1) + ": nothing came back"
                             + (m ? " (clarity " + String (m->confidence, 1) + ")" : String()));
                        ++failures;
                        continue;
                    }

                    results.add (m->samples);
                    say ("  run " + String (i + 1) + ": " + String (m->samples) + " samples, " + String (m->milliseconds(), 3)
                         + " ms, clarity " + String (m->confidence, 1));
                    waitMs (100);
                }

                if (! results.isEmpty())
                {
                    results.sort();
                    const auto median = results[results.size() / 2];
                    say ("RESULT buffer " + b + ": measured " + String (median) + " samples ("
                         + String (median * 1000.0 / reported.sampleRate, 2) + " ms), spread "
                         + String (results.getLast() - results.getFirst()) + " samples; driver reports "
                         + String (reported.roundTripSamples()) + " samples (" + String (reported.roundTripMs(), 2)
                         + " ms); difference " + String (median - reported.roundTripSamples()) + " samples");
                }
            }

            return failures == 0 ? 0 : 1;
        }

        //------------------------------------------------------------------------------------------
        int insertPlugin()
        {
            const auto format = args.get ("plugin-format");
            const auto id = args.get ("plugin");

            if (id.isEmpty())
                return 0;

            const auto scanner = File::getSpecialLocation (File::currentExecutableFile).getSiblingFile ("carthage-scan");
            const auto outcome = engine::scan::runScanner (scanner, format, id, 60000);

            if (outcome.kind != engine::scan::Outcome::Kind::found)
            {
                say ("scan failed: " + outcome.detail);
                return 1;
            }

            const auto result = onMessageThread ([&]
            {
                session->getPlugins().addScanned (outcome.descriptions);
                return session->insertPlugin (outcome.descriptions.getFirst());
            });

            if (result.failed())
            {
                say ("insert failed: " + result.getErrorMessage());
                return 1;
            }

            waitMs (1500);
            const auto& d = outcome.descriptions.getFirst();
            say ("plugin: " + d.name + " " + d.version + " by " + d.manufacturerName + " (" + d.pluginFormatName
                 + "), reported latency " + String (onMessageThread ([this]
                 {
                     auto* p = session->getInsertedPlugin();
                     return p != nullptr ? p->getLatencySeconds() * 1000.0 : 0.0;
                 }), 2) + " ms");
            return 0;
        }

        int soak()
        {
            if (! openDevice (args.getInt ("buffer", 64)))
                return 1;

            onMessageThreadVoid ([this]
            {
                session->setInput (args.getInt ("input", 1) - 1, args.has ("stereo"));
                session->setMonitoring (true);
                session->setRecordFormat (engine::TakeRecorder::SampleFormat::int24);

                // --metronome: the click track plays all the while, so the real-time check covers
                // Tracktion's click node and Carthage's sounds (W38, W41).
                if (args.has ("metronome"))
                    session->setMetronomeOn (true);
            });

            if (insertPlugin() != 0)
                return 1;

            const auto seconds = args.getInt ("seconds", 600);
            const auto startXruns = xruns();
            const auto startLate = onMessageThread ([this] { return session->getCallback().getLateCallbackCount(); });
            const auto startSamples = onMessageThread ([this] { return session->getCallback().getSamplesProcessed(); });

            if (const auto r = onMessageThread ([this] { session->setArmed (session->getTrack(), true); return session->startRecording(); }); r.failed())
            {
                say ("record failed: " + r.getErrorMessage());
                return 1;
            }

            say ("recording and monitoring for " + String (seconds) + " s");
            sayWorkgroupJoins();
            float worstPeak = 0.0f;
            double sumAverage = 0.0;
            int samples = 0;
            float maxInput = 0.0f;
            const auto start = Time::getMillisecondCounterHiRes();

            onMessageThreadVoid ([this] { session->getCallback().takeLoad(); });

            // --preview FILE: the sample browser's preview plays (and restarts) all the while, so
            // the real-time check covers it too.
            const File preview (args.get ("preview"));

            while (Time::getMillisecondCounterHiRes() - start < seconds * 1000.0)
            {
                if (preview.existsAsFile())
                    onMessageThreadVoid ([this, preview] { session->getPreview().play (preview); });

                waitMs (1000);
                const auto load = onMessageThread ([this] { return session->getCallback().takeLoad(); });
                const auto peaks = onMessageThread ([this] { return session->getCallback().takeInputPeaks(); });
                maxInput = std::max ({ maxInput, peaks[0], peaks[1] });
                worstPeak = std::max (worstPeak, load.peak);
                sumAverage += load.average;
                ++samples;

                const auto elapsed = static_cast<int> ((Time::getMillisecondCounterHiRes() - start) / 1000.0);

                if (elapsed % 60 == 0 || elapsed < 2)
                    say ("  " + String (elapsed) + " s: load " + String (load.average * 100.0f, 1) + "% avg, "
                         + String (load.peak * 100.0f, 1) + "% worst; engine cpu "
                         + String (onMessageThread ([this] { return session->getEngine().getDeviceManager().getCpuUsage(); }) * 100.0f, 1)
                         + "%; dropouts " + String (xruns() - startXruns) + "; late blocks "
                         + String (onMessageThread ([this] { return session->getCallback().getLateCallbackCount(); }) - startLate) + "; overflows "
                         + String (onMessageThread ([this] { return session->getRecorder().getOverflowCount(); })));
            }

            const auto wallSeconds = (Time::getMillisecondCounterHiRes() - start) / 1000.0;
            const auto result = onMessageThread ([this] { return session->stopRecording(); });
            const auto dropouts = xruns() - startXruns;
            const auto late = onMessageThread ([this] { return session->getCallback().getLateCallbackCount(); }) - startLate;
            const auto rate = onMessageThread ([this] { return session->getCallback().getSampleRate(); });
            const auto processed = onMessageThread ([this] { return session->getCallback().getSamplesProcessed(); }) - startSamples;
            const auto shortfall = wallSeconds * rate - static_cast<double> (processed);
            sayWorkgroupJoins();

            say ("RESULT soak: " + String (seconds) + " s at buffer " + args.get ("buffer", "64") + ", dropouts "
                 + String (dropouts) + ", recorder overflows " + String (result.overflows) + " (" + String (result.framesDropped)
                 + " samples lost), audio-thread load " + String (sumAverage / std::max (1, samples) * 100.0, 1)
                 + "% average and " + String (worstPeak * 100.0f, 1) + "% worst block, late blocks " + String (late) + ", device ran "
                 + String (static_cast<double> (processed) / rate, 3) + " s of audio in " + String (wallSeconds, 3)
                 + " s of wall time (shortfall " + String (roundToInt (shortfall)) + " samples), take "
                 + String (static_cast<double> (result.framesWritten) / rate, 2) + " s ("
                 + String (result.framesWritten) + " samples) in " + result.file.getFileName()
                 + ", loudest input " + String (Decibels::gainToDecibels (maxInput), 1) + " dBFS");

            return dropouts == 0 && late == 0 && result.overflows == 0 && result.succeeded() ? 0 : 1;
        }

        //------------------------------------------------------------------------------------------
        /** First sample index where |x| exceeds `fraction` of the local peak, for each click. */
        static Array<double> onsets (const AudioBuffer<float>& b, double rate, float threshold)
        {
            Array<double> result;
            const auto* d = b.getReadPointer (0);
            const int refractory = static_cast<int> (rate * 0.25);

            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                if (std::abs (d[i]) > threshold)
                {
                    result.add (i / rate);
                    i += refractory;
                }
            }

            return result;
        }

        static void sayWorkgroupJoins()
        {
            const auto j = bench::getWorkgroupJoins();

            if (j.counted)
                say ("workgroup joins (os_workgroup_join): " + String (j.joined) + " joined, " + String (j.failed)
                     + " failed, " + String (j.left) + " left; threads in a workgroup now: " + String (j.inWorkgroupNow()));
        }

        int scanall()
        {
            const auto exe = File::getSpecialLocation (File::currentExecutableFile).getSiblingFile ("carthage-scan");

            if (args.has ("folder"))
                onMessageThreadVoid ([this] { session->getPlugins().setExtraFolders ({ File (args.get ("folder")) }); });
            auto scanner = onMessageThread ([&] { return std::make_shared<engine::PluginScanner> (session->getPlugins(), exe); });
            scanner->maxParallel = args.getInt ("parallel", 4);
            scanner->timeoutMs = args.getInt ("timeout-s", 60) * 1000;

            onMessageThreadVoid ([&] { scanner->start (true); });
            int lastDone = -1;

            for (;;)
            {
                waitMs (1000);
                const auto p = onMessageThread ([&] { return scanner->getProgress(); });

                if (p.done != lastDone)
                {
                    say ("  " + String (p.seconds, 0) + " s: " + String (p.done) + " of " + String (p.total)
                         + " (" + String (p.found) + " found, " + String (p.blocked) + " blocked)"
                         + (p.current.isEmpty() ? String() : "; now " + p.current.joinIntoString (", ")));
                    lastDone = p.done;
                }

                if (! p.running)
                {
                    const auto scanned = onMessageThread ([this] { return session->getPlugins().getScanned(); });
                    int au = 0, vst3 = 0, instruments = 0;

                    for (const auto& d : scanned)
                    {
                        (d.pluginFormatName == "AudioUnit" ? au : vst3)++;
                        instruments += d.isInstrument ? 1 : 0;
                    }

                    for (const auto& name : p.newlyBlocked)
                        say ("blocked: " + name);

                    const auto blocked = onMessageThread ([this]
                    {
                        StringArray out;
                        for (const auto& id : session->getPlugins().getBlocked())
                            out.add (id + ": " + session->getPlugins().getBlockReason (id));
                        return out;
                    });

                    for (const auto& b : blocked)
                        say ("  reason: " + b);

                    say ("RESULT scanall: " + String (p.total) + " plugin files checked in " + String (p.seconds, 1) + " s with "
                         + String (scanner->maxParallel) + " scanners; " + String (p.found) + " loaded, " + String (p.blocked)
                         + " blocked; " + String (scanned.size()) + " plugin types (" + String (au) + " AU, " + String (vst3)
                         + " VST3, " + String (instruments) + " instruments)");
                    onMessageThreadVoid ([&] { scanner.reset(); });
                    return 0;
                }
            }
        }

        int wgprobe()
        {
            auto current = [this] { return onMessageThread ([this] { return session->getDeviceManager().getDeviceAudioWorkgroup(); }); };
            auto yesNo = [] (bool b) { return String (b ? "yes" : "no"); };
            auto joinable = [] (const AudioWorkgroup& wg)
            {
                bool ok = false;
                std::thread t ([&] { WorkgroupToken token; wg.join (token); ok = static_cast<bool> (token); });
                t.join();
                return ok;
            };

            const auto atStart = current();
            say ("at start: valid " + yesNo (static_cast<bool> (atStart)) + ", joinable " + yesNo (joinable (atStart))
                 + ", max threads " + String (static_cast<int> (atStart ? atStart.getMaxParallelThreadCount() : 0)));

            if (! openDevice (args.getInt ("buffer", 64)))
                return 1;

            const auto after = current();
            say ("after reopening: valid " + yesNo (static_cast<bool> (after)) + ", same as at start " + yesNo (after == atStart)
                 + ", start one still joinable " + yesNo (joinable (atStart)) + ", new one joinable " + yesNo (joinable (after)));
            sayWorkgroupJoins();   // the probe's own joins count here: a check that the counter works
            return 0;
        }

        int takes()
        {
            if (! openDevice (args.getInt ("buffer", 64)))
                return 1;

            if (insertPlugin() != 0)
                return 1;

            const auto count = args.getInt ("count", 6);
            const auto seconds = args.getInt ("seconds", 2);
            const auto gap = args.getInt ("gap-ms", 500);
            int early = 0;

            for (int i = 0; i < count; ++i)
            {
                const bool fromStart = (i % 2) == 1;

                if (fromStart)
                    onMessageThreadVoid ([this] { session->returnToStart(); });

                if (args.has ("while-playing"))
                    onMessageThreadVoid ([this] { session->play(); });

                waitMs (gap);
                onMessageThreadVoid ([this] { session->setArmed (session->getTrack(), true); [[maybe_unused]] const auto started = session->startRecording(); });

                for (int t = 0; t < seconds * 100 && onMessageThread ([this] { return session->isRecording(); }); ++t)
                    waitMs (10);

                onMessageThreadVoid ([this] { if (session->isRecording()) session->stopRecording(); });
                waitMs (gap);

                const auto take = onMessageThread ([this] { return session->getLastTake(); });
                const auto length = take ? static_cast<double> (take->recording.framesWritten) / 48000.0 : 0.0;
                const bool ended = take && (take->interrupted || take->placedByGuess);
                early += ended ? 1 : 0;
                say ("take " + String (i + 1) + (fromStart ? " (from start)" : "") + ": " + String (length, 3) + " s, starts at "
                     + String (take ? take->startSeconds : 0.0, 4) + " s" + (ended ? ", ENDED EARLY" : ""));
            }

            say ("RESULT takes: " + String (count) + " takes, " + String (early) + " ended early");
            return early == 0 ? 0 : 1;
        }

        int align()
        {
            if (! openDevice (args.getInt ("buffer", 64)))
                return 1;

            const auto in = args.getInt ("in", 1) - 1;
            const auto seconds = args.getInt ("seconds", 10);

            // The metronome plays on the master output (outputs 1-2); the cable brings it back to
            // `in`. Monitoring is off so the cable can not feed back.
            onMessageThreadVoid ([&]
            {
                session->setInput (in, false);
                session->setMonitoring (false);
                auto& edit = session->getEdit();
                edit.clickTrackEnabled = true;
                edit.clickTrackRecordingOnly = false;
                edit.clickTrackEmphasiseBars = false;
                session->returnToStart();
            });

            // Move the master output (the click with it) to the pair that holds the cable's output.
            if (args.has ("click-out"))
            {
                const auto out = args.getInt ("click-out", 1) - 1;
                const auto moved = onMessageThread ([&]
                {
                    auto& dm = session->getEngine().getDeviceManager();

                    for (int i = 0; i < dm.getNumWaveOutDevices(); ++i)
                        if (auto* wo = dm.getWaveOutDevice (i); wo != nullptr && wo->getChannels().containsDeviceChannel (out))
                        {
                            dm.setDefaultWaveOutDevice (wo->getDeviceID());
                            return wo->getName();
                        }

                    return String();
                });

                if (moved.isEmpty())
                {
                    say ("no output holds channel " + String (out + 1));
                    return 1;
                }

                say ("click plays on " + moved);
                waitMs (500);
            }

            if (args.has ("measured-out"))
            {
                const auto out = args.getInt ("measured-out", 1) - 1;
                onMessageThreadVoid ([&] { session->getProbe().start (out, in); });
                std::optional<engine::LatencyProbe::Measurement> m;
                for (int t = 0; t < 400 && ! m; ++t) { waitMs (10); m = onMessageThread ([this] { return session->getProbe().takeResult(); }); }

                if (m && m->confidence >= engine::LatencyProbe::minimumConfidence)
                {
                    say ("using the measured round trip: " + String (m->samples) + " samples");
                    onMessageThreadVoid ([&] { session->setMeasuredRoundTrip (m->samples); });
                }
            }

            waitMs (args.getInt ("settle-ms", 0));
            onMessageThreadVoid ([this] { session->setArmed (session->getTrack(), true); [[maybe_unused]] const auto started = session->startRecording(); });

            // Session may end the take itself if playback stops under it.
            for (int t = 0; t < seconds * 100 && onMessageThread ([this] { return session->isRecording(); }); ++t)
                waitMs (10);

            onMessageThreadVoid ([this] { if (session->isRecording()) session->stopRecording(); });
            waitMs (300);

            const auto take = onMessageThread ([this] { return session->getLastTake(); });

            if (! take || take->recording.file == File())
            {
                say ("no take");
                return 1;
            }

            const auto& result = take->recording;

            // The click as the engine plays it: its first sample over the threshold. Without bar
            // emphasis (set above) the click node plays the little click on every beat.
            const auto rate = onMessageThread ([this] { return session->getCallback().getSampleRate(); });
            AudioFormatManager formats;
            carthage::audio::registerReadable (formats);

            std::unique_ptr<AudioFormatReader> clickReader (formats.createReaderFor (
                std::make_unique<MemoryInputStream> (tracktion::TracktionBinaryData::littleclick_wav, static_cast<size_t> (tracktion::TracktionBinaryData::littleclick_wavSize), false)));
            AudioBuffer<float> click (1, static_cast<int> (clickReader->lengthInSamples));
            clickReader->read (&click, 0, click.getNumSamples(), 0, true, false);
            const auto clickPeak = click.getMagnitude (0, 0, click.getNumSamples());
            const auto clickOnset = onsets (click, clickReader->sampleRate, clickPeak * 0.3f).getFirst();

            std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (result.file));
            AudioBuffer<float> recorded (1, static_cast<int> (reader->lengthInSamples));
            reader->read (&recorded, 0, recorded.getNumSamples(), 0, true, false);
            const auto peak = recorded.getMagnitude (0, 0, recorded.getNumSamples());

            if (peak < 0.01f)
            {
                say ("the take is silent (peak " + String (peak) + "): check the cable from the click's output to input " + String (in + 1));
                return 1;
            }

            // Edit time of each recorded click = clip start + time in file (the take was placed by
            // Session, corrected for latency). Beats at 120 bpm fall every 0.5 s.
            const auto beat = 60.0 / onMessageThread ([this] { return session->getEdit().tempoSequence.getTempos()[0]->getBpm(); });
            Array<double> errors;
            const auto found = onsets (recorded, rate, peak * 0.3f);
            String times;
            for (auto t : found) times << String (t, 4) << " ";
            if (take->interrupted || take->placedByGuess)
                say (String ("note: playback stopped during the take, so it ended after ")
                     + String (static_cast<double> (result.framesWritten) / rate, 3) + " s"
                     + (take->placedByGuess ? "; it never ran, so the take was placed where it began" : ""));

            say ("take starts at edit time " + String (take->startSeconds, 4) + " s; click onsets in the file (s): " + times.trim());

            for (auto t : found)
            {
                const auto editTime = take->startSeconds + t;
                const auto nearestBeat = std::round ((editTime - clickOnset) / beat) * beat;
                errors.add ((editTime - clickOnset - nearestBeat) * rate);
            }

            if (errors.isEmpty())
                return 1;

            errors.sort();
            String list;
            for (auto e : errors) list << String (e, 1) << " ";

            say ("clicks found: " + String (errors.size()) + "; error per click (samples): " + list.trim());
            say ("RESULT align: buffer " + args.get ("buffer", "64") + ", take placed " + String (take->alignmentSamples)
                 + " samples earlier; median error " + String (errors[errors.size() / 2], 1) + " samples ("
                 + String (errors[errors.size() / 2] * 1000.0 / rate, 3) + " ms), range " + String (errors.getFirst(), 1)
                 + " to " + String (errors.getLast(), 1));
            return 0;
        }
    };

    //==============================================================================================
    class BenchApp final : public JUCEApplicationBase
    {
    public:
        const String getApplicationName() override       { return "carthage-bench"; }
        const String getApplicationVersion() override    { return CARTHAGE_VERSION_STRING; }
        bool moreThanOneInstanceAllowed() override       { return true; }
        void anotherInstanceStarted (const String&) override {}
        void suspended() override {}
        void resumed() override {}
        void systemRequestedQuit() override              { quit(); }
        void unhandledException (const std::exception*, const String&, int) override {}

        void initialise (const String&) override
        {
            auto argv = getCommandLineParameterArray();

            if (argv.isEmpty())
            {
                say ("usage: carthage-bench devices|latency|soak|align [options] (see tools/bench/BenchMain.cpp)");
                setApplicationReturnValue (2);
                quit();
                return;
            }

            worker = std::make_unique<std::thread> ([this, argv]
            {
                const auto code = Bench (parse (argv)).run();
                MessageManager::callAsync ([this, code] { setApplicationReturnValue (code); quit(); });
            });
        }

        void shutdown() override
        {
            if (worker != nullptr && worker->joinable())
                worker->join();
        }

    private:
        std::unique_ptr<std::thread> worker;
    };
}

#if JUCE_WINDOWS
// carthage_juce is compiled once, as for a GUI app, so on Windows it holds JUCE's GUI entry point
// (JUCEApplicationBase::main(), which reads the command line from Windows) and not the console one
// START_JUCE_APPLICATION calls from a console program. Start through the GUI one.
int main()
{
    juce::JUCEApplicationBase::createInstance = []() -> juce::JUCEApplicationBase* { return new BenchApp(); };
    return juce::JUCEApplicationBase::main();
}
#else
START_JUCE_APPLICATION (BenchApp)
#endif
