#include "SettingsPanel.h"

namespace carthage::ui
{
    using namespace juce;
    using engine::TakeRecorder;

    namespace
    {
        String rateText (double r)
        {
            return r >= 1000.0 ? String (r / 1000.0, (std::fmod (r, 1000.0) != 0.0) ? 1 : 0) + " kHz" : String (r) + " Hz";
        }

        String msText (double ms)  { return String (ms, 1) + " ms"; }

        constexpr int rowHeight = 28, labelWidth = 116;
    }

    SettingsPanel::SettingsPanel (engine::Session& s, ApplicationCommandManager& commands, Array<CommandID> shortcutCommands)
        : session (s), shortcuts (commands, std::move (shortcutCommands))
    {
        styleLabel (title, theme::textLg, theme::text, true);
        title.setText ("Settings", dontSendNotification);

        find.setTextToShowWhenEmpty (String::fromUTF8 ("Find a setting\xe2\x80\xa6"), theme::faint);
        find.setFont (theme::uiFont (theme::textMd));
        find.setIndents (theme::space2, 6);
        find.onTextChange = [this] { viewport.setViewPosition (0, 0); resized(); };
        find.onEscapeKey = [this] { find.clear(); resized(); };
        addAndMakeVisible (find);
        styleLabel (noMatch, theme::textMd, theme::muted);
        noMatch.setJustificationType (Justification::topLeft);
        body.addChildComponent (noMatch);

        for (auto [label, text] : { std::pair { &interfaceHeading, "Interface" }, { &recordingHeading, "Recording" },
                                    { &latencyHeading, "Latency" }, { &loadHeading, "Load" }, { &projectsHeading, "Projects" }, { &browserHeading, "Browser" }, { &timelineHeading, "Timeline" }, { &metronomeHeading, "Metronome" }, { &windowsHeading, "Windows" },
                                    { &updatesHeading, "Updates" },
                                    { &shortcutsHeading, "Keyboard shortcuts" } })
        {
            styleLabel (*label, theme::textSm, theme::muted, true);
            label->setText (text, dontSendNotification);
            body.addAndMakeVisible (*label);   // every heading lives in the scrolling body
        }

        for (auto [label, text] : { std::pair { &inputDeviceLabel, "Input" }, { &outputDeviceLabel, "Output" },
                                    { &rateLabel, "Sample rate" }, { &bufferLabel, "Buffer size" },
                                    { &formatLabel, "Bit depth" }, { &reportedLabel, "Driver reports" },
                                    { &measuredLabel, "Measured" }, { &loopOutLabel, "Send from" },
                                    { &loopInLabel, "Return to" }, { &cpuLabel, "Engine" },
                                    { &threadLabel, "Audio thread" }, { &dropoutsLabel, "Dropouts" },
                                    { &versionLabel, "Version" } })
        {
            styleLabel (*label, theme::textMd, theme::muted);
            label->setText (text, dontSendNotification);
        }

        for (auto* v : { &reportedValue, &measuredValue, &cpuValue, &threadValue, &dropoutsValue, &versionValue })
            styleFigure (*v);

        // The version this copy is, so an update can be seen to have happened.
        versionValue.setText (CARTHAGE_VERSION_STRING, dontSendNotification);
        versionValue.setTooltip ("Build " + String (CARTHAGE_BUILD_NUMBER));

        styleLabel (loopHint, theme::textSm, theme::muted);
        loopHint.setText ("Connect a cable from an output to an input, choose them here, then measure. "
                          "Everything else goes quiet while the probe plays.", dontSendNotification);
        loopHint.setJustificationType (Justification::topLeft);

        closeButton.setTooltip ("Close settings");
        closeButton.onClick = [this] { if (onClose) onClose(); };

        // The title stays put; everything below it scrolls when the window is short.
        addAndMakeVisible (title);
        addAndMakeVisible (closeButton);
        popButton.setTooltip ("Settings in a window of its own");
        popButton.onClick = [this] { if (onPopOut) onPopOut(); };
        addAndMakeVisible (popButton);
        addAndMakeVisible (viewport);
        viewport.setViewedComponent (&body, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (theme::space2);

        for (auto* c : { &inputDevice, &outputDevice, &rate, &buffer, &format, &loopOut, &loopIn })
            body.addAndMakeVisible (*c);

        // Projects: where samples live (W30).
        styleLabel (copySamplesHint, theme::textSm, theme::muted);
        copySamplesHint.setJustificationType (Justification::topLeft);
        copySamplesHint.setText ("Samples you paste or drop into a project are copied into its Samples folder, so the folder "
                                 "holds everything the project needs. Off: they stay where they are.", dontSendNotification);
        copySamples.setToggleState (session.getAlwaysCopySamples(), dontSendNotification);
        copySamples.setTooltip ("File > Collect samples into project does the same for a project now");
        copySamples.onClick = [this] { session.setAlwaysCopySamples (copySamples.getToggleState()); };

        // Browser (F19): panel width, text size, and how a click plays a sound.
        auto& props = session.getEngine().getPropertyStorage().getPropertiesFile();
        styleLabel (panelWidthLabel, theme::textMd, theme::muted);
        panelWidthLabel.setText ("Panel width", dontSendNotification);
        panelWidth.setSliderStyle (Slider::LinearHorizontal);
        panelWidth.setTextBoxStyle (Slider::NoTextBox, true, 0, 0);
        panelWidth.setRange (240.0, 520.0, 10.0);
        panelWidth.setValue (props.getIntValue ("panelWidth", 300), dontSendNotification);
        panelWidth.setTooltip ("How wide the samples and plugins panel is. Double-click for the default; scroll to adjust, Shift for fine steps.");
        panelWidth.onValueChange = [this] { if (onPanelWidthChanged) onPanelWidthChanged (static_cast<int> (panelWidth.getValue())); };

        styleLabel (textSizeLabel, theme::textMd, theme::muted);
        textSizeLabel.setText ("Text size", dontSendNotification);
        textSize.addItem ("Small", 12);
        textSize.addItem ("Medium", 13);
        textSize.addItem ("Large", 15);
        textSize.setSelectedId (roundToInt (props.getDoubleValue ("browserTextSize", 13.0)), dontSendNotification);
        textSize.setTooltip ("The size of the names in the sample browser");
        textSize.onChange = [this]
        {
            session.getEngine().getPropertyStorage().getPropertiesFile().setValue ("browserTextSize", textSize.getSelectedId());
            if (onBrowserTextSizeChanged) onBrowserTextSizeChanged (static_cast<float> (textSize.getSelectedId()));
        };

        showLoad.setToggleState (props.getBoolValue ("showLoad", false), dontSendNotification);
        showLoad.setTooltip ("A bar beside the latency figure: the audio thread's average load and its worst recent block");
        showLoad.onClick = [this]
        {
            session.getEngine().getPropertyStorage().getPropertiesFile().setValue ("showLoad", showLoad.getToggleState());
            if (onShowLoadChanged) onShowLoadChanged (showLoad.getToggleState());
        };

        playWhileHeld.setToggleState (props.getBoolValue ("playWhileHeld", false), dontSendNotification);
        playWhileHeld.setTooltip ("Off: a click plays the whole sound. On: a sound plays while the mouse button, Enter or Space is held.");
        playWhileHeld.onClick = [this]
        {
            session.getEngine().getPropertyStorage().getPropertiesFile().setValue ("playWhileHeld", playWhileHeld.getToggleState());
            if (onPlayWhileHeldChanged) onPlayWhileHeldChanged (playWhileHeld.getToggleState());
        };

        fitLoops.setToggleState (session.getFitLoopsToTempo(), dontSendNotification);
        fitLoops.setTooltip ("A loop dropped on a lane is stretched to the project's tempo, keeping its pitch, when its tempo is known. "
                             "Off: loops play as recorded.");
        fitLoops.onClick = [this] { session.setFitLoopsToTempo (fitLoops.getToggleState()); };
        preMaster.setToggleState (session.getUsePreMaster(), dontSendNotification);
        preMaster.setTooltip ("A channel between every channel and master, beside master in the mixer, for the processing the whole mix shares. "
                              "Off, everything goes straight to master; its plugins stay, unused.");
        preMaster.onClick = [this] { session.setUsePreMaster (preMaster.getToggleState()); };

        // Windows (F21): how many layouts the Layouts menu offers.
        styleLabel (slotsLabel, theme::textMd, theme::muted);
        slotsLabel.setText ("Layout slots", dontSendNotification);
        for (int i = 1; i <= 9; ++i)
            slots.addItem (String (i), i);
        slots.setSelectedId (jlimit (1, 9, session.getEngine().getPropertyStorage().getPropertiesFile().getIntValue ("layoutSlots", 4)), dontSendNotification);
        slots.setTooltip ("How many window layouts the Layouts menu keeps; Cmd+Option+1 to 9 recall them, with Shift to save");
        slots.onChange = [this] { session.getEngine().getPropertyStorage().getPropertiesFile().setValue ("layoutSlots", slots.getSelectedId()); };

        styleLabel (uiScaleLabel, theme::textMd, theme::muted);
        uiScaleLabel.setText ("Interface size", dontSendNotification);
        uiScale.setSliderStyle (Slider::LinearHorizontal);
        uiScale.setTextBoxStyle (Slider::TextBoxRight, false, 52, 22);
        uiScale.setRange (75.0, 150.0, 2.5);
        uiScale.setTextValueSuffix (" %");
        uiScale.setNumDecimalPlacesToDisplay (0);
        uiScale.setChangeNotificationOnlyOnRelease (true);   // the whole window rescales: once, on release
        uiScale.setTooltip ("Everything in Carthage larger or smaller. Cmd+plus and Cmd+minus step it, Cmd+0 is 100 %.");
        uiScale.onValueChange = [this] { if (onUiScaleChanged) onUiScaleChanged (uiScale.getValue() / 100.0); };

        for (auto* c : std::initializer_list<Component*> { &windowsHeading, &slotsLabel, &slots, &uiScaleLabel, &uiScale })
            body.addAndMakeVisible (c);

        for (auto* c : std::initializer_list<Component*> { &browserHeading, &panelWidthLabel, &panelWidth, &textSizeLabel, &textSize, &playWhileHeld, &fitLoops, &preMaster })
            body.addAndMakeVisible (c);

        styleLabel (atLaunchLabel, theme::textMd, theme::muted);
        atLaunchLabel.setText ("When it opens", dontSendNotification);
        atLaunch.addItem ("Show the start screen", 1);
        atLaunch.addItem ("Reopen the last session", 2);
        atLaunch.addItem ("Start an empty project", 3);
        atLaunch.setTooltip ("What Carthage shows at launch. The last session is the saved projects and the window layout you quit with.");
        atLaunch.onChange = [this]
        {
            using A = engine::Session::AtLaunch;
            session.setAtLaunch (atLaunch.getSelectedId() == 2 ? A::lastSession : atLaunch.getSelectedId() == 3 ? A::emptyProject : A::startScreen);
        };

        styleLabel (clickSoundLabel, theme::textMd, theme::muted);
        clickSoundLabel.setText ("Sound", dontSendNotification);
        clickSound.addItem ("Tick-tock", 1);
        clickSound.addItem ("Hi-hat", 2);
        clickSound.addItem ("Cowbell", 3);
        clickSound.addItem (String::fromUTF8 ("Your own sound\xe2\x80\xa6"), 4);
        clickSound.setTooltip ("What the metronome plays. The first beat of a bar is louder and a little brighter.");
        clickSound.onChange = [this]
        {
            const auto id = clickSound.getSelectedId();

            if (id == 4)
                chooseOwnClick();
            else if (id >= 1)
                session.setMetronomeSound (static_cast<metronome::Sound> (id - 1));
        };

        styleLabel (clickLevelLabel, theme::textMd, theme::muted);
        clickLevelLabel.setText ("Level", dontSendNotification);
        clickLevel.setSliderStyle (Slider::LinearHorizontal);
        clickLevel.setTextBoxStyle (Slider::TextBoxRight, false, 52, 22);
        clickLevel.setRange (20.0, 100.0, 1.0);
        clickLevel.setTextValueSuffix (" %");
        clickLevel.setTooltip ("How loud the metronome is, for this project");
        clickLevel.onValueChange = [this] { session.getEdit().clickTrackGain = static_cast<float> (clickLevel.getValue() / 100.0); };
        clickAccent.setTooltip ("The first beat of each bar plays the louder, brighter sound. For this project.");
        clickAccent.onClick = [this] { session.getEdit().clickTrackEmphasiseBars = clickAccent.getToggleState(); };
        clickWhileRecording.setTooltip ("The metronome sounds only while recording, not while playing back. For this project.");
        clickWhileRecording.onClick = [this] { session.getEdit().clickTrackRecordingOnly = clickWhileRecording.getToggleState(); };

        for (auto* c : std::initializer_list<Component*> { &clickSoundLabel, &clickSound, &clickLevelLabel, &clickLevel, &clickAccent, &clickWhileRecording })
            body.addAndMakeVisible (c);

        checkPlugins.setToggleState (session.getCheckPluginsAtLaunch(), dontSendNotification);
        checkPlugins.setTooltip ("Off: plugins are checked at the first launch, then when you press Rescan in the plugin browser. "
                                 "On: new or updated plugins are checked each time Carthage opens.");
        checkPlugins.onClick = [this] { session.setCheckPluginsAtLaunch (checkPlugins.getToggleState()); };
        body.addAndMakeVisible (checkPlugins);

        styleLabel (beatShadingLabel, theme::textMd, theme::muted);
        beatShadingLabel.setText ("Shade inside the beat", dontSendNotification);
        beatShading.addItem ("Off", 1);
        beatShading.addItem ("Every other beat", 2);
        beatShading.addItem ("Every other 1/2 beat", 3);
        beatShading.addItem ("Every other 1/3 beat", 4);
        beatShading.addItem ("Every other 1/4 beat", 5);
        beatShading.setTooltip ("Tints every other step on the lanes, so the steps inside a beat are easy to find when programming drums. Halves and quarters also get a line at the half beat.");
        beatShading.setSelectedId (1 + session.getBeatShading(), dontSendNotification);
        beatShading.onChange = [this]
        {
            session.setBeatShading (beatShading.getSelectedId() - 1);
            if (onBeatShadingChanged) onBeatShadingChanged();
        };
        body.addAndMakeVisible (beatShadingLabel);
        body.addAndMakeVisible (beatShading);

        clickPlaces.setToggleState (session.getClickPlacesClip(), dontSendNotification);
        clickPlaces.setTooltip ("As in FL Studio: with the select tool, a click on an empty spot places a copy of the last clip you used. Shift-drag still selects.");
        clickPlaces.onClick = [this] { session.setClickPlacesClip (clickPlaces.getToggleState()); };
        rightClickDeletes.setToggleState (session.getRightClickDeletes(), dontSendNotification);
        rightClickDeletes.setTooltip ("As in FL Studio: a right-click deletes the clip under the pointer; a right-drag deletes every clip it crosses.");
        rightClickDeletes.onClick = [this] { session.setRightClickDeletes (rightClickDeletes.getToggleState()); };
        for (auto* c : std::initializer_list<Component*> { &clickPlaces, &rightClickDeletes })
            body.addAndMakeVisible (c);

        styleLabel (tempoFollowLabel, theme::textMd, theme::muted);
        tempoFollowLabel.setText ("Tempo changes", dontSendNotification);
        tempoFollow.addItem ("Ask each time", 1);
        tempoFollow.addItem ("Clips stay in time", 2);
        tempoFollow.addItem ("Clips move with the beat", 3);
        tempoFollow.addItem ("Clips stretch with the beat", 4);
        tempoFollow.setTooltip ("What clips do when you change the tempo. Fitted loops always follow the beat.");
        tempoFollow.onChange = [this]
        {
            const auto id = tempoFollow.getSelectedId();
            session.setTempoFollow (id <= 1 ? std::nullopt : std::optional (static_cast<editing::TempoFollow> (id - 2)));
        };

        for (auto* c : std::initializer_list<Component*> { &projectsHeading, &atLaunchLabel, &atLaunch, &tempoFollowLabel, &tempoFollow, &copySamples, &copySamplesHint, &versionLabel, &versionValue, &checkNow, &updatesHeading, &updatesHint, &betaUpdates, &interfaceHeading, &recordingHeading,
                                                           &latencyHeading, &loadHeading, &inputDeviceLabel, &outputDeviceLabel,
                                                           &rateLabel, &bufferLabel, &formatLabel, &reportedLabel, &reportedValue,
                                                           &measuredLabel, &measuredValue, &loopOutLabel, &loopInLabel, &loopHint,
                                                           &measureButton, &useMeasured, &cpuLabel, &cpuValue, &threadLabel,
                                                           &threadValue, &dropoutsLabel, &dropoutsValue, &resetDropouts, &showLoad,
                                                           &shortcutsHeading, &shortcuts })
            body.addAndMakeVisible (c);

        shortcuts.onHeightChanged = [this] { resized(); };
        shortcuts.onSearchChanged = [this] { showShortcutsSection(); };   // the box and its results stay in view

        styleLabel (updatesHint, theme::textSm, theme::muted);
        updatesHint.setJustificationType (Justification::topLeft);
        betaUpdates.setTooltip ("Also get test versions before they are released. You can switch back at any time.");
        checkNow.setTooltip ("See whether a newer Carthage is out");
        checkNow.onClick = [this] { if (onCheckForUpdates) onCheckForUpdates(); };
        betaUpdates.onClick = [this]
        {
            if (onBetaUpdatesChanged)
                onBetaUpdatesChanged (betaUpdates.getToggleState());

            showUpdatesHint();
        };

        format.addItem ("16-bit", 1);
        format.addItem ("24-bit", 2);
        format.addItem ("32-bit float", 3);
        format.setTooltip ("The bit depth of new takes. 24-bit is plenty for a guitar; 32-bit float can not clip.");
        format.onChange = [this]
        {
            const auto id = format.getSelectedId();
            session.setRecordFormat (id == 1 ? TakeRecorder::SampleFormat::int16
                                             : id == 3 ? TakeRecorder::SampleFormat::float32 : TakeRecorder::SampleFormat::int24);
        };

        inputDevice.setTooltip ("The interface you play into");
        outputDevice.setTooltip ("The interface you listen through");
        rate.setTooltip ("Samples per second. Higher rates cost more processing and lower the latency a little.");
        buffer.setTooltip ("Samples per block. Smaller buffers mean less delay between playing and hearing, and more load.");
        loopOut.setTooltip ("The output the probe plays from");
        loopIn.setTooltip ("The input the cable comes back into");
        measureButton.setTooltip ("Play a short chirp and time how long it takes to come back");
        useMeasured.setTooltip ("Line new takes up with what the cable measured, instead of what the driver says");
        resetDropouts.setTooltip ("Start counting dropouts again from zero");

        inputDevice.onChange = [this] { applyDevice ([this] (auto& setup) { setup.inputDeviceName = inputDevice.getText(); }); };
        outputDevice.onChange = [this] { applyDevice ([this] (auto& setup) { setup.outputDeviceName = outputDevice.getText(); }); };
        rate.onChange = [this] { applyDevice ([this] (auto& setup) { setup.sampleRate = rate.getSelectedId(); }); };
        buffer.onChange = [this] { applyDevice ([this] (auto& setup) { setup.bufferSize = buffer.getSelectedId(); }); };

        const auto rememberLoopback = [this]
        {
            if (loopOut.getSelectedId() > 0 && loopIn.getSelectedId() > 0)
                session.setLoopbackChannels ({ loopOut.getSelectedId() - 1, loopIn.getSelectedId() - 1 });
        };
        loopOut.onChange = rememberLoopback;
        loopIn.onChange = rememberLoopback;

        measureButton.onClick = [this] { startMeasurement(); };

        testButton.setTooltip ("Tries each buffer size for a few seconds, silently, and recommends settings for this computer. You choose whether to use them.");
        testButton.onClick = [this] { if (onRunSettingsTest) onRunSettingsTest(); };
        styleLabel (testHint, theme::textSm, theme::muted);
        testHint.setText ("About 30 seconds. Your input is not heard while it runs.", dontSendNotification);
        body.addAndMakeVisible (testButton);
        body.addAndMakeVisible (testHint);
        useMeasured.onClick = [this]
        {
            session.setMeasuredRoundTrip (useMeasured.getToggleState() && lastMeasurement
                                              ? std::optional<int> (lastMeasurement->samples) : std::nullopt);
            updateLatency();
        };
        useMeasured.setEnabled (false);
        resetDropouts.onClick = [this] { dropoutBase = lastXruns; };

        refresh();
    }

    void SettingsPanel::applyDevice (const std::function<void (AudioDeviceManager::AudioDeviceSetup&)>& change)
    {
        if (session.isRecording())
            return;   // never pull the device out from under a take

        auto& adm = session.getDeviceManager();
        auto setup = adm.getAudioDeviceSetup();
        change (setup);
        setup.useDefaultInputChannels = true;
        setup.useDefaultOutputChannels = true;

        if (auto err = adm.setAudioDeviceSetup (setup, true); err.isNotEmpty())
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "The interface did not accept that", err);

        refresh();
    }

    void SettingsPanel::chooseOwnClick()
    {
        const auto previous = session.getMetronomeCustomFile();
        clickChooser = std::make_unique<FileChooser> ("Choose a sound for the metronome", previous.existsAsFile() ? previous : File(),
                                                      "*" + engine::SamplePreview::extensions().joinIntoString (";*"));
        clickChooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                                   [safe = SafePointer<SettingsPanel> (this)] (const FileChooser& fc)
        {
            if (safe == nullptr)
                return;

            if (const auto file = fc.getResult(); file.existsAsFile())
            {
                if (auto r = safe->session.setMetronomeSound (metronome::Sound::custom, file); r.failed())
                    AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Not used", r.getErrorMessage());
            }

            safe->refresh();   // shows the sound in use, whichever it is
        });
    }

    void SettingsPanel::refresh()
    {
        atLaunch.setSelectedId (1 + static_cast<int> (session.getAtLaunch()), dontSendNotification);
        clickSound.setSelectedId (1 + static_cast<int> (session.getMetronomeSound()), dontSendNotification);
        clickLevel.setValue (session.getEdit().getClickTrackVolume() * 100.0, dontSendNotification);
        clickAccent.setToggleState (session.getEdit().clickTrackEmphasiseBars.get(), dontSendNotification);
        clickWhileRecording.setToggleState (session.getEdit().clickTrackRecordingOnly.get(), dontSendNotification);

        const auto follow = session.getTempoFollow();
        tempoFollow.setSelectedId (follow ? 2 + static_cast<int> (*follow) : 1, dontSendNotification);

        auto& adm = session.getDeviceManager();
        const auto setup = adm.getAudioDeviceSetup();
        auto* device = adm.getCurrentAudioDevice();

        if (auto* type = adm.getCurrentDeviceTypeObject())
        {
            const auto fill = [] (ComboBox& box, const StringArray& names, const String& current)
            {
                box.clear (dontSendNotification);
                for (int i = 0; i < names.size(); ++i)
                    box.addItem (names[i], i + 1);
                box.setSelectedId (names.indexOf (current) + 1, dontSendNotification);
            };

            fill (inputDevice, type->getDeviceNames (true), setup.inputDeviceName);
            fill (outputDevice, type->getDeviceNames (false), setup.outputDeviceName);
        }

        rate.clear (dontSendNotification);
        buffer.clear (dontSendNotification);
        loopOut.clear (dontSendNotification);
        loopIn.clear (dontSendNotification);

        if (device != nullptr)
        {
            for (auto r : device->getAvailableSampleRates())
                if (r >= 44100.0 && r <= 192000.0)   // the brief's range
                    rate.addItem (rateText (r), roundToInt (r));

            rate.setSelectedId (roundToInt (device->getCurrentSampleRate()), dontSendNotification);

            for (auto b : device->getAvailableBufferSizes())
                buffer.addItem (String (b) + " samples, " + msText (b * 1000.0 / device->getCurrentSampleRate()), b);

            buffer.setSelectedId (device->getCurrentBufferSizeSamples(), dontSendNotification);

            const auto outs = device->getOutputChannelNames();
            const auto ins = device->getInputChannelNames();

            const auto label = [] (const StringArray& names, int i, const String& kind)
            {
                return names[i].trim().isNotEmpty() ? String (i + 1) + "  " + names[i].trim() : kind + " " + String (i + 1);
            };

            for (int i = 0; i < outs.size(); ++i) loopOut.addItem (label (outs, i, "Output"), i + 1);
            for (int i = 0; i < ins.size(); ++i)  loopIn.addItem (label (ins, i, "Input"), i + 1);

            // The cable this interface was last measured with, or the first output you do not
            // listen on into the first input that is not the guitar (Session::getLoopbackChannels).
            const auto loopback = session.getLoopbackChannels();
            loopOut.setSelectedId (loopback.output + 1, dontSendNotification);
            loopIn.setSelectedId (loopback.input + 1, dontSendNotification);
        }

        const auto f = session.getRecordFormat();
        format.setSelectedId (f == TakeRecorder::SampleFormat::int16 ? 1 : f == TakeRecorder::SampleFormat::float32 ? 3 : 2,
                              dontSendNotification);

        // A measurement belongs to one device configuration.
        lastMeasurement.reset();
        session.setMeasuredRoundTrip (std::nullopt);
        useMeasured.setToggleState (false, dontSendNotification);
        useMeasured.setEnabled (false);
        updateLatency();
    }

    void SettingsPanel::updateLatency()
    {
        const auto l = session.getReportedLatency();
        reportedValue.setText (String (l.roundTripSamples()) + " samples, " + msText (l.roundTripMs()), dontSendNotification);
        reportedValue.setTooltip ("Input " + String (l.inputSamples) + " + output " + String (l.outputSamples)
                                  + " samples, including one " + String (l.bufferSize) + "-sample buffer each way");

        if (isMeasuring())
            measuredValue.setText ("Measuring…", dontSendNotification);
        else if (! lastMeasurement)
            measuredValue.setText ("Not measured", dontSendNotification);
        else if (lastMeasurement->confidence < engine::LatencyProbe::minimumConfidence)
            measuredValue.setText ("Nothing came back", dontSendNotification);
        else
        {
            const auto diff = lastMeasurement->samples - l.roundTripSamples();
            measuredValue.setText (String (lastMeasurement->samples) + " samples, " + msText (lastMeasurement->milliseconds()),
                                   dontSendNotification);
            measuredValue.setTooltip ("The driver's figure is " + String (std::abs (diff)) + " samples "
                                      + (diff >= 0 ? "short" : "long") + ". Peak clarity " + String (lastMeasurement->confidence, 1));
        }

        measuredValue.setColour (Label::textColourId,
                                 lastMeasurement && lastMeasurement->confidence < engine::LatencyProbe::minimumConfidence
                                     ? theme::warn : theme::text);
        measureButton.setEnabled (! isMeasuring() && ! session.isRecording());
    }

    bool SettingsPanel::isMeasuring() const
    {
        return session.getProbe().isBusy();
    }

    void SettingsPanel::startMeasurement()
    {
        if (session.getProbe().start (loopOut.getSelectedId() - 1, loopIn.getSelectedId() - 1))
        {
            if (onMeasure)
                onMeasure();

            updateLatency();
        }
    }

    void SettingsPanel::showMeasurement (const engine::LatencyProbe::Measurement& m)
    {
        lastMeasurement = m;
        const bool good = m.confidence >= engine::LatencyProbe::minimumConfidence;
        useMeasured.setEnabled (good);

        if (! good)
        {
            useMeasured.setToggleState (false, dontSendNotification);
            session.setMeasuredRoundTrip (std::nullopt);
        }
        else if (useMeasured.getToggleState())
        {
            session.setMeasuredRoundTrip (m.samples);
        }

        updateLatency();
    }

    void SettingsPanel::frame (int xruns, float engineCpu, engine::AudioCallback::Load load)
    {
        lastXruns = xruns;
        threadPeakShown = std::max (load.peak, threadPeakShown * 0.95f);
        cpuValue.setText (String (roundToInt (engineCpu * 100.0f)) + "%", dontSendNotification);
        threadValue.setText (String (roundToInt (load.average * 100.0f)) + "% average, "
                             + String (roundToInt (threadPeakShown * 100.0f)) + "% worst", dontSendNotification);
        cpuValue.setTooltip ("The engine's own estimate of its share of the audio deadline");
        threadValue.setTooltip ("Time spent in each audio block, over the time the block lasts");

        const auto dropouts = std::max (0, xruns - dropoutBase);
        dropoutsValue.setText (String (dropouts), dontSendNotification);
        dropoutsValue.setColour (Label::textColourId, dropouts > 0 ? theme::warn : theme::text);
        dropoutsValue.setTooltip ("Times the interface ran out of audio since this count started");

        if (measureButton.isEnabled() == (isMeasuring() || session.isRecording()))
            updateLatency();

        // The device stays as it is while a take records (applyDevice refuses); say so on the controls.
        const bool recording = session.isRecording();

        if (inputDevice.isEnabled() == recording)
            for (auto* c : { &inputDevice, &outputDevice, &rate, &buffer, &format })
            {
                if (recording)
                    c->getProperties().set ("tooltip", c->getTooltip());

                c->setEnabled (! recording);
                c->setTooltip (recording ? "Stop recording to change this" : c->getProperties()["tooltip"].toString());
            }
    }

    void SettingsPanel::paint (Graphics& g)
    {
        g.fillAll (theme::bg2);
        g.setColour (theme::line);
        g.fillRect (0, 0, 1, getHeight());
    }

    void SettingsPanel::setBetaUpdates (bool on, bool available, const String& note)
    {
        betaUpdates.setToggleState (on, dontSendNotification);
        betaUpdates.setEnabled (available);
        checkNow.setEnabled (available);
        unavailableNote = note;
        showUpdatesHint();
    }

    void SettingsPanel::showUpdatesHint()
    {
        // Why updates are off, or which feed this copy follows when it is the beta one.
        updatesHint.setText (unavailableNote.isNotEmpty() ? unavailableNote
                             : betaUpdates.getToggleState() ? "You get test versions before they are released. Switch this off to get released versions only."
                                                            : String(),
                             dontSendNotification);
        resized();
    }

    void SettingsPanel::resized()
    {
        auto outer = getLocalBounds().reduced (theme::space4, 0).withTrimmedLeft (1);
        outer.removeFromTop (theme::space4);
        auto top = outer.removeFromTop (rowHeight);
        closeButton.setBounds (top.removeFromRight (rowHeight));
        popButton.setBounds (top.removeFromRight (rowHeight));
        title.setBounds (top);
        outer.removeFromTop (theme::space1);

        find.setBounds (outer.removeFromTop (rowHeight));
        outer.removeFromTop (theme::space2);

        // The body is as tall as its rows; the viewport scrolls it when the window is shorter.
        viewport.setBounds (outer.withX (1).withRight (getWidth()));
        const auto bodyWidth = viewport.getWidth() - theme::space4 - viewport.getScrollBarThickness();
        auto r = Rectangle<int> (theme::space4 - 1, 0, bodyWidth - theme::space4 + 1, 10000);
        const auto startY = r.getY();
        const auto query = find.getText().trim();

        // Each line knows its words (label, button text, tooltip), so the search can keep it or
        // hide it (W41). A heading that matches keeps its whole section.
        struct Line
        {
            std::vector<Component*> parts;
            int height = 0;
            std::function<void (Rectangle<int>)> place;
        };

        struct Section
        {
            Label* heading = nullptr;
            std::vector<Line> lines;
        };

        const auto wordsOf = [] (Component* c)
        {
            String words;
            if (auto* l = dynamic_cast<Label*> (c))                words << l->getText() << " ";
            if (auto* b = dynamic_cast<Button*> (c))               words << b->getButtonText() << " ";
            if (auto* t = dynamic_cast<SettableTooltipClient*> (c)) words << t->getTooltip() << " ";
            if (auto* cb = dynamic_cast<ComboBox*> (c))            for (int i = 0; i < cb->getNumItems(); ++i) words << cb->getItemText (i) << " ";
            return words;
        };

        const auto matches = [&] (const Line& line)
        {
            if (query.isEmpty())
                return true;

            String words;
            for (auto* c : line.parts) words << wordsOf (c);

            for (const auto& word : StringArray::fromTokens (query, true))
                if (! words.containsIgnoreCase (word))
                    return false;

            return true;
        };

        const auto row = [] (Label& l, Component& c) -> Line
        {
            return { { &l, &c }, rowHeight + theme::space1, [&l, &c] (Rectangle<int> line)
            {
                line = line.withHeight (rowHeight);
                l.setBounds (line.removeFromLeft (labelWidth));
                c.setBounds (line);
            } };
        };

        const auto whole = [] (Component& c, int height) -> Line
        {
            return { { &c }, height, [&c] (Rectangle<int> line) { c.setBounds (line); } };
        };

        std::vector<Section> sections;
        sections.push_back ({ &interfaceHeading, { row (inputDeviceLabel, inputDevice), row (outputDeviceLabel, outputDevice),
                                                   row (rateLabel, rate), row (bufferLabel, buffer),
                                                   Line { { &testButton, &testHint }, rowHeight + 20 + theme::space1, [this] (Rectangle<int> b)
                                                          {
                                                              testButton.setBounds (b.removeFromTop (rowHeight).removeFromLeft (150));
                                                              testHint.setBounds (b.removeFromTop (20));
                                                          } } } });
        sections.push_back ({ &recordingHeading, { row (formatLabel, format) } });
        sections.push_back ({ &latencyHeading, { row (reportedLabel, reportedValue), row (measuredLabel, measuredValue),
                                                 Line { { &loopHint, &loopOutLabel, &loopOut, &loopInLabel, &loopIn, &measureButton },
                                                        48 + 2 * (rowHeight + theme::space1) + rowHeight + theme::space2,
                                                        [this] (Rectangle<int> b)
                                                        {
                                                            loopHint.setBounds (b.removeFromTop (48));
                                                            for (auto [l, c] : { std::pair<Label*, Component*> { &loopOutLabel, &loopOut }, { &loopInLabel, &loopIn } })
                                                            {
                                                                auto line = b.removeFromTop (rowHeight);
                                                                l->setBounds (line.removeFromLeft (labelWidth));
                                                                c->setBounds (line);
                                                                b.removeFromTop (theme::space1);
                                                            }
                                                            measureButton.setBounds (b.removeFromTop (rowHeight).removeFromLeft (96));
                                                        } },
                                                 whole (useMeasured, rowHeight) } });
        sections.push_back ({ &loadHeading, { row (cpuLabel, cpuValue), row (threadLabel, threadValue),
                                              Line { { &dropoutsLabel, &dropoutsValue, &resetDropouts }, rowHeight, [this] (Rectangle<int> line)
                                                     {
                                                         dropoutsLabel.setBounds (line.removeFromLeft (labelWidth));
                                                         resetDropouts.setBounds (line.removeFromRight (64));
                                                         dropoutsValue.setBounds (line);
                                                     } },
                                              whole (showLoad, rowHeight) } });
        sections.push_back ({ &projectsHeading, { row (atLaunchLabel, atLaunch), row (tempoFollowLabel, tempoFollow),
                                                  Line { { &copySamples, &copySamplesHint }, rowHeight + 50, [this] (Rectangle<int> b)
                                                         {
                                                             copySamples.setBounds (b.removeFromTop (rowHeight));
                                                             copySamplesHint.setBounds (b);
                                                         } } } });
        sections.push_back ({ &windowsHeading, { row (slotsLabel, slots), row (uiScaleLabel, uiScale) } });
        sections.push_back ({ &browserHeading, { row (panelWidthLabel, panelWidth), row (textSizeLabel, textSize),
                                                 whole (playWhileHeld, rowHeight), whole (fitLoops, rowHeight), whole (preMaster, rowHeight), whole (checkPlugins, rowHeight) } });
        sections.push_back ({ &metronomeHeading, { row (clickSoundLabel, clickSound), row (clickLevelLabel, clickLevel),
                                                   whole (clickAccent, rowHeight), whole (clickWhileRecording, rowHeight) } });
        sections.push_back ({ &timelineHeading, { row (beatShadingLabel, beatShading), whole (clickPlaces, rowHeight), whole (rightClickDeletes, rowHeight) } });
        const auto hintHeight = updatesHint.getText().isEmpty() ? 0 : 36;
        sections.push_back ({ &updatesHeading, { Line { { &versionLabel, &versionValue, &checkNow }, rowHeight + theme::space1, [this] (Rectangle<int> line)
                                                        {
                                                            line = line.withHeight (rowHeight);
                                                            versionLabel.setBounds (line.removeFromLeft (labelWidth));
                                                            checkNow.setBounds (line.removeFromRight (96));
                                                            versionValue.setBounds (line);
                                                        } },
                                                 whole (betaUpdates, rowHeight), whole (updatesHint, hintHeight) } });

        bool anyShown = false;

        for (auto& section : sections)
        {
            const auto headingMatches = query.isNotEmpty() && section.heading->getText().containsIgnoreCase (query);
            std::vector<const Line*> shown;

            for (const auto& line : section.lines)
            {
                const auto keep = headingMatches || matches (line);

                for (auto* c : line.parts)
                    c->setVisible (keep);

                if (keep)
                    shown.push_back (&line);
            }

            section.heading->setVisible (! shown.empty());

            if (shown.empty())
                continue;

            anyShown = true;
            r.removeFromTop (theme::space4);
            section.heading->setBounds (r.removeFromTop (20));
            r.removeFromTop (theme::space1);

            for (const auto* line : shown)
                line->place (r.removeFromTop (line->height));
        }

        // The shortcuts take the same search into their own list.
        const auto shortcutsShown = shortcuts.showMatching (query) || shortcutsHeading.getText().containsIgnoreCase (query);
        shortcutsHeading.setVisible (shortcutsShown);
        shortcuts.setVisible (shortcutsShown);

        if (shortcutsShown)
        {
            anyShown = true;
            r.removeFromTop (theme::space4);
            shortcutsHeading.setBounds (r.removeFromTop (20));
            r.removeFromTop (theme::space1);
            shortcuts.setBounds (r.withHeight (shortcuts.getIdealHeight()));   // it reports a new height if it needs one
            r.removeFromTop (shortcuts.getIdealHeight());
        }

        noMatch.setVisible (! anyShown);
        noMatch.setText ("No setting matches \"" + query + "\". Try another word, or clear the search with Escape.", dontSendNotification);
        noMatch.setBounds (Rectangle<int> (theme::space4 - 1, theme::space4, bodyWidth - theme::space4, 60));

        // While searching, the body is as tall as what it shows. Otherwise at least tall enough to
        // scroll the shortcuts to the top: a shortcut search that leaves few rows would shrink
        // the body, and the viewport would slide the search box down.
        body.setSize (bodyWidth, query.isNotEmpty() ? std::max (r.getY() - startY + theme::space4, 80)
                                                    : std::max (r.getY() - startY + theme::space4, shortcutsHeading.getY() - 8 + viewport.getHeight()));
    }

    void SettingsPanel::setInWindow (bool inWindow)
    {
        popButton.setGlyph (inWindow ? IconButton::Glyph::popIn : IconButton::Glyph::popOut);
        popButton.setTooltip (inWindow ? "Settings back into the main window" : "Settings in a window of its own");
        closeButton.setVisible (! inWindow);   // its window has its own close button
    }
}
