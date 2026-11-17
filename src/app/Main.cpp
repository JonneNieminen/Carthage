#include "Commands.h"
#include "Paths.h"

#include <engine/Session.h>
#include <ui/MainComponent.h>
#include <ui/theme/LookAndFeel.h>

namespace carthage::app
{
    using namespace juce;

    /** The menu bar reads the command table: names, shortcuts and ticks come from there. */
    class MenuBar final : public MenuBarModel
    {
    public:
        explicit MenuBar (ApplicationCommandManager& c) : commands (c)
        {
            setApplicationCommandManagerToWatch (&commands);
        }

        StringArray getMenuBarNames() override   { return { "Transport", "Input", "Plugin" }; }

        PopupMenu getMenuForIndex (int index, const String&) override
        {
            PopupMenu m;

            switch (index)
            {
                case 0:
                    m.addCommandItem (&commands, commands::playStop);
                    m.addCommandItem (&commands, commands::record);
                    m.addCommandItem (&commands, commands::returnToStart);
                    break;
                case 1:
                    m.addCommandItem (&commands, commands::toggleMonitoring);
                    m.addSeparator();
                    m.addCommandItem (&commands, commands::measureLatency);
                    break;
                case 2:
                    m.addCommandItem (&commands, commands::choosePlugin);
                    m.addCommandItem (&commands, commands::openPluginEditor);
                    m.addCommandItem (&commands, commands::bypassPlugin);
                    break;
                default:
                    break;
            }

            return m;
        }

        void menuItemSelected (int, int) override {}

        /** The app menu on macOS: settings live there, as on every Mac app. */
        PopupMenu appleMenu()
        {
            PopupMenu m;
            m.addCommandItem (&commands, commands::audioSettings);
            return m;
        }

    private:
        ApplicationCommandManager& commands;
    };

    class MainWindow final : public DocumentWindow
    {
    public:
        MainWindow (engine::Session& session, ApplicationCommandManager& commands)
            : DocumentWindow ("Carthage", theme::bg, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            auto* content = new ui::MainComponent (session, commands);
            setContentOwned (content, false);

            commands.registerAllCommandsForTarget (content);
            commands.setFirstCommandTarget (content);
            addKeyListener (commands.getKeyMappings());

            setResizable (true, false);
            setResizeLimits (960, 600, 10000, 10000);
            centreWithSize (1280, 780);
            setVisible (true);
            content->grabKeyboardFocus();
        }

        void closeButtonPressed() override   { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    class Application final : public JUCEApplication
    {
    public:
        const String getApplicationName() override      { return "Carthage"; }
        const String getApplicationVersion() override   { return CARTHAGE_VERSION_STRING; }
        bool moreThanOneInstanceAllowed() override      { return false; }

        void initialise (const String&) override
        {
            LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

            engine::Folders folders;
            folders.settings = Paths::settings();
            folders.pluginCache = Paths::pluginCache();
            folders.project = Paths::currentProject();
            folders.recordings = Paths::recordingsFor (folders.project);

            session = std::make_unique<engine::Session> (folders, Paths::scanner());

            menuBar = std::make_unique<MenuBar> (commands);
            window = std::make_unique<MainWindow> (*session, commands);

           #if JUCE_MAC
            auto apple = menuBar->appleMenu();
            MenuBarModel::setMacMainMenu (menuBar.get(), &apple);
           #else
            window->setMenuBar (menuBar.get());
           #endif

            tooltips = std::make_unique<TooltipWindow> (nullptr, 600);
        }

        void shutdown() override
        {
           #if JUCE_MAC
            MenuBarModel::setMacMainMenu (nullptr);
           #endif
            tooltips.reset();
            window.reset();
            menuBar.reset();
            session.reset();
            LookAndFeel::setDefaultLookAndFeel (nullptr);
        }

        void systemRequestedQuit() override
        {
            // Never lose a take in progress: finish and save it before quitting.
            if (session != nullptr && session->isRecording())
                session->stopRecording();

            quit();
        }

    private:
        ui::LookAndFeel lookAndFeel;
        ApplicationCommandManager commands;
        std::unique_ptr<engine::Session> session;
        std::unique_ptr<MenuBar> menuBar;
        std::unique_ptr<MainWindow> window;
        std::unique_ptr<TooltipWindow> tooltips;
    };
}

START_JUCE_APPLICATION (carthage::app::Application)
