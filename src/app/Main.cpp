#include "Commands.h"
#include "Paths.h"
#include "Updater.h"
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

        StringArray getMenuBarNames() override
        {
           #if JUCE_MAC
            return { "File", "Transport", "Input", "Plugin", "Edit", "Tools", "Layouts", "Window" };
           #else
            return { "File", "Transport", "Input", "Plugin", "Edit", "Tools", "Layouts", "Window", "Help" };   // macOS has the app menu for Help's item
           #endif
        }

        PopupMenu getMenuForIndex (int index, const String&) override
        {
            PopupMenu m;

            switch (index)
            {
                case 0:
                    m.addCommandItem (&commands, commands::newProject);
                    m.addCommandItem (&commands, commands::openProject);
                    m.addSeparator();
                    m.addCommandItem (&commands, commands::closeProject);
                    m.addCommandItem (&commands, commands::saveProject);
                    m.addCommandItem (&commands, commands::saveProjectAs);
                    m.addCommandItem (&commands, commands::saveAsTemplate);
                    m.addSeparator();
                    m.addCommandItem (&commands, commands::collectSamples);
                    m.addSeparator();
                    m.addCommandItem (&commands, commands::startScreen);
                   #if ! JUCE_MAC
                    m.addSeparator();   // macOS has these in the app menu
                    m.addCommandItem (&commands, commands::audioSettings);
                    m.addCommandItem (&commands, commands::keyboardShortcuts);
                   #endif
                    break;
                case 1:
                    m.addCommandItem (&commands, commands::playStop);
                    m.addCommandItem (&commands, commands::record);
                    m.addCommandItem (&commands, commands::returnToStart);
                    break;
                case 2:
                    m.addCommandItem (&commands, commands::toggleMonitoring);
                    m.addSeparator();
                    m.addCommandItem (&commands, commands::measureLatency);
                    break;
                case 3:
                    m.addCommandItem (&commands, commands::choosePlugin);
                    m.addCommandItem (&commands, commands::openPluginEditor);
                    m.addCommandItem (&commands, commands::bypassPlugin);
                    m.addSeparator();
                    m.addCommandItem (&commands, commands::keepPluginOnTop);
                    break;
                case 4:
                    for (auto id : { commands::undo, commands::redo })
                        m.addCommandItem (&commands, id);
                    m.addSeparator();
                    for (auto id : { commands::cutClips, commands::copyClips, commands::pasteClips })
                        m.addCommandItem (&commands, id);
                    m.addSeparator();
                    for (auto id : { commands::splitAtPlayhead, commands::duplicateClips, commands::deleteClips,
                                     commands::nudgeLeft, commands::nudgeRight })
                        m.addCommandItem (&commands, id);
                    m.addSeparator();
                    for (auto id : { commands::addMarker, commands::loopSelection, commands::toggleGrid })
                        m.addCommandItem (&commands, id);
                    m.addSeparator();
                    for (auto id : { commands::lanesTaller, commands::lanesShorter, commands::uiLarger, commands::uiSmaller, commands::uiActualSize })
                        m.addCommandItem (&commands, id);
                    break;
                case 5:
                    for (auto id : commands::tools())
                        m.addCommandItem (&commands, id);
                    break;
                case 6:
                {
                    // Layouts (F21): the slots Settings offers, saving into them, the default, and
                    // which views are in windows of their own.
                    const auto count = slots ? slots() : 4;

                    for (int i = 0; i < count; ++i)
                        m.addCommandItem (&commands, commands::recallLayoutFirst + i);

                    PopupMenu save;
                    for (int i = 0; i < count; ++i)
                        save.addCommandItem (&commands, commands::saveLayoutFirst + i);

                    m.addSubMenu ("Save layout as", save);
                    m.addSeparator();
                    m.addCommandItem (&commands, commands::defaultLayout);
                    m.addSeparator();

                    for (auto id : { commands::windowSamples, commands::windowPlugins, commands::windowTimeline,
                                     commands::windowSignal, commands::windowSettings })
                        m.addCommandItem (&commands, id);
                    break;
                }
                case 7:
                    m.addCommandItem (&commands, commands::commandPalette);
                    m.addSeparator();
                   #if JUCE_MAC
                    m.addCommandItem (&commands, commands::minimiseWindow);
                   #endif
                    m.addCommandItem (&commands, commands::toggleFullScreen);
                    m.addSeparator();
                    m.addCommandItem (&commands, commands::sampleBrowser);
                    m.addCommandItem (&commands, commands::pluginBrowser);
                    m.addSeparator();
                    m.addCommandItem (&commands, commands::nextProject);
                    m.addCommandItem (&commands, commands::previousProject);
                    break;
                case 8:
                    m.addCommandItem (&commands, commands::checkForUpdates);
                    break;
                default:
                    break;
            }

            return m;
        }

        void menuItemSelected (int, int) override {}

        /** How many layout slots Settings offers (F21). */
        std::function<int()> slots;

        /** The app menu on macOS: settings live there, as on every Mac app. */
        PopupMenu appleMenu()
        {
            PopupMenu m;
            m.addCommandItem (&commands, commands::checkForUpdates);
            m.addSeparator();
            m.addCommandItem (&commands, commands::audioSettings);
            m.addCommandItem (&commands, commands::keyboardShortcuts);
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

        void initialise (const String& commandLine) override
        {
            // A project given on the command line (or by the OS) opens once the engine is up.
            for (const auto& arg : StringArray::fromTokens (commandLine, true))
                if (const File f (arg.unquoted()); File::isAbsolutePath (arg.unquoted()) && f.hasFileExtension (carthage::project::extension))
                    projectToOpen = f;

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
            folders.project = Paths::newUntitledProject();
            folders.autosave = Paths::autosave();

            // Before the session marks its own new project as open: what did the last run leave?
            const auto unclosed = carthage::autosave::Store (folders.autosave).findUnclosed();

            session = std::make_unique<engine::Session> (folders, Paths::scanner());
            session->onProjectStateChanged = [this]
            {
                updateTitle();

                if (auto* c = content())
                    c->projectStateChanged();

                commands.commandStatusChanged();   // next/previous project follow the tab count
            };

            menuBar = std::make_unique<MenuBar> (commands);
            auto& content = window->showSession (*session, commands);
            menuBar->slots = [this] { auto* c = this->content(); return c != nullptr ? c->layoutSlots() : 4; };
            commands.registerAllCommandsForTarget (this);   // check for updates, quit
            restoreShortcuts();

           #if JUCE_MAC
            auto apple = menuBar->appleMenu();
            MenuBarModel::setMacMainMenu (menuBar.get(), &apple);
           #else
            window->setMenuBar (menuBar.get());
           #endif

            if (! inputAllowed)
                content.showProblem ("Carthage can not hear your inputs. Allow it in System Settings, "
                                          "Privacy & Security, Microphone, then restart Carthage.");

            startUpdater (content);
            connectStartScreen();
            updateTitle();

            // A project named at launch opens; else a crash's copy is offered; else what Settings
            // says: the start screen (F20), the last session (F21), or the empty project.
            using AtLaunch = engine::Session::AtLaunch;

            if (projectToOpen.existsAsFile())
                report (session->openProject (projectToOpen));
            else if (! unclosed.empty())
                offerRecovery (unclosed);
            else if (session->getAtLaunch() == AtLaunch::lastSession && reopenLastSession())
                {}
            else if (session->getAtLaunch() != AtLaunch::emptyProject)
                content.showStartScreen (true);

            // Plugins installed or updated since the last run are checked in the background;
            // --rescan-plugins checks them all (for measuring the UI during a full scan).
            session->getPluginScanner().start (getCommandLineParameterArray().contains ("--rescan-plugins"));
        }

        /** Updates (docs/RELEASING.md): the banner, the beta toggle, a quiet check on launch. */
        void startUpdater (ui::MainComponent& content)
        {
            Updater::Hooks hooks;
            hooks.isBusy = [this] { return session != nullptr && (session->isPlaying() || session->isRecording()); };

            // Nothing in Carthage is unsaved yet (projects arrive in phase 4); a take in progress
            // is the only work to protect, and isBusy already refuses a restart during one.
            // A restart for an update saves nothing by itself: with unsaved changes it waits for
            // the user (the banner's restart then asks to save, like quitting).
            hooks.prepareToRestart = [this]
            {
                if (session == nullptr)
                    return true;

                if (session->isRecording())
                    return false;

                for (int i = 0; i < session->getNumProjects(); ++i)
                    if (session->describeProject (i).unsaved)
                        return false;   // any tab: the update waits until they are saved

                rememberSession();
                session->close();
                return true;
            };

            hooks.showOffer = [&content] (std::optional<Updater::Offer> offer)
            {
                std::optional<ui::UpdateBanner::Model> model;

                if (offer)
                    model = ui::UpdateBanner::Model { offer->version, offer->notes,
                                                      static_cast<ui::UpdateBanner::Stage> (offer->stage),
                                                      offer->progress, offer->installOnQuitChosen, offer->canInstallOnQuit };

                content.showUpdate (std::move (model));
            };

            hooks.showMessage = [&content] (const String& text, bool warning) { content.showMessage (text, warning); };

            updater = Updater::create (std::move (hooks));

            auto& banner = content.getUpdateBanner();
            banner.onInstallOnQuit = [this] { updater->installOnQuit(); };
            banner.onRestartNow = [this] { updater->restartNow(); };
            banner.onDismiss = [this] { updater->dismiss(); };

            auto& settings = content.getSettings();
            settings.setBetaUpdates (updater->getReceivesBetaUpdates(), updater->isAvailable(),
                                     updater->isAvailable() ? String() : updater->whyUnavailable());
            settings.onCheckForUpdates = [this] { updater->checkNow(); };
            settings.onBetaUpdatesChanged = [this] (bool beta)
            {
                updater->setReceivesBetaUpdates (beta);
                updater->checkInBackground();   // the other feed may have something
            };

            updater->checkInBackground();
        }

        //==========================================================================================
        // Application-level commands (the window passes on what it does not handle).
        void getAllCommands (Array<CommandID>& ids) override
        {
            JUCEApplication::getAllCommands (ids);
            ids.addArray (commands::application());
        }

        void getCommandInfo (CommandID id, ApplicationCommandInfo& info) override
        {
            if (commands::application().contains (id))
            {
                commands::describe (id, info);
                if (id == commands::checkForUpdates)
                    info.setActive (updater != nullptr);
                else if (id == commands::minimiseWindow || id == commands::toggleFullScreen)
                    info.setActive (window != nullptr);
                else if (id == commands::nextProject || id == commands::previousProject)
                    info.setActive (session != nullptr && ! session->isRecording() && session->getNumProjects() > 1);
                else
                    info.setActive (session != nullptr && ! session->isRecording());

                if (id == commands::toggleFullScreen && window != nullptr)
                    info.setTicked (window->isFullScreen());
                return;
            }

            JUCEApplication::getCommandInfo (id, info);
        }

        bool perform (const InvocationInfo& info) override
        {
            switch (info.commandID)
            {
                case commands::checkForUpdates:
                    if (updater != nullptr)
                        updater->checkNow();
                    return true;
                case commands::newProject:      newProject(); return true;
                case commands::openProject:     openProject(); return true;
                case commands::saveProject:     save ({}); return true;
                case commands::saveProjectAs:   saveAs ({}); return true;
                case commands::closeProject:    closeProject(); return true;
                case commands::startScreen:     if (auto* c = content()) c->showStartScreen (true); return true;
                case commands::saveAsTemplate:  saveAsTemplate(); return true;
                case commands::collectSamples:
                    session->collectSamples ([this] (const Result& r, int copied)
                    {
                        report (r, copied == 0 ? String ("Every sample is already in the project")
                                               : copied == 1 ? String ("Copied a sample into the project")
                                                             : "Copied " + String (copied) + " samples into the project");
                    });
                    return true;
                case commands::nextProject:
                case commands::previousProject:
                {
                    const auto n = session->getNumProjects();
                    const auto step = info.commandID == commands::nextProject ? 1 : n - 1;
                    report (session->showProject ((session->getFrontIndex() + step) % n));
                    return true;
                }
                case commands::minimiseWindow:  if (window != nullptr) window->setMinimised (true); return true;
                case commands::toggleFullScreen:
                    if (window != nullptr) window->setFullScreen (! window->isFullScreen());
                    return true;
                default:                        return JUCEApplication::perform (info);
            }
        }

        //==========================================================================================
        // Shortcuts: only the user's changes are kept, so new defaults in an update still arrive.
        struct ShortcutSaver final : ChangeListener
        {
            explicit ShortcutSaver (KeyPressMappingSet& m) : mappings (m)   { mappings.addChangeListener (this); }
            ~ShortcutSaver() override                                       { mappings.removeChangeListener (this); }

            void changeListenerCallback (ChangeBroadcaster*) override
            {
                if (auto xml = mappings.createXml (true))
                    if (Paths::shortcuts().getParentDirectory().createDirectory())
                        xml->writeTo (Paths::shortcuts());
            }

            KeyPressMappingSet& mappings;
        };

        void restoreShortcuts()
        {
            auto* mappings = commands.getKeyMappings();

            if (auto xml = parseXML (Paths::shortcuts()))
                mappings->restoreFromXml (*xml);

            shortcutSaver = std::make_unique<ShortcutSaver> (*mappings);
        }

        //==========================================================================================
        // Projects
        ui::MainComponent* content() const
        {
            return window != nullptr ? dynamic_cast<ui::MainComponent*> (window->getContentComponent()) : nullptr;
        }

        void report (const Result& r, const String& success = {})
        {
            if (auto* c = content())
            {
                if (r.failed())
                    c->showMessage (r.getErrorMessage(), true);
                else if (success.isNotEmpty())
                    c->showMessage (success, false);
            }
        }

        void updateTitle()
        {
            if (window == nullptr || session == nullptr)
                return;

            const auto edited = session->hasUnsavedChanges();
            auto title = session->getProjectName();

           #if ! JUCE_MAC
            if (edited)
                title << " *";   // macOS shows it in the close button instead
           #endif

            window->setName (title + String::fromUTF8 (" \xe2\x80\x94 Carthage"));

            if (auto* peer = window->getPeer())
                peer->setDocumentEditedStatus (edited);
        }

        /** Runs `then` once the current project may be closed: at once if nothing is unsaved,
            otherwise after asking (and saving, if the user says so). */
        void whenClosable (const std::function<void()>& then)
        {
            if (session == nullptr || ! session->hasUnsavedChanges())
            {
                then();
                return;
            }

            enum { saveIt = 1, discard = 2, cancel = 0 };

            const auto options = MessageBoxOptions()
                                     .withIconType (MessageBoxIconType::QuestionIcon)
                                     .withTitle (String::fromUTF8 ("Save changes to \xe2\x80\x9c") + session->getProjectName() + String::fromUTF8 ("\xe2\x80\x9d?"))
                                     .withMessage ("If you don't save, the changes since you last saved are lost.")
                                     .withButton (MessageBoxButtonOptions ("Save", saveIt).withPreferredAction())
                                     .withButton (MessageBoxButtonOptions ("Don't save", discard).withRole (MessageBoxButtonRole::destroy))
                                     .withButton (MessageBoxButtonOptions ("Cancel", cancel).withRole (MessageBoxButtonRole::cancel))
                                     .withAssociatedComponent (content());

            AlertWindow::showAsync (options, [this, then] (int result)
            {
                if (result == saveIt)
                    save ([then] (bool saved) { if (saved) then(); });
                else if (result == discard)
                    then();
            });
        }

        /** The folders of the open tabs: a new untitled one must not share a name with them. */
        Array<File> openFolders() const
        {
            Array<File> folders;

            for (int i = 0; i < session->getNumProjects(); ++i)
                folders.add (session->describeProject (i).file.getParentDirectory());

            return folders;
        }

        /** A new tab (it takes the place of an untouched untitled one). */
        void newProject (const File& templateFile = {})
        {
            report (session->newProject (Paths::newUntitledProject (openFolders()), templateFile));
        }

        void openProject()
        {
            chooser = std::make_unique<FileChooser> ("Open a project", Paths::projects(), "*.carthage");
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                                  [this] (const FileChooser& fc)
                                  {
                                      if (const auto file = fc.getResult(); file != File())
                                          report (session->openProject (file));
                                  });
        }

        /** Asks for a name, then keeps a copy of the front project as a template (F20). */
        void saveAsTemplate()
        {
            auto* w = new AlertWindow ("Save as template", "A name for the template. New projects can start from it on the start screen.",
                                       MessageBoxIconType::NoIcon, content());
            w->addTextEditor ("name", session->isUntitled() ? String ("My template") : session->getProjectName());
            w->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
            w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));

            w->enterModalState (true, ModalCallbackFunction::create ([this, w] (int result)
            {
                const auto name = File::createLegalFileName (w->getTextEditorContents ("name").trim());

                if (result != 1 || name.isEmpty())
                    return;

                const auto folder = Paths::templates().getChildFile (name);

                if (folder.exists())
                {
                    report (Result::fail ("There is already a template called " + name + ". Choose another name."));
                    return;
                }

                session->saveCopy (folder, [this, name] (const Result& r) { report (r, "Saved the template " + name); });
            }), true);
        }

        /** The start screen's choices go where the File menu's do. */
        void connectStartScreen()
        {
            auto* c = content();

            if (c == nullptr)
                return;

            auto& start = c->getStartScreen();
            start.setTemplatesFolder (Paths::templates());
            start.onNew = [this] (const File& templateFile) { newProject (templateFile); };
            start.onOpen = [this] (const File& file)
            {
                if (auto r = session->openProject (file); r.failed())
                {
                    report (r);
                    session->forgetRecentProject (file);
                }
            };
            start.onOpenOther = [this] { openProject(); };
            start.onClose = [this] { if (auto* m = content()) m->showStartScreen (false); };
            start.onShowAtStartChanged = [this] (bool on)
            {
                session->setAtLaunch (on ? engine::Session::AtLaunch::startScreen : engine::Session::AtLaunch::emptyProject);

                if (auto* m = content())
                    m->getSettings().refresh();
            };
        }

        /** Cmd/Ctrl+W (W34): closes the front tab after asking; the last one closes the window. */
        void closeProject()
        {
            if (session->getNumProjects() < 2)
            {
                systemRequestedQuit();
                return;
            }

            if (session->isRecording())
                session->stopRecording();

            whenClosable ([this] { session->closeProject(); });
        }

        /** Quitting: each tab with unsaved changes comes forward in turn and is asked about; a
            Cancel stops the quit with the rest still open. */
        void closeEveryProjectThenQuit()
        {
            for (int i = 0; i < session->getNumProjects(); ++i)
            {
                if (! session->describeProject (i).unsaved)
                    continue;

                [[maybe_unused]] auto r = session->showProject (i);

                whenClosable ([this]
                {
                    if (session->getNumProjects() > 1)
                    {
                        session->closeProject();
                        closeEveryProjectThenQuit();
                    }
                    else
                    {
                        session->close();
                        quit();
                    }
                });
                return;
            }

            session->close();
            quit();
        }

        /** Saves; asks where first if the project was never saved. `then` gets whether it was. */
        void save (const std::function<void (bool)>& then)
        {
            if (session->isUntitled())
            {
                saveAs (then);
                return;
            }

            // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks): std::function owns the copy; the analyzer loses it
            session->save ([this, then] (const Result& r)
            {
                report (r, "Saved");

                if (then)
                    then (r.wasOk());
            });
        }

        /** A project is a folder: the name the user types becomes the folder and the file in it. */
        void saveAs (const std::function<void (bool)>& then)
        {
            const auto suggested = session->isUntitled() ? Paths::projects().getChildFile ("My song")
                                                          : session->getProjectFolder().getSiblingFile (session->getProjectName() + " copy");

            chooser = std::make_unique<FileChooser> ("Save the project as", suggested, String());
            chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting,
                                  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks): as in save()
                                  [this, then] (const FileChooser& fc)
                                  {
                                      auto folder = fc.getResult();

                                      if (folder == File())
                                      {
                                          if (then) then (false);
                                          return;
                                      }

                                      if (folder.hasFileExtension (carthage::project::extension))
                                          folder = folder.withFileExtension ({});

                                      if (folder.existsAsFile())
                                      {
                                          report (Result::fail (folder.getFileName() + " is a file. Choose another name."));
                                          if (then) then (false);
                                          return;
                                      }

                                      session->saveAs (folder, [this, then, folder] (const Result& r)
                                      {
                                          report (r, "Saved as " + folder.getFileName());

                                          if (then)
                                              then (r.wasOk());
                                      });
                                  });
        }

        /** Carthage did not quit cleanly last time: offer the newest autosave. */
        void offerRecovery (const std::vector<carthage::autosave::Store::Recovery>& unclosed)
        {
            if (unclosed.empty())
                return;

            auto* store = session->getAutosaves();

            // Only the newest is offered; the others keep their autosaves on disk.
            for (size_t i = 1; i < unclosed.size(); ++i)
                store->markClosed (unclosed[i].projectId);

            const auto& r = unclosed.front();
            const auto name = r.projectFile.getParentDirectory().getFileName();

            const auto options = MessageBoxOptions()
                                     .withIconType (MessageBoxIconType::WarningIcon)
                                     .withTitle ("Carthage did not close properly")
                                     .withMessage (String::fromUTF8 ("It saved a copy of \xe2\x80\x9c") + name + String::fromUTF8 ("\xe2\x80\x9d at ")
                                                   + r.savedAt.formatted ("%H:%M on %e %b") + ". Open it?")
                                     .withButton (MessageBoxButtonOptions ("Open the copy", 1).withPreferredAction())
                                     .withButton (MessageBoxButtonOptions ("Not now", 0).withRole (MessageBoxButtonRole::cancel))
                                     .withAssociatedComponent (content());

            AlertWindow::showAsync (options, [this, r] (int result)
            {
                if (result == 1)
                    report (session->recover (r), "Opened the copy. Save it to keep it.");
                else if (auto* s = session->getAutosaves())
                    s->markClosed (r.projectId);
            });
        }

        void shutdown() override
        {
           #if JUCE_MAC
            MenuBarModel::setMacMainMenu (nullptr);
           #endif
            shortcutSaver.reset();
            updater.reset();     // its hooks point at the window and the engine
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

            if (session == nullptr)
            {
                quit();
                return;
            }

            rememberSession();   // before the tabs close one by one
            closeEveryProjectThenQuit();
        }

        /** The saved projects open now, the front one, and the window layout (F21). */
        void rememberSession()
        {
            XmlElement xml ("SESSION");
            xml.setAttribute ("front", session->getFrontIndex());

            for (int i = 0; i < session->getNumProjects(); ++i)
                if (const auto p = session->describeProject (i); ! p.untitled)
                    xml.createNewChildElement ("PROJECT")->setAttribute ("path", p.file.getFullPathName());

            if (auto* c = content())
                if (auto layout = c->getLayout().createXml())
                    xml.addChildElement (layout.release());

            session->getEngine().getPropertyStorage().getPropertiesFile().setValue ("lastSession", xml.toString (XmlElement::TextFormat().singleLine()));
        }

        /** At launch: the projects and layout Carthage quit with. False if there was none. */
        bool reopenLastSession()
        {
            const auto xml = parseXML (session->getEngine().getPropertyStorage().getPropertiesFile().getValue ("lastSession"));

            if (xml == nullptr || ! xml->hasTagName ("SESSION"))
                return false;

            int opened = 0;

            for (auto* p : xml->getChildWithTagNameIterator ("PROJECT"))
                if (const File f (p->getStringAttribute ("path")); f.existsAsFile() && session->openProject (f).wasOk())
                    ++opened;

            if (opened > 0)
                [[maybe_unused]] auto r = session->showProject (std::clamp (xml->getIntAttribute ("front"), 0, session->getNumProjects() - 1));

            if (auto* layout = xml->getChildByName ("LAYOUT"); layout != nullptr)
                if (auto* c = content())
                    c->applyLayout (ValueTree::fromXml (*layout));

            return opened > 0;
        }

    private:
        ui::LookAndFeel lookAndFeel;
        ApplicationCommandManager commands;
        std::unique_ptr<engine::Session> session;
        std::unique_ptr<MenuBar> menuBar;
        std::unique_ptr<MainWindow> window;
        std::unique_ptr<TooltipWindow> tooltips;
        std::unique_ptr<Updater> updater;
        std::unique_ptr<FileChooser> chooser;
        std::unique_ptr<ShortcutSaver> shortcutSaver;
        File projectToOpen;
    };
}

START_JUCE_APPLICATION (carthage::app::Application)
