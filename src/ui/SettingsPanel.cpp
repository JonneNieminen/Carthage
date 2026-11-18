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

        for (auto [label, text] : { std::pair { &interfaceHeading, "Interface" }, { &recordingHeading, "Recording" },
                                    { &latencyHeading, "Latency" }, { &loadHeading, "Load" }, { &projectsHeading, "Projects" }, { &browserHeading, "Browser" },
                                    { &updatesHeading, "Updates" },
                                    { &shortcutsHeading, "Keyboard shortcuts" } })
        {
            styleLabel (*label, theme::textSm, theme::muted, true);
            label->setText (text, dontSendNotification);
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
        panelWidth.setTooltip ("How wide the samples and plugins panel is");
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

        for (auto* c : std::initializer_list<Component*> { &browserHeading, &panelWidthLabel, &panelWidth, &textSizeLabel, &textSize, &playWhileHeld, &fitLoops })
            body.addAndMakeVisible (c);

        for (auto* c : std::initializer_list<Component*> { &projectsHeading, &copySamples, &copySamplesHint, &versionLabel, &versionValue, &checkNow, &updatesHeading, &updatesHint, &betaUpdates, &interfaceHeading, &recordingHeading,
                                                           &latencyHeading, &loadHeading, &inputDeviceLabel, &outputDeviceLabel,
                                                           &rateLabel, &bufferLabel, &formatLabel, &reportedLabel, &reportedValue,
                                                           &measuredLabel, &measuredValue, &loopOutLabel, &loopInLabel, &loopHint,
                                                           &measureButton, &useMeasured, &cpuLabel, &cpuValue, &threadLabel,
                                                           &threadValue, &dropoutsLabel, &dropoutsValue, &resetDropouts,
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

    void SettingsPanel::applyDevice (std::function<void (AudioDeviceManager::AudioDeviceSetup&)> change)
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

    void SettingsPanel::refresh()
    {
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
        title.setBounds (top);
        outer.removeFromTop (theme::space1);

        // The body is as tall as its rows; the viewport scrolls it when the window is shorter.
        viewport.setBounds (outer.withX (1).withRight (getWidth()));
        const auto bodyWidth = viewport.getWidth() - theme::space4 - viewport.getScrollBarThickness();
        auto r = Rectangle<int> (theme::space4 - 1, 0, bodyWidth - theme::space4 + 1, 10000);
        const auto startY = r.getY();

        const auto heading = [&] (Label& l)
        {
            r.removeFromTop (theme::space4);
            l.setBounds (r.removeFromTop (20));
            r.removeFromTop (theme::space1);
        };

        const auto row = [&] (Label& l, Component& c)
        {
            auto line = r.removeFromTop (rowHeight);
            l.setBounds (line.removeFromLeft (labelWidth));
            c.setBounds (line);
            r.removeFromTop (theme::space1);
        };

        heading (interfaceHeading);
        row (inputDeviceLabel, inputDevice);
        row (outputDeviceLabel, outputDevice);
        row (rateLabel, rate);
        row (bufferLabel, buffer);

        heading (recordingHeading);
        row (formatLabel, format);

        heading (latencyHeading);
        row (reportedLabel, reportedValue);
        row (measuredLabel, measuredValue);
        loopHint.setBounds (r.removeFromTop (48));
        row (loopOutLabel, loopOut);
        row (loopInLabel, loopIn);
        auto actions = r.removeFromTop (rowHeight);
        measureButton.setBounds (actions.removeFromLeft (96));
        r.removeFromTop (theme::space2);
        useMeasured.setBounds (r.removeFromTop (rowHeight));

        heading (loadHeading);
        row (cpuLabel, cpuValue);
        row (threadLabel, threadValue);
        auto drops = r.removeFromTop (rowHeight);
        dropoutsLabel.setBounds (drops.removeFromLeft (labelWidth));
        resetDropouts.setBounds (drops.removeFromRight (64));
        dropoutsValue.setBounds (drops);

        heading (projectsHeading);
        copySamples.setBounds (r.removeFromTop (rowHeight));
        copySamplesHint.setBounds (r.removeFromTop (50));

        heading (browserHeading);
        row (panelWidthLabel, panelWidth);
        row (textSizeLabel, textSize);
        playWhileHeld.setBounds (r.removeFromTop (rowHeight));
        fitLoops.setBounds (r.removeFromTop (rowHeight));

        heading (updatesHeading);
        {
            auto line = r.removeFromTop (rowHeight);
            versionLabel.setBounds (line.removeFromLeft (labelWidth));
            checkNow.setBounds (line.removeFromRight (96));
            versionValue.setBounds (line);
            r.removeFromTop (theme::space1);
        }
        betaUpdates.setBounds (r.removeFromTop (rowHeight));
        const auto hintHeight = updatesHint.getText().isEmpty() ? 0 : 36;
        updatesHint.setBounds (r.removeFromTop (hintHeight));

        heading (shortcutsHeading);
        shortcuts.setBounds (r.withHeight (shortcuts.getIdealHeight()));   // it reports a new height if it needs one
        r.removeFromTop (shortcuts.getIdealHeight());

        // At least tall enough to scroll the shortcuts to the top: a search that leaves few rows
        // would otherwise shrink the body, and the viewport would slide the search box down.
        body.setSize (bodyWidth, std::max (r.getY() - startY + theme::space4,
                                           shortcutsHeading.getY() - 8 + viewport.getHeight()));
    }
}
