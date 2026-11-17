// carthage-snapshot: renders the app's real window content to PNG files, in fixed states, on the real
// engine. For checking the UI against design/README.md without a screen (a locked Mac, CI later),
// and for README screenshots. It works in a temporary folder and never touches the user's settings.
//
//   carthage-snapshot OUTPUT_FOLDER [--device NAME] [--output-device NAME]
//                     [--plugin-format VST3 --plugin FILE_OR_ID] [--record-seconds 3]
//
// Writes main.png, settings.png, settings-updates.png, update*.png (the update banner with sample text), plugin.png
// (with --plugin) and take.png (after recording).

#include <app/Commands.h>
#include <engine/Session.h>
#include <ui/MainComponent.h>
#include <ui/theme/LookAndFeel.h>

#include <iostream>

using namespace carthage;
using namespace juce;

namespace
{
    template <typename F>
    auto onMessageThread (F&& f)  { return *MessageManager::callSync (std::forward<F> (f)); }

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

            std::cout << "wrote " << file.getFullPathName() << std::endl;
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
                f.recordings = f.project.getChildFile ("Recordings");
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

            Thread::sleep (1500);
            capture (outDir.getChildFile ("main.png"));

            onMessageThreadVoid ([this] { content->toggleSettings(); });
            Thread::sleep (300);
            capture (outDir.getChildFile ("settings.png"));
            onMessageThreadVoid ([this] { content->getSettings().showUpdatesSection(); });
            capture (outDir.getChildFile ("settings-updates.png"));
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
                    std::cout << "plugin scan failed: " << outcome.detail << std::endl;
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
