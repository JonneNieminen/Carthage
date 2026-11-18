#pragma once

#include "Controls.h"
#include "PluginBrowser.h"
#include "SampleBrowser.h"
#include "ProjectTabs.h"
#include "SettingsPanel.h"
#include "Timeline.h"
#include "UpdateBanner.h"

#include <app/Commands.h>
#include <engine/Session.h>

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
        SampleBrowser& getSampleBrowser() noexcept  { return samples; }

        /** Shows the left panel on samples or plugins; with `toggle`, the same view again hides it. */
        void showPanel (bool samplesView, bool toggle);

        /** Settings: the left panel's width and the sample browser's text size (F19). */
        void setPanelWidth (int);

    private:
        class SignalRow;

        engine::Session& session;
        juce::ApplicationCommandManager& commands;
        ProjectTabs tabs { session };

        IconButton recordButton { IconButton::Glyph::record };
        IconButton playButton { IconButton::Glyph::play };
        IconButton toStartButton { IconButton::Glyph::toStart };
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
        bool browserOpen = true, panelShowsSamples = false;
        juce::TextButton samplesTab { "Samples" }, pluginsTab { "Plugins" };
        int panelWidth = PluginBrowser::preferredWidth;
        juce::Rectangle<int> panelArea;



        // Toolbar: the tools (labelled by their keys), the grid menu and the snap toggle.
        juce::OwnedArray<juce::TextButton> toolButtons;
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
