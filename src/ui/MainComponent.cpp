#include "MainComponent.h"

namespace carthage::ui
{
    using namespace juce;
    namespace cmd = app::commands;

    //==============================================================================================
    /** A plugin's own editor in a plain host window (docs/ARCHITECTURE.md, "Plugins"). */
    class PluginWindow final : public DocumentWindow
    {
    public:
        PluginWindow (AudioPluginInstance& instance, std::function<void()> closed)
            : DocumentWindow (instance.getName(), theme::bg, DocumentWindow::closeButton), onClosed (std::move (closed))
        {
            setUsingNativeTitleBar (true);

            // A plugin without its own editor gets JUCE's generic parameter view.
            AudioProcessorEditor* editor = instance.hasEditor() ? instance.createEditorAndMakeActive() : nullptr;

            if (editor == nullptr)
                editor = new GenericAudioProcessorEditor (instance);

            setContentOwned (editor, true);
            setResizable (editor->isResizable(), false);
            centreWithSize (std::max (getWidth(), 200), std::max (getHeight(), 100));
            setVisible (true);
        }

        void closeButtonPressed() override   { if (onClosed) onClosed(); }

    private:
        std::function<void()> onClosed;
    };

    //==============================================================================================
    /** The signal path on one line: input, plugin, output, joined by a cable that lights when the
        input is audible. The plugin is the one raised thing on it. */
    class MainComponent::SignalRow final : public Component, public DragAndDropTarget
    {
    public:
        SignalRow (engine::Session& s, ApplicationCommandManager& c) : session (s), commands (c)
        {
            styleLabel (inputHeading, theme::textSm, theme::muted, true);
            styleLabel (outputHeading, theme::textSm, theme::muted, true);
            styleLabel (pluginName, theme::textLg, theme::text, true);
            styleLabel (pluginDetail, theme::textSm, theme::muted);
            inputHeading.setText ("Input", dontSendNotification);
            outputHeading.setText ("Output", dontSendNotification);

            input.setTooltip ("The input your guitar is plugged into");
            output.setTooltip ("Where you hear Carthage");
            monitor.setTooltip (cmd::tooltip (commands, cmd::toggleMonitoring));
            choose.setTooltip (cmd::tooltip (commands, cmd::choosePlugin));
            open.setTooltip (cmd::tooltip (commands, cmd::openPluginEditor));
            bypass.setTooltip (cmd::tooltip (commands, cmd::bypassPlugin));
            remove.setTooltip ("Take the plugin out of the path");

            monitor.setToggleState (session.isMonitoring(), dontSendNotification);
            monitor.onClick = [this] { commands.invokeDirectly (cmd::toggleMonitoring, false); };
            choose.onClick = [this] { commands.invokeDirectly (cmd::choosePlugin, false); };
            open.onClick = [this] { commands.invokeDirectly (cmd::openPluginEditor, false); };
            bypass.onClick = [this] { commands.invokeDirectly (cmd::bypassPlugin, false); };
            remove.onClick = [this] { if (onRemove) onRemove(); };

            input.onChange = [this]
            {
                const auto id = input.getSelectedId();
                session.setInput ((id - 1) % 1000, id > 1000);
            };

            output.onChange = [this]
            {
                auto& dm = session.getEngine().getDeviceManager();
                if (auto* wo = dm.getWaveOutDevice (output.getSelectedId() - 1))
                    dm.setDefaultWaveOutDevice (wo->getDeviceID());
            };

            for (auto* child : std::initializer_list<Component*> { &inputHeading, &input, &meterL, &meterR, &monitor,
                                                                   &pluginName, &pluginDetail, &choose, &open, &bypass,
                                                                   &remove, &outputHeading, &output })
                addAndMakeVisible (child);

            // Buttons sit on the card, so they take the next surface up.
            for (auto* b : { &choose, &open, &bypass, &remove })
                b->setColour (TextButton::buttonColourId, theme::hover);

            refreshDevices();
            refreshPlugin();
        }

        void refreshDevices()
        {
            input.clear (dontSendNotification);
            output.clear (dontSendNotification);

            if (auto* device = session.getDeviceManager().getCurrentAudioDevice())
            {
                // The interface's own channel names when it has them ("Analogue 1"). A bare number, or
                // nothing (the built-in microphone calls its channel "1"), reads as "Input 1".
                const auto names = device->getInputChannelNames();
                const auto ins = names.size();
                const auto nameOf = [&] (int i)
                {
                    const auto n = names[i].trim();
                    return n.isEmpty() || n.containsOnly ("0123456789") ? "Input " + String (i + 1) : n;
                };

                // Ids: channel + 1 for mono, 1000 + channel + 1 for a stereo pair.
                for (int i = 0; i < ins; ++i)
                    input.addItem (nameOf (i), i + 1);

                if (ins > 1)
                {
                    input.addSeparator();
                    for (int i = 0; i + 1 < ins; i += 2)
                        input.addItem (nameOf (i) + " + " + nameOf (i + 1), 1000 + i + 1);
                }

                input.setSelectedId (session.isInputStereo() ? 1000 + session.getInputChannel() + 1
                                                             : session.getInputChannel() + 1, dontSendNotification);
            }

            auto& dm = session.getEngine().getDeviceManager();

            for (int i = 0; i < dm.getNumWaveOutDevices(); ++i)
                if (auto* wo = dm.getWaveOutDevice (i))
                {
                    output.addItem (wo->getName(), i + 1);
                    if (wo->getDeviceID() == dm.getDefaultWaveOutDeviceID())
                        output.setSelectedId (i + 1, dontSendNotification);
                }

            meterR.setVisible (session.isInputStereo());
            resized();
        }

        void refreshPlugin()
        {
            auto* plugin = session.getInsertedPlugin();
            const bool has = plugin != nullptr;

            if (scanning.isNotEmpty())
            {
                pluginName.setText (scanning, dontSendNotification);
                pluginDetail.setText ("Checking it in a separate process first", dontSendNotification);
            }
            else if (has)
            {
                pluginName.setText (plugin->getName(), dontSendNotification);
                String detail = plugin->getVendor();

                if (auto* ext = dynamic_cast<tracktion::ExternalPlugin*> (plugin))
                    detail << "  " << (ext->desc.pluginFormatName == "AudioUnit" ? "Audio Unit" : ext->desc.pluginFormatName);

                if (! plugin->isEnabled())
                    detail << "  bypassed";

                pluginDetail.setText (detail, dontSendNotification);
            }
            else
            {
                pluginName.setText ("No plugin", dontSendNotification);
                pluginDetail.setText (message.isNotEmpty() ? message : "Choose an amp sim or effect to play through",
                                      dontSendNotification);
            }

            pluginDetail.setColour (Label::textColourId, messageIsWarning && ! has ? theme::warn : theme::muted);
            choose.setButtonText (has ? "Change" : "Choose a plugin");
            choose.setEnabled (scanning.isEmpty());
            open.setVisible (has);
            bypass.setVisible (has);
            remove.setVisible (has);
            bypass.setToggleState (has && ! plugin->isEnabled(), dontSendNotification);
            resized();
            repaint();
        }

        /** A take records the input it started with: the pickers lock until it ends. Changing either
            would rebuild the engine's device list, which stops playback under the take. */
        void setRecording (bool recording)
        {
            if (input.isEnabled() != recording)
                return;

            input.setEnabled (! recording);
            output.setEnabled (! recording);
            input.setTooltip (recording ? "Stop recording to change the input" : "The input your guitar is plugged into");
            output.setTooltip (recording ? "Stop recording to change the output" : "Where you hear Carthage");
        }

        void setMessage (const String& m, bool warning)   { message = m; messageIsWarning = warning; refreshPlugin(); }
        void setScanning (const String& name)             { scanning = name; refreshPlugin(); }

        void frame (std::array<float, 2> peaks, double dt)
        {
            meterL.push (peaks[0], dt);
            if (meterR.isVisible()) meterR.push (peaks[1], dt);

            // The cable is live when the input is audible: monitoring on, and some signal arriving.
            const bool live = session.isMonitoring() && std::max (peaks[0], peaks[1]) > 0.0005f;
            if (live != cableLive) { cableLive = live; repaint (cableArea()); }
            if (monitor.getToggleState() != session.isMonitoring()) monitor.setToggleState (session.isMonitoring(), dontSendNotification);
        }

        std::function<void()> onRemove;

        /** A plugin dragged from the browser onto the card: its library key. */
        std::function<void (const String&)> onPluginDropped;

        bool isInterestedInDragSource (const SourceDetails& d) override
        {
            return d.description.toString().startsWith (PluginBrowser::dragPrefix);
        }

        void itemDragEnter (const SourceDetails&) override   { dropHover = true; repaint(); }
        void itemDragExit (const SourceDetails&) override    { dropHover = false; repaint(); }

        void itemDropped (const SourceDetails& d) override
        {
            dropHover = false;
            repaint();

            if (onPluginDropped)
                onPluginDropped (d.description.toString().fromFirstOccurrenceOf (PluginBrowser::dragPrefix, false, false));
        }

        void paint (Graphics& g) override
        {
            g.fillAll (theme::bg2);
            g.setColour (theme::line);
            g.fillRect (0, getHeight() - 1, getWidth(), 1);

            // The cable: one line through the row, behind the plugin card.
            const auto cable = cableArea().toFloat();
            const auto y = cable.getCentreY();
            g.setColour (cableLive ? theme::accent : theme::lineStrong);
            Path p;
            p.startNewSubPath (cable.getX(), y);
            p.lineTo (cable.getRight(), y);
            g.strokePath (p, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.fillEllipse (cable.getX() - 3.0f, y - 3.0f, 6.0f, 6.0f);
            g.fillEllipse (cable.getRight() - 3.0f, y - 3.0f, 6.0f, 6.0f);

            // The plugin card.
            const auto r = cardBounds.toFloat();
            g.setColour (theme::card);
            g.fillRoundedRectangle (r, theme::radiusMd);
            g.setColour (session.getInsertedPlugin() != nullptr && session.getInsertedPlugin()->isEnabled() && cableLive
                             ? theme::accent.withAlpha (0.6f) : theme::line);
            g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusMd, theme::stroke);

            if (dropHover)
            {
                // A plugin is being dragged over the row: the card is where it lands.
                g.setColour (theme::accent);
                g.drawRoundedRectangle (r.expanded (2.0f), theme::radiusMd + 2.0f, 2.0f);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (theme::space4, theme::space3);

            auto in = r.removeFromLeft (220);
            inputHeading.setBounds (in.removeFromTop (16));
            in.removeFromTop (theme::space1);
            input.setBounds (in.removeFromTop (28));
            in.removeFromTop (theme::space2);
            auto meters = in.removeFromTop (meterR.isVisible() ? 14 : 6);
            if (meterR.isVisible())
            {
                meterL.setBounds (meters.removeFromTop (6));
                meters.removeFromTop (2);
                meterR.setBounds (meters.removeFromTop (6));
            }
            else
            {
                meterL.setBounds (meters);
            }
            in.removeFromTop (theme::space2);
            monitor.setBounds (in.removeFromTop (22));

            auto out = r.removeFromRight (200);
            outputHeading.setBounds (out.removeFromTop (16));
            out.removeFromTop (theme::space1);
            output.setBounds (out.removeFromTop (28));

            inputEdge = in.getRight() + theme::space2;
            outputEdge = out.getX() - theme::space2;

            const auto middle = Rectangle<int> (inputEdge, r.getY(), outputEdge - inputEdge, r.getHeight());
            cardBounds = middle.withSizeKeepingCentre (std::min (460, middle.getWidth() - 2 * theme::space8), 76);

            auto card = cardBounds.reduced (theme::space4, theme::space3);
            auto buttons = card.removeFromRight (open.isVisible() ? 236 : 132).withSizeKeepingCentre (open.isVisible() ? 236 : 132, 28);
            choose.setBounds (buttons.removeFromLeft (open.isVisible() ? 72 : 132));

            if (open.isVisible())
            {
                buttons.removeFromLeft (theme::space1);
                open.setBounds (buttons.removeFromLeft (52));
                buttons.removeFromLeft (theme::space1);
                bypass.setBounds (buttons.removeFromLeft (64));
                buttons.removeFromLeft (theme::space1);
                remove.setBounds (buttons);
            }

            pluginName.setBounds (card.removeFromTop (card.getHeight() / 2).withTrimmedRight (theme::space2));
            pluginDetail.setBounds (card.withTrimmedRight (theme::space2));
        }

    private:
        engine::Session& session;
        ApplicationCommandManager& commands;
        Label inputHeading, outputHeading, pluginName, pluginDetail;
        ComboBox input, output;
        LevelMeter meterL, meterR;
        ToggleButton monitor { "Hear the input" };
        TextButton choose { "Choose a plugin" }, open { "Open" }, bypass { "Bypass" }, remove { "Remove" };
        Rectangle<int> cardBounds;
        bool dropHover = false;
        int inputEdge = 0, outputEdge = 0;
        bool cableLive = false, messageIsWarning = false;
        String message, scanning;

        Rectangle<int> cableArea() const
        {
            return { inputEdge, cardBounds.getCentreY() - 4, outputEdge - inputEdge, 8 };
        }
    };

    //==============================================================================================
    MainComponent::MainComponent (engine::Session& s, ApplicationCommandManager& c)
        : session (s), commands (c), takeLane (s.getTrack()), settings (s), browser (s)
    {
        signalRow = std::make_unique<SignalRow> (session, commands);
        signalRow->onRemove = [this] { closePluginWindow(); session.removePlugin(); signalRow->setMessage ({}, false); };

        recordButton.setTooltip (cmd::tooltip (commands, cmd::record));
        playButton.setTooltip (cmd::tooltip (commands, cmd::playStop));
        toStartButton.setTooltip (cmd::tooltip (commands, cmd::returnToStart));
        settingsButton.setTooltip (cmd::tooltip (commands, cmd::audioSettings));

        recordButton.onClick = [this] { commands.invokeDirectly (cmd::record, false); };
        playButton.onClick = [this] { commands.invokeDirectly (cmd::playStop, false); };
        toStartButton.onClick = [this] { commands.invokeDirectly (cmd::returnToStart, false); };
        settingsButton.onClick = [this] { commands.invokeDirectly (cmd::audioSettings, false); };

        styleFigure (timeLabel, theme::textXl);
        styleFigure (latencyLabel, theme::textMd, theme::text);
        styleLabel (latencyWords, theme::textSm, theme::muted);
        latencyWords.setText ("round trip", dontSendNotification);
        styleLabel (statusLabel, theme::textSm, theme::muted);
        latencyLabel.setJustificationType (Justification::centredRight);
        latencyLabel.setTooltip ("Round trip from the input jack to your ears, as the interface's driver reports it. "
                                 "Measure the real figure in audio settings.");
        latencyWords.setTooltip (latencyLabel.getTooltip());
        loadMeter.setTooltip ("Audio-thread load: the bar is the average, the figure the worst recent block");

        for (auto* comp : std::initializer_list<Component*> { &recordButton, &playButton, &toStartButton, &timeLabel,
                                                              &loadMeter, &latencyLabel, &latencyWords, &settingsButton,
                                                              signalRow.get(), &takeLane, &statusLabel })
            addAndMakeVisible (comp);

        addChildComponent (settings);
        addAndMakeVisible (browser);
        browser.onInsert = [this] (const PluginDescription& d) { insertPlugin (d); };
        signalRow->onPluginDropped = [this] (const String& key)
        {
            if (const auto* e = session.getPluginLibrary().findEntry (key))
                insertPlugin (e->description);
        };
        session.onPluginsChanged = [this] { browser.refresh(); };
        addChildComponent (updateBanner);   // shown by the app's updater
        settings.onClose = [this] { toggleSettings(); };

        session.onDeviceChanged = [this]
        {
            signalRow->refreshDevices();
            settings.refresh();
            updateFigures();
        };

        session.onTakeEnded = [this] (const engine::TakeRecorder::Result& result)
        {
            takeFinished (result);
            updateTransportButtons();
            commands.commandStatusChanged();
        };

        setWantsKeyboardFocus (true);
        vblank = std::make_unique<VBlankAttachment> (this, [this] (double t) { frame (t); });
        updateTransportButtons();
        updateFigures();   // the round trip and load show from the first paint, not the first tick
        setStatus ("Ready");
    }

    MainComponent::~MainComponent()
    {
        vblank.reset();
        session.onDeviceChanged = nullptr;
        session.onTakeEnded = nullptr;
        session.onPluginsChanged = nullptr;
        closePluginWindow();
    }

    //==============================================================================================
    void MainComponent::getAllCommands (Array<CommandID>& ids)
    {
        ids.addArray (cmd::all());
    }

    void MainComponent::getCommandInfo (CommandID id, ApplicationCommandInfo& info)
    {
        cmd::describe (id, info);
        const bool hasPlugin = session.getInsertedPlugin() != nullptr;

        switch (id)
        {
            case cmd::toggleMonitoring:  info.setTicked (session.isMonitoring()); break;
            case cmd::openPluginEditor:  info.setActive (hasPlugin); break;
            case cmd::bypassPlugin:      info.setActive (hasPlugin);
                                         info.setTicked (hasPlugin && ! session.getInsertedPlugin()->isEnabled()); break;
            case cmd::record:            info.setTicked (session.isRecording()); break;
            case cmd::audioSettings:     info.setTicked (settingsOpen); break;
            case cmd::pluginBrowser:     info.setTicked (browserOpen); break;
            case cmd::measureLatency:    info.setActive (! session.isRecording()); break;
            default: break;
        }
    }

    bool MainComponent::perform (const InvocationInfo& info)
    {
        switch (info.commandID)
        {
            case cmd::record:           toggleRecord(); return true;
            case cmd::playStop:         togglePlay(); return true;
            case cmd::returnToStart:    if (! session.isRecording()) session.returnToStart(); return true;
            case cmd::toggleMonitoring: session.setMonitoring (! session.isMonitoring()); return true;
            case cmd::choosePlugin:     choosePlugin(); return true;
            case cmd::pluginBrowser:    toggleBrowser(); return true;
            case cmd::openPluginEditor: openPluginWindow(); return true;
            case cmd::bypassPlugin:
                if (auto* p = session.getInsertedPlugin())
                {
                    p->setEnabled (! p->isEnabled());
                    signalRow->refreshPlugin();
                }
                return true;
            case cmd::audioSettings:    toggleSettings(); return true;
            case cmd::measureLatency:
                if (! settingsOpen) toggleSettings();
                return true;
            default: return false;
        }
    }

    //==============================================================================================
    void MainComponent::toggleRecord()
    {
        if (session.isRecording())
        {
            takeFinished (session.stopRecording());
        }
        else
        {
            if (session.getProbe().isBusy())
                return;

            const auto from = session.getPositionSeconds();

            if (auto r = session.startRecording(); r.failed())
            {
                setStatus (r.getErrorMessage(), theme::warn);
                return;
            }

            recordingFrom = from;
            setStatus ("Recording", theme::clip);
        }

        updateTransportButtons();
        commands.commandStatusChanged();
    }

    void MainComponent::takeFinished (const engine::TakeRecorder::Result& result)
    {
        recordingFrom.reset();
        const auto& take = session.getLastTake();
        const auto seconds = String (static_cast<double> (result.framesWritten) / std::max (1.0, session.getCallback().getSampleRate()), 2);

        if (! result.succeeded())
            setStatus (result.error, theme::warn);
        else if (take && take->placedByGuess)
            setStatus ("Recording stopped: playback did not start. Saved " + result.file.getFileName() + ", " + seconds
                           + " s, placed where recording began; check its timing", theme::warn);
        else if (take && take->interrupted)
            setStatus ("Recording stopped because playback stopped. Saved " + result.file.getFileName() + ", " + seconds
                           + " s, in time", theme::warn);
        else if (result.overflows > 0)
            setStatus ("Take saved, but " + String (result.framesDropped) + " samples were lost: the disk could not keep up ("
                           + String (result.overflows) + " times)", theme::warn);
        else if (take)
            setStatus ("Saved " + result.file.getFileName() + ", " + seconds + " s, placed "
                           + String (take->alignmentSamples) + " samples earlier for latency");
    }

    void MainComponent::togglePlay()
    {
        if (session.isRecording())
            toggleRecord();
        else if (session.isPlaying())
            session.stop();
        else
            session.play();

        updateTransportButtons();
    }

    void MainComponent::updateTransportButtons()
    {
        const bool playing = session.isPlaying();
        recordButton.setLit (theme::clip, session.isRecording());
        playButton.setGlyph (playing ? IconButton::Glyph::stop : IconButton::Glyph::play);
        playButton.setLit (theme::accent, playing);
        toStartButton.setEnabled (! session.isRecording());
        signalRow->setRecording (session.isRecording());
    }

    //==============================================================================================
    void MainComponent::choosePlugin()
    {
        // The browser is where plugins are chosen; this opens it with the search ready.
        if (! browserOpen)
            toggleBrowser();

        browser.focusSearch();
    }

    void MainComponent::toggleBrowser()
    {
        browserOpen = ! browserOpen;
        browser.setVisible (browserOpen);
        resized();
        commands.commandStatusChanged();
    }

    void MainComponent::insertPlugin (const PluginDescription& d)
    {
        closePluginWindow();
        const auto r = session.insertPlugin (d);
        signalRow->setMessage (r.failed() ? r.getErrorMessage() : String(), r.failed());
        setStatus (r.failed() ? d.name + " could not be loaded" : d.name + " is in the path", r.failed() ? theme::warn : theme::muted);
        browser.refresh();   // Recent
        commands.commandStatusChanged();
    }

    void MainComponent::openPluginWindow()
    {
        if (pluginWindow != nullptr)
        {
            pluginWindow->toFront (true);
            return;
        }

        if (auto* ext = dynamic_cast<tracktion::ExternalPlugin*> (session.getInsertedPlugin()))
            if (auto* instance = ext->getAudioPluginInstance())
                pluginWindow = std::make_unique<PluginWindow> (*instance, [this]
                {
                    MessageManager::callAsync ([safe = SafePointer<MainComponent> (this)] { if (safe) safe->closePluginWindow(); });
                });
    }

    void MainComponent::closePluginWindow()
    {
        pluginWindow.reset();
    }

    //==============================================================================================
    void MainComponent::toggleSettings()
    {
        settingsOpen = ! settingsOpen;

        if (settingsOpen)
        {
            settings.refresh();
            updateFigures();   // fill the load figures now, not on the next tick
        }

        layoutSettings (true);
        layoutUpdateBanner();
        commands.commandStatusChanged();
    }

    void MainComponent::layoutSettings (bool animate)
    {
        const auto shown = getLocalBounds().removeFromRight (SettingsPanel::preferredWidth).withTrimmedTop (56);
        const auto hidden = shown.withX (getWidth());

        settings.setVisible (true);
        settings.toFront (false);

        // Motion answers the action and is short (design/README.md); no animation while playing.
        if (animate && ! session.isPlaying())
        {
            settings.setBounds (settingsOpen ? hidden : shown);
            Desktop::getInstance().getAnimator().animateComponent (&settings, settingsOpen ? shown : hidden, 1.0f,
                                                                   theme::slow, false, 0.0, 0.0);
        }
        else
        {
            settings.setBounds (settingsOpen ? shown : hidden);
        }

        if (! settingsOpen && ! animate)
            settings.setVisible (false);
    }

    void MainComponent::setStatus (const String& text, Colour colour)
    {
        statusLabel.setText (text, dontSendNotification);
        statusLabel.setColour (Label::textColourId, colour);
    }

    //==============================================================================================
    void MainComponent::frame (double t)
    {
        const auto dt = lastFrameTime > 0.0 ? std::clamp (t - lastFrameTime, 0.0, 0.1) : 1.0 / 60.0;
        lastFrameTime = t;
        ++frameCount;

        auto& callback = session.getCallback();
        signalRow->frame (callback.takeInputPeaks(), dt);

        const auto position = session.getPositionSeconds();
        takeLane.setPlayhead (position, recordingFrom);

        const auto whole = static_cast<int> (position);
        const auto ms = static_cast<int> ((position - whole) * 1000.0);
        timeLabel.setText (String (whole / 60) + ":" + String (whole % 60).paddedLeft ('0', 2) + "." + String (ms).paddedLeft ('0', 3),
                           dontSendNotification);

        if (auto m = session.getProbe().takeResult())
        {
            settings.showMeasurement (*m);
            setStatus (m->confidence >= engine::LatencyProbe::minimumConfidence
                           ? "Measured round trip: " + String (m->samples) + " samples, " + String (m->milliseconds(), 2) + " ms"
                           : "The probe heard nothing back. Check the cable and the two channels.",
                       m->confidence >= engine::LatencyProbe::minimumConfidence ? theme::muted : theme::warn);
        }

        // Slower figures: about eight times a second.
        if (frameCount % 15 == 0)
            updateFigures();
    }

    void MainComponent::updateFigures()
    {
        auto& callback = session.getCallback();

        {
            const auto load = callback.takeLoad();
            loadMeter.setLoad (load.average, load.peak);

            const auto reported = session.getReportedLatency();
            latencyLabel.setText (String (reported.roundTripMs(), 1) + " ms", dontSendNotification);

            int xruns = 0;
            if (auto* device = session.getDeviceManager().getCurrentAudioDevice())
                xruns = device->getXRunCount();

            if (settingsOpen)
                settings.frame (xruns, session.getEngine().getDeviceManager().getCpuUsage(), load);

            if (session.isRecording() && session.getRecorder().getOverflowCount() > 0)
                setStatus ("Recording, but the disk is falling behind: "
                               + String (session.getRecorder().getFramesDropped()) + " samples lost", theme::warn);

            if (playButton.isMouseOverOrDragging() == false)
                updateTransportButtons();

            updateBanner.setBusy (session.isPlaying() || session.isRecording());
            browser.frame();
        }
    }

    //==============================================================================================
    void MainComponent::paint (Graphics& g)
    {
        g.fillAll (theme::bg);
    }

    void MainComponent::resized()
    {
        auto r = getLocalBounds();

        auto top = r.removeFromTop (56).reduced (theme::space4, theme::space2 + 2);
        recordButton.setBounds (top.removeFromLeft (36));
        top.removeFromLeft (theme::space1);
        playButton.setBounds (top.removeFromLeft (36));
        top.removeFromLeft (theme::space1);
        toStartButton.setBounds (top.removeFromLeft (36));
        top.removeFromLeft (theme::space4);
        timeLabel.setBounds (top.removeFromLeft (140));

        settingsButton.setBounds (top.removeFromRight (36));
        top.removeFromRight (theme::space4);
        latencyWords.setBounds (top.removeFromRight (64));
        top.removeFromRight (theme::space1);   // the figure and its words read as "6.3 ms round trip"
        latencyLabel.setBounds (top.removeFromRight (72));
        top.removeFromRight (theme::space6);
        loadMeter.setBounds (top.removeFromRight (140).withSizeKeepingCentre (140, 20));

        // The browser docks on the left, under the transport; the rest of the window shifts over.
        browser.setBounds (r.removeFromLeft (browserOpen ? PluginBrowser::preferredWidth : 0));

        signalRow->setBounds (r.removeFromTop (132));

        statusLabel.setBounds (r.removeFromBottom (28).reduced (theme::space4, 0));
        takeLane.setBounds (r.reduced (theme::space4, theme::space3));

        layoutSettings (false);
        layoutUpdateBanner();
    }

    void MainComponent::showUpdate (std::optional<UpdateBanner::Model> model)
    {
        updateBanner.show (std::move (model));
        layoutUpdateBanner();
    }

    void MainComponent::layoutUpdateBanner()
    {
        // Floats over the bottom right of the lane, above the status line and left of the settings
        // when they are open; it never moves the rest of the window.
        const auto right = settingsOpen ? getWidth() - SettingsPanel::preferredWidth : getWidth();
        const auto w = UpdateBanner::preferredWidth;
        const auto h = updateBanner.getPreferredHeight (w);
        updateBanner.setBounds (right - w - theme::space2, getHeight() - 28 - h, w, h);
        updateBanner.toFront (false);
    }
}
