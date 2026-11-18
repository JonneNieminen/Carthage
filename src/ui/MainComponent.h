#pragma once

#include "Controls.h"
#include "PluginBrowser.h"
#include "SettingsPanel.h"
#include "TakeLane.h"
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
                                public juce::DragAndDropContainer
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

        /** Shows a problem the user has to fix outside Carthage, in the status line. */
        void showProblem (const juce::String& text)    { setStatus (text, theme::warn); }

        /** A short message in the status line (update checks the user asked for). */
        void showMessage (const juce::String& text, bool warning)  { setStatus (text, warning ? theme::warn : theme::muted); }

        /** The update banner and the settings' update toggle, for the app's updater. */
        void showUpdate (std::optional<UpdateBanner::Model>);
        UpdateBanner& getUpdateBanner() noexcept    { return updateBanner; }
        SettingsPanel& getSettings() noexcept       { return settings; }
        PluginBrowser& getPluginBrowser() noexcept  { return browser; }

    private:
        class SignalRow;

        engine::Session& session;
        juce::ApplicationCommandManager& commands;

        IconButton recordButton { IconButton::Glyph::record };
        IconButton playButton { IconButton::Glyph::play };
        IconButton toStartButton { IconButton::Glyph::toStart };
        IconButton settingsButton { IconButton::Glyph::settings };
        juce::Label timeLabel, latencyLabel, latencyWords, statusLabel;
        LoadMeter loadMeter;

        std::unique_ptr<SignalRow> signalRow;
        TakeLane takeLane;
        SettingsPanel settings;
        bool settingsOpen = false;
        UpdateBanner updateBanner;
        PluginBrowser browser;
        bool browserOpen = true;

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
