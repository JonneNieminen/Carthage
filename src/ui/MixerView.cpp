#include "MixerView.h"

#include <engine/Editing.h>
#include <engine/SampleEdits.h>

namespace carthage::ui
{
    using namespace juce;

    namespace
    {
        constexpr int stripWidth = 64, masterWidth = 80, stripGap = 2, columnWidth = 248;
        constexpr float minDb = -60.0f, maxDb = 6.0f;

        const Identifier colourProp ("carthageColour");

        const Colour* const palette[] = { &theme::laneSky, &theme::laneTide, &theme::laneKelp, &theme::laneSand,
                                          &theme::laneCoral, &theme::laneMurex, &theme::laneRose, &theme::laneSlate };

        Colour colourOf (const tracktion::AudioTrack& t, int index)
        {
            if (const auto chosen = t.state[colourProp].toString(); chosen.isNotEmpty())
                return Colour::fromString (chosen);

            return *palette[static_cast<size_t> (index) % std::size (palette)];
        }

        String shortName (const String& name)
        {
            return name.startsWith ("Channel ") ? name.substring (8) : name;   // the number says it in a narrow strip
        }

        /** dB to a meter's height, 0 to 1, on the fader's scale. */
        float meterProportion (float db)
        {
            if (db <= minDb)
                return 0.0f;

            const auto x = (std::min (db, maxDb) - minDb) / (maxDb - minDb);
            return std::pow (x, 2.2f);   // more room for the loud end, where mixing happens
        }
    }

    //==============================================================================================
    /** One channel's strip, or master's (track null). */
    class MixerView::Strip final : public Component, public SettableTooltipClient
    {
    public:
        Strip (MixerView& v, tracktion::AudioTrack* t, int indexIn) : view (v), track (t), index (indexIn)
        {
            auto& edit = view.session.getEdit();
            volume = track != nullptr ? tracktion::VolumeAndPanPlugin::Ptr (track->getVolumePlugin()) : edit.getMasterVolumePlugin();

            if (auto* m = track != nullptr ? editing::peakMeterOf (*track) : editing::masterPeakMeter (edit))
            {
                peakMeter = m;
                m->measurer.addClient (peakClient);
            }

            if (auto* m = track != nullptr ? editing::rmsMeterOf (*track) : editing::masterRMSMeter (edit))
            {
                rmsMeter = m;
                m->measurer.addClient (rmsClient);
            }

            fader.setSliderStyle (Slider::LinearVertical);
            fader.setTextBoxStyle (Slider::NoTextBox, true, 0, 0);
            fader.setRange (minDb, maxDb, 0.1);
            fader.setSkewFactorFromMidPoint (-12.0);
            fader.setDefault (0.0);
            fader.setTooltip ("Volume. Double-click: 0 dB. Scroll to adjust, Shift for fine steps.");
            fader.onDragStart = [this] { editing::begin (view.session.getEdit(), nameForUndo() + " volume"); };
            fader.onValueChange = [this]
            {
                if (volume != nullptr && std::abs (volume->getVolumeDb() - static_cast<float> (fader.getValue())) > 0.01f)
                    volume->setVolumeDb (static_cast<float> (fader.getValue()));
                repaint (figureArea);
            };
            addAndMakeVisible (fader);

            pan.setSliderStyle (Slider::LinearHorizontal);   // a marker on a line: centre is the default
            pan.setTextBoxStyle (Slider::NoTextBox, true, 0, 0);
            pan.setRange (-1.0, 1.0, 0.01);
            pan.setDefault (0.0);
            pan.setTooltip ("Pan. Double-click: centre.");
            pan.onDragStart = [this] { editing::begin (view.session.getEdit(), nameForUndo() + " pan"); };
            pan.onValueChange = [this]
            {
                if (volume != nullptr && std::abs (volume->getPan() - static_cast<float> (pan.getValue())) > 0.001f)
                    volume->setPan (static_cast<float> (pan.getValue()));
            };
            addAndMakeVisible (pan);

            readModel();
            setTooltip (track != nullptr ? "Click to select. Right-click to rename, colour, route or delete it." : "Master: everything you hear goes through it");
        }

        ~Strip() override
        {
            if (peakMeter != nullptr) peakMeter->measurer.removeClient (peakClient);
            if (rmsMeter != nullptr)  rmsMeter->measurer.removeClient (rmsClient);
        }

        tracktion::AudioTrack* getTrack() const noexcept   { return track; }

        /** The fader and pan from the model (an undo, the toolbar's master), unless being dragged. */
        void readModel()
        {
            if (volume == nullptr)
                return;

            if (! fader.isMouseButtonDown())
                fader.setValue (std::clamp (volume->getVolumeDb(), minDb, maxDb), dontSendNotification);

            if (! pan.isMouseButtonDown())
                pan.setValue (volume->getPan(), dontSendNotification);
        }

        /** Meters: what arrived since the last frame, falling back gently. */
        void tick()
        {
            const auto take = [] (tracktion::LevelMeasurer::Client& c, std::array<float, 2>& shown, float fall)
            {
                const auto channels = std::max (1, std::min (2, c.getNumChannelsUsed()));
                bool changed = false;

                for (int ch = 0; ch < 2; ++ch)
                {
                    const auto db = c.getAndClearAudioLevel (std::min (ch, channels - 1)).dB;
                    const auto next = std::max (db, shown[static_cast<size_t> (ch)] - fall);

                    if (std::abs (next - shown[static_cast<size_t> (ch)]) > 0.05f)
                    {
                        shown[static_cast<size_t> (ch)] = next;
                        changed = true;
                    }
                }

                return changed;
            };

            auto changed = take (peakClient, peakDb, 1.5f);
            changed = take (rmsClient, rmsDb, 0.9f) || changed;

            if (std::max (peakDb[0], peakDb[1]) > 0.0f && ! clipped)
            {
                clipped = true;
                changed = true;
            }

            readModel();

            if (changed)
                repaint (meterArea);
        }

        void paint (Graphics& g) override
        {
            auto& edit = view.session.getEdit();
            const auto r = getLocalBounds().toFloat();
            const auto chosen = track != nullptr && view.selected == track->itemID;

            g.setColour (track == nullptr ? theme::card : theme::bg2);
            g.fillRoundedRectangle (r, theme::radiusSm);

            if (chosen)
            {
                g.setColour (theme::accent);   // the selected channel: live
                g.drawRoundedRectangle (r.reduced (0.75f), theme::radiusSm, 1.5f);
            }

            // Colour, name, where it goes.
            const auto colour = track != nullptr ? colourOf (*track, index) : theme::text;
            g.setColour (track != nullptr && editing::isChannelMuted (*track) ? colour.withMultipliedAlpha (0.35f) : colour);
            g.fillRoundedRectangle (colourBar.toFloat(), 1.5f);

            g.setColour (theme::text);
            g.setFont (theme::denseFont (theme::textSm, true));
            g.drawFittedText (track != nullptr ? shortName (track->getName()) : String ("Master"), nameArea, Justification::centred, 1, 0.8f);

            if (track != nullptr)
            {
                auto* into = editing::outputOf (*track);
                g.setColour (outputArea.contains (getMouseXYRelative()) && isMouseOver() ? theme::text : theme::muted);
                g.setFont (theme::denseFont (theme::textXs));
                g.drawFittedText (String::fromUTF8 ("\xe2\x86\x92 ") + (into != nullptr ? shortName (into->getName()) : String ("Master")),
                                  outputArea, Justification::centred, 1, 0.8f);

                pill (g, muteArea, "M", editing::isChannelMuted (*track), theme::warn);
                pill (g, soloArea, "S", editing::isChannelSoloed (*track), theme::ok);
            }

            // The figure under the fader, in the time face: it holds still as it changes.
            g.setColour (theme::muted);
            g.setFont (theme::timeFont (theme::textXs));
            const auto db = fader.getValue();
            g.drawText (db <= minDb + 0.05 ? String::fromUTF8 ("\xe2\x88\x92\xe2\x88\x9e") : String (db, 1), figureArea, Justification::centred, false);

            paintMeter (g);
            juce::ignoreUnused (edit);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (theme::space1, theme::space1);
            colourBar = r.removeFromTop (3);
            r.removeFromTop (theme::space1);
            nameArea = r.removeFromTop (16);

            if (track != nullptr)
                outputArea = r.removeFromTop (14);

            r.removeFromTop (theme::space1);
            pan.setBounds (r.removeFromTop (16));
            r.removeFromTop (theme::space1);

            if (track != nullptr)
            {
                auto pills = r.removeFromTop (18);
                muteArea = pills.removeFromLeft (pills.getWidth() / 2).reduced (2, 0);
                soloArea = pills.reduced (2, 0);
                r.removeFromTop (theme::space1);
            }

            figureArea = r.removeFromBottom (16);
            r.removeFromBottom (theme::space1);
            meterArea = r.removeFromRight (track != nullptr ? 14 : 18);
            fader.setBounds (r);
        }

        void mouseDown (const MouseEvent& e) override
        {
            if (track == nullptr)
                return;

            view.select (track);

            if (e.mods.isPopupMenu())
            {
                view.showMenu (*track);
                return;
            }

            if (muteArea.contains (e.getPosition()))
                editing::setChannelMuted (*track, ! editing::isChannelMuted (*track));
            else if (soloArea.contains (e.getPosition()))
                editing::setChannelSoloed (*track, ! editing::isChannelSoloed (*track));
            else if (outputArea.contains (e.getPosition()))
                view.showOutputMenu (*track, *this);
            else if (meterArea.contains (e.getPosition()))
                clipped = false;   // a click on the meter clears its clip light

            repaint();
        }

        void mouseDoubleClick (const MouseEvent& e) override
        {
            if (track == nullptr || ! nameArea.contains (e.getPosition()))
                return;

            auto* editor = new TextEditor();   // owned by the CallOutBox below
            editor->setText (track->getName(), false);
            editor->setSize (160, 26);
            editor->setFont (theme::uiFont());
            editor->selectAll();
            auto* safeTrack = track;
            auto& edit = view.session.getEdit();
            editor->onReturnKey = [editor, safeTrack, &edit]
            {
                if (const auto name = editor->getText().trim(); name.isNotEmpty() && name != safeTrack->getName())
                {
                    editing::begin (edit, "Rename channel");
                    safeTrack->setName (name);
                }

                if (auto* box = editor->findParentComponentOfClass<CallOutBox>())
                    box->dismiss();
            };
            CallOutBox::launchAsynchronously (std::unique_ptr<Component> (editor), localAreaToGlobal (nameArea), nullptr);
            editor->grabKeyboardFocus();
        }

        void mouseMove (const MouseEvent&) override   { repaint(); }
        void mouseExit (const MouseEvent&) override   { repaint(); }

    private:
        MixerView& view;
        tracktion::AudioTrack* track;
        int index;
        tracktion::VolumeAndPanPlugin::Ptr volume;
        juce::ReferenceCountedObjectPtr<tracktion::LevelMeterPlugin> peakMeter, rmsMeter;   // held: the clients are inside them
        tracktion::LevelMeasurer::Client peakClient, rmsClient;
        std::array<float, 2> peakDb { -100.0f, -100.0f }, rmsDb { -100.0f, -100.0f };
        bool clipped = false;

        ValueSlider fader { 0.0 }, pan { 0.0 };
        Rectangle<int> colourBar, nameArea, outputArea, muteArea, soloArea, meterArea, figureArea;

        String nameForUndo() const   { return track != nullptr ? track->getName() : String ("Master"); }

        void paintMeter (Graphics& g) const
        {
            const auto area = meterArea.toFloat().withTrimmedTop (2.0f);
            const auto barWidth = (area.getWidth() - 2.0f) / 2.0f;

            for (int ch = 0; ch < 2; ++ch)
            {
                const auto bar = Rectangle<float> (area.getX() + static_cast<float> (ch) * (barWidth + 2.0f), area.getY() + 5.0f, barWidth, area.getHeight() - 5.0f);
                g.setColour (theme::well);
                g.fillRoundedRectangle (bar, 1.5f);

                // RMS as the body, the peak as a line above it: loudness and its edges.
                const auto rms = meterProportion (rmsDb[static_cast<size_t> (ch)]);
                const auto peak = meterProportion (peakDb[static_cast<size_t> (ch)]);
                const auto hot = peakDb[static_cast<size_t> (ch)] > -6.0f;
                g.setColour (hot ? theme::warn : theme::ok);
                g.fillRect (bar.withTop (bar.getBottom() - rms * bar.getHeight()));

                if (peak > 0.0f)
                {
                    g.setColour (theme::text.withAlpha (0.8f));
                    g.fillRect (bar.withY (bar.getBottom() - peak * bar.getHeight()).withHeight (1.5f));
                }
            }

            // The clip light: over 0 dB at any moment since it was last cleared.
            g.setColour (clipped ? theme::clip : theme::well);
            g.fillRoundedRectangle (area.withHeight (3.0f), 1.0f);
        }

        void pill (Graphics& g, Rectangle<int> r, const String& letter, bool on, Colour onColour) const
        {
            const auto f = r.toFloat();
            g.setColour (on ? onColour : theme::well);
            g.fillRoundedRectangle (f, theme::radiusXs);

            if (! on)
            {
                g.setColour (theme::line);
                g.drawRoundedRectangle (f.reduced (0.5f), theme::radiusXs, theme::stroke);
            }

            g.setColour (on ? theme::onAccent : theme::muted);
            g.setFont (theme::uiFont (theme::textXs, true));
            g.drawText (letter, r, Justification::centred, false);
        }
    };

    //==============================================================================================
    MixerView::MixerView (engine::Session& s) : session (s)
    {
        setOpaque (true);
        viewport.setViewedComponent (&stripHolder, false);
        viewport.setScrollBarsShown (false, true);
        addAndMakeVisible (viewport);

        addButton.setTooltip ("Add a channel at the end");
        addButton.onClick = [this]
        {
            auto& edit = session.getEdit();
            editing::begin (edit, "Add channel");

            if (auto* added = editing::addChannel (edit))
            {
                select (added);

                if (onStatus)
                    onStatus ("Added " + added->getName());
            }
        };
        addAndMakeVisible (addButton);

        styleLabel (columnName, theme::textLg, theme::text, true);
        for (auto* l : { &columnInto, &columnFrom, &columnPlugins })
            styleLabel (*l, theme::textSm, theme::text, true);
        for (auto* l : { &columnFromList, &columnPluginList })
        {
            styleLabel (*l, theme::textSm, theme::muted);
            l->setJustificationType (Justification::topLeft);
        }

        columnInto.setText ("Plays into", dontSendNotification);
        columnFrom.setText ("Sound from", dontSendNotification);
        columnPlugins.setText ("Plugins", dontSendNotification);
        output.setTooltip ("Where this channel's sound goes: master, or another channel (a loop back is refused)");
        output.onChange = [this]
        {
            auto* channel = selectedChannel();
            const auto id = output.getSelectedId();

            if (channel == nullptr || id <= 0)
                return;

            const auto all = editing::channels (session.getEdit());
            chooseOutput (*channel, id == 1 ? nullptr : all[id - 2]);
        };

        for (auto* c : std::initializer_list<Component*> { &columnName, &columnInto, &output, &columnFrom, &columnFromList, &columnPlugins, &columnPluginList })
            addAndMakeVisible (c);

        projectChanged();
        startTimerHz (30);
    }

    MixerView::~MixerView()
    {
        projectClosing();
    }

    void MixerView::projectClosing()
    {
        if (editState.isValid())
            editState.removeListener (this);

        editState = {};
        strips.clear();   // let go of the old edit's meters before it goes
    }

    void MixerView::projectChanged()
    {
        projectClosing();
        editState = session.getEdit().state;
        editState.addListener (this);
        selected = {};
        rebuild();
    }

    void MixerView::valueTreePropertyChanged (ValueTree& tree, const Identifier& id)
    {
        // Names, colours, routes and mutes repaint; a track becoming a channel rebuilds.
        if (id == tracktion::IDs::name || id == colourProp || id == Identifier ("carthageRole") || id == tracktion::IDs::dst
            || id == tracktion::IDs::mute || id == Identifier ("carthageMuted") || id == Identifier ("carthageSoloed"))
        {
            if (id == Identifier ("carthageRole") || id == tracktion::IDs::dst)
                triggerRebuild();
            else
                repaint();

            juce::ignoreUnused (tree);
            refreshColumn();
        }
    }

    void MixerView::triggerRebuild()
    {
        rebuildPending = true;   // done on the next timer tick: an edit may change many tracks at once
    }

    void MixerView::rebuild()
    {
        rebuildPending = false;
        strips.clear();
        stripHolder.removeAllChildren();

        if (! editState.isValid())
            return;

        strips.add (std::make_unique<Strip> (*this, nullptr, -1));   // master
        addAndMakeVisible (*strips.getFirst());
        int i = 0;

        for (auto* channel : editing::channels (session.getEdit()))
        {
            auto* strip = strips.add (std::make_unique<Strip> (*this, channel, i++));
            stripHolder.addAndMakeVisible (strip);
        }

        if (selectedChannel() == nullptr)
            if (const auto all = editing::channels (session.getEdit()); ! all.isEmpty())
                selected = all.getFirst()->itemID;

        resized();
        refreshColumn();
        repaint();
    }

    tracktion::AudioTrack* MixerView::selectedChannel() const
    {
        for (auto* ch : editing::channels (session.getEdit()))
            if (ch->itemID == selected)
                return ch;

        return nullptr;
    }

    void MixerView::select (tracktion::AudioTrack* channel)
    {
        selected = channel != nullptr ? channel->itemID : tracktion::EditItemID();

        for (auto* s : strips)
            s->repaint();

        refreshColumn();
    }

    void MixerView::refreshColumn()
    {
        auto* channel = selectedChannel();
        auto& edit = session.getEdit();

        for (auto* c : std::initializer_list<Component*> { &columnName, &columnInto, &output, &columnFrom, &columnFromList, &columnPlugins, &columnPluginList })
            c->setVisible (channel != nullptr);

        if (channel == nullptr)
            return;

        columnName.setText (channel->getName(), dontSendNotification);

        output.clear (dontSendNotification);
        output.addItem ("Master", 1);
        const auto all = editing::channels (edit);

        for (int i = 0; i < all.size(); ++i)
            if (all[i] != channel)
                output.addItem (all[i]->getName(), i + 2);

        auto* into = editing::outputOf (*channel);
        output.setSelectedId (into != nullptr ? all.indexOf (into) + 2 : 1, dontSendNotification);

        // What plays into it: the samples routed here, channels routed here, the live input.
        StringArray from;

        if (channel == &session.getTrack())
            from.add ("The live input");

        std::set<String> samples;
        for (auto* c : editing::allClips (edit))
            if (editing::routeOf (*c) == channel)
                samples.insert (editing::sampleName (*c));

        for (const auto& name : samples)
            from.add (name);

        for (auto* other : all)
            if (other != channel && editing::outputOf (*other) == channel)
                from.add (other->getName());

        columnFromList.setText (from.isEmpty() ? String ("Nothing yet. Right-click a clip on the lanes and choose Route to.")
                                               : from.joinIntoString ("\n"), dontSendNotification);

        StringArray plugins;
        for (auto* p : editing::insertsOf (*channel))
            plugins.add (p->getName());

        columnPluginList.setText (plugins.isEmpty() ? String ("None on this channel") : plugins.joinIntoString ("\n"), dontSendNotification);
    }

    void MixerView::chooseOutput (tracktion::AudioTrack& channel, tracktion::AudioTrack* destination)
    {
        auto& edit = session.getEdit();
        editing::begin (edit, "Route " + channel.getName());

        if (const auto r = editing::setOutput (channel, destination); r.failed())
        {
            if (onStatus) onStatus (r.getErrorMessage());
        }
        else if (onStatus)
        {
            onStatus (channel.getName() + " plays into " + (destination != nullptr ? destination->getName() : String ("master")));
        }

        refreshColumn();
        repaint();
    }

    void MixerView::showOutputMenu (tracktion::AudioTrack& channel, Component& from)
    {
        PopupMenu m;
        auto* into = editing::outputOf (channel);
        auto* held = &channel;
        m.addItem ("Master", true, into == nullptr, [this, held] { chooseOutput (*held, nullptr); });
        m.addSeparator();

        for (auto* other : editing::channels (session.getEdit()))
            if (other != &channel)
            {
                const auto loops = other->getOutput().feedsInto (&channel);
                m.addItem (other->getName() + (loops ? String (" (plays into this one)") : String()), ! loops, into == other,
                           [this, held, other] { chooseOutput (*held, other); });
            }

        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&from));
    }

    void MixerView::showMenu (tracktion::AudioTrack& channel)
    {
        auto& edit = session.getEdit();
        auto* held = &channel;
        const auto index = editing::channels (edit).indexOf (&channel);
        const auto isInput = &channel == &session.getTrack();

        PopupMenu m;
        PopupMenu colours;
        const std::pair<const char*, const Colour*> named[] = { { "Sky", &theme::laneSky }, { "Tide", &theme::laneTide }, { "Kelp", &theme::laneKelp },
                                                                { "Sand", &theme::laneSand }, { "Coral", &theme::laneCoral }, { "Murex", &theme::laneMurex },
                                                                { "Rose", &theme::laneRose }, { "Slate", &theme::laneSlate } };
        const auto current = channel.state[colourProp].toString();
        colours.addItem ("Automatic (by its place)", true, current.isEmpty(), [&edit, held]
        {
            editing::begin (edit, "Channel colour");
            held->state.removeProperty (colourProp, &edit.getUndoManager());
        });
        colours.addSeparator();

        for (const auto& [label, colour] : named)
        {
            PopupMenu::Item item (label);
            item.colour = *colour;
            item.isTicked = current == colour->toString();
            item.action = [&edit, held, c = *colour]
            {
                editing::begin (edit, "Channel colour");
                held->state.setProperty (colourProp, c.toString(), &edit.getUndoManager());
            };
            colours.addItem (item);
        }

        m.addSubMenu ("Colour", colours);
        m.addItem ("Move left", index > 0, false, [&edit, held, index] { editing::begin (edit, "Move channel"); editing::moveChannel (*held, index - 1); });
        m.addItem ("Move right", index < editing::channels (edit).size() - 1, false, [&edit, held, index] { editing::begin (edit, "Move channel"); editing::moveChannel (*held, index + 1); });
        m.addSeparator();
        m.addItem ("Add a channel after it", [this, &edit, held]
        {
            editing::begin (edit, "Add channel");
            if (auto* added = editing::addChannel (edit, held))
                select (added);
        });
        m.addItem (isInput ? "Delete channel (the input plays here)" : "Delete channel", ! isInput, false, [this, &edit, held]
        {
            const auto name = held->getName();
            editing::begin (edit, "Delete channel");
            selected = {};
            editing::deleteChannel (*held);

            if (onStatus)
                onStatus ("Deleted " + name + "; what played into it plays to master. Undo brings it back.");
        });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void MixerView::timerCallback()
    {
        if (rebuildPending)
            rebuild();

        if (! isShowing())
            return;

        for (auto* s : strips)
            s->tick();
    }

    void MixerView::paint (Graphics& g)
    {
        g.fillAll (theme::bg);
        g.setColour (theme::line);
        g.fillRect (getWidth() - columnWidth - 1, 0, 1, getHeight());
    }

    void MixerView::resized()
    {
        auto r = getLocalBounds();
        auto column = r.removeFromRight (columnWidth).reduced (theme::space4, theme::space3);
        r = r.reduced (theme::space2);

        // Add channel along the bottom; master and the strips share one height above it.
        auto bottom = r.removeFromBottom (28);
        r.removeFromBottom (theme::space1);
        const auto height = r.getHeight() - viewport.getScrollBarThickness();

        if (! strips.isEmpty())
        {
            strips.getFirst()->setBounds (r.removeFromLeft (masterWidth).withHeight (height));   // master, pinned
            r.removeFromLeft (theme::space2);
        }

        addButton.setBounds (bottom.withX (r.getX()).withWidth (120).reduced (0, 2));
        viewport.setBounds (r);
        stripHolder.setSize (std::max (r.getWidth(), (strips.size() - 1) * (stripWidth + stripGap)), height);

        for (int i = 1; i < strips.size(); ++i)
            strips[i]->setBounds ((i - 1) * (stripWidth + stripGap), 0, stripWidth, height);

        columnName.setBounds (column.removeFromTop (28));
        column.removeFromTop (theme::space2);
        columnInto.setBounds (column.removeFromTop (18));
        output.setBounds (column.removeFromTop (28));
        column.removeFromTop (theme::space4);
        columnFrom.setBounds (column.removeFromTop (18));
        columnFromList.setBounds (column.removeFromTop (std::min (160, column.getHeight() / 2)));
        column.removeFromTop (theme::space3);
        columnPlugins.setBounds (column.removeFromTop (18));
        columnPluginList.setBounds (column);
    }
}
