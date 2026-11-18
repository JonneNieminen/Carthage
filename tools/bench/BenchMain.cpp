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

#include <engine/Editing.h>
#include <engine/Session.h>

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

    void say (const String& s)    { std::cout << s << std::endl; }

    template <typename F>
    auto onMessageThread (F&& f)  { return *MessageManager::callSync (std::forward<F> (f)); }

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
            onMessageThreadVoid ([&] { start ([&] (Result r) { result = r; done.signal(); }); });
            done.wait (10000);
            return result.value_or (Result::fail ("timed out"));
        }

        int clipCount()
        {
            return onMessageThread ([this]
            {
                int n = 0;
                for (auto* t : tracktion::getAudioTracks (session->getEdit()))
                    n += t->getClips().size();
                return n;
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
                edit->ensureNumberOfAudioTracks (8);
                const auto lanes = tracktion::getAudioTracks (*edit);
                Random random (42);
                std::array<double, 8> next {};

                for (int i = 0; i < count; ++i)
                {
                    const auto lane = i % 8;
                    const auto& take = takes[random.nextInt (takes.size())];
                    const auto length = 1.0 + random.nextInt (3) * 0.5;
                    next[static_cast<size_t> (lane)] += random.nextInt (3) * 0.5;
                    tracktion::insertWaveClip (*lanes[lane], "Clip " + String (i + 1), take,
                                               { { tracktion::TimePosition::fromSeconds (next[static_cast<size_t> (lane)]), tracktion::TimeDuration::fromSeconds (length) } },
                                               tracktion::DeleteExistingClips::no);
                    next[static_cast<size_t> (lane)] += length;
                }

                lanes[0]->setName ("Guitar");
                for (int i = 1; i < lanes.size(); ++i)
                    lanes[i]->setName ("Lane " + String (i + 1));

                return carthage::project::write (carthage::project::snapshot (*edit, file, carthage::project::Info::createNew()), file);
            });

            say (r.wasOk() ? "wrote " + carthage::project::fileIn (out).getFullPathName() + " with " + String (count) + " clips"
                           : r.getErrorMessage());
            return r.wasOk() ? 0 : 1;
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
                for (auto* lane : editing::lanes (session->getEdit()))
                    for (auto* c : lane->getClips())
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
                session->collectSamples ([done] (Result r, int n)
                {
                    done (r.failed() ? r : n == 1 ? Result::ok() : Result::fail ("copied " + String (n)));
                });
            });
            check (collected.wasOk(), "one sample copied: " + (collected.wasOk() ? String ("yes") : collected.getErrorMessage()));
            check (onMessageThread ([this]
                   {
                       const auto samples = carthage::project::samplesIn (session->getProjectFolder());
                       for (auto* lane : editing::lanes (session->getEdit()))
                           for (auto* c : lane->getClips())
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
            });

            if (insertPlugin() != 0)
                return 1;

            const auto seconds = args.getInt ("seconds", 600);
            const auto startXruns = xruns();
            const auto startLate = onMessageThread ([this] { return session->getCallback().getLateCallbackCount(); });
            const auto startSamples = onMessageThread ([this] { return session->getCallback().getSamplesProcessed(); });

            if (const auto r = onMessageThread ([this] { return session->startRecording(); }); r.failed())
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
                onMessageThreadVoid ([this] { session->startRecording(); });

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
            onMessageThreadVoid ([this] { session->startRecording(); });

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
            formats.registerBasicFormats();

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
