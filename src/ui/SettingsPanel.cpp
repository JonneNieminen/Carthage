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

    SettingsPanel::SettingsPanel (engine::Session& s) : session (s)
    {
        styleLabel (title, theme::textLg, theme::text, true);
        title.setText ("Audio settings", dontSendNotification);

        for (auto [label, text] : { std::pair { &interfaceHeading, "Interface" }, { &recordingHeading, "Recording" },
                                    { &latencyHeading, "Latency" }, { &loadHeading, "Load" } })
        {
            styleLabel (*label, theme::textSm, theme::muted, true);
            label->setText (text, dontSendNotification);
        }

        for (auto [label, text] : { std::pair { &inputDeviceLabel, "Input" }, { &outputDeviceLabel, "Output" },
                                    { &rateLabel, "Sample rate" }, { &bufferLabel, "Buffer size" },
                                    { &formatLabel, "Bit depth" }, { &reportedLabel, "Driver reports" },
                                    { &measuredLabel, "Measured" }, { &loopOutLabel, "Send from" },
                                    { &loopInLabel, "Return to" }, { &cpuLabel, "Engine" },
                                    { &threadLabel, "Audio thread" }, { &dropoutsLabel, "Dropouts" } })
        {
            styleLabel (*label, theme::textMd, theme::muted);
            label->setText (text, dontSendNotification);
        }

        for (auto* v : { &reportedValue, &measuredValue, &cpuValue, &threadValue, &dropoutsValue })
            styleFigure (*v);

        styleLabel (loopHint, theme::textSm, theme::muted);
        loopHint.setText ("Connect a cable from an output to an input, choose them here, then measure. "
                          "Everything else goes quiet while the probe plays.", dontSendNotification);
        loopHint.setJustificationType (Justification::topLeft);

        closeButton.setTooltip ("Close audio settings");
        closeButton.onClick = [this] { if (onClose) onClose(); };

        for (auto* c : { &inputDevice, &outputDevice, &rate, &buffer, &format, &loopOut, &loopIn })
            addAndMakeVisible (*c);

        for (auto* c : std::initializer_list<Component*> { &title, &closeButton, &interfaceHeading, &recordingHeading,
                                                           &latencyHeading, &loadHeading, &inputDeviceLabel, &outputDeviceLabel,
                                                           &rateLabel, &bufferLabel, &formatLabel, &reportedLabel, &reportedValue,
                                                           &measuredLabel, &measuredValue, &loopOutLabel, &loopInLabel, &loopHint,
                                                           &measureButton, &useMeasured, &cpuLabel, &cpuValue, &threadLabel,
                                                           &threadValue, &dropoutsLabel, &dropoutsValue, &resetDropouts })
            addAndMakeVisible (c);

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

            // A sensible default for a loopback cable: the last output pair into the last input,
            // away from the monitors and the guitar.
            loopOut.setSelectedId (std::max (1, outs.size()), dontSendNotification);
            loopIn.setSelectedId (std::max (1, ins.size()), dontSendNotification);
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

    void SettingsPanel::resized()
    {
        auto r = getLocalBounds().reduced (theme::space4, theme::space4).withTrimmedLeft (1);

        auto top = r.removeFromTop (rowHeight);
        closeButton.setBounds (top.removeFromRight (rowHeight));
        title.setBounds (top);

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
    }
}
