#pragma once

#include "Controls.h"
#include "SettingsPanel.h"
#include "TakeLane.h"

#include <app/Commands.h>
#include <engine/Session.h>

namespace carthage::ui
{
    class PluginWindow;

    /** Phase 1's window: transport across the top, the signal path (input -> plugin -> output) on
        one line, and the lane of takes below it. The audio settings slide in from the right.

        Animates only from the display's frame callback (VBlankAttachment): meters, the playhead,
        the load figure. Nothing else moves. */
    class MainComponent final : public juce::Component, public juce::ApplicationCommandTarget
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

        std::unique_ptr<PluginWindow> pluginWindow;
        std::optional<double> recordingFrom;
        double lastFrameTime = 0.0;
        int frameCount = 0;
        std::unique_ptr<juce::VBlankAttachment> vblank;

        void frame (double timestampSeconds);
        void toggleRecord();
        void togglePlay();
        void updateTransportButtons();
        void choosePlugin();
        void scanAndInsert (const engine::PluginCandidate&);
        void openPluginWindow();
        void closePluginWindow();
        void setStatus (const juce::String&, juce::Colour = theme::muted);
        void layoutSettings (bool animate);
    };
}
