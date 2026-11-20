// carthage-snapshot: renders the app's real window content to PNG files, in fixed states, on the real
// engine. For checking the UI against design/README.md without a screen (a locked Mac, CI later),
// and for README screenshots. It works in a temporary folder and never touches the user's settings.
//
//   carthage-snapshot OUTPUT_FOLDER [--device NAME] [--output-device NAME]
//                     [--plugin-format VST3 --plugin FILE_OR_ID] [--record-seconds 3] [--scan]
//
// Writes main.png, samples*.png (with --samples FOLDER), start*.png (start.png with --project FILE), tabs.png, settings.png, settings-updates.png, settings-shortcuts.png, mixer.png, mixer-in.png, mixer-buses.png, export.png, update*.png (the update banner with sample text), plugin.png
// (with --plugin) and take.png (after recording).

#include <app/Commands.h>
#include <engine/Session.h>
#include <engine/PluginScan.h>
#include <engine/SampleEdits.h>
#include <ui/ExportWindow.h>
#include <ui/MainComponent.h>
#include <ui/theme/LookAndFeel.h>

#include <iostream>

using namespace carthage;
using namespace juce;

namespace
{
    template <typename F>
    // callSync is empty only if the message loop has stopped; then .value() throwing is right.
    auto onMessageThread (F&& f)  { return MessageManager::callSync (std::forward<F> (f)).value(); }   // NOLINT(bugprone-unchecked-optional-access)

    template <typename F>
    void onMessageThreadVoid (F&& f) { MessageManager::callSync (std::forward<F> (f)); }

    String argValue (const StringArray& args, const String& key, const String& fallback = {})
    {
        const auto i = args.indexOf (key);
        return i >= 0 && i + 1 < args.size() ? args[i + 1] : fallback;
    }

    class SnapshotApp final : public JUCEApplicationBase
    {
    public:
        const String getApplicationName() override       { return "carthage-snapshot"; }
        const String getApplicationVersion() override    { return CARTHAGE_VERSION_STRING; }
        bool moreThanOneInstanceAllowed() override       { return true; }
        void anotherInstanceStarted (const String&) override {}
        void suspended() override {}
        void resumed() override {}
        void systemRequestedQuit() override              { quit(); }
        void unhandledException (const std::exception*, const String&, int) override {}

        void initialise (const String&) override
        {
            args = getCommandLineParameterArray();

            if (args.isEmpty())
            {
                std::cout << "usage: carthage-snapshot OUTPUT_FOLDER [options] (see tools/snapshot/SnapshotMain.cpp)\n";
                setApplicationReturnValue (2);
                quit();
                return;
            }

            LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
            worker = std::make_unique<std::thread> ([this]
            {
                const auto code = run();
                MessageManager::callAsync ([this, code] { setApplicationReturnValue (code); quit(); });
            });
        }

        void shutdown() override
        {
            if (worker != nullptr && worker->joinable())
                worker->join();

            LookAndFeel::setDefaultLookAndFeel (nullptr);
        }

    private:
        StringArray args;
        ui::LookAndFeel lookAndFeel;
        ApplicationCommandManager commands;
        std::unique_ptr<engine::Session> session;
        std::unique_ptr<ui::MainComponent> content;
        std::unique_ptr<std::thread> worker;

        void capture (const File& file)
        {
            Thread::sleep (400);   // let a frame or two and any short animation run

            onMessageThreadVoid ([&]
            {
                // Twice the logical size, like the Retina display the app is drawn for.
                const auto image = content->createComponentSnapshot (content->getLocalBounds(), true, 2.0f);
                file.deleteFile();
                FileOutputStream out (file);
                PNGImageFormat().writeImageToStream (image, out);
            });

            std::cout << "wrote " << file.getFullPathName() << '\n';
        }

        int run()
        {
            const File outDir (File::getCurrentWorkingDirectory().getChildFile (args[0]));
            outDir.createDirectory();
            const auto temp = File::getSpecialLocation (File::tempDirectory).getChildFile ("carthage-snapshot")
                                  .getNonexistentChildFile ("run", {}, false);

            onMessageThreadVoid ([&]
            {
                engine::Folders f;
                f.settings = temp.getChildFile ("Settings");
                f.pluginCache = temp.getChildFile ("Plugins");
                f.project = temp.getChildFile ("Project");
                session = std::make_unique<engine::Session> (f, File::getSpecialLocation (File::currentExecutableFile)
                                                                     .getSiblingFile ("carthage-scan"));

                if (const auto device = argValue (args, "--device"); device.isNotEmpty())
                {
                    auto setup = session->getDeviceManager().getAudioDeviceSetup();
                    setup.inputDeviceName = device;
                    setup.outputDeviceName = argValue (args, "--output-device", device);
                    setup.bufferSize = 64;
                    setup.useDefaultInputChannels = setup.useDefaultOutputChannels = true;
                    session->getDeviceManager().setAudioDeviceSetup (setup, true);
                }

                content = std::make_unique<ui::MainComponent> (*session, commands);
                commands.registerAllCommandsForTarget (content.get());
                commands.setFirstCommandTarget (content.get());
                content->setSize (1280, 780);
                content->setVisible (true);
            });

            if (args.contains ("--scan"))
            {
                // A real scan of this computer's plugins, so the browser shows them.
                onMessageThreadVoid ([this] { session->getPluginScanner().start (true); });
                Thread::sleep (2000);
                capture (outDir.getChildFile ("browser-scanning.png"));

                while (onMessageThread ([this] { return session->getPluginScanner().getProgress().running; }))
                    Thread::sleep (250);

                onMessageThreadVoid ([this] { content->getPluginBrowser().refresh(); });
            }

            Thread::sleep (1500);
            capture (outDir.getChildFile ("main.png"));

            // The picture each save keeps with its version (W41).
            onMessageThreadVoid ([this, &outDir]
            {
                const auto jpeg = session->takePicture ? session->takePicture() : MemoryBlock();
                outDir.getChildFile ("version-picture.jpg").replaceWithData (jpeg.getData(), jpeg.getSize());
            });

            // The narrowest window Carthage allows: the toolbar folds (W38).
            onMessageThreadVoid ([this] { content->setSize (960, 780); });
            capture (outDir.getChildFile ("main-narrow.png"));
            onMessageThreadVoid ([this] { content->setSize (1280, 780); });

            onMessageThreadVoid ([this] { content->getPluginBrowser().setSearch ("amp"); });
            capture (outDir.getChildFile ("browser-search.png"));
            onMessageThreadVoid ([this] { content->getPluginBrowser().setSearch ({});
                                          content->getPluginBrowser().setShowUnavailable (true); });
            capture (outDir.getChildFile ("browser-unavailable.png"));
            onMessageThreadVoid ([this] { content->getPluginBrowser().setShowUnavailable (false); });

            // The sample browser (phase 4b): empty, then with a folder of sounds (--samples FOLDER).
            onMessageThreadVoid ([this] { content->showPanel (true, false); });
            capture (outDir.getChildFile ("samples-empty.png"));

            if (const auto folder = argValue (args, "--samples"); folder.isNotEmpty())
            {
                onMessageThreadVoid ([this, folder]
                {
                    auto& library = session->getSampleLibrary();
                    library.addRoot (File::getCurrentWorkingDirectory().getChildFile (folder));
                    library.indexNow();
                    content->getSampleBrowser().openAllFolders();
                });
                Thread::sleep (3000);   // the analyser finds keys and tempos
                capture (outDir.getChildFile ("samples.png"));

                onMessageThreadVoid ([this]
                {
                    // The longest sound, so it is still playing when the picture is taken.
                    const auto& all = session->getSampleLibrary().getSamples();
                    const auto longest = std::max_element (all.begin(), all.end(), [] (const auto& x, const auto& y)
                    {
                        return x.file.getSize() < y.file.getSize();
                    });
                    if (longest != all.end())
                        session->getPreview().play (longest->file);
                });
                Thread::sleep (1500);
                // Offscreen there is no display frame to drive the progress; drive it once here.
                onMessageThreadVoid ([this] { content->getSampleBrowser().frame(); });
                capture (outDir.getChildFile ("samples-playing.png"));
                onMessageThreadVoid ([this] { session->getPreview().stop();
                                              content->getSampleBrowser().setSearch ("loop"); });
                capture (outDir.getChildFile ("samples-search.png"));
                onMessageThreadVoid ([this] { content->getSampleBrowser().setSearch ({}); });
            }

            onMessageThreadVoid ([this] { content->showPanel (false, false); });

            // The start screen (phase 4b): empty, then with a recent project and a template made
            // from it (--project FILE).
            onMessageThreadVoid ([this, temp] { content->getStartScreen().setTemplatesFolder (temp.getChildFile ("Templates"));
                                                content->showStartScreen (true); });
            capture (outDir.getChildFile ("start-empty.png"));

            if (const auto project = argValue (args, "--project"); project.isNotEmpty())
            {
                onMessageThreadVoid ([this, project] { session->openProject (File::getCurrentWorkingDirectory().getChildFile (project)); });
                Thread::sleep (500);
                std::atomic<bool> saved { false };
                onMessageThreadVoid ([this, temp, &saved] { session->saveCopy (temp.getChildFile ("Templates/Four on the floor"),
                                                                               [&saved] (const Result&) { saved = true; }); });
                while (! saved) Thread::sleep (50);
                onMessageThreadVoid ([this] { content->showStartScreen (true); });
                Thread::sleep (800);   // miniatures
                capture (outDir.getChildFile ("start.png"));
            }

            onMessageThreadVoid ([this] { content->showStartScreen (false); });

            // Two projects as tabs.
            onMessageThreadVoid ([this, temp] { session->newProject (temp.getChildFile ("Second song")); });
            capture (outDir.getChildFile ("tabs.png"));

            // Phase 4c: the command palette, and the main window with the timeline in its own.
            onMessageThreadVoid ([this] { commands.invokeDirectly (app::commands::commandPalette, false); });
            capture (outDir.getChildFile ("palette.png"));
            onMessageThreadVoid ([this] { commands.invokeDirectly (app::commands::commandPalette, false); });   // Cmd+K again closes it
            onMessageThreadVoid ([this] { content->setDetached (ui::MainComponent::View::timeline, true);
                                          content->setDetached (ui::MainComponent::View::plugins, true); });
            capture (outDir.getChildFile ("views-away.png"));
            onMessageThreadVoid ([this] { content->defaultLayout(); });

            // Phase 5a: the mixer in place of the lanes (F9), with sounds routed to a few channels,
            // a bus, a muted and a soloed channel, and the third channel selected.
            onMessageThreadVoid ([this, temp]
            {
                auto& edit = session->getEdit();
                const auto tone = temp.getChildFile ("Kick.wav");
                {
                    WavAudioFormat wav;
                    std::unique_ptr<OutputStream> out = std::make_unique<FileOutputStream> (tone);
                    auto writer = wav.createWriterFor (out, AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (1).withBitsPerSample (16));
                    AudioBuffer<float> b (1, 24000);
                    for (int i = 0; i < b.getNumSamples(); ++i)
                        b.setSample (0, i, 0.5f * std::sin (static_cast<float> (i) * 0.01f) * std::exp (-static_cast<float> (i) / 6000.0f));
                    writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
                }

                const auto lanes = carthage::editing::lanes (edit);
                const auto channels = carthage::editing::channels (edit);
                carthage::editing::begin (edit, "Mixer shot");

                if (auto* kick = carthage::editing::insertFile (edit, lanes[1], tone, 0.0))
                    carthage::editing::routeSample (*kick, channels[2]);

                channels[2]->setName ("Kick");
                channels[3]->setName ("Snare");
                channels[4]->setName ("Drum bus");
                [[maybe_unused]] auto r1 = carthage::editing::setOutput (*channels[2], channels[4]);
                [[maybe_unused]] auto r2 = carthage::editing::setOutput (*channels[3], channels[4]);
                carthage::editing::faderOf (*channels[2])->setVolumeDb (-4.5f);
                carthage::editing::faderOf (*channels[3])->setVolumeDb (-9.0f);
                carthage::editing::faderOf (*channels[3])->setPan (-0.4f);
                carthage::editing::setChannelMuted (*channels[6], true);
                channels[7]->setName ("Reverb");
                [[maybe_unused]] auto r3 = carthage::editing::addSend (*channels[2], *channels[7], -8.0f);   // Kick also into Reverb
                commands.invokeDirectly (app::commands::toggleMixer, false);
                content->getMixer().select (channels[2]);
            });
            Thread::sleep (400);
            capture (outDir.getChildFile ("mixer.png"));

            // Cmd+R: the export window for this project (W43), drawn on its own.
            onMessageThreadVoid ([this] { commands.invokeDirectly (app::commands::exportMix, false); });
            Thread::sleep (400);
            onMessageThreadVoid ([&]
            {
                for (int i = 0; i < TopLevelWindow::getNumTopLevelWindows(); ++i)
                    if (auto* w = dynamic_cast<carthage::ui::ExportWindow*> (TopLevelWindow::getTopLevelWindow (i)))
                        if (auto* c = w->getContentComponent())
                        {
                            const auto image = c->createComponentSnapshot (c->getLocalBounds(), true, 2.0f);
                            const auto file = outDir.getChildFile ("export.png");
                            file.deleteFile();
                            FileOutputStream out (file);
                            PNGImageFormat().writeImageToStream (image, out);
                            w->setVisible (false);
                        }
            });

            // The Drum bus selected: its cables come in from Kick and Snare (W52).
            onMessageThreadVoid ([this] { content->getMixer().select (carthage::editing::channels (session->getEdit())[4]); });
            Thread::sleep (200);
            capture (outDir.getChildFile ("mixer-in.png"));

            // Bus channels on, every 4th (Settings > Mixer): Snare and Reverb are buses.
            onMessageThreadVoid ([this]
            {
                auto& props = session->getEngine().getPropertyStorage().getPropertiesFile();
                props.setValue ("busChannels", true);
                props.setValue ("busEvery", 4);
                content->getMixer().busesChanged();
            });
            Thread::sleep (200);
            capture (outDir.getChildFile ("mixer-buses.png"));
            onMessageThreadVoid ([this]
            {
                session->getEngine().getPropertyStorage().getPropertiesFile().setValue ("busChannels", false);
                content->getMixer().busesChanged();
            });
            onMessageThreadVoid ([this] { content->getMixer().select (carthage::editing::channels (session->getEdit())[2]); });

            // CARTHAGE_SNAPSHOT_PLUGIN=<VST3 file>: a real plugin onto Kick's Line through the
            // mixer, its window opened as a click on the slot opens it; it must still hold its
            // editor seconds later (owner, 2026-11-20: windows flashed and vanished).
            if (const auto pluginPath = SystemStats::getEnvironmentVariable ("CARTHAGE_SNAPSHOT_PLUGIN", {}); pluginPath.isNotEmpty())
            {
                const auto scanner = File::getSpecialLocation (File::currentExecutableFile).getParentDirectory().getParentDirectory()
                                         .getParentDirectory().getSiblingFile ("../src/scan/carthage_scan_artefacts/Release/carthage_scan");
                const auto outcome = carthage::engine::scan::runScanner (scanner, "VST3", pluginPath, 60000);
                std::cout << "plugin scan: " << (outcome.kind == carthage::engine::scan::Outcome::Kind::found ? "found" : outcome.detail.toStdString()) << "\n" << std::flush;

                if (outcome.kind == carthage::engine::scan::Outcome::Kind::found)
                {
                    onMessageThreadVoid ([this, &outcome]
                    {
                        session->getPlugins().addScanned (outcome.descriptions);
                        content->getMixer().addPluginToSelected (outcome.descriptions.getFirst());
                    });
                    Thread::sleep (800);
                    const auto opened = onMessageThread ([this]
                    {
                        auto* kick = carthage::editing::channels (session->getEdit())[2];
                        auto inserts = carthage::editing::insertsOf (*kick);
                        if (inserts.isEmpty()) return String ("no plugin on the Line");
                        content->getPluginWindows().open (*inserts.getFirst());
                        return String ("opened ") + inserts.getFirst()->getName();
                    });
                    std::cout << opened << "\n" << std::flush;
                    Thread::sleep (3000);
                    const auto state = onMessageThread ([this]
                    {
                        auto* kick = carthage::editing::channels (session->getEdit())[2];
                        auto inserts = carthage::editing::insertsOf (*kick);
                        if (inserts.isEmpty()) return String ("FAILED: the plugin is gone");
                        const auto open = content->getPluginWindows().isOpen (*inserts.getFirst());
                        auto* ext = dynamic_cast<tracktion::ExternalPlugin*> (inserts.getFirst());
                        return String (open ? "window open" : "FAILED: window closed") + ", instance "
                             + (ext != nullptr && ext->getAudioPluginInstance() != nullptr ? "alive" : "FAILED: gone");
                    });
                    std::cout << "after 3 s: " << state << "\n" << std::flush;
                    capture (outDir.getChildFile ("mixer-plugin.png"));
                    onMessageThreadVoid ([this] { content->getPluginWindows().closeAll(); });
                }
            }

            onMessageThreadVoid ([this] { commands.invokeDirectly (app::commands::toggleMixer, false); });

            onMessageThreadVoid ([this] { content->toggleSettings(); });
            Thread::sleep (300);
            capture (outDir.getChildFile ("settings.png"));
            onMessageThreadVoid ([this] { content->getSettings().setSearch ("metronome"); });
            capture (outDir.getChildFile ("settings-search.png"));
            onMessageThreadVoid ([this] { content->getSettings().setSearch ({}); });
            onMessageThreadVoid ([this] { content->getSettings().setBetaUpdates (true, true, {});
                                          content->getSettings().showUpdatesSection(); });
            capture (outDir.getChildFile ("settings-updates.png"));
            onMessageThreadVoid ([this] { content->getSettings().showShortcutsSection(); });
            capture (outDir.getChildFile ("settings-shortcuts.png"));
            onMessageThreadVoid ([this] { content->toggleSettings(); });

            // The update banner in its states, with sample text (no release is fetched).
            ui::UpdateBanner::Model update { "0.1.1", "Takes stay in time when the buffer changes\n"
                                                      "The settings remember your loopback cable\n"
                                                      "Check for updates from the app menu",
                                             ui::UpdateBanner::Stage::found, 0.0, false, true };
            onMessageThreadVoid ([&] { content->showUpdate (update); });
            capture (outDir.getChildFile ("update.png"));
            update.stage = ui::UpdateBanner::Stage::downloading;
            update.progress = 0.42;
            update.installOnQuitChosen = true;
            onMessageThreadVoid ([&] { content->showUpdate (update); });
            capture (outDir.getChildFile ("update-downloading.png"));
            update.stage = ui::UpdateBanner::Stage::ready;
            onMessageThreadVoid ([&] { content->showUpdate (update); });
            capture (outDir.getChildFile ("update-ready.png"));
            onMessageThreadVoid ([&] { content->showUpdate (std::nullopt); });

            if (const auto id = argValue (args, "--plugin"); id.isNotEmpty())
            {
                const auto scanner = File::getSpecialLocation (File::currentExecutableFile).getSiblingFile ("carthage-scan");
                const auto outcome = engine::scan::runScanner (scanner, argValue (args, "--plugin-format", "VST3"), id, 60000);

                if (outcome.kind == engine::scan::Outcome::Kind::found)
                {
                    onMessageThreadVoid ([&]
                    {
                        session->getPlugins().addScanned (outcome.descriptions);
                        session->insertPlugin (outcome.descriptions.getFirst());
                        content->resized();
                    });
                    // The plugin row refreshes from the command target; nudge it the way the app does.
                    onMessageThreadVoid ([this] { commands.invokeDirectly (app::commands::bypassPlugin, false);
                                                  commands.invokeDirectly (app::commands::bypassPlugin, false); });
                    Thread::sleep (1000);
                    capture (outDir.getChildFile ("plugin.png"));
                }
                else
                {
                    std::cout << "plugin scan failed: " << outcome.detail << '\n';
                }
            }

            const auto seconds = argValue (args, "--record-seconds", "3").getIntValue();

            if (seconds > 0)
            {
                onMessageThreadVoid ([this] { commands.invokeDirectly (app::commands::record, false); });
                Thread::sleep (seconds * 1000);
                capture (outDir.getChildFile ("recording.png"));
                onMessageThreadVoid ([this] { commands.invokeDirectly (app::commands::record, false); });
                Thread::sleep (1500);   // the waveform thumbnail builds on its own thread
                capture (outDir.getChildFile ("take.png"));

                // Shading inside the beat (W41), on halves.
                onMessageThreadVoid ([this] { session->setBeatShading (2); content->getTimeline().gridChanged(); });
                capture (outDir.getChildFile ("take-shading.png"));

                // The sample editor on the take (W42).
                onMessageThreadVoid ([this]
                {
                    if (auto* c = carthage::editing::allClips (session->getEdit()).getFirst())
                        content->getTimeline().onClipDoubleClicked (*c);
                });
                Thread::sleep (600);   // its waveform reads in the background
                capture (outDir.getChildFile ("sample-editor.png"));

                // With a shape drawn on it (owner, 4e): rise, a dip with a bend, fall.
                onMessageThreadVoid ([this]
                {
                    if (auto* c = carthage::editing::allClips (session->getEdit()).getFirst())
                    {
                        const auto length = c->getPosition().getLength().inSeconds();
                        carthage::editing::begin (session->getEdit(), "Shape");
                        carthage::editing::setEnvelope (*c, { { 0.0, -60.0f, 0.0f }, { length * 0.1, 0.0f, 0.6f }, { length * 0.5, -12.0f, -0.6f },
                                                              { length * 0.8, 0.0f, 0.0f }, { length, -60.0f, 0.0f } });
                        session->getEdit().getUndoManager().dispatchPendingMessages();
                    }

                    content->projectStateChanged();   // what the app's session hook does after an edit
                });
                Thread::sleep (300);
                capture (outDir.getChildFile ("sample-editor-shape.png"));

                // Reversed (owner, 4f): the waveform and the shape drawn mirrored, here and on the lane.
                onMessageThreadVoid ([this]
                {
                    if (auto* c = carthage::editing::allClips (session->getEdit()).getFirst())
                    {
                        carthage::editing::begin (session->getEdit(), "Reverse sample");
                        auto sound = carthage::editing::soundOf (*c);
                        sound.reversed = true;
                        carthage::editing::setSound (*c, sound);
                        session->getEdit().getUndoManager().dispatchPendingMessages();
                    }

                    content->projectStateChanged();
                });
                Thread::sleep (300);
                capture (outDir.getChildFile ("sample-editor-reversed.png"));
                onMessageThreadVoid ([this] { session->setBeatShading (0); content->getTimeline().gridChanged(); });
            }

            onMessageThreadVoid ([this] { content.reset(); session.reset(); });
            temp.deleteRecursively();
            return 0;
        }
    };
}

#if JUCE_WINDOWS
// carthage_juce is compiled once, as for a GUI app, so on Windows it holds JUCE's GUI entry point
// (JUCEApplicationBase::main(), which reads the command line from Windows) and not the console one
// START_JUCE_APPLICATION calls from a console program. Start through the GUI one.
int main()
{
    juce::JUCEApplicationBase::createInstance = []() -> juce::JUCEApplicationBase* { return new SnapshotApp(); };
    return juce::JUCEApplicationBase::main();
}
#else
START_JUCE_APPLICATION (SnapshotApp)
#endif
