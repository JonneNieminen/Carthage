#pragma once

#include "RoutingView.h"

#include <array>

#include "Controls.h"
#include "ShortcutEditor.h"

#include <engine/Session.h>

namespace carthage::ui
{
    /** Settings: audio (brief, feature 8, phase 1 part) and updates (feature 12): interface, sample rate, buffer size,
        recording bit depth, the round trip as the driver reports it and as a loopback measures it,
        and the engine's load and dropouts, and every other preference.

        A view like the mixer (owner, 2026-11-20): it takes the place of the lanes, or a window of
        its own. Pages on the left (Audio, Recording, Projects, Timeline, Mixer, Browser, Display,
        Shortcuts, Updates), the search above them looking through every page, the chosen page
        on the right. It never covers the transport. */
    class SettingsPanel final : public juce::Component
    {
    public:
        SettingsPanel (engine::Session&, juce::ApplicationCommandManager&, juce::Array<juce::CommandID> shortcutCommands);
        ~SettingsPanel() override;

        /** Re-reads the device after it changed. */
        void refresh();

        /** Once per frame from the main component. */
        void frame (int xruns, float engineCpu, engine::AudioCallback::Load);

        std::function<void()> onClose;

        /** Into its own window and back (F21); the window shows the pop-in glyph. */
        std::function<void()> onPopOut;
        void setInWindow (bool);
        std::function<void()> onMeasure;

        /** "Receive beta updates": the app's updater owns the setting; the panel shows it. `note`
            explains when the toggle is off (a development build). */
        std::function<void (bool)> onBetaUpdatesChanged;

        /** The browser's settings (F19); the window applies them. */
        std::function<void (int)> onPanelWidthChanged;

        /** "Test this computer" (W41); the window runs the test and says when it ends. */
        std::function<void()> onRunSettingsTest;
        void setTestRunning (bool running)   { testButton.setEnabled (! running); testButton.setButtonText (running ? juce::String::fromUTF8 ("Testing\xe2\x80\xa6") : juce::String ("Test this computer")); }
        std::function<void (double)> onUiScaleChanged;
        void showUiScale (double s)   { uiScale.setValue (s * 100.0, juce::dontSendNotification); }
        void showPanelWidth (int w)   { panelWidth.setValue (w, juce::dontSendNotification); }
        std::function<void (float)> onBrowserTextSizeChanged;
        std::function<void (bool)> onPlayWhileHeldChanged;

        /** "Show the load in the toolbar" (off by default, owner's list F22). */
        std::function<void (bool)> onShowLoadChanged;
        std::function<void()> onBeatShadingChanged;

        /** "Mark bus channels" or its interval changed: the mixer draws them again. */
        std::function<void()> onBusChannelsChanged;

        /** Settings > Mixer chose how the Routing section draws the mix (W61). */
        std::function<void (routing::Style)> onRoutingStyleChanged;

        /** "Check now", beside the version: the same as "Check for updates…" in the menu. */
        std::function<void()> onCheckForUpdates;
        void setBetaUpdates (bool on, bool available, const juce::String& note);

        /** The pages, in the order the list shows them. */
        enum class Page { audio, recording, projects, timeline, mixer, browser, display, shortcuts, updates };
        static constexpr int pageCount = 9;
        void showPage (Page);

        /** Shows the Updates page. */
        void showUpdatesSection()    { showPage (Page::updates); }

        /** Puts `text` in the Settings search (the snapshot tool shows a search this way). */
        void setSearch (const juce::String& text)   { find.setText (text, true); }

        /** Shows the keyboard shortcuts page. */
        void showShortcutsSection()  { showPage (Page::shortcuts); }

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

        static constexpr int sidebarWidth = 212;

    private:
        engine::Session& session;

        class PageList;
        std::unique_ptr<PageList> pageList;
        class LoopbackPicture;
        std::unique_ptr<LoopbackPicture> loopPicture;   // an interface with a cable from an output to an input
        Page page = Page::audio;
        bool layingOut = false;
        std::array<int, pageCount> matches {};   // per page, while searching
        juce::Label pageTitle, pageIntro;
        std::array<std::unique_ptr<juce::Label>, pageCount> pageHeadings;   // over each page's results while searching
        static juce::String nameOf (Page);
        static juce::String introOf (Page);

        juce::TextEditor find;   // Settings search (W41): filters the body
        juce::Label noMatch;
        juce::Label title, interfaceHeading, recordingHeading, latencyHeading, loadHeading, updatesHeading, updatesHint, shortcutsHeading,
                    projectsHeading, copySamplesHint;
        Switch copySamples { "Copy samples into the project" };
        juce::Label atLaunchLabel, tempoFollowLabel, timelineHeading, mixerHeading, loudnessLabel;
        juce::ComboBox loudness;

        juce::Label metronomeHeading, clickSoundLabel, clickLevelLabel;
        juce::ComboBox clickSound;
        ValueSlider clickLevel { 60.0 };   // %, Tracktion's own range 20-100; double-click: 60 % (owner)
        Switch clickAccent { "Accent the first beat of each bar" };
        Switch clickWhileRecording { "Only while recording" };
        std::unique_ptr<juce::FileChooser> clickChooser;
        void chooseOwnClick();
        juce::Label beatShadingLabel;
        juce::ComboBox beatShading;
        Switch clickPlaces { "Click an empty spot to place the last clip" };
        Switch rightClickDeletes { "Right-click deletes a clip" };
        juce::ComboBox atLaunch, tempoFollow;
        juce::Label windowsHeading, slotsLabel, uiScaleLabel;
        ValueSlider uiScale { 100.0 };   // %; double-click returns here
        juce::ComboBox slots;
        juce::Label browserHeading, panelWidthLabel, textSizeLabel;
        ValueSlider panelWidth { 300.0 };   // px; double-click returns here
        juce::ComboBox textSize;
        Switch playWhileHeld { "Play sounds only while held" };
        Switch fitLoops { "Fit loops to the project tempo" };
        Switch preMaster { "Pre-Master channel before master" };
        Switch busChannels { "Mark bus channels" };
        juce::Label exportHeading, exportFormatLabel, exportRateLabel, countInLabel;
        juce::ComboBox countIn;
        Switch lowLatency { "Low-latency monitoring" };
        juce::ComboBox exportFormat, exportRate;
        juce::Label busEveryLabel, routingHeading, routingHint;
        std::array<std::unique_ptr<routing::Preview>, std::size (routing::styles)> routingPreviews;
        void chooseRoutingStyle (routing::Style);
        juce::ComboBox busEvery;
        Switch checkPlugins { "Check for new plugins at every launch" };
        juce::Label processesHeading, processesHint;   // plugins in their own processes (5f-4)
        Switch pluginProcesses { "Run plugins in their own processes" };
        Switch groupAutomatically { "Group plugins automatically" };
        juce::TextButton forgetCrashes { "Forget crash history" };
        ShortcutEditor shortcuts;
        juce::Viewport viewport;
        juce::Component body;
        Switch betaUpdates { "Receive beta updates" };
        juce::Label versionLabel, versionValue;
        juce::TextButton checkNow { "Check now" };
        juce::String unavailableNote;
        void showUpdatesHint();
        IconButton closeButton { IconButton::Glyph::close };
        IconButton popButton { IconButton::Glyph::popOut };

        juce::Label inputDeviceLabel, outputDeviceLabel, rateLabel, bufferLabel, formatLabel, testHint;
        juce::TextButton testButton { "Test this computer" };
        juce::ComboBox inputDevice, outputDevice, rate, buffer, format;

        juce::Label reportedLabel, reportedValue, measuredLabel, measuredValue, loopOutLabel, loopInLabel, loopHint;
        juce::ComboBox loopOut, loopIn;
        juce::TextButton measureButton { "Measure" };
        Switch useMeasured { "Place takes using the measured round trip" };

        juce::Label cpuLabel, cpuValue, threadLabel, threadValue, dropoutsLabel, dropoutsValue;
        juce::TextButton resetDropouts { "Reset" };
        Switch showLoad { "Show the load in the toolbar" };
        int dropoutBase = 0, lastXruns = 0;
        float threadPeakShown = 0.0f;

        std::optional<engine::LatencyProbe::Measurement> lastMeasurement;

        void applyDevice (const std::function<void (juce::AudioDeviceManager::AudioDeviceSetup&)>&);
        void updateLatency();
        void startMeasurement();

    public:
        /** The main component hands finished measurements here. */
        void showMeasurement (const engine::LatencyProbe::Measurement&);
        bool isMeasuring() const;
    };
}
