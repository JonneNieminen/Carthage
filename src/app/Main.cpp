#include "Commands.h"
#include "Paths.h"
#include "platform/MicrophoneAccess.h"

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

    /** Shown while the system asks the user for access to audio inputs. */
    class WaitingForAccess final : public Component
    {
    public:
        WaitingForAccess()  { setSize (1280, 780); }

        void paint (Graphics& g) override
        {
            g.fillAll (theme::bg);
            g.setColour (theme::muted);
            g.setFont (theme::uiFont (theme::textLg));
            g.drawFittedText ("Carthage is asking macOS for access to your audio inputs.\n"
                              "Answer the system prompt to continue.",
                              getLocalBounds().reduced (theme::space8), Justification::centred, 3);
        }
    };

    class MainWindow final : public DocumentWindow
    {
    public:
        MainWindow()
            : DocumentWindow ("Carthage", theme::bg, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new WaitingForAccess(), false);
            setResizable (true, false);
            setResizeLimits (960, 600, 10000, 10000);
            centreWithSize (1280, 780);
            setVisible (true);
        }

        /** Swaps in the real window content once the engine exists. */
        ui::MainComponent& showSession (engine::Session& session, ApplicationCommandManager& commands)
        {
            const auto bounds = getBounds();
            auto* content = new ui::MainComponent (session, commands);
            setContentOwned (content, false);
            setBounds (bounds);

            commands.registerAllCommandsForTarget (content);
            commands.setFirstCommandTarget (content);
            addKeyListener (commands.getKeyMappings());
            content->grabKeyboardFocus();
            return *content;
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
            tooltips = std::make_unique<TooltipWindow> (nullptr, 600);
            window = std::make_unique<MainWindow>();

            // Ask for input access before the engine opens the device: on macOS an unanswered
            // prompt would otherwise block startup inside CoreAudio with no window on screen.
            requestMicrophoneAccess ([this] (bool granted) { start (granted); });
        }

        void start (bool inputAllowed)
        {
            engine::Folders folders;
            folders.settings = Paths::settings();
            folders.pluginCache = Paths::pluginCache();
            folders.project = Paths::currentProject();
            folders.recordings = Paths::recordingsFor (folders.project);

            session = std::make_unique<engine::Session> (folders, Paths::scanner());

            menuBar = std::make_unique<MenuBar> (commands);
            auto& content = window->showSession (*session, commands);

           #if JUCE_MAC
            auto apple = menuBar->appleMenu();
            MenuBarModel::setMacMainMenu (menuBar.get(), &apple);
           #else
            window->setMenuBar (menuBar.get());
           #endif

            if (! inputAllowed)
                content.showProblem ("Carthage can not hear your inputs. Allow it in System Settings, "
                                          "Privacy & Security, Microphone, then restart Carthage.");
        }

        void shutdown() override
        {
           #if JUCE_MAC
            MenuBarModel::setMacMainMenu (nullptr);
           #endif
            window.reset();      // components go before the engine they show
            tooltips.reset();
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
