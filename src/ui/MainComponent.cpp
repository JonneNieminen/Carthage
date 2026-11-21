#include "MainComponent.h"
#include "ExportWindow.h"

#include <engine/Trace.h>

namespace carthage::ui
{
    using namespace juce;
    namespace cmd = app::commands;

    //==============================================================================================
    /** A plugin's own editor in a plain host window (docs/ARCHITECTURE.md, "Plugins"). */
    namespace
    {
        /** A window brought back where its display no longer is (unplugged) comes to the main one. */
        Rectangle<int> onScreen (Rectangle<int> r)
        {
            const auto& displays = Desktop::getInstance().getDisplays();

            for (const auto& d : displays.displays)
            {
                const auto shared = d.userBounds.toNearestInt().getIntersection (r);

                if (shared.getWidth() >= 120 && shared.getHeight() >= 60)
                    return r;
            }

            const auto area = displays.getPrimaryDisplay()->userBounds.toNearestInt();
            return r.withSize (std::min (r.getWidth(), area.getWidth()), std::min (r.getHeight(), area.getHeight()))
                    .withCentre (area.getCentre());
        }
    }

    //==============================================================================================
    /** A view in a window of its own (F21): a slim strip with the way back, the view below.
        Closing the window brings the view back too. Shortcuts work here as in the main window. */
    class MainComponent::ViewWindow final : public DocumentWindow
    {
    public:
        ViewWindow (MainComponent& m, View v, Component& content)
            : DocumentWindow (nameOf (v) + String::fromUTF8 (" \xe2\x80\x94 Carthage"), theme::bg, DocumentWindow::allButtons),
              owner (m), view (v), holder (*this, content, v != View::settings)
        {
            setUsingNativeTitleBar (true);
            setContentNonOwned (&holder, false);
            setResizable (true, false);
            addKeyListener (owner.commands.getKeyMappings());
        }

        ~ViewWindow() override
        {
            removeKeyListener (owner.commands.getKeyMappings());
            clearContentComponent();
        }

        void closeButtonPressed() override   { back(); }

        /** Back into the main window, after this call returns (it deletes this window). */
        void back()
        {
            MessageManager::callAsync ([safe = Component::SafePointer<MainComponent> (&owner), v = view]
            {
                if (safe != nullptr)
                    safe->setDetached (v, false);
            });
        }

        Component& getHolder() noexcept   { return holder; }

    private:
        struct Holder final : Component
        {
            Holder (ViewWindow& w, Component& c, bool withStrip) : content (c), strip (withStrip)
            {
                popIn.setTooltip ("Back into the main window");
                popIn.onClick = [&w] { w.back(); };
                addAndMakeVisible (popIn);
                popIn.setVisible (strip);
                addAndMakeVisible (content);
            }

            void paint (Graphics& g) override   { g.fillAll (theme::bg2); }

            void resized() override
            {
                auto r = getLocalBounds();

                if (strip)
                {
                    auto top = r.removeFromTop (28);
                    popIn.setBounds (top.removeFromRight (28).reduced (2));
                }

                content.setBounds (r);
            }

            Component& content;
            bool strip;
            IconButton popIn { IconButton::Glyph::popIn };
        };

        MainComponent& owner;
        View view;
        Holder holder;
    };


    //==============================================================================================
    /** The front project's notes, typed straight into the project. */
    class MainComponent::NotesWindow final : public DocumentWindow
    {
    public:
        explicit NotesWindow (MainComponent& m)
            : DocumentWindow ("Notes", theme::bg2, DocumentWindow::closeButton | DocumentWindow::minimiseButton), owner (m)
        {
            setUsingNativeTitleBar (true);
            editor.setMultiLine (true, true);
            editor.setReturnKeyStartsNewLine (true);
            editor.setFont (theme::uiFont (theme::textMd));
            editor.setTextToShowWhenEmpty ("Lyrics, the settings you used, what to try next. Saved with the project.", theme::faint);
            editor.setIndents (theme::space3, theme::space3);
            editor.onTextChange = [this] { editing::setNotes (owner.session.getEdit(), editor.getText()); };
            editor.setSize (420, 360);
            setContentNonOwned (&editor, true);
            setResizable (true, false);
            addKeyListener (owner.commands.getKeyMappings());
        }

        ~NotesWindow() override
        {
            removeKeyListener (owner.commands.getKeyMappings());
            clearContentComponent();
        }

        void show()
        {
            refresh();
            setVisible (true);
            toFront (true);
            editor.grabKeyboardFocus();
        }

        void refresh()
        {
            setName ("Notes: " + owner.session.getProjectName());
            editor.setText (editing::notes (owner.session.getEdit()), false);
        }

        void closeButtonPressed() override   { setVisible (false); }

    private:
        MainComponent& owner;
        TextEditor editor;
    };

    //==============================================================================================
    /** File > Restore a version (W41): each version with its picture, newest first. */
    class MainComponent::VersionsWindow final : public DocumentWindow
    {
    public:
        VersionsWindow (MainComponent& m, const Array<File>& versions)
            : DocumentWindow ("Restore a version: " + m.session.getProjectName(), theme::bg2, DocumentWindow::closeButton), owner (m)
        {
            setUsingNativeTitleBar (true);
            list.setSize (rowWidth, versions.size() * rowHeight);

            for (const auto& v : versions)
                list.addAndMakeVisible (rows.add (new Row (*this, v)));

            for (int i = 0; i < rows.size(); ++i)
                rows[i]->setBounds (0, i * rowHeight, rowWidth, rowHeight);

            viewport.setViewedComponent (&list, false);
            viewport.setScrollBarsShown (true, false);
            viewport.setSize (rowWidth + viewport.getScrollBarThickness(), std::min (3, versions.size()) * rowHeight);
            setContentNonOwned (&viewport, true);
            setResizable (true, false);
            addKeyListener (owner.commands.getKeyMappings());
        }

        ~VersionsWindow() override
        {
            removeKeyListener (owner.commands.getKeyMappings());
            clearContentComponent();
        }

        void closeButtonPressed() override   { setVisible (false); }

    private:
        static constexpr int rowHeight = 196, rowWidth = 640, pictureWidth = 300;

        struct Row final : Component
        {
            Row (VersionsWindow& w, File f) : window (w), file (std::move (f))
            {
                picture = ImageFileFormat::loadFrom (project::pictureOfVersion (file));
                const auto when = file.getLastModificationTime();
                styleLabel (date, theme::textMd, theme::text, true);
                date.setText (when.formatted ("%A %e %B, %H:%M"), dontSendNotification);
                styleLabel (note, theme::textSm, theme::muted);
                note.setText (file.getFileNameWithoutExtension().endsWith ("before restoring") ? "Kept just before a restore"
                                                                                               : "Kept when the next save replaced it",
                              dontSendNotification);
                restore.onClick = [this] { window.owner.askToRestore (file); };
                for (auto* c : std::initializer_list<Component*> { &date, &note, &restore })
                    addAndMakeVisible (c);
            }

            void paint (Graphics& g) override
            {
                const auto box = getLocalBounds().reduced (theme::space3).removeFromLeft (pictureWidth).toFloat();
                g.setColour (theme::well);
                g.fillRoundedRectangle (box, theme::radiusSm);

                if (picture.isValid())
                    g.drawImage (picture, box.reduced (2.0f), RectanglePlacement::centred);
                else
                {
                    g.setColour (theme::faint);
                    g.setFont (theme::uiFont (theme::textSm));
                    g.drawText ("No picture: saved before pictures were kept", box.reduced (theme::space2), Justification::centred, true);
                }

                g.setColour (theme::line);
                g.fillRect (0, getHeight() - 1, getWidth(), 1);
            }

            void resized() override
            {
                auto r = getLocalBounds().reduced (theme::space3);
                r.removeFromLeft (pictureWidth + theme::space4);
                date.setBounds (r.removeFromTop (24));
                note.setBounds (r.removeFromTop (20));
                r.removeFromTop (theme::space3);
                restore.setBounds (r.removeFromTop (30).removeFromLeft (120));
            }

            VersionsWindow& window;
            File file;
            Image picture;
            Label date, note;
            TextButton restore { String::fromUTF8 ("Restore\xe2\x80\xa6") };
        };

        MainComponent& owner;
        Viewport viewport;
        Component list;
        OwnedArray<Row> rows;
    };

    //==============================================================================================
    MainComponent::MainComponent (engine::Session& s, ApplicationCommandManager& c)
        : sampleEditor (s),
          sampleEditorHeight (jlimit (SampleEditor::minHeight, SampleEditor::maxHeight,
                                      s.getEngine().getPropertyStorage().getPropertiesFile().getIntValue ("sampleEditorHeight", SampleEditor::defaultHeight))),
          session (s), commands (c), timeline (s), mixer (s, pluginWindows), settings (s, c, cmd::inMenuOrder()), browser (s), samples (s)
    {


        recordButton.onClick = [this] { commands.invokeDirectly (cmd::record, false); };
        playButton.onClick = [this] { commands.invokeDirectly (cmd::playStop, false); };
        toStartButton.onClick = [this] { commands.invokeDirectly (cmd::returnToStart, false); };
        metronomeButton.onClick = [this] { commands.invokeDirectly (cmd::toggleMetronome, false); };
        loopButton.onClick = [this] { commands.invokeDirectly (cmd::toggleLoop, false); };
        punchButton.onClick = [this] { commands.invokeDirectly (cmd::togglePunch, false); };
        mixerButton.onClick = [this] { commands.invokeDirectly (cmd::toggleMixer, false); };
        mixer.onStatus = [this] (const String& text) { setStatus (text); };
        addChildComponent (mixer);
        popMixer.setTooltip ("The mixer in a window of its own");
        popMixer.onClick = [this] { setDetached (View::mixer, true); };
        addChildComponent (popMixer);
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

        for (auto* comp : std::initializer_list<Component*> { &recordButton, &playButton, &toStartButton, &metronomeButton, &loopButton, &punchButton, &timeLabel,
                                                              &loadMeter, &latencyLabel, &latencyWords, &settingsButton, &mixerButton,
                                                              &timeline, &statusLabel })
            addAndMakeVisible (comp);

        addChildComponent (settings);
        addAndMakeVisible (browser);
        addChildComponent (samples);

        // The left panel: samples or plugins, chosen at its top (F19). The choice and the
        // width are remembered.
        auto& props = session.getEngine().getPropertyStorage().getPropertiesFile();
        panelWidth = jlimit (240, 520, props.getIntValue ("panelWidth", PluginBrowser::preferredWidth));
        panelEdge.getCurrentWidth = [this] { return panelWidth; };
        panelEdge.onResize = [this] (int w) { setPanelWidth (w); settings.showPanelWidth (panelWidth); };
        panelEdge.onFit = [this]
        {
            const auto showingSamples = samples.isVisible() && ! isDetached (View::samples);
            setPanelWidth (showingSamples ? samples.idealWidth() : browser.idealWidth());
            settings.showPanelWidth (panelWidth);
        };
        addChildComponent (panelEdge);

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
        settings.onUiScaleChanged = [this] (double s) { setUiScale (s); };
        Desktop::getInstance().setGlobalScaleFactor (static_cast<float> (uiScale()));
        settings.showUiScale (uiScale());
        settings.onBrowserTextSizeChanged = [this] (float px) { samples.setTextSize (px); };
        settings.onPlayWhileHeldChanged = [this] (bool held) { samples.setPlayWhileHeld (held); };
        settings.onShowLoadChanged = [this] (bool) { resized(); };
        settings.onBeatShadingChanged = [this] { timeline.gridChanged(); };
        settings.onBusChannelsChanged = [this] { mixer.busesChanged(); };
        settings.onRoutingStyleChanged = [this] (ui::routing::Style style) { mixer.setRoutingStyle (style); };
        settings.onRunSettingsTest = [this] { runSettingsTest (false); };

        addChildComponent (sampleEditor);
        addChildComponent (sampleEditorEdge);
        sampleEditorEdge.setAlongTop (true);
        sampleEditorEdge.setTooltip ("Drag to make the sample editor taller or shorter");
        sampleEditorEdge.getCurrentWidth = [this] { return sampleEditorHeight; };
        sampleEditorEdge.onResize = [this] (int h)
        {
            sampleEditorHeight = jlimit (SampleEditor::minHeight, SampleEditor::maxHeight, h);
            this->props().setValue ("sampleEditorHeight", sampleEditorHeight);
            resized();
        };
        sampleEditorEdge.onFit = [this] { sampleEditorEdge.onResize (SampleEditor::defaultHeight); };
        sampleEditor.onClose = [this] { closeSampleEditor(); };
        sampleEditor.onStatus = [this] (const String& text) { setStatus (text); };
        timeline.onClipDoubleClicked = [this] (tracktion::Clip& c)
        {
            // The same clip again closes it (W42).
            if (sampleEditor.isVisible() && sampleEditor.getClipID() == c.itemID)
                closeSampleEditor();
            else
                openSampleEditor (c);
        };
        showPanel (panelShowsSamples, false);

        // Views in their own windows (F21): a pop-out button on each, the way back on its window.
        popTimeline.setTooltip ("The timeline in a window of its own");
        popTimeline.onClick = [this] { setDetached (View::timeline, true); };
        popPanel.onClick = [this]
        {
            const auto samplesHere = ! isDetached (View::samples), pluginsHere = ! isDetached (View::plugins);
            setDetached (samplesHere && (panelShowsSamples || ! pluginsHere) ? View::samples : View::plugins, true);
        };
        settings.onPopOut = [this] { toggleDetached (View::settings); };
        styleLabel (timelineAway, theme::textMd, theme::muted);
        timelineAway.setJustificationType (Justification::centred);
        timelineAway.setText ("The timeline is in its own window.", dontSendNotification);
        timelineBack.onClick = [this] { setDetached (View::timeline, false); };

        for (auto* pop : std::initializer_list<Component*> { &popTimeline, &popPanel })
            addAndMakeVisible (pop);

        addChildComponent (timelineAway);
        addChildComponent (timelineBack);
        addChildComponent (startScreen);
        addChildComponent (palette);
        palette.setOwnCommand (cmd::commandPalette);
        addChildComponent (viewsPalette);
        viewsPalette.setOwnCommand (cmd::viewsPalette);
        viewsPalette.setOnly (cmd::views(), String::fromUTF8 ("Go to a view\xe2\x80\xa6"));

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
        tabs.onNotes = [this] (int index)
        {
            if (session.showProject (index).wasOk())
            {
                if (notesWindow == nullptr || ! notesWindow->isVisible())
                    toggleNotes();
                else
                    refreshNotes();
            closeSampleEditor();   // its clip belonged to the other project
            }
        };
        tabs.onRename = [this] (int index)
        {
            if (session.showProject (index).failed())
                return;

            // Never saved: it has no folder to rename yet, so naming it is saving it.
            if (session.isUntitled())
            {
                commands.invokeDirectly (cmd::saveProjectAs, false);
                return;
            }

            askToRenameProject();
        };
        browser.onInsert = [this] (const PluginDescription& d) { mixer.addPluginToSelected (d); };   // onto the selected channel's Line (5b)
        pluginWindows.keepOnTop = [this] { return keepPluginOnTop(); };
        session.onPluginsChanged = [this] { browser.refresh(); };

        // The toolbar: a button per tool, labelled with its current key, and the grid.
        for (int i = 0; i < 8; ++i)
        {
            auto* b = toolButtons.add (new ToolButton (static_cast<ToolButton::Glyph> (i)));
            b->onClick = [this, i] { timeline.setTool (static_cast<Timeline::Tool> (i)); refreshToolbar(); };
            addAndMakeVisible (b);
        }

        tempo.setSliderStyle (Slider::LinearBar);
        tempo.setTextBoxStyle (Slider::TextBoxLeft, false, 76, 24);
        tempo.setRange (time::TapTempo::minBpm, time::TapTempo::maxBpm, 0.01);
        tempo.setNumDecimalPlacesToDisplay (2);
        tempo.setTextValueSuffix (" bpm");
        tempo.setSkewFactorFromMidPoint (120.0);
        tempo.setTooltip ("The project's tempo. Drag, scroll (Shift for fine steps), or click and type; double-click returns to 120.");
        tempo.onValueChange = [this] { setTempo (tempo.getValue()); };
        tempo.onDragEnd = [this] { tempoSettled(); };
        tapButton.setTooltip ("Tap in time, at least twice, to set the tempo");
        tapButton.onClick = [this]
        {
            if (const auto bpm = tapTempo.tap (Time::getMillisecondCounterHiRes() / 1000.0))
            {
                tempo.setValue (std::round (*bpm * 10.0) / 10.0);   // to a tenth: taps are not finer
            }
        };
        for (auto* comp : std::initializer_list<Component*> { &tempo, &tapButton })
            addAndMakeVisible (comp);
        refreshTempo();

        styleLabel (masterLabel, theme::textSm, theme::muted);
        masterLabel.setText ("App loudness", dontSendNotification);
        masterVolume.setSliderStyle (Slider::LinearHorizontal);
        masterVolume.setTextBoxStyle (Slider::TextBoxRight, false, 50, 22);   // click the figure to type one
        masterVolume.setRange (-60.0, 6.0, 0.1);
        masterVolume.setTextValueSuffix (" dB");
        masterVolume.setNumDecimalPlacesToDisplay (1);
        // App loudness (owner): the app's own volume, apart from master's fader and every project.
        masterVolume.setDefault (0.0);
        masterVolume.setTooltip ("App loudness: how loud Carthage plays through your interface, whatever the project. Master's "
                                 "fader and exports are not changed by it. Drag, scroll, or click the figure and type one; "
                                 "double-click returns to 0 dB.");
        masterVolume.onValueChange = [this] { session.setAppLoudness (static_cast<float> (masterVolume.getValue())); };
        for (auto* comp : std::initializer_list<Component*> { &masterLabel, &masterVolume })
            addAndMakeVisible (comp);
        refreshMasterVolume();

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
            mixer.devicesChanged();
            settings.refresh();
            updateFigures();
        };

        // Projects: the view is saved with the project; a new project replaces the edit.
        session.getViewState = [this] { return timeline.getViewState(); };
        session.takePicture = [this] { return pictureOfWindows(); };
        session.onProjectClosing = [this]
        {
            closePluginWindow();
            mixer.projectClosing();

            if (exportWindow != nullptr && exportWindow->isExporting())
                exportWindow.reset();   // an export holds its project: it stops when that project goes back
        };
        session.onProjectChanged = [this]
        {
            showStartScreen (false);   // a project came forward: that is what the start screen was for
            tabs.refresh();
            timeline.projectChanged();
            mixer.projectChanged();
            refreshToolbar();
            refreshMasterVolume();
            tempoBefore.reset();   // a change in progress belonged to the other project
            refreshNotes();

            if (exportWindow != nullptr && exportWindow->isVisible())
                exportWindow->show();   // what the front project would export

            ++tempoChanges;
            refreshTempo();
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
        exportWindow.reset();  // an export in progress is cancelled, before the edit goes
        viewWindows.clear();   // the views they show are members here: their windows go first

        commands.getKeyMappings()->removeChangeListener (this);
        vblank.reset();
        session.onDeviceChanged = nullptr;
        session.onTakeEnded = nullptr;
        session.onPluginsChanged = nullptr;
        session.getViewState = nullptr;
        session.takePicture = nullptr;
        session.onProjectClosing = nullptr;
        session.onProjectChanged = nullptr;
        closePluginWindow();
    }

    void MainComponent::refreshShortcutText()
    {
        recordButton.setTooltip (cmd::tooltip (commands, cmd::record));
        playButton.setTooltip (cmd::tooltip (commands, cmd::playStop));
        toStartButton.setTooltip (cmd::tooltip (commands, cmd::returnToStart));
        metronomeButton.setTooltip (cmd::tooltip (commands, cmd::toggleMetronome) + ". Its sound and level are in Settings.");
        loopButton.setTooltip (cmd::tooltip (commands, cmd::toggleLoop) + ". Right-drag on the ruler to set the region; double-click it to clear it.");
        punchButton.setTooltip (cmd::tooltip (commands, cmd::togglePunch) + ". Needs a loop region.");
        settingsButton.setTooltip (cmd::tooltip (commands, cmd::audioSettings));
        mixerButton.setTooltip (cmd::tooltip (commands, cmd::toggleMixer));
        snapButton.setTooltip (cmd::tooltip (commands, cmd::toggleGrid));

        for (int i = 0; i < toolButtons.size(); ++i)
        {
            const auto id = cmd::tools()[i];
            const auto keys = commands.getKeyMappings()->getKeyPressesAssignedToCommand (id);
            auto* b = toolButtons[i];

            // The label is the tool's key, so a rebound tool shows its new key; one with no key
            // shows the first letter of its name.
            b->setKeyText (keys.isEmpty() ? commands.getNameOfCommand (id).substring (0, 1)
                                          : keys.getFirst().getTextDescriptionWithIcons().toUpperCase());
            b->setTooltip (cmd::tooltip (commands, id));
        }
    }

    //==============================================================================================
    void MainComponent::getAllCommands (Array<CommandID>& ids)
    {
        ids.addArray (cmd::all());
        ids.addArray (cmd::layouts());
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
            case cmd::undoHistory:       info.setActive (session.getEdit().getUndoManager().canUndo()); break;
            case cmd::pasteClips:        info.setActive (timeline.canPaste() && ! session.isRecording()); break;
            case cmd::windowSamples:     info.setTicked (isDetached (View::samples)); break;
            case cmd::windowPlugins:     info.setTicked (isDetached (View::plugins)); break;
            case cmd::windowTimeline:    info.setTicked (isDetached (View::timeline)); break;
            case cmd::windowMixer:       info.setTicked (isDetached (View::mixer)); break;
            case cmd::toggleMixer:       info.setTicked (mixerShown || isDetached (View::mixer)); break;
            case cmd::windowSignal:      info.setActive (false); break;   // the signal row is gone (W51); the id stays for saved shortcuts
            case cmd::windowSettings:    info.setTicked (isDetached (View::settings)); break;
            case cmd::keepPluginOnTop:   info.setTicked (keepPluginOnTop()); break;
            case cmd::toggleMetronome:   info.setTicked (session.isMetronomeOn()); break;
            case cmd::togglePunch:       info.setTicked (session.isPunchOn()); break;
            case cmd::toggleLoop:        info.setTicked (editing::isLooping (session.getEdit()));
                                         info.setActive (editing::loopRegion (session.getEdit()).has_value()); break;
            case cmd::restoreVersion:    info.setActive (! session.isUntitled() && ! session.isRecording()); break;
            case cmd::copyClips:         info.setActive (! timeline.getSelectedClips().isEmpty() || showingTooltip().isNotEmpty()); break;
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
            default:
                if (id >= cmd::recallLayoutFirst && id < cmd::afterLayouts)
                {
                    const auto save = id >= cmd::saveLayoutFirst;
                    const auto n = id - (save ? cmd::saveLayoutFirst : cmd::recallLayoutFirst) + 1;
                    const auto saved = props().getValue ("layout" + String (n)).isNotEmpty();
                    info.setActive (n <= layoutSlots() && (save || saved));
                    info.shortName = save ? "Save as " + layoutName (n) : layoutName (n);

                    if (! save && ! saved)
                        info.shortName << " (empty)";
                }
                break;
        }
    }

    void MainComponent::toggleNotes()
    {
        if (notesWindow == nullptr)
        {
            notesWindow = std::make_unique<NotesWindow> (*this);
            notesWindow->centreAroundComponent (this, 420, 360);
        }

        if (notesWindow->isVisible() && notesWindow->isActiveWindow())
            notesWindow->setVisible (false);
        else
            notesWindow->show();
    }

    MemoryBlock MainComponent::pictureOfWindows()
    {
        // Side by side, each at the same height: the main window first, then each view's window.
        constexpr float height = 420.0f;
        constexpr int maxWidth = 2400;
        Array<Image> shots;
        shots.add (createComponentSnapshot (getLocalBounds(), true, 1.0f));

        for (auto& [view, window] : viewWindows)
            if (window != nullptr && window->isVisible())
                shots.add (window->getHolder().createComponentSnapshot (window->getHolder().getLocalBounds(), true, 1.0f));

        int width = 0;
        for (const auto& shot : shots)
            width += roundToInt (static_cast<float> (shot.getWidth()) * height / static_cast<float> (std::max (1, shot.getHeight()))) + 8;

        const auto scale = std::min (1.0f, static_cast<float> (maxWidth) / static_cast<float> (std::max (1, width)));
        Image picture (Image::RGB, std::max (1, roundToInt (static_cast<float> (width) * scale)), roundToInt (height * scale), true);
        Graphics g (picture);
        g.fillAll (theme::bg);
        float x = 0.0f;

        for (const auto& shot : shots)
        {
            const auto w = static_cast<float> (shot.getWidth()) * height / static_cast<float> (std::max (1, shot.getHeight())) * scale;
            g.drawImage (shot, Rectangle<float> (x, 0.0f, w, height * scale), RectanglePlacement::stretchToFit);
            x += w + 8.0f * scale;
        }

        JPEGImageFormat jpeg;
        jpeg.setQuality (0.7f);
        MemoryOutputStream out;
        jpeg.writeImageToStream (picture, out);
        return out.getMemoryBlock();
    }

    void MainComponent::showVersions()
    {
        const auto versions = project::versionsOf (session.getProjectFile());

        if (versions.isEmpty())
        {
            setStatus ("No versions yet: each save keeps the one before it");
            return;
        }

        versionsWindow = std::make_unique<VersionsWindow> (*this, versions);
        versionsWindow->centreAroundComponent (this, versionsWindow->getWidth(), versionsWindow->getHeight());
        versionsWindow->setVisible (true);
        versionsWindow->toFront (true);
    }

    void MainComponent::askToRestore (const File& version)
    {
        const auto when = version.getLastModificationTime();
        auto* w = new AlertWindow ("Restore the version from " + when.formatted ("%e %b %H:%M") + "?",
                                   "The project as it is now is kept as a version too, so you can come back to it.",
                                   MessageBoxIconType::NoIcon, this);
        w->addButton ("Restore", 1, KeyPress (KeyPress::returnKey));
        w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
        w->enterModalState (true, ModalCallbackFunction::create ([this, version] (int choice)
        {
            if (choice != 1)
                return;

            if (versionsWindow != nullptr)
                versionsWindow->setVisible (false);

            if (auto r = session.restoreVersion (version); r.failed())
                setStatus ("Not restored: " + r.getErrorMessage(), theme::warn);
            else
                setStatus ("Restored. Save to keep it as the project.");
        }), true);
    }

    void MainComponent::refreshNotes()
    {
        if (notesWindow != nullptr)
            notesWindow->refresh();
    }

    void MainComponent::runSettingsTest (bool atFirstLaunch)
    {
        if (settingsTest != nullptr && settingsTest->isRunning())
            return;

        settingsTest = std::make_unique<engine::SettingsTest> (session);
        setStatus ("Testing this computer's audio settings, silently");
        settings.setTestRunning (true);

        settingsTest->start ([this] (const String& line) { setStatus ("Settings test: " + line); },
                             [safe = SafePointer<MainComponent> (this), atFirstLaunch] (const engine::SettingsTest::Recommendation& r)
        {
            if (safe == nullptr)
                return;

            safe->settings.setTestRunning (false);
            safe->settings.refresh();

            if (! r.valid)
            {
                safe->setStatus ("Settings test: " + r.reason, theme::warn);
                return;
            }

            safe->setStatus ("Settings test done");
            safe->offerRecommendation (r, atFirstLaunch);
        });
    }

    void MainComponent::offerRecommendation (const engine::SettingsTest::Recommendation& r, bool atFirstLaunch)
    {
        auto& adm = session.getDeviceManager();
        const auto current = adm.getAudioDeviceSetup();
        const auto ms = [] (int samples, double rate) { return String (samples * 1000.0 / rate, 1) + " ms"; };

        StringArray lines;
        lines.add ("Buffer size: " + String (r.bufferSize) + " samples, " + ms (r.bufferSize, r.sampleRate)
                   + (r.bufferSize == current.bufferSize ? String (" (as now)") : " (now " + String (current.bufferSize) + ")"));
        lines.add ("Sample rate: " + String (r.sampleRate / 1000.0, 1) + " kHz" + (std::abs (r.sampleRate - current.sampleRate) < 1.0 ? String (" (as now)") : String()));
        lines.add ("Plugins checked at the same time: " + String (r.parallelChecks) + (r.parallelChecks == session.getParallelChecks() ? String (" (as now)") : String()));

        auto* w = new AlertWindow (atFirstLaunch ? "Carthage tested this computer" : "Recommended settings",
                                   String (atFirstLaunch ? "To start with settings that suit this computer, Carthage tried your interface silently. " : String())
                                       + "It recommends:\n\n" + lines.joinIntoString ("\n") + "\n\n" + r.reason
                                       + "\n\nYou can run the test again from Settings > Interface.",
                                   MessageBoxIconType::NoIcon, this);
        w->addButton ("Use these", 1, KeyPress (KeyPress::returnKey));
        w->addButton ("Keep mine", 0, KeyPress (KeyPress::escapeKey));

        w->enterModalState (true, ModalCallbackFunction::create ([this, r] (int choice)
        {
            if (choice != 1)
            {
                log::info ("Settings test: recommendation declined");
                return;
            }

            auto& devices = session.getDeviceManager();
            auto setup = devices.getAudioDeviceSetup();
            setup.bufferSize = r.bufferSize;
            setup.sampleRate = r.sampleRate;

            if (auto err = devices.setAudioDeviceSetup (setup, true); err.isNotEmpty())
                setStatus ("The interface did not accept that: " + err, theme::warn);
            else
                setStatus ("Using the recommended settings");

            session.setParallelChecks (r.parallelChecks);
            log::info ("Settings test: recommendation used (" + String (r.bufferSize) + " samples, " + String (r.sampleRate, 0) + " Hz, "
                       + String (r.parallelChecks) + " checks at once)");
            settings.refresh();
        }), true);
    }

    bool MainComponent::isPlaying() const
    {
        return session.isPlaying();
    }

    void MainComponent::openSampleEditor (tracktion::Clip& c)
    {
        const auto opening = ! sampleEditor.isVisible();

        motion::showAgain (sampleEditor);   // closing faded it to alpha 0; the rise below may be skipped
        sampleEditorEdge.setVisible (true);
        resized();
        sampleEditor.show (c);

        // Opening answers the double-click: the panel rises a little into place and fades in
        // (W45). Showing another sample in an open editor does not move.
        if (opening && ! isPlaying() && ! motion::reduced())
        {
            const auto place = sampleEditor.getBounds();
            sampleEditor.setBounds (place.translated (0, theme::space4));
            sampleEditor.setAlpha (0.0f);
            Desktop::getInstance().getAnimator().animateComponent (&sampleEditor, place, 1.0f, theme::fast, false, 0.0, 0.0);
        }
    }

    void MainComponent::closeSampleEditor()
    {
        if (! sampleEditor.isVisible())
            return;

        auto& animator = Desktop::getInstance().getAnimator();
        animator.cancelAnimation (&sampleEditor, true);

        if (! isPlaying() && ! motion::reduced())
            animator.fadeOut (&sampleEditor, theme::fast);   // a picture of it fades; the panel itself is hidden now

        sampleEditor.setVisible (false);
        sampleEditorEdge.setVisible (false);
        resized();
        timeline.grabKeyboardFocus();
    }

    void MainComponent::exportLog()
    {
        const auto text = "Carthage log, exported " + Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S") + "\n"
                          + log::systemSummary() + "\nAudio: " + session.describeAudio() + "\n\n" + log::text();
        saveOrCopy ("Export log", text, "Carthage log.txt");
    }

    void MainComponent::offerCrashReport (const String& note)
    {
        log::warning ("The last run stopped unexpectedly; a report was offered");
        const auto text = note + "\n\nThe log of that run:\n" + log::previousRun();
        saveOrCopy ("Carthage stopped unexpectedly last time",
                    log::withoutHome (text), "Carthage crash report.txt");
    }

    void MainComponent::saveOrCopy (const String& title, const String& rawText, const String& fileName)
    {
        // Nothing is sent anywhere: the person saves it or copies it, and the home folder is ~.
        const auto text = log::withoutHome (rawText);
        auto* w = new AlertWindow (title, "Save it as a file, or copy it to paste into a message. Your home folder shows as ~.",
                                   MessageBoxIconType::NoIcon, this);
        w->addButton (String::fromUTF8 ("Save\xe2\x80\xa6"), 1, KeyPress (KeyPress::returnKey));
        w->addButton ("Copy", 2);
        w->addButton ("Not now", 0, KeyPress (KeyPress::escapeKey));

        w->enterModalState (true, ModalCallbackFunction::create ([this, text, fileName] (int choice)
        {
            if (choice == 2)
            {
                copyToClipboard (text);
                setStatus ("Copied to the clipboard");
            }
            else if (choice == 1)
            {
                logChooser = std::make_unique<FileChooser> ("Save as", File::getSpecialLocation (File::userDesktopDirectory).getChildFile (fileName), "*.txt");
                logChooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting,
                                         [safe = SafePointer<MainComponent> (this), text] (const FileChooser& fc)
                {
                    if (safe == nullptr || fc.getResult() == File())
                        return;

                    if (fc.getResult().replaceWithText (text))
                        safe->setStatus ("Saved " + fc.getResult().getFileName());
                    else
                        safe->setStatus ("Could not save " + fc.getResult().getFileName(), theme::warn);
                });
            }
        }), true);
    }

    void MainComponent::showUndoHistory()
    {
        // Newest first; choosing a step undoes it and everything after it, in one go (W38).
        auto& um = session.getEdit().getUndoManager();
        const auto steps = um.getUndoDescriptions();

        PopupMenu m;
        m.addSectionHeader ("Undo back to before");

        for (int i = 0; i < steps.size(); ++i)   // JUCE lists them newest first
        {
            const auto count = i + 1;
            m.addItem (steps[i].isNotEmpty() ? steps[i] : String ("Change"), [this, count, edit = &session.getEdit()]
            {
                if (edit != &session.getEdit())
                    return;   // another project came forward meanwhile

                for (int n = 0; n < count; ++n)
                    edit->getUndoManager().undo();

                commands.commandStatusChanged();
                setStatus (count == 1 ? String ("Undid 1 step") : "Undid " + String (count) + " steps");
            });
        }

        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&timeline).withMinimumWidth (260));
    }

    String MainComponent::layoutName (int n)
    {
        const auto name = props().getValue ("layoutName" + String (n)).trim();
        return name.isNotEmpty() ? name : "Layout " + String (n);
    }

    void MainComponent::askToRenameLayout (int n)
    {
        auto* w = new AlertWindow ("Rename layout " + String (n), "A name for the menu and the palette. Leave it empty for \"Layout " + String (n) + "\".",
                                   MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", props().getValue ("layoutName" + String (n)), "Name");
        w->addButton ("Rename", 1, KeyPress (KeyPress::returnKey));
        w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));

        w->enterModalState (true, ModalCallbackFunction::create ([this, w, n] (int choice)
        {
            if (choice != 1)
                return;

            props().setValue ("layoutName" + String (n), w->getTextEditorContents ("name").trim());
            commands.commandStatusChanged();
            setStatus ("Layout " + String (n) + " is now " + layoutName (n));
        }), true);
    }

    int MainComponent::layoutSlots()
    {
        return jlimit (1, cmd::maxLayoutSlots, props().getIntValue ("layoutSlots", 4));
    }

    bool MainComponent::keepPluginOnTop()
    {
        return props().getBoolValue ("keepPluginOnTop", true);
    }

    PropertiesFile& MainComponent::props()
    {
        return session.getEngine().getPropertyStorage().getPropertiesFile();
    }

    bool MainComponent::perform (const InvocationInfo& info)
    {
        switch (info.commandID)
        {
            case cmd::record:           toggleRecord(); return true;
            case cmd::playStop:         togglePlay(); return true;
            case cmd::returnToStart:    if (! session.isRecording()) session.returnToStart(); return true;
            case cmd::toggleMetronome:
                session.setMetronomeOn (! session.isMetronomeOn());
                updateTransportButtons();
                settings.refresh();
                commands.commandStatusChanged();
                return true;
            case cmd::toggleLoop:
                editing::setLooping (session.getEdit(), ! editing::isLooping (session.getEdit()));
                updateTransportButtons();
                timeline.loopChanged();
                commands.commandStatusChanged();
                return true;
            case cmd::toggleMonitoring: session.setMonitoring (! session.isMonitoring()); return true;
            case cmd::choosePlugin:     choosePlugin(); return true;
            case cmd::pluginBrowser:    showPanel (false, true); return true;
            case cmd::sampleBrowser:    showPanel (true, true); return true;
            case cmd::toolSelect: case cmd::toolDraw: case cmd::toolPaint: case cmd::toolSlice:
            case cmd::toolDelete: case cmd::toolMute: case cmd::toolZoom: case cmd::toolStretch:
                toolCommand (info); return true;
            case cmd::undo:             session.getEdit().getUndoManager().undo(); commands.commandStatusChanged(); return true;
            case cmd::redo:             session.getEdit().getUndoManager().redo(); commands.commandStatusChanged(); return true;
            // A key equivalent arrives inside the menu bar's own handling; a list opened there is
            // closed again at once (undo history did nothing from ⌥⌘Z). Open it just after.
            case cmd::undoHistory:      MessageManager::callAsync ([safe = SafePointer<MainComponent> (this)] { if (safe != nullptr) safe->showUndoHistory(); }); return true;
            case cmd::exportLog:        exportLog(); return true;
            case cmd::projectNotes:     toggleNotes(); return true;
            case cmd::restoreVersion:   MessageManager::callAsync ([safe = SafePointer<MainComponent> (this)] { if (safe != nullptr) safe->showVersions(); }); return true;
            case cmd::toggleGrid:       timeline.getGrid().enabled = ! timeline.getGrid().enabled; timeline.gridChanged(); refreshToolbar(); return true;
            case cmd::splitAtPlayhead:  timeline.splitAtPlayhead(); return true;
            case cmd::duplicateClips:   timeline.duplicateSelection(); return true;
            case cmd::copyClips:
                // Cmd+C while a tooltip shows copies the tooltip (W38).
                if (const auto tip = showingTooltip(); tip.isNotEmpty())
                {
                    copyToClipboard (tip);
                    setStatus ("Tooltip copied");
                    return true;
                }

                timeline.copySelection();
                commands.commandStatusChanged();
                return true;
            case cmd::cutClips:         timeline.copySelection(); timeline.deleteSelection(true); commands.commandStatusChanged(); return true;
            case cmd::pasteClips:       timeline.paste(); return true;
            case cmd::windowSamples:    toggleDetached (View::samples); return true;
            case cmd::windowPlugins:    toggleDetached (View::plugins); return true;
            case cmd::windowTimeline:   toggleDetached (View::timeline); return true;
            case cmd::windowMixer:      toggleDetached (View::mixer); return true;
            case cmd::toggleMixer:
                if (isDetached (View::mixer))
                    setDetached (View::mixer, true);   // in its own window: bring it forward
                else if (settingsOpen && ! isDetached (View::settings))
                {
                    settingsOpen = false;   // Settings and the mixer share the place of the lanes
                    settingsButton.setLit (theme::accent, false);
                    mixerShown = true;
                }
                else
                    mixerShown = ! mixerShown;

                mixerButton.setLit (theme::accent, mixerShown || isDetached (View::mixer));
                resized();
                commands.commandStatusChanged();
                return true;
            case cmd::windowSignal:     return true;
            case cmd::windowSettings:   toggleDetached (View::settings); return true;
            case cmd::defaultLayout:    defaultLayout(); setStatus ("Back to the default layout"); return true;
            case cmd::commandPalette:   viewsPalette.close(); if (palette.isVisible()) palette.close(); else palette.open(); return true;
            case cmd::viewsPalette:     palette.close(); if (viewsPalette.isVisible()) viewsPalette.close(); else viewsPalette.open(); return true;
            case cmd::uiLarger:         setUiScale (uiScale() + 0.125); return true;
            case cmd::uiSmaller:        setUiScale (uiScale() - 0.125); return true;
            case cmd::uiActualSize:     setUiScale (1.0); return true;
            case cmd::addLaneBelow:     timeline.addLaneBelowCurrent(); return true;
            case cmd::moveLaneUp:       timeline.moveCurrentLane (-1); return true;
            case cmd::togglePunch:
                session.setPunch (! session.isPunchOn());
                setStatus (session.isPunchOn() ? (editing::loopRegion (session.getEdit()) ? String ("Punch on: takes keep only what falls inside the loop region")
                                                                                          : String ("Punch on: set a loop region on the ruler; until then takes record as usual"))
                                               : String ("Punch off: recording runs until you stop"));
                updateTransportButtons();
                commands.commandStatusChanged();
                return true;
            case cmd::exportMix:
                if (exportWindow == nullptr)
                {
                    exportWindow = std::make_unique<ExportWindow> (session, commands);
                    exportWindow->centreAroundComponent (this, exportWindow->getWidth(), exportWindow->getHeight());
                }

                exportWindow->show();
                return true;
            case cmd::previousLane:     timeline.selectNeighbourLane (-1); return true;
            case cmd::nextLane:         timeline.selectNeighbourLane (1); return true;
            case cmd::moveLaneDown:     timeline.moveCurrentLane (1); return true;
            case cmd::lanesTaller:      timeline.setLaneHeight (timeline.getLaneHeight() + Timeline::laneHeightStep); return true;
            case cmd::lanesShorter:     timeline.setLaneHeight (timeline.getLaneHeight() - Timeline::laneHeightStep); return true;
            case cmd::keepPluginOnTop:
                props().setValue ("keepPluginOnTop", ! keepPluginOnTop());
                pluginWindows.setAlwaysOnTop (keepPluginOnTop());
                commands.commandStatusChanged();
                return true;
            case cmd::deleteClips:      timeline.deleteSelection(); return true;
            case cmd::nudgeLeft:        timeline.nudgeSelection (-1); return true;
            case cmd::nudgeRight:       timeline.nudgeSelection (1); return true;
            case cmd::addMarker:        timeline.addMarkerAtPlayhead(); return true;
            case cmd::loopSelection:    timeline.loopSelection(); return true;
            case cmd::openPluginEditor:
                if (auto* p = mixer.firstPluginOfSelected())
                    pluginWindows.open (*p);
                return true;
            case cmd::bypassPlugin:
                if (auto* p = mixer.firstPluginOfSelected())
                {
                    editing::begin (session.getEdit(), (editing::isBypassed (*p) ? "Turn on " : "Bypass ") + p->getName());
                    editing::setBypassed (*p, ! editing::isBypassed (*p));
                    mixer.repaint();
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
            default:
                if (info.commandID >= cmd::saveLayoutFirst && info.commandID < cmd::afterLayouts)
                {
                    const auto n = info.commandID - cmd::saveLayoutFirst + 1;

                    if (auto xml = getLayout().createXml())
                        props().setValue ("layout" + String (n), xml->toString (XmlElement::TextFormat().singleLine()));

                    setStatus ("Saved as " + layoutName (n));
                    commands.commandStatusChanged();
                    return true;
                }

                if (info.commandID >= cmd::recallLayoutFirst && info.commandID < cmd::saveLayoutFirst)
                {
                    const auto n = info.commandID - cmd::recallLayoutFirst + 1;

                    if (auto xml = parseXML (props().getValue ("layout" + String (n))))
                    {
                        applyLayout (ValueTree::fromXml (*xml));
                        setStatus (layoutName (n));
                    }

                    return true;
                }

                return false;
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
            const auto countingIn = session.getCountInBars() > 0 && ! session.isPlaying();

            if (auto r = session.startRecording(); r.failed())
            {
                setStatus (r.getErrorMessage(), theme::warn);
                return;
            }

            recordingFrom = from;
            const auto armed = editing::armedChannels (session.getEdit()).size();
            setStatus ((countingIn ? "Counting in, then recording " : "Recording ")
                           + (armed > 1 ? String (armed) + " channels" : String ("1 channel"))
                           + (session.isPunchOn() && editing::loopRegion (session.getEdit()) ? ", inside the loop region" : ""),
                       theme::clip);
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
        else if (take && take->clips == 0)
            setStatus ("Nothing was kept: the take ended before it reached the loop region (punch is on)", theme::warn);
        else if (take && (take->channels > 1 || take->clips > take->channels))
            setStatus ("Saved " + String (take->channels) + (take->channels > 1 ? " takes" : " take") + " as " + String (take->clips) + " clips"
                           + (take->clips > take->channels ? " (one per loop pass; the newest plays, earlier ones are muted)" : "")
                           + ", placed " + String (take->alignmentSamples) + " samples earlier for latency");
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
        metronomeButton.setLit (theme::accent, session.isMetronomeOn());   // live: it sounds when playing
        loopButton.setLit (theme::accent, editing::isLooping (session.getEdit()));
        loopButton.setEnabled (editing::loopRegion (session.getEdit()).has_value());
        punchButton.setLit (theme::clip, session.isPunchOn());   // a recording mode: the record colour
        punchButton.setEnabled (editing::loopRegion (session.getEdit()).has_value() || session.isPunchOn());
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

    bool MainComponent::showsLoad()
    {
        return props().getBoolValue ("showLoad", false);
    }

    double MainComponent::uiScale()
    {
        return jlimit (0.75, 1.5, props().getDoubleValue ("uiScale", 1.0));
    }

    void MainComponent::setUiScale (double scale)
    {
        scale = jlimit (0.75, 1.5, std::round (scale * 40.0) / 40.0);   // in steps of 2.5 %
        props().setValue ("uiScale", scale);
        Desktop::getInstance().setGlobalScaleFactor (static_cast<float> (scale));
        settings.showUiScale (scale);
        setStatus ("Interface at " + String (roundToInt (scale * 100.0)) + " %");
    }

    void MainComponent::askToRenameProject()
    {
        // Asks first (F22): renaming moves the project's folder, which undo cannot reverse.
        const auto oldName = session.getProjectName();
        auto* w = new AlertWindow ("Rename " + oldName,
                                   "The project's folder and file take the new name. The old folder goes to the bin.",
                                   MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", oldName, "New name");
        w->addButton ("Rename", 1, KeyPress (KeyPress::returnKey));
        w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));

        w->enterModalState (true, ModalCallbackFunction::create ([this, w] (int choice)
        {
            const auto name = w->getTextEditorContents ("name");

            if (choice != 1)
                return;

            session.renameProject (name, [this] (const Result& r)
            {
                if (r.failed())
                    setStatus ("Not renamed: " + r.getErrorMessage(), theme::warn);
                else
                    setStatus ("Renamed to " + session.getProjectName());

                tabs.refresh();
            });
        }), true);   // deletes the window when it closes
    }

    void MainComponent::refreshTempo()
    {
        tempo.setValue (session.getEdit().tempoSequence.getTempo (0)->getBpm(), dontSendNotification);
    }

    void MainComponent::setTempo (double bpm)
    {
        auto& edit = session.getEdit();

        if (std::abs (edit.tempoSequence.getTempo (0)->getBpm() - bpm) < 1.0e-6)
            return;

        if (! tempoBefore)
        {
            edit.getUndoManager().beginNewTransaction ("Change tempo");
            tempoBefore = editing::placesInBeats (edit);
        }

        editing::setTempo (edit, bpm);
        editing::followTempo (edit, *tempoBefore, editing::TempoFollow::leaveInTime);   // fitted loops keep their beats
        timeline.gridChanged();

        // Settles after a short pause (wheel steps, taps); a drag settles when it ends.
        const auto change = ++tempoChanges;
        Timer::callAfterDelay (800, [safe = SafePointer<MainComponent> (this), change]
        {
            if (safe != nullptr && change == safe->tempoChanges && ! safe->tempo.isMouseButtonDown())
                safe->tempoSettled();
        });
    }

    void MainComponent::tempoSettled()
    {
        if (! tempoBefore)
            return;

        auto before = std::move (*tempoBefore);
        tempoBefore.reset();

        if (std::none_of (before.begin(), before.end(), [] (const auto& p) { return ! p.fitted; }))
            return;   // no clips, or only fitted loops, which have followed already

        if (const auto how = session.getTempoFollow())
        {
            editing::followTempo (session.getEdit(), before, *how);
            timeline.gridChanged();
            return;
        }

        askHowClipsFollow (std::move (before));
    }

    void MainComponent::askHowClipsFollow (std::vector<editing::BeatPlace> before)
    {
        auto* w = new AlertWindow ("The tempo is now " + String (tempo.getValue(), 2) + " bpm",
                                   "What should the clips already on the lanes do?",
                                   MessageBoxIconType::NoIcon, this);
        rememberTempoFollow.setToggleState (false, dontSendNotification);
        rememberTempoFollow.setSize (320, 24);
        w->addCustomComponent (&rememberTempoFollow);
        w->addButton ("Stretch with the beat", 3);
        w->addButton ("Move with the beat", 2);
        w->addButton ("Stay in time", 1, KeyPress (KeyPress::escapeKey));

        auto* edit = &session.getEdit();
        w->enterModalState (true, ModalCallbackFunction::create ([this, edit, before = std::move (before)] (int choice)
        {
            const auto how = static_cast<editing::TempoFollow> (std::max (1, choice) - 1);

            if (rememberTempoFollow.getToggleState())
                session.setTempoFollow (how);

            // The project may have changed behind the question; then its clips are not these.
            if (edit == &session.getEdit())
            {
                editing::followTempo (*edit, before, how);
                timeline.gridChanged();
                settings.refresh();
            }
        }), true);
    }

    void MainComponent::refreshMasterVolume()
    {
        masterVolume.setValue (session.getAppLoudness(), dontSendNotification);   // the app's, not the project's
    }

    void MainComponent::refreshToolbar()
    {
        for (int i = 0; i < toolButtons.size(); ++i)
        {
            toolButtons[i]->setChosen (static_cast<int> (timeline.getTool()) == i);
        }

        // Snap reads like the tools (owner): the accent while it is on, plain while it is off.
        const auto snapping = timeline.getGrid().enabled;
        snapButton.setColour (TextButton::buttonColourId, snapping ? theme::accent : theme::hover.withAlpha (0.0f));
        snapButton.setColour (TextButton::textColourOffId, snapping ? theme::onAccent : theme::mist);

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

        // A browser in its own window is shown there: choosing it brings that window forward.
        const auto chosen = samplesView ? View::samples : View::plugins;

        if (isDetached (chosen))
            viewWindows[static_cast<int> (chosen)]->toFront (true);

        resized();   // the panel's contents follow below

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
            startScreen.setShowAtStart (session.getAtLaunch() == engine::Session::AtLaunch::startScreen);
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

    bool MainComponent::closeFrontWindow()
    {
        // The sample editor, while it is in front, closes first (W42).
        if (sampleEditor.isVisible() && sampleEditor.hasKeyboardFocus (true))
        {
            closeSampleEditor();
            return true;
        }

        auto* front = TopLevelWindow::getActiveTopLevelWindow();

        if (front == nullptr)
            return false;

        for (auto& [view, window] : viewWindows)
        {
            if (window.get() == front)
            {
                window->back();
                return true;
            }
        }

        if (pluginWindows.closeIfFront (front))
            return true;

        // The notes and the versions close by themselves; the notes are already in the project.
        if (notesWindow != nullptr && static_cast<Component*> (notesWindow.get()) == front)
        {
            notesWindow->setVisible (false);
            return true;
        }

        if (exportWindow != nullptr && static_cast<Component*> (exportWindow.get()) == front)
        {
            exportWindow->closeButtonPressed();
            return true;
        }

        if (versionsWindow != nullptr && static_cast<Component*> (versionsWindow.get()) == front)
        {
            versionsWindow->setVisible (false);
            return true;
        }

        // In the main window, Settings or the mixer in place of the lanes close first, back to
        // the lanes (owner): Cmd/Ctrl+W closes the view in front before the project.
        if (front == getTopLevelComponent())
        {
            if (settingsOpen && ! isDetached (View::settings))
            {
                toggleSettings();
                return true;
            }

            if (mixerShown && ! isDetached (View::mixer))
            {
                commands.invokeDirectly (cmd::toggleMixer, false);
                return true;
            }
        }

        return false;
    }

    void MainComponent::closePluginWindow()
    {
        pluginWindows.closeAll();
    }

    //==============================================================================================
    void MainComponent::toggleSettings()
    {
        // Settings in its own window: the command brings that window forward.
        if (isDetached (View::settings))
        {
            viewWindows[static_cast<int> (View::settings)]->toFront (true);
            return;
        }

        settingsOpen = ! settingsOpen;

        if (settingsOpen)
        {
            settings.refresh();
            updateFigures();   // fill the load figures now, not on the next tick
        }

        settingsButton.setLit (theme::accent, settingsOpen);
        resized();
        commands.commandStatusChanged();

        if (settingsOpen && settings.isShowing())
            settings.grabKeyboardFocus();   // so Escape reaches it, wherever the focus was
    }

    void MainComponent::layoutSettings (bool)
    {
        // Settings is a view (owner): it takes the place of the lanes, as the mixer does, or a
        // window of its own.
        if (isDetached (View::settings))
            return;

        settings.setVisible (settingsOpen);

        if (settingsOpen)
        {
            settings.setBounds (centreArea);
            settings.toFront (false);
        }
    }

    void MainComponent::setStatus (const String& text, Colour colour)
    {
        statusLabel.setText (text, dontSendNotification);
        statusLabel.setColour (Label::textColourId, colour);

        if (colour == theme::warn || colour == theme::clip)
            log::warning (text);   // what the person was warned about is in the log (W38)
    }

    //==============================================================================================
    void MainComponent::frame (double t)
    {
        const trace::Scope traced ("Frame");
        if (frameLog)
            logFrame (t);

        lastFrameTime = t;
        ++frameCount;

        auto& callback = session.getCallback();
        juce::ignoreUnused (callback.takeInputPeaks());   // the channels' meters show the input now

        if (samples.isVisible())
            samples.frame();   // the playing sound's progress

        const auto position = session.getPositionSeconds();
        timeline.setPlayhead (position, recordingFrom);

        // Undo and redo can change the tempo; follow them unless the figure is being changed.
        if (! tempoBefore && ! tempo.isMouseButtonDown() && ! tempo.hasKeyboardFocus (true))
            refreshTempo();

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

        // The toolbar folds when the window is narrow (or the interface large, W38): the least
        // needed parts go first, each still a tooltip or a menu item away.
        struct Fold { bool words, masterLabel, load, master, tap; int time, grid, tool; };
        constexpr auto wide = ToolButton::width, narrow = ToolButton::narrowWidth;
        const Fold folds[] = { { true,  true,  true,  true,  true,  100, 104, wide },
                               { false, false, true,  true,  true,  100, 104, wide },
                               { false, false, false, true,  true,  100, 92,  wide },
                               { false, false, false, true,  true,  100, 92,  narrow },   // narrower tools before the loudness goes
                               { false, false, false, false, false, 100, 92,  wide },
                               { false, false, false, false, false, 100, 92,  narrow } };
        const auto widthOf = [this] (const Fold& f)
        {
            return 6 * 36 + 5 * theme::space1 + 36 + theme::space1 + theme::space2 + f.time + theme::space2 + 80
                   + (f.tap ? theme::space1 + 40 : 0) + theme::space3
                   + (f.master ? (f.masterLabel ? 84 : 0) + 100 + theme::space3 : 0)
                   + toolButtons.size() * (f.tool + 1) + theme::space3 + f.grid + theme::space1 + 56
                   + theme::space3 + 36 + theme::space1 + 36 + theme::space4 + (f.words ? 64 + theme::space1 : 0) + 64 + theme::space2
                   + (f.load && showsLoad() ? 140 : 0);
        };
        auto fold = folds[std::size (folds) - 1];
        for (const auto& f : folds)
            if (widthOf (f) <= top.getWidth()) { fold = f; break; }

        recordButton.setBounds (top.removeFromLeft (36));
        top.removeFromLeft (theme::space1);
        playButton.setBounds (top.removeFromLeft (36));
        top.removeFromLeft (theme::space1);
        toStartButton.setBounds (top.removeFromLeft (36));
        top.removeFromLeft (theme::space1);
        metronomeButton.setBounds (top.removeFromLeft (36));
        top.removeFromLeft (theme::space1);
        loopButton.setBounds (top.removeFromLeft (36));
        top.removeFromLeft (theme::space1);
        punchButton.setBounds (top.removeFromLeft (36));
        top.removeFromLeft (theme::space2);
        timeLabel.setBounds (top.removeFromLeft (fold.time));
        top.removeFromLeft (theme::space2);
        tempo.setBounds (top.removeFromLeft (80).withSizeKeepingCentre (80, 26));
        tapButton.setVisible (fold.tap);
        if (fold.tap)
        {
            top.removeFromLeft (theme::space1);
            tapButton.setBounds (top.removeFromLeft (40).withSizeKeepingCentre (40, 26));
        }
        top.removeFromLeft (theme::space3);

        masterLabel.setVisible (fold.master && fold.masterLabel);
        masterVolume.setVisible (fold.master);
        if (fold.master)
        {
            if (fold.masterLabel)
                masterLabel.setBounds (top.removeFromLeft (84));
            masterVolume.setBounds (top.removeFromLeft (100).withSizeKeepingCentre (100, 24));
            top.removeFromLeft (theme::space3);
        }

        for (auto* b : toolButtons)
        {
            b->setShowsKey (fold.tool == ToolButton::width);
            b->setBounds (top.removeFromLeft (fold.tool).withSizeKeepingCentre (fold.tool, ToolButton::height));
            top.removeFromLeft (1);
        }

        top.removeFromLeft (theme::space3);
        gridMenu.setBounds (top.removeFromLeft (fold.grid).withSizeKeepingCentre (fold.grid, 28));
        top.removeFromLeft (theme::space1);
        snapButton.setBounds (top.removeFromLeft (56).withSizeKeepingCentre (56, 28));

        // The mixer and Settings beside Snap, within easy reach (owner), not at the far right.
        top.removeFromLeft (theme::space3);
        mixerButton.setBounds (top.removeFromLeft (36));
        top.removeFromLeft (theme::space1);
        settingsButton.setBounds (top.removeFromLeft (36));
        latencyWords.setVisible (fold.words);
        if (fold.words)
        {
            latencyWords.setBounds (top.removeFromRight (64));
            top.removeFromRight (theme::space1);   // the figure and its words read as "6.3 ms round trip"
        }
        latencyLabel.setBounds (top.removeFromRight (64));
        top.removeFromRight (theme::space2);
        loadMeter.setVisible (fold.load && showsLoad());   // hidden unless Settings shows it (owner's list F22)
        if (loadMeter.isVisible())
            loadMeter.setBounds (top.removeFromRight (140).withSizeKeepingCentre (140, 20));

        // The panel docks on the left, under the transport; the rest of the window shifts over.
        // Its switch sits on the panel's own surface at the top. A browser in its own window
        // leaves the panel to the other; with both away, the panel closes (F21).
        const auto samplesHere = ! isDetached (View::samples), pluginsHere = ! isDetached (View::plugins);
        const auto panelOpen = browserOpen && (samplesHere || pluginsHere);
        const auto showingSamples = samplesHere && (panelShowsSamples || ! pluginsHere);

        samplesTab.setVisible (panelOpen);
        pluginsTab.setVisible (panelOpen);
        popPanel.setVisible (panelOpen);
        samples.setVisible (! samplesHere || (panelOpen && showingSamples));
        browser.setVisible (! pluginsHere || (panelOpen && ! showingSamples));

        for (auto [tab, isSamples] : { std::pair { &samplesTab, true }, { &pluginsTab, false } })
        {
            const auto shown = isSamples == showingSamples;
            tab->setColour (TextButton::buttonColourId, shown ? theme::hover : theme::bg2);
            tab->setColour (TextButton::textColourOffId, shown ? theme::text : theme::muted);
            tab->setTooltip ((isSamples ? samplesHere : pluginsHere) ? (isSamples ? "Your samples" : "Your plugins")
                                                                       : String ("In its own window: click to bring it forward"));
        }

        if (panelOpen)
        {
            auto panel = r.removeFromLeft (panelWidth);
            panelArea = panel;
            auto switchRow = panel.removeFromTop (40).reduced (theme::space3, 0).withTrimmedTop (theme::space2).withTrimmedRight (1);
            popPanel.setBounds (switchRow.removeFromRight (28).withHeight (28));
            switchRow.removeFromRight (theme::space1);
            samplesTab.setBounds (switchRow.removeFromLeft (switchRow.getWidth() / 2 - 2).withHeight (28));
            switchRow.removeFromLeft (4);
            pluginsTab.setBounds (switchRow.withHeight (28));
            popPanel.setTooltip (String ("The ") + (showingSamples ? "samples" : "plugins") + " in a window of their own");

            if (samplesHere && showingSamples)  samples.setBounds (panel);
            if (pluginsHere && ! showingSamples) browser.setBounds (panel);

            panelEdge.setBounds (panelArea.getRight() - PanelEdge::thickness / 2, panelArea.getY(), PanelEdge::thickness, panelArea.getHeight());
            panelEdge.setVisible (true);
            panelEdge.toFront (false);
        }
        else
        {
            panelArea = {};
            panelEdge.setVisible (false);
        }

        statusLabel.setBounds (r.removeFromBottom (28).reduced (theme::space4, 0));

        // The timeline away: its place says where it is, and brings it back.
        const auto timelineHere = ! isDetached (View::timeline);
        timelineAway.setVisible (! timelineHere);
        timelineBack.setVisible (! timelineHere);
        popTimeline.setVisible (timelineHere);

        if (sampleEditor.isVisible())
        {
            const auto editorArea = r.removeFromBottom (std::min (sampleEditorHeight, r.getHeight() - 120));
            sampleEditor.setBounds (editorArea);
            sampleEditorEdge.setBounds (editorArea.getX(), editorArea.getY() - PanelEdge::thickness / 2, editorArea.getWidth(), PanelEdge::thickness);
            sampleEditorEdge.toFront (false);
        }

        // The mixer in place of the lanes (W48), when shown and not in its own window; Settings
        // over both while it is open here.
        centreArea = r;
        settingsButton.setLit (theme::accent, settingsOpen);
        const auto settingsHere = settingsOpen && ! isDetached (View::settings);
        const auto mixerHere = mixerShown && ! isDetached (View::mixer) && ! settingsHere;

        if (! isDetached (View::mixer))
            mixer.setVisible (mixerHere);

        popMixer.setVisible (mixerHere);

        if (timelineHere)
            timeline.setVisible (! mixerHere && ! settingsHere);

        if (settingsHere)
        {
            popTimeline.setVisible (false);
            timelineAway.setVisible (false);
            timelineBack.setVisible (false);
        }
        else if (mixerHere)
        {
            mixer.setBounds (r);
            popMixer.setBounds (r.getRight() - 30, r.getY() + 4, 24, 24);
            popMixer.toFront (false);
            popTimeline.setVisible (false);
        }
        else if (timelineHere)
        {
            timeline.setBounds (r);
            popTimeline.setBounds (r.getX() + 4, r.getY() + (Timeline::rulerHeight - 24) / 2, 24, 24);   // centred on the ruler row
            popTimeline.toFront (false);
        }
        else
        {
            auto centre = r.withSizeKeepingCentre (360, 72);
            timelineAway.setBounds (centre.removeFromTop (32));
            timelineBack.setBounds (centre.removeFromTop (32).withSizeKeepingCentre (132, 30));
        }

        if (! isDetached (View::settings))
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
        const auto right = getWidth();
        const auto w = UpdateBanner::preferredWidth;
        const auto h = updateBanner.getPreferredHeight (w);
        updateBanner.setBounds (right - w - theme::space2, getHeight() - 28 - h, w, h);
        updateBanner.toFront (false);
    }

    //==============================================================================================
    // Views in their own windows (F21)
    String MainComponent::nameOf (View v)
    {
        switch (v)
        {
            case View::samples:  return "Samples";
            case View::plugins:  return "Plugins";
            case View::timeline: return "Main grid";   // the owner's name for it, as on its ruler
            case View::signal:   return "Signal path (gone)";
            case View::settings: return "Settings";
            case View::mixer:    return "Mixer";
        }

        return {};
    }

    Component& MainComponent::componentOf (View v)
    {
        switch (v)
        {
            case View::samples:  return samples;
            case View::plugins:  return browser;
            case View::timeline:
            case View::signal:   return timeline;   // signal: not a view any more (W51)
            case View::settings: return settings;
            case View::mixer:    return mixer;
        }

        return timeline;
    }

    void MainComponent::toggleDetached (View v)
    {
        setDetached (v, ! isDetached (v));
    }

    void MainComponent::setDetached (View v, bool detached, std::optional<Rectangle<int>> bounds)
    {
        auto& comp = componentOf (v);
        const auto key = static_cast<int> (v);

        if (detached)
        {
            if (const auto found = viewWindows.find (key); found != viewWindows.end())
            {
                if (bounds)
                    found->second->setBounds (onScreen (*bounds));

                found->second->toFront (true);
                return;
            }

            if (auto* parent = comp.getParentComponent())
                parent->removeChildComponent (&comp);

            if (v == View::settings)
            {
                settingsOpen = true;
                settings.refresh();
                settings.setInWindow (true);
            }

            comp.setVisible (true);
            auto window = std::make_unique<ViewWindow> (*this, v, comp);

            // Its own size, beside the main window, on the same display, unless a layout says.
            const auto size = v == View::timeline ? Rectangle<int> (0, 0, 1100, 520)
                            : v == View::mixer    ? Rectangle<int> (0, 0, 1200, 480)
                            : v == View::signal   ? Rectangle<int> (0, 0, 900, 160)
                            : v == View::settings ? Rectangle<int> (0, 0, 920, 660)
                                                  : Rectangle<int> (0, 0, 320, 720);
            const auto* top = getTopLevelComponent();
            const auto near = top != nullptr ? size.withPosition (top->getRight() - size.getWidth() / 2, top->getY() + 40) : size;
            window->setBounds (onScreen (bounds.value_or (near)));
            window->setVisible (true);
            viewWindows[key] = std::move (window);
        }
        else
        {
            const auto found = viewWindows.find (key);

            if (found == viewWindows.end())
                return;

            if (auto* parent = comp.getParentComponent())
                parent->removeChildComponent (&comp);

            viewWindows.erase (found);

            if (v == View::settings)
            {
                settings.setInWindow (false);
                settingsOpen = false;
                addChildComponent (settings);
            }
            else
            {
                addAndMakeVisible (comp);
            }
        }

        viewsChanged();
    }

    void MainComponent::viewsChanged()
    {
        startScreen.toFront (false);
        resized();

        if (! isDetached (View::settings))
            layoutSettings (false);

        commands.commandStatusChanged();
    }

    void MainComponent::defaultLayout()
    {
        for (auto v : views)
            setDetached (v, false);

        browserOpen = true;
        panelShowsSamples = true;
        settingsOpen = false;
        setPanelWidth (PluginBrowser::preferredWidth);

        if (auto* top = getTopLevelComponent())
            top->setBounds (onScreen (Rectangle<int> (1280, 780 + ProjectTabs::height)
                                          .withCentre (Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.toNearestInt().getCentre())));

        showPanel (false, false);
        layoutSettings (false);
    }

    ValueTree MainComponent::getLayout() const
    {
        ValueTree t ("LAYOUT");

        if (const auto* top = getTopLevelComponent())
            t.setProperty ("main", top->getBounds().toString(), nullptr);

        t.setProperty ("panelOpen", browserOpen, nullptr);
        t.setProperty ("panelShowsSamples", panelShowsSamples, nullptr);
        t.setProperty ("panelWidth", panelWidth, nullptr);
        t.setProperty ("settingsOpen", settingsOpen, nullptr);

        for (auto v : views)
        {
            ValueTree view ("VIEW");
            view.setProperty ("id", static_cast<int> (v), nullptr);

            if (const auto found = viewWindows.find (static_cast<int> (v)); found != viewWindows.end())
            {
                view.setProperty ("detached", true, nullptr);
                view.setProperty ("bounds", found->second->getBounds().toString(), nullptr);
            }

            t.appendChild (view, nullptr);
        }


        return t;
    }

    void MainComponent::applyLayout (const ValueTree& t)
    {
        if (! t.hasType ("LAYOUT"))
            return;

        if (auto* top = getTopLevelComponent(); top != nullptr && t.hasProperty ("main"))
            top->setBounds (onScreen (Rectangle<int>::fromString (t["main"].toString())));

        for (auto view : t)
        {
            const auto v = static_cast<View> (static_cast<int> (view["id"]));

            if (std::find (views.begin(), views.end(), v) == views.end())
                continue;   // a view that is gone (the signal row, W51)

            if (static_cast<bool> (view["detached"]))
                setDetached (v, true, Rectangle<int>::fromString (view["bounds"].toString()));
            else
                setDetached (v, false);
        }

        browserOpen = static_cast<bool> (t.getProperty ("panelOpen", true));
        setPanelWidth (static_cast<int> (t.getProperty ("panelWidth", PluginBrowser::preferredWidth)));

        if (! isDetached (View::settings))
            settingsOpen = static_cast<bool> (t.getProperty ("settingsOpen", false));


        showPanel (static_cast<bool> (t.getProperty ("panelShowsSamples", true)), false);
        browserOpen = static_cast<bool> (t.getProperty ("panelOpen", true));
        viewsChanged();
    }
}
