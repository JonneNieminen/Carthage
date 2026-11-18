#pragma once

#include "Controls.h"
#include "ShortcutEditor.h"

#include <engine/Session.h>

namespace carthage::ui
{
    /** Settings: audio (brief, feature 8, phase 1 part) and updates (feature 12): interface, sample rate, buffer size,
        recording bit depth, the round trip as the driver reports it and as a loopback measures it,
        and the engine's load and dropouts. Slides in from the right; it never covers the transport. */
    class SettingsPanel final : public juce::Component
    {
    public:
        SettingsPanel (engine::Session&, juce::ApplicationCommandManager&, juce::Array<juce::CommandID> shortcutCommands);

        /** Re-reads the device after it changed. */
        void refresh();

        /** Once per frame from the main component. */
        void frame (int xruns, float engineCpu, engine::AudioCallback::Load);

        std::function<void()> onClose;
        std::function<void()> onMeasure;

        /** "Receive beta updates": the app's updater owns the setting; the panel shows it. `note`
            explains when the toggle is off (a development build). */
        std::function<void (bool)> onBetaUpdatesChanged;

        /** The browser's settings (F19); the window applies them. */
        std::function<void (int)> onPanelWidthChanged;
        std::function<void (float)> onBrowserTextSizeChanged;
        std::function<void (bool)> onPlayWhileHeldChanged;

        /** "Check now", beside the version: the same as "Check for updates…" in the menu. */
        std::function<void()> onCheckForUpdates;
        void setBetaUpdates (bool on, bool available, const juce::String& note);

        /** Scrolls the body so the Updates section is in view. */
        void showUpdatesSection()    { viewport.setViewPosition (0, updatesHeading.getY() - 8); }

        /** Scrolls the body so the keyboard shortcuts are in view. */
        void showShortcutsSection()  { viewport.setViewPosition (0, shortcutsHeading.getY() - 8); }

        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr int preferredWidth = 340;

    private:
        engine::Session& session;

        juce::Label title, interfaceHeading, recordingHeading, latencyHeading, loadHeading, updatesHeading, updatesHint, shortcutsHeading,
                    projectsHeading, copySamplesHint;
        juce::ToggleButton copySamples { "Copy samples into the project" };
        juce::Label browserHeading, panelWidthLabel, textSizeLabel;
        juce::Slider panelWidth;
        juce::ComboBox textSize;
        juce::ToggleButton playWhileHeld { "Play sounds only while held" };
        ShortcutEditor shortcuts;
        juce::Viewport viewport;
        juce::Component body;
        juce::ToggleButton betaUpdates { "Receive beta updates" };
        juce::Label versionLabel, versionValue;
        juce::TextButton checkNow { "Check now" };
        juce::String unavailableNote;
        void showUpdatesHint();
        IconButton closeButton { IconButton::Glyph::close };

        juce::Label inputDeviceLabel, outputDeviceLabel, rateLabel, bufferLabel, formatLabel;
        juce::ComboBox inputDevice, outputDevice, rate, buffer, format;

        juce::Label reportedLabel, reportedValue, measuredLabel, measuredValue, loopOutLabel, loopInLabel, loopHint;
        juce::ComboBox loopOut, loopIn;
        juce::TextButton measureButton { "Measure" };
        juce::ToggleButton useMeasured { "Place takes using the measured round trip" };

        juce::Label cpuLabel, cpuValue, threadLabel, threadValue, dropoutsLabel, dropoutsValue;
        juce::TextButton resetDropouts { "Reset" };
        int dropoutBase = 0, lastXruns = 0;
        float threadPeakShown = 0.0f;

        std::optional<engine::LatencyProbe::Measurement> lastMeasurement;

        void applyDevice (std::function<void (juce::AudioDeviceManager::AudioDeviceSetup&)>);
        void updateLatency();
        void startMeasurement();

    public:
        /** The main component hands finished measurements here. */
        void showMeasurement (const engine::LatencyProbe::Measurement&);
        bool isMeasuring() const;
    };
}
