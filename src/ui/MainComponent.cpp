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
            refreshTooltips();
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

        /** Tooltips name the current shortcuts. */
        void refreshTooltips()
        {
            monitor.setTooltip (cmd::tooltip (commands, cmd::toggleMonitoring));
            choose.setTooltip (cmd::tooltip (commands, cmd::choosePlugin));
            open.setTooltip (cmd::tooltip (commands, cmd::openPluginEditor));
            bypass.setTooltip (cmd::tooltip (commands, cmd::bypassPlugin));
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
        : session (s), commands (c), timeline (s), settings (s, c, cmd::inMenuOrder()), browser (s), samples (s)
    {
        signalRow = std::make_unique<SignalRow> (session, commands);
        signalRow->onRemove = [this] { closePluginWindow(); session.removePlugin(); signalRow->setMessage ({}, false); };


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
                                                              signalRow.get(), &timeline, &statusLabel })
            addAndMakeVisible (comp);

        addChildComponent (settings);
        addAndMakeVisible (browser);
        addChildComponent (samples);

        // The left panel: samples or plugins, chosen at its top (F19). The choice and the
        // width are remembered.
        auto& props = session.getEngine().getPropertyStorage().getPropertiesFile();
        panelShowsSamples = props.getBoolValue ("panelShowsSamples", false);
        panelWidth = jlimit (240, 520, props.getIntValue ("panelWidth", PluginBrowser::preferredWidth));

        for (auto [tab, isSamples] : { std::pair { &samplesTab, true }, { &pluginsTab, false } })
        {
            tab->setClickingTogglesState (false);
            tab->onClick = [this, v = isSamples] { showPanel (v, false); };
            addAndMakeVisible (*tab);
        }

        samplesTab.setTooltip ("Your samples");
        pluginsTab.setTooltip ("Your plugins");
        session.getSampleLibrary().onChange = [this] { samples.refresh(); };
        settings.onPanelWidthChanged = [this] (int w) { setPanelWidth (w); };
        settings.onBrowserTextSizeChanged = [this] (float px) { samples.setTextSize (px); };
        settings.onPlayWhileHeldChanged = [this] (bool held) { samples.setPlayWhileHeld (held); };
        showPanel (panelShowsSamples, false);

        addChildComponent (startScreen);

        // Tabs: a click brings a project forward; closing asks to save, as Cmd/Ctrl+W does.
        addAndMakeVisible (tabs);
        tabs.onSelect = [this] (int index)
        {
            if (auto r = session.showProject (index); r.failed())
                setStatus (r.getErrorMessage(), theme::warn);
        };
        tabs.onClose = [this] (int index)
        {
            if (session.showProject (index).wasOk())
                commands.invokeDirectly (cmd::closeProject, false);
        };
        tabs.onNew = [this] { commands.invokeDirectly (cmd::newProject, false); };
        browser.onInsert = [this] (const PluginDescription& d) { insertPlugin (d); };
        signalRow->onPluginDropped = [this] (const String& key)
        {
            if (const auto* e = session.getPluginLibrary().findEntry (key))
                insertPlugin (e->description);
        };
        session.onPluginsChanged = [this] { browser.refresh(); };

        // The toolbar: a button per tool, labelled with its current key, and the grid.
        for (int i = 0; i < 8; ++i)
        {
            auto* b = toolButtons.add (new TextButton());
            b->onClick = [this, i] { timeline.setTool (static_cast<Timeline::Tool> (i)); refreshToolbar(); };
            addAndMakeVisible (b);
        }

        snapButton.onClick = [this] { commands.invokeDirectly (cmd::toggleGrid, false); };
        addAndMakeVisible (snapButton);

        gridMenu.setTooltip ("The grid clips snap to");
        gridMenu.onChange = [this] { showGridMenu(); };
        addAndMakeVisible (gridMenu);

        timeline.onToolUsed = [this] { usedWhileHeld = true; };
        timeline.onStatus = [this] (const String& text) { setStatus (text); };
        timeline.onSelectionChanged = [this] { commands.commandStatusChanged(); };
        refreshToolbar();
        addChildComponent (updateBanner);   // shown by the app's updater
        settings.onClose = [this] { toggleSettings(); };

        session.onDeviceChanged = [this]
        {
            signalRow->refreshDevices();
            settings.refresh();
            updateFigures();
        };

        // Projects: the view is saved with the project; a new project replaces the edit.
        session.getViewState = [this] { return timeline.getViewState(); };
        session.onProjectClosing = [this] { closePluginWindow(); };
        session.onProjectChanged = [this]
        {
            showStartScreen (false);   // a project came forward: that is what the start screen was for
            tabs.refresh();
            timeline.projectChanged();
            refreshToolbar();
            signalRow->refreshPlugin();
            signalRow->setMessage ({}, false);
            updateTransportButtons();
            commands.commandStatusChanged();
        };

        session.onTakeEnded = [this] (const engine::TakeRecorder::Result& result)
        {
            takeFinished (result);
            updateTransportButtons();
            commands.commandStatusChanged();
        };

        // Shortcuts can change at any time (the shortcut editor): labels and tooltips follow.
        commands.getKeyMappings()->addChangeListener (this);
        refreshShortcutText();

        setWantsKeyboardFocus (true);
        frameLog = SystemStats::getEnvironmentVariable ("CARTHAGE_FRAME_LOG", {}).isNotEmpty();
        vblank = std::make_unique<VBlankAttachment> (this, [this] (double t) { frame (t); });
        updateTransportButtons();
        updateFigures();   // the round trip and load show from the first paint, not the first tick
        setStatus ("Ready");
    }

    MainComponent::~MainComponent()
    {
        commands.getKeyMappings()->removeChangeListener (this);
        vblank.reset();
        session.onDeviceChanged = nullptr;
        session.onTakeEnded = nullptr;
        session.onPluginsChanged = nullptr;
        session.getViewState = nullptr;
        session.onProjectClosing = nullptr;
        session.onProjectChanged = nullptr;
        closePluginWindow();
    }

    void MainComponent::refreshShortcutText()
    {
        recordButton.setTooltip (cmd::tooltip (commands, cmd::record));
        playButton.setTooltip (cmd::tooltip (commands, cmd::playStop));
        toStartButton.setTooltip (cmd::tooltip (commands, cmd::returnToStart));
        settingsButton.setTooltip (cmd::tooltip (commands, cmd::audioSettings));
        snapButton.setTooltip (cmd::tooltip (commands, cmd::toggleGrid));
        signalRow->refreshTooltips();

        for (int i = 0; i < toolButtons.size(); ++i)
        {
            const auto id = cmd::tools()[i];
            const auto keys = commands.getKeyMappings()->getKeyPressesAssignedToCommand (id);
            auto* b = toolButtons[i];

            // The label is the tool's key, so a rebound tool shows its new key; one with no key
            // shows the first letter of its name.
            b->setButtonText (keys.isEmpty() ? commands.getNameOfCommand (id).substring (0, 1)
                                             : keys.getFirst().getTextDescriptionWithIcons().toUpperCase());
            b->setTooltip (cmd::tooltip (commands, id));
        }
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
            case cmd::pluginBrowser:     info.setTicked (browserOpen && ! panelShowsSamples); break;
            case cmd::sampleBrowser:     info.setTicked (browserOpen && panelShowsSamples); break;
            case cmd::toggleGrid:        info.setTicked (timeline.getGrid().enabled); break;
            case cmd::undo:              info.setActive (session.getEdit().getUndoManager().canUndo());
                                         info.shortName = "Undo " + session.getEdit().getUndoManager().getUndoDescription(); break;
            case cmd::redo:              info.setActive (session.getEdit().getUndoManager().canRedo()); break;
            case cmd::pasteClips:        info.setActive (timeline.canPaste() && ! session.isRecording()); break;
            case cmd::copyClips:
            case cmd::cutClips:
            case cmd::duplicateClips:
            case cmd::deleteClips:
            case cmd::nudgeLeft:
            case cmd::nudgeRight:
            case cmd::loopSelection:     info.setActive (! timeline.getSelectedClips().isEmpty()); break;
            case cmd::toolSelect: case cmd::toolDraw: case cmd::toolPaint: case cmd::toolSlice:
            case cmd::toolDelete: case cmd::toolMute: case cmd::toolZoom: case cmd::toolStretch:
                info.setTicked (cmd::tools()[static_cast<int> (timeline.getTool())] == id); break;
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
            case cmd::pluginBrowser:    showPanel (false, true); return true;
            case cmd::sampleBrowser:    showPanel (true, true); return true;
            case cmd::toolSelect: case cmd::toolDraw: case cmd::toolPaint: case cmd::toolSlice:
            case cmd::toolDelete: case cmd::toolMute: case cmd::toolZoom: case cmd::toolStretch:
                toolCommand (info); return true;
            case cmd::undo:             session.getEdit().getUndoManager().undo(); commands.commandStatusChanged(); return true;
            case cmd::redo:             session.getEdit().getUndoManager().redo(); commands.commandStatusChanged(); return true;
            case cmd::toggleGrid:       timeline.getGrid().enabled = ! timeline.getGrid().enabled; timeline.gridChanged(); refreshToolbar(); return true;
            case cmd::splitAtPlayhead:  timeline.splitAtPlayhead(); return true;
            case cmd::duplicateClips:   timeline.duplicateSelection(); return true;
            case cmd::copyClips:        timeline.copySelection(); commands.commandStatusChanged(); return true;
            case cmd::cutClips:         timeline.copySelection(); timeline.deleteSelection(true); commands.commandStatusChanged(); return true;
            case cmd::pasteClips:       timeline.paste(); return true;
            case cmd::deleteClips:      timeline.deleteSelection(); return true;
            case cmd::nudgeLeft:        timeline.nudgeSelection (-1); return true;
            case cmd::nudgeRight:       timeline.nudgeSelection (1); return true;
            case cmd::addMarker:        timeline.addMarkerAtPlayhead(); return true;
            case cmd::loopSelection:    timeline.loopSelection(); return true;
            case cmd::openPluginEditor: openPluginWindow(); return true;
            case cmd::bypassPlugin:
                if (auto* p = session.getInsertedPlugin())
                {
                    p->setEnabled (! p->isEnabled());
                    signalRow->refreshPlugin();
                }
                return true;
            case cmd::audioSettings:    toggleSettings(); return true;
            case cmd::keyboardShortcuts:
                if (! settingsOpen) toggleSettings();
                settings.showShortcutsSection();
                return true;
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
    void MainComponent::toolCommand (const InvocationInfo& info)
    {
        const auto tool = static_cast<Timeline::Tool> (cmd::tools().indexOf (info.commandID));

        // From a menu or a button: just switch.
        if (info.invocationMethod != ApplicationCommandTarget::InvocationInfo::fromKeyPress)
        {
            timeline.setTool (tool);
            heldToolCommand = 0;
            refreshToolbar();
            return;
        }

        if (info.isKeyDown)
        {
            if (heldToolCommand == info.commandID)
                return;   // key repeat

            if (heldToolCommand == 0)
                toolBeforeHold = timeline.getTool();

            heldToolCommand = info.commandID;
            usedWhileHeld = false;
            timeline.setTool (tool);
        }
        else if (heldToolCommand == info.commandID)
        {
            // Released: a tap keeps the tool; a hold (or using it while held) goes back (F7).
            if (info.millisecsSinceKeyPressed > 300 || usedWhileHeld)
                timeline.setTool (toolBeforeHold);

            heldToolCommand = 0;
        }

        refreshToolbar();
    }

    void MainComponent::refreshToolbar()
    {
        for (int i = 0; i < toolButtons.size(); ++i)
        {
            const bool on = static_cast<int> (timeline.getTool()) == i;
            // The active tool is live: the accent (AGENTS.md, UI).
            toolButtons[i]->setColour (TextButton::buttonColourId, on ? theme::accent : theme::bg);
            toolButtons[i]->setColour (TextButton::textColourOffId, on ? theme::onAccent : theme::muted);
        }

        snapButton.setColour (TextButton::buttonColourId, timeline.getGrid().enabled ? theme::hover : theme::bg);
        snapButton.setColour (TextButton::textColourOffId, timeline.getGrid().enabled ? theme::text : theme::faint);

        // The grid menu: divisions, then the feel, then adaptive; the current choice first.
        const auto& g = timeline.getGrid();
        gridMenu.clear (dontSendNotification);
        static constexpr const char* names[] = { "Bar", "Beat", "1/2 beat", "1/4 beat", "1/8 beat", "1/16 beat", "1/32 beat", "1/64 beat" };

        for (int i = 0; i < 8; ++i)
            gridMenu.addItem (names[i], i + 1);

        gridMenu.addSeparator();
        gridMenu.addItem ("Straight", 101);
        gridMenu.addItem ("Triplet", 102);
        gridMenu.addItem ("Dotted", 103);
        gridMenu.addSeparator();
        gridMenu.addItem (String ("Adaptive (follows zoom)") + (g.adaptive ? " - on" : ""), 201);
        gridMenu.setText (g.describe (timeline.beatsPerBar(), timeline.getPixelsPerBeat()), dontSendNotification);
        commands.commandStatusChanged();
    }

    void MainComponent::showGridMenu()
    {
        auto& g = timeline.getGrid();
        const auto id = gridMenu.getSelectedId();

        if (id >= 1 && id <= 8)        g.division = static_cast<time::Division> (id - 1);
        else if (id == 101)            g.feel = time::Feel::straight;
        else if (id == 102)            g.feel = time::Feel::triplet;
        else if (id == 103)            g.feel = time::Feel::dotted;
        else if (id == 201)            g.adaptive = ! g.adaptive;

        g.enabled = true;
        timeline.gridChanged();
        refreshToolbar();
    }

    void MainComponent::choosePlugin()
    {
        // The browser is where plugins are chosen; this opens it with the search ready.
        showPanel (false, false);
        browser.focusSearch();
    }

    void MainComponent::toggleBrowser()
    {
        showPanel (panelShowsSamples, true);
    }

    void MainComponent::showPanel (bool samplesView, bool toggle)
    {
        browserOpen = toggle && browserOpen && panelShowsSamples == samplesView ? false : true;
        panelShowsSamples = samplesView;
        session.getEngine().getPropertyStorage().getPropertiesFile().setValue ("panelShowsSamples", samplesView);

        browser.setVisible (browserOpen && ! samplesView);
        samples.setVisible (browserOpen && samplesView);
        samplesTab.setVisible (browserOpen);
        pluginsTab.setVisible (browserOpen);

        // The switch: the chosen side stands out as the plugin filters' chosen kind does.
        for (auto [tab, isSamples] : { std::pair { &samplesTab, true }, { &pluginsTab, false } })
        {
            tab->setColour (TextButton::buttonColourId, isSamples == samplesView ? theme::hover : theme::bg2);
            tab->setColour (TextButton::textColourOffId, isSamples == samplesView ? theme::text : theme::muted);
        }

        resized();
        commands.commandStatusChanged();
    }

    void MainComponent::showStartScreen (bool show)
    {
        if (show)
        {
            startScreen.setShowAtStart (session.getEngine().getPropertyStorage().getPropertiesFile().getBoolValue ("showStartScreen", true));
            startScreen.refresh();
            startScreen.toFront (false);
        }

        startScreen.setVisible (show);
        resized();
    }

    void MainComponent::setPanelWidth (int w)
    {
        panelWidth = jlimit (240, 520, w);
        session.getEngine().getPropertyStorage().getPropertiesFile().setValue ("panelWidth", panelWidth);
        resized();
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
        if (frameLog)
            logFrame (t);

        const auto dt = lastFrameTime > 0.0 ? std::clamp (t - lastFrameTime, 0.0, 0.1) : 1.0 / 60.0;
        lastFrameTime = t;
        ++frameCount;

        auto& callback = session.getCallback();
        signalRow->frame (callback.takeInputPeaks(), dt);

        if (samples.isVisible())
            samples.frame();   // the playing sound's progress

        const auto position = session.getPositionSeconds();
        timeline.setPlayhead (position, recordingFrom);

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

    void MainComponent::logFrame (double t)
    {
        // CARTHAGE_FRAME_LOG=1: once a second on stderr, the display's frame intervals as this
        // window saw them, and whether a plugin scan was running (docs/TESTING.md, "Frame time").
        if (lastFrameTime > 0.0)
        {
            const auto ms = (t - lastFrameTime) * 1000.0;
            frameStats.worst = std::max (frameStats.worst, ms);
            frameStats.sum += ms;
            ++frameStats.count;
            frameStats.longest.push_back (ms);
        }

        if (t - frameStats.since >= 1.0 && frameStats.count > 0)
        {
            std::sort (frameStats.longest.begin(), frameStats.longest.end());
            const auto p99 = frameStats.longest[std::min (frameStats.longest.size() - 1, frameStats.longest.size() * 99 / 100)];
            const auto nominal = frameStats.longest[frameStats.longest.size() / 2];
            int late = 0;

            for (auto v : frameStats.longest)
                late += v > nominal * 1.5 ? 1 : 0;

            const auto scan = session.getPluginScanner().getProgress();
            std::fprintf (stderr, "FRAMES %d frames, median %.2f ms, p99 %.2f ms, worst %.2f ms, late %d; scan %s %d/%d\n",
                          frameStats.count, nominal, p99, frameStats.worst, late,
                          scan.running ? "running" : "idle", scan.done, scan.total);
            frameStats = {};
            frameStats.since = t;
        }
    }

    //==============================================================================================
    void MainComponent::paint (Graphics& g)
    {
        g.fillAll (theme::bg);

        if (! panelArea.isEmpty())
        {
            // The panel's switch sits on the panel's surface, with its edge line.
            const auto top = panelArea.withHeight (40);
            g.setColour (theme::bg2);
            g.fillRect (top);
            g.setColour (theme::line);
            g.fillRect (top.getRight() - 1, top.getY(), 1, top.getHeight());
        }
    }

    void MainComponent::resized()
    {
        auto r = getLocalBounds();
        tabs.setBounds (r.removeFromTop (ProjectTabs::height));

        auto top = r.removeFromTop (56).reduced (theme::space4, theme::space2 + 2);
        recordButton.setBounds (top.removeFromLeft (36));
        top.removeFromLeft (theme::space1);
        playButton.setBounds (top.removeFromLeft (36));
        top.removeFromLeft (theme::space1);
        toStartButton.setBounds (top.removeFromLeft (36));
        top.removeFromLeft (theme::space4);
        timeLabel.setBounds (top.removeFromLeft (140));
        top.removeFromLeft (theme::space4);

        for (auto* b : toolButtons)
        {
            b->setBounds (top.removeFromLeft (28).withSizeKeepingCentre (28, 28));
            top.removeFromLeft (2);
        }

        top.removeFromLeft (theme::space3);
        gridMenu.setBounds (top.removeFromLeft (170).withSizeKeepingCentre (170, 28));
        top.removeFromLeft (theme::space1);
        snapButton.setBounds (top.removeFromLeft (64).withSizeKeepingCentre (64, 28));

        settingsButton.setBounds (top.removeFromRight (36));
        top.removeFromRight (theme::space4);
        latencyWords.setBounds (top.removeFromRight (64));
        top.removeFromRight (theme::space1);   // the figure and its words read as "6.3 ms round trip"
        latencyLabel.setBounds (top.removeFromRight (72));
        top.removeFromRight (theme::space6);
        loadMeter.setBounds (top.removeFromRight (140).withSizeKeepingCentre (140, 20));

        // The panel docks on the left, under the transport; the rest of the window shifts over.
        // Its switch sits on the panel's own surface at the top.
        if (browserOpen)
        {
            auto panel = r.removeFromLeft (panelWidth);
            panelArea = panel;
            auto switchRow = panel.removeFromTop (40).reduced (theme::space3, 0).withTrimmedTop (theme::space2).withTrimmedRight (1);
            samplesTab.setBounds (switchRow.removeFromLeft (switchRow.getWidth() / 2 - 2).withHeight (28));
            switchRow.removeFromLeft (4);
            pluginsTab.setBounds (switchRow.withHeight (28));
            browser.setBounds (panel);
            samples.setBounds (panel);
        }
        else
        {
            panelArea = {};
        }

        signalRow->setBounds (r.removeFromTop (132));

        statusLabel.setBounds (r.removeFromBottom (28).reduced (theme::space4, 0));
        timeline.setBounds (r);

        layoutSettings (false);
        layoutUpdateBanner();
        startScreen.setBounds (getLocalBounds().withTrimmedTop (ProjectTabs::height));
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
