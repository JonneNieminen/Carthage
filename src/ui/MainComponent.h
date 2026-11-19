#pragma once

#include <engine/time/TapTempo.h>
#include "Controls.h"
#include "PluginBrowser.h"
#include "SampleBrowser.h"
#include "StartScreen.h"
#include "CommandPalette.h"
#include "ProjectTabs.h"
#include "SettingsPanel.h"
#include "Timeline.h"
#include "UpdateBanner.h"

#include <app/Commands.h>
#include <engine/Session.h>
#include <engine/SettingsTest.h>

#include <array>
#include <map>

namespace carthage::ui
{
    class PluginWindow;

    /** Phase 1's window: transport across the top, the signal path (input -> plugin -> output) on
        one line, and the lane of takes below it. The audio settings slide in from the right.

        Animates only from the display's frame callback (VBlankAttachment): meters, the playhead,
        the load figure. Nothing else moves. */
    class MainComponent final : public juce::Component, public juce::ApplicationCommandTarget,
                                public juce::DragAndDropContainer, private juce::ChangeListener
    {
    public:
        MainComponent (engine::Session&, juce::ApplicationCommandManager&);
        ~MainComponent() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        // ApplicationCommandTarget
        juce::ApplicationCommandTarget* getNextCommandTarget() override  { return nullptr; }
        void getAllCommands (juce::Array<juce::CommandID>&) override;
        void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
        bool perform (const InvocationInfo&) override;

        void toggleSettings();

        /** The open projects changed (name, unsaved state, order): the tab strip follows. */
        void projectStateChanged()   { tabs.refresh(); }

        /** Shows a problem the user has to fix outside Carthage, in the status line. */
        void showProblem (const juce::String& text)    { setStatus (text, theme::warn); }

        /** A short message in the status line (update checks the user asked for). */
        void showMessage (const juce::String& text, bool warning)  { setStatus (text, warning ? theme::warn : theme::muted); }

        /** The update banner and the settings' update toggle, for the app's updater. */
        void showUpdate (std::optional<UpdateBanner::Model>);
        UpdateBanner& getUpdateBanner() noexcept    { return updateBanner; }
        SettingsPanel& getSettings() noexcept       { return settings; }
        PluginBrowser& getPluginBrowser() noexcept  { return browser; }
        Timeline& getTimeline() noexcept           { return timeline; }
        SampleBrowser& getSampleBrowser() noexcept  { return samples; }
        StartScreen& getStartScreen() noexcept      { return startScreen; }

        /** The start screen over the window, below the tabs (F20). */
        void showStartScreen (bool);
        bool isStartScreenShowing() const noexcept  { return startScreen.isVisible(); }

        /** Shows the left panel on samples or plugins; with `toggle`, the same view again hides it. */
        void showPanel (bool samplesView, bool toggle);

        //==========================================================================================
        // Views in their own windows (F21). Each view is docked in this window or shown in a
        // window of its own; a layout is where every view and window is.
        enum class View { samples, plugins, timeline, signal, settings };
        static constexpr std::array<View, 5> views { View::samples, View::plugins, View::timeline, View::signal, View::settings };
        static juce::String nameOf (View);

        bool isDetached (View view) const   { return viewWindows.count (static_cast<int> (view)) > 0; }
        void setDetached (View, bool detached, std::optional<juce::Rectangle<int>> bounds = {});
        void toggleDetached (View);

        /** Every view back in this window, the panel and settings as they start (the default). */
        void defaultLayout();

        /** Layout slots offered (Settings, 1 to 9) and the plugin windows' place (F21). */
        int layoutSlots();
        bool keepPluginOnTop();


        juce::ValueTree getLayout() const;
        void applyLayout (const juce::ValueTree&);

        /** Cmd/Ctrl+W in a view's own window sends the view back, and in the plugin window closes
            it (W38). True when the window in front was one of those, so the key was used. */
        bool closeFrontWindow();

        /** Layout slot `n` (1..9) by its name: "Layout n" until the person names it (W38). */
        juce::String layoutName (int n);
        void showUndoHistory();

        /** Export log (W38): this run's log with the computer and audio device on top, home as ~. */
        void exportLog();
        void saveOrCopy (const juce::String& title, const juce::String& text, const juce::String& fileName);

        /** The settings test (W41): runs silently, then asks Yes/No to use what it recommends.
            `atFirstLaunch` words the question for a first run. */
        void runSettingsTest (bool atFirstLaunch);

        /** After a crash: the note it left and the log of that run, offered to save or copy. */
        void offerCrashReport (const juce::String& note);
        void askToRenameLayout (int n);

        /** Settings: the left panel's width and the sample browser's text size (F19). */
        void setPanelWidth (int);

    private:
        std::unique_ptr<juce::FileChooser> logChooser;   // Export log's Save
        std::unique_ptr<engine::SettingsTest> settingsTest;
        void offerRecommendation (const engine::SettingsTest::Recommendation&, bool atFirstLaunch);

        // Project notes (W38): a window of their own that follows the project in front.
        class NotesWindow;
        std::unique_ptr<NotesWindow> notesWindow;
        void toggleNotes();
        void showVersions();
        void refreshNotes();
        class SignalRow;

        engine::Session& session;
        juce::ApplicationCommandManager& commands;
        ProjectTabs tabs { session };

        IconButton recordButton { IconButton::Glyph::record };
        IconButton playButton { IconButton::Glyph::play };
        IconButton toStartButton { IconButton::Glyph::toStart };
        IconButton metronomeButton { IconButton::Glyph::metronome };
        IconButton settingsButton { IconButton::Glyph::settings };
        juce::Label timeLabel, latencyLabel, latencyWords, statusLabel;
        LoadMeter loadMeter;

        std::unique_ptr<SignalRow> signalRow;
        Timeline timeline;
        SettingsPanel settings;
        bool settingsOpen = false;
        UpdateBanner updateBanner;
        PluginBrowser browser;
        SampleBrowser samples;
        StartScreen startScreen { session };
        CommandPalette palette { commands };
        CommandPalette viewsPalette { commands };   // Cmd/Ctrl+J (W41)
        bool browserOpen = true, panelShowsSamples = false;
        juce::TextButton samplesTab { "Samples" }, pluginsTab { "Plugins" };
        int panelWidth = PluginBrowser::preferredWidth;
        juce::Rectangle<int> panelArea;
        PanelEdge panelEdge;



        // The project's master level, beside the transport (owner's list F22): a drag, the wheel or
        // a typed figure; double-click returns to a new project's level. Phase 5 makes it the
        // master fader's twin.
        static constexpr double newProjectMasterDb = -3.0;   // Tracktion's level for a new edit
        ValueSlider masterVolume { newProjectMasterDb };
        juce::Label masterLabel;
        void refreshMasterVolume();

        // The project's tempo, beside the time (owner's list F22): drag, scroll or type it, or tap
        // it. Double-click returns to 120.
        ValueSlider tempo { 120.0 };
        juce::TextButton tapButton { "Tap" };
        time::TapTempo tapTempo;
        void refreshTempo();
        void setTempo (double bpm);

        // A tempo change is one gesture (a drag, a typed figure, taps, wheel steps): the clips'
        // beats are taken at its start, and when it settles the clips follow as Settings says or
        // as the person answers (W38).
        std::optional<std::vector<editing::BeatPlace>> tempoBefore;
        int tempoChanges = 0;
        juce::ToggleButton rememberTempoFollow { "Remember this choice (Settings can change it)" };   // outlives the question's window
        void tempoSettled();
        void askHowClipsFollow (std::vector<editing::BeatPlace> before);
        void askToRenameProject();

        /** App-wide interface size (W38): 75 to 150 %, kept in the settings. */
        void setUiScale (double);
        double uiScale();
        bool showsLoad();

        // Toolbar: the tools (icons with their keys), the grid menu and the snap toggle.
        juce::OwnedArray<ToolButton> toolButtons;
        juce::ComboBox gridMenu;
        juce::TextButton snapButton { "Snap" };
        void refreshToolbar();
        void refreshShortcutText();
        void changeListenerCallback (juce::ChangeBroadcaster*) override   { refreshShortcutText(); }
        void showGridMenu();

        // Hold-to-switch (F7): the tool before a held key, and whether the tool was used meanwhile.
        juce::CommandID heldToolCommand = 0;
        Timeline::Tool toolBeforeHold = Timeline::Tool::select;
        bool usedWhileHeld = false;
        void toolCommand (const InvocationInfo&);

        std::unique_ptr<PluginWindow> pluginWindow;

        class ViewWindow;
        std::map<int, std::unique_ptr<ViewWindow>> viewWindows;   // by View
        IconButton popPanel { IconButton::Glyph::popOut }, popTimeline { IconButton::Glyph::popOut },
                   popSignal { IconButton::Glyph::popOut };
        juce::Label timelineAway;
        juce::TextButton timelineBack { "Bring it back" };
        std::optional<juce::Rectangle<int>> pluginWindowBounds;   // from the last layout applied
        juce::Component& componentOf (View);
        void viewsChanged();
        juce::PropertiesFile& props();
        std::optional<double> recordingFrom;
        double lastFrameTime = 0.0;
        int frameCount = 0;
        std::unique_ptr<juce::VBlankAttachment> vblank;

        void frame (double timestampSeconds);
        void logFrame (double timestampSeconds);

        struct FrameStats { double since = 0.0, worst = 0.0, sum = 0.0; int count = 0; std::vector<double> longest; };
        FrameStats frameStats;
        bool frameLog = false;
        void toggleRecord();
        void takeFinished (const engine::TakeRecorder::Result&);
        void togglePlay();
        void updateTransportButtons();
        void updateFigures();
        void choosePlugin();
        void insertPlugin (const juce::PluginDescription&);
        void toggleBrowser();
        void openPluginWindow();
        void closePluginWindow();
        void setStatus (const juce::String&, juce::Colour = theme::muted);
        void layoutSettings (bool animate);
        void layoutUpdateBanner();
    };
}
