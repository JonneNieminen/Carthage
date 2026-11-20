#include "MixerView.h"

#include "PluginBrowser.h"

#include <engine/Editing.h>
#include <engine/SampleEdits.h>

namespace carthage::ui
{
    using namespace juce;

    namespace
    {
        constexpr int stripWidth = 64, masterWidth = 80, stripGap = 2, columnWidth = 248, cableBand = 18;
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
    class MixerView::Strip final : public Component, public SettableTooltipClient, public DragAndDropTarget
    {
    public:
        Strip (MixerView& v, tracktion::AudioTrack* t, int indexIn) : view (v), track (t), index (indexIn),
                                                                      pre (t != nullptr && t == editing::preMaster (v.session.getEdit()))
        {
            auto& edit = view.session.getEdit();
            volume = track != nullptr ? tracktion::VolumeAndPanPlugin::Ptr (editing::faderOf (*track)) : edit.getMasterVolumePlugin();

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
            fader.getProperties().set ("fader", true);   // the larger cap (owner)
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

            pan.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);   // a half ring (owner), filled from the centre
            pan.getProperties().set ("bipolar", true);
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

            sendLevel.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
            sendLevel.setTextBoxStyle (Slider::NoTextBox, true, 0, 0);
            sendLevel.setRange (minDb, maxDb, 0.1);
            sendLevel.setSkewFactorFromMidPoint (-12.0);
            sendLevel.setDefault (0.0);
            sendLevel.setTooltip ("How much of the selected channel goes into this one. Double-click: 0 dB.");
            sendLevel.onDragStart = [this] { editing::begin (view.session.getEdit(), "Send level"); };
            sendLevel.onValueChange = [this]
            {
                if (auto* from = view.selectedChannel(); from != nullptr && track != nullptr)
                    editing::setSendLevel (*from, *track, static_cast<float> (sendLevel.getValue()));
            };
            addChildComponent (sendLevel);

            // The wheel adjusts only the selected channel's controls; over any other strip it
            // scrolls the mixer (owner, W51).
            for (auto* control : { &fader, &pan, &sendLevel })
                control->acceptsWheel = [this, control] { return control == &sendLevel ? true : isSelected(); };

            readModel();
            setTooltip (track != nullptr ? "Click to select. Right-click to rename, colour, route or delete it." : "Master: everything you hear goes through it");
        }

        ~Strip() override
        {
            if (peakMeter != nullptr) peakMeter->measurer.removeClient (peakClient);
            if (rmsMeter != nullptr)  rmsMeter->measurer.removeClient (rmsClient);
        }

        tracktion::AudioTrack* getTrack() const noexcept   { return track; }

        bool isSelected() const   { return track != nullptr ? view.selected == track->itemID : view.masterSelected; }

        /** Cmd+wheel zooms the strips, as on the lanes; a plain wheel goes on to the mixer's scrolling. */
        void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
        {
            if (e.mods.isCommandDown())
                view.zoomBy (std::pow (1.15f, w.deltaY * 8.0f));
            else
                Component::mouseWheelMove (e, w);
        }

        void mouseMagnify (const MouseEvent&, float scale) override   { view.zoomBy (scale); }

        /** A channel's strip: not master's, not the Pre-Master's (no input, route or mute here). */
        bool channelStrip() const noexcept   { return track != nullptr && ! pre; }

        /** The route button and send knob follow the selected channel. */
        void refreshRoute()
        {
            auto* from = view.selectedChannel();
            // Routes are between channels and master; the Pre-Master is passed through, not chosen.
            const auto shown = from != nullptr && from != track && ! pre && from != editing::preMaster (view.session.getEdit());
            route = shown ? view.routeTo (track) : Route::none;
            showsRoute = shown;
            sendLevel.setVisible (route == Route::send);

            if (route == Route::send)
                for (const auto& send : editing::sendsOf (*from))
                    if (send.to == track)
                        sendLevel.setValue (std::clamp (send.gainDb, minDb, maxDb), dontSendNotification);

            repaint();
        }

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
            const auto chosen = track != nullptr ? view.selected == track->itemID : view.masterSelected;

            g.setColour (! channelStrip() ? theme::card : theme::bg2);
            g.fillRoundedRectangle (r, theme::radiusSm);

            if (chosen || dropHover)
            {
                g.setColour (theme::accent);   // the selected channel, or where a plugin would land: live
                g.drawRoundedRectangle (r.reduced (0.75f), theme::radiusSm, dropHover ? 2.5f : 1.5f);
            }

            // Colour, name, where it goes.
            const auto colour = channelStrip() ? colourOf (*track, index) : theme::text;
            g.setColour (track != nullptr && editing::isChannelMuted (*track) ? colour.withMultipliedAlpha (0.35f) : colour);
            g.fillRoundedRectangle (colourBar.toFloat(), 1.5f);

            g.setColour (theme::text);
            g.setFont (theme::denseFont (theme::textSm, true));
            g.drawFittedText (track != nullptr ? shortName (track->getName()) : String ("Master"), nameArea, Justification::centred, 1, 0.7f);

            if (channelStrip())
            {
                auto* into = editing::outputOf (*track);
                g.setColour (outputArea.contains (getMouseXYRelative()) && isMouseOver() ? theme::text : theme::muted);
                g.setFont (theme::denseFont (theme::textXs));
                g.drawFittedText (String::fromUTF8 ("\xe2\x86\x92 ") + (into != nullptr ? shortName (into->getName()) : String ("Master")),
                                  outputArea, Justification::centred, 1, 0.8f);

                // Its input: where it comes from, lit while heard (live).
                const auto in = editing::inputOf (*track);
                const auto heard = in.isSet() && editing::isMonitoring (*track);
                g.setColour (heard ? theme::accentText : in.isSet() ? theme::muted : theme::faint);
                g.setFont (theme::denseFont (theme::textXs, heard));
                g.drawFittedText ((in.isSet() ? String::fromUTF8 ("\xe2\x86\x90 ") + view.inputName (in) : String::fromUTF8 ("\xe2\x86\x90 no input")),
                                  inputArea, Justification::centred, 1, 0.8f);

                pill (g, muteArea, "M", editing::isChannelMuted (*track), theme::warn);
                pill (g, soloArea, "S", editing::isChannelSoloed (*track), theme::ok);
            }

            if (track == nullptr)
            {
                auto& dm = view.session.getEngine().getDeviceManager();
                auto* out = dm.getDefaultWaveOutDevice();
                g.setColour (outputArea.contains (getMouseXYRelative()) && isMouseOver() ? theme::text : theme::muted);
                g.setFont (theme::denseFont (theme::textXs));
                g.drawFittedText (String::fromUTF8 ("\xe2\x86\x92 ") + (out != nullptr ? out->getName() : String ("no output")),
                                  outputArea, Justification::centred, 1, 0.7f);
            }

            // The figure under the fader, in the time face: it holds still as it changes.
            g.setColour (theme::muted);
            g.setFont (theme::timeFont (theme::textXs));
            const auto db = fader.getValue();
            g.drawText (db <= minDb + 0.05 ? String::fromUTF8 ("\xe2\x88\x92\xe2\x88\x9e") : String (db, 1), figureArea, Justification::centred, false);

            paintMeter (g);
            juce::ignoreUnused (edit);

            if (showsRoute)
            {
                // FL's route button: an arrow into this strip, lit when the selected channel's
                // sound comes here (accent: live); "SC" when it keys a plugin here.
                const auto lit = route != Route::none;
                auto button = (route == Route::send ? routeArea.withWidth (routeArea.getWidth() / 2) : routeArea).reduced (6, 2).toFloat();
                g.setColour (lit ? theme::accent : theme::well);
                g.fillRoundedRectangle (button, theme::radiusXs);

                if (! lit)
                {
                    g.setColour (theme::line);
                    g.drawRoundedRectangle (button.reduced (0.5f), theme::radiusXs, theme::stroke);
                }

                g.setColour (lit ? theme::onAccent : theme::muted);

                if (route == Route::sidechain)
                {
                    g.setFont (theme::uiFont (theme::textXs, true));
                    g.drawText ("SC", button, Justification::centred, false);
                }
                else
                {
                    const auto c = button.getCentre();
                    Path arrow;   // up into the strip
                    arrow.startNewSubPath (c.x, c.y + 4.0f);
                    arrow.lineTo (c.x, c.y - 4.0f);
                    arrow.startNewSubPath (c.x - 3.5f, c.y - 0.5f);
                    arrow.lineTo (c.x, c.y - 4.0f);
                    arrow.lineTo (c.x + 3.5f, c.y - 0.5f);
                    g.strokePath (arrow, PathStrokeType (1.5f, PathStrokeType::curved, PathStrokeType::rounded));
                }
            }
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (theme::space1, theme::space1);
            colourBar = r.removeFromTop (3);
            r.removeFromTop (theme::space1);
            nameArea = r.removeFromTop (16);

            if (channelStrip())
            {
                inputArea = r.removeFromTop (14);
                outputArea = r.removeFromTop (14);
            }

            r.removeFromTop (theme::space1);
            pan.setBounds (r.removeFromTop (24).withSizeKeepingCentre (36, 24));
            r.removeFromTop (theme::space1);

            if (channelStrip())
            {
                auto pills = r.removeFromTop (18);
                muteArea = pills.removeFromLeft (pills.getWidth() / 2).reduced (2, 0);
                soloArea = pills.reduced (2, 0);
                r.removeFromTop (theme::space1);
            }

            if (track == nullptr)
                outputArea = r.removeFromBottom (14);   // master's output: the interface (owner)

            // The route button (and a send's knob) at the foot: shown while another channel is selected.
            routeArea = r.removeFromBottom (22);
            sendLevel.setBounds (routeArea.withTrimmedLeft (routeArea.getWidth() / 2).withSizeKeepingCentre (26, 20));
            r.removeFromBottom (theme::space1);
            figureArea = r.removeFromBottom (16);
            r.removeFromBottom (theme::space1);
            meterArea = r.removeFromRight (channelStrip() ? 14 : 18);
            fader.setBounds (r);
        }

        void mouseDown (const MouseEvent& e) override
        {
            if (showsRoute && routeArea.contains (e.getPosition()) && ! sendLevel.getBounds().contains (e.getPosition()))
            {
                if (e.mods.isPopupMenu() && track != nullptr)
                    view.showRouteMenu (*track, *this);
                else
                    view.toggleRoute (track, e.mods.isCommandDown());

                return;   // routing does not change the selection: the source stays selected
            }

            if (track == nullptr)
            {
                view.selectMaster();   // master has a Line too (owner)

                if (outputArea.contains (e.getPosition()))
                {
                    PopupMenu m;
                    auto& dm = view.session.getEngine().getDeviceManager();

                    for (int i = 0; i < dm.getNumWaveOutDevices(); ++i)
                        if (auto* wo = dm.getWaveOutDevice (i))
                            m.addItem (wo->getName(), true, wo->getDeviceID() == dm.getDefaultWaveOutDeviceID(),
                                       [&dm, id = wo->getDeviceID(), this] { dm.setDefaultWaveOutDevice (id); repaint(); });

                    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this));
                }

                return;
            }

            view.select (track);

            if (pre)
                return;   // the Pre-Master: selected for its Line; nothing else to set here

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
            else if (inputArea.contains (e.getPosition()))
            {
                PopupMenu m;
                view.fillInputs (m, *track);
                m.showMenuAsync (PopupMenu::Options().withTargetComponent (this));
            }
            else if (meterArea.contains (e.getPosition()))
                clipped = false;   // a click on the meter clears its clip light

            repaint();
        }

        void mouseDoubleClick (const MouseEvent& e) override
        {
            if (! channelStrip() || ! nameArea.contains (e.getPosition()))
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

        // A plugin from the browser, or an insert from a Line, dropped on the strip: to the end of
        // this channel's Line (owner: drag plugins onto strips).
        bool isInterestedInDragSource (const SourceDetails& d) override
        {
            const auto text = d.description.toString();
            return text.startsWith (PluginBrowser::dragPrefix) || text.startsWith (MixerView::insertDragPrefix);
        }

        void itemDragEnter (const SourceDetails&) override   { dropHover = true; repaint(); }
        void itemDragExit (const SourceDetails&) override    { dropHover = false; repaint(); }

        void itemDropped (const SourceDetails& d) override
        {
            dropHover = false;
            const auto text = d.description.toString();

            if (text.startsWith (PluginBrowser::dragPrefix))
            {
                if (const auto* e = view.session.getPluginLibrary().findEntry (text.fromFirstOccurrenceOf (PluginBrowser::dragPrefix, false, false)))
                    view.addPlugin (track, -1, e->description);
            }
            else if (auto* plugin = view.draggedInsert (d.description))
            {
                auto& edit = view.session.getEdit();
                editing::begin (edit, "Move " + plugin->getName());
                editing::moveInsert (*plugin, editing::pluginListOf (edit, track), -1);
                track != nullptr ? view.select (track) : view.selectMaster();
            }

            repaint();
        }

    private:
        MixerView& view;
        bool dropHover = false;
        tracktion::AudioTrack* track;
        int index;
        bool pre;   // the Pre-Master's strip
        tracktion::VolumeAndPanPlugin::Ptr volume;
        juce::ReferenceCountedObjectPtr<tracktion::LevelMeterPlugin> peakMeter, rmsMeter;   // held: the clients are inside them
        tracktion::LevelMeasurer::Client peakClient, rmsClient;
        std::array<float, 2> peakDb { -100.0f, -100.0f }, rmsDb { -100.0f, -100.0f };
        bool clipped = false;

        ValueSlider fader { 0.0 }, pan { 0.0 }, sendLevel { 0.0 };
        Route route = Route::none;
        bool showsRoute = false;
        Rectangle<int> routeArea;
        Rectangle<int> colourBar, nameArea, outputArea, inputArea, muteArea, soloArea, meterArea, figureArea;

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
    /** The strips side by side, and under them the selected channel's cables (W51): a line from
        it to every channel its sound reaches, solid for outputs and sends, dashed for sidechains. */
    class MixerView::StripHolder final : public Component
    {
    public:
        explicit StripHolder (MixerView& v) : view (v) {}

        void paint (Graphics& g) override
        {
            auto* from = view.selectedChannel();
            const Strip* source = nullptr;
            struct Drop { float x; Route kind; };
            std::vector<Drop> drops;

            for (auto* s : view.strips)
            {
                if (s->getTrack() == nullptr || s->getParentComponent() != this)
                    continue;

                if (s->getTrack() == from)
                    source = s;
                else if (from != nullptr)
                    if (const auto kind = view.routeTo (s->getTrack()); kind != Route::none)
                        drops.push_back ({ static_cast<float> (s->getBounds().getCentreX()), kind });
            }

            if (source == nullptr || drops.empty())
                return;

            const auto y = static_cast<float> (getHeight()) - static_cast<float> (cableBand) * 0.5f;
            const auto top = static_cast<float> (getHeight() - cableBand);
            const auto sx = static_cast<float> (source->getBounds().getCentreX());
            auto left = sx, right = sx;

            for (const auto& d : drops)
            {
                left = std::min (left, d.x);
                right = std::max (right, d.x);
            }

            g.setColour (theme::accent.withAlpha (0.8f));
            g.drawLine (left, y, right, y, 1.5f);
            g.fillEllipse (Rectangle<float> (7.0f, 7.0f).withCentre ({ sx, y }));   // where it comes from
            g.drawLine (sx, top, sx, y, 1.5f);

            for (const auto& d : drops)
            {
                Path stub;
                stub.startNewSubPath (d.x, y);
                stub.lineTo (d.x, top + 1.0f);

                if (d.kind == Route::sidechain)
                {
                    const float dashes[] = { 3.0f, 3.0f };
                    PathStrokeType (1.5f).createDashedStroke (stub, stub, dashes, 2);
                    g.fillPath (stub);
                }
                else
                {
                    g.strokePath (stub, PathStrokeType (1.5f));
                }

                Path head;   // an arrow up into the destination strip
                head.addTriangle (d.x - 4.0f, top + 5.0f, d.x + 4.0f, top + 5.0f, d.x, top);
                g.fillPath (head);
            }
        }

    private:
        MixerView& view;
    };

    //==============================================================================================
    /** The selected channel's Line (5b, W51): every plugin on it in order and one empty slot
        after them, so it grows as plugins are added. A click on a plugin opens its window; its
        dot bypasses it; its knob mixes wet and dry; drag to reorder, or onto another channel's
        strip. Double-click the empty slot to choose a plugin; drop one from the browser anywhere. */
    class MixerView::InsertLine final : public Component, public DragAndDropTarget
    {
    public:
        explicit InsertLine (MixerView& v) : view (v) {}

        void setChannel (tracktion::AudioTrack* channel)   { track = channel; refresh(); }

        void refresh()
        {
            mixes.clear();
            auto& edit = view.session.getEdit();
            const auto inserts = editing::insertsOf (editing::pluginListOf (edit, track));

            for (auto* p : inserts)
            {
                auto* knob = mixes.add (std::make_unique<ValueSlider> (1.0));
                knob->setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
                knob->setTextBoxStyle (Slider::NoTextBox, true, 0, 0);
                knob->setRange (0.0, 1.0, 0.01);
                knob->setValue (editing::mixOf (*p), dontSendNotification);
                knob->setVisible (editing::hasMix (*p));
                knob->setTooltip ("Wet and dry: how much of " + p->getName() + " you hear. Double-click: all wet.");
                tracktion::Plugin::Ptr held (p);
                knob->onDragStart = [&edit, held] { editing::begin (edit, held->getName() + " mix"); };
                knob->onValueChange = [k = knob, held] { editing::setMix (*held, static_cast<float> (k->getValue())); };
                addAndMakeVisible (knob);
            }

            plugins.clear();
            for (auto* p : inserts)
                plugins.add (p);

            resized();
            repaint();
        }

        void paint (Graphics& g) override
        {
            for (int i = 0; i <= plugins.size(); ++i)
            {
                const auto r = rowBounds (i).toFloat();
                const auto empty = i == plugins.size();
                g.setColour (empty ? theme::well : theme::card);
                g.fillRoundedRectangle (r, theme::radiusSm);

                if (empty)
                {
                    g.setColour (dropSlot == i ? theme::accent : theme::line);
                    g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusSm, dropSlot == i ? 1.5f : theme::stroke);
                    g.setColour (theme::muted);
                    g.setFont (theme::uiFont (theme::textSm));
                    g.drawText ("Double-click to add a plugin", r.reduced (theme::space2, 0), Justification::centredLeft, true);
                    continue;
                }

                auto* p = plugins[i].get();
                const auto bypassed = editing::isBypassed (*p);
                const auto dot = dotBounds (i).toFloat();
                g.setColour (bypassed ? theme::faint : theme::ok);
                bypassed ? g.drawEllipse (dot.reduced (1.0f), 1.5f) : g.fillEllipse (dot);

                g.setColour (bypassed ? theme::muted : theme::text);
                g.setFont (theme::uiFont (theme::textSm, ! bypassed));
                g.drawText (p->getName(), nameBounds (i), Justification::centredLeft, true);

                if (dropSlot == i)
                {
                    g.setColour (theme::accent);   // where a dragged plugin goes: before this one
                    g.fillRect (r.withHeight (2.0f).translated (0.0f, -3.0f));
                }
            }
        }

        void resized() override
        {
            for (int i = 0; i < mixes.size(); ++i)
                mixes[i]->setBounds (rowBounds (i).removeFromRight (rowHeight).reduced (3));
        }

        void mouseDown (const MouseEvent& e) override
        {
            const auto i = rowAt (e.getPosition());
            dragging = -1;

            if (i < 0 || i >= plugins.size())
                return;

            auto& plugin = *plugins[i];
            auto& edit = view.session.getEdit();

            if (e.mods.isPopupMenu())
            {
                showMenu (plugin);
                return;
            }

            if (dotBounds (i).expanded (4).contains (e.getPosition()))
            {
                editing::begin (edit, editing::isBypassed (plugin) ? "Turn on " + plugin.getName() : "Bypass " + plugin.getName());
                editing::setBypassed (plugin, ! editing::isBypassed (plugin));
                repaint();
                return;
            }

            dragging = i;   // a drag moves it; a click without one opens it
        }

        void mouseDrag (const MouseEvent& e) override
        {
            if (dragging < 0 || e.getDistanceFromDragStart() < 5)
                return;

            const auto id = plugins[dragging]->itemID.toString();
            dragging = -1;

            if (auto* container = DragAndDropContainer::findParentDragContainerFor (this))
                container->startDragging (String (MixerView::insertDragPrefix) + id, this);
        }

        void mouseUp (const MouseEvent& e) override
        {
            if (dragging >= 0 && e.getDistanceFromDragStart() < 5)
                view.windows.open (*plugins[dragging]);

            dragging = -1;
        }

        void mouseDoubleClick (const MouseEvent& e) override
        {
            if (rowAt (e.getPosition()) != plugins.size())
                return;

            auto* channel = track;
            auto& v = view;
            auto chooser = std::make_unique<PluginChooser> (view.session.getPluginLibrary(), [&v, channel] (const PluginDescription& d) { v.addPlugin (channel, -1, d); });
            CallOutBox::launchAsynchronously (std::move (chooser), localAreaToGlobal (rowBounds (plugins.size())), nullptr);
        }

        bool isInterestedInDragSource (const SourceDetails& d) override
        {
            const auto text = d.description.toString();
            return text.startsWith (PluginBrowser::dragPrefix) || text.startsWith (MixerView::insertDragPrefix);
        }

        void itemDragMove (const SourceDetails& d) override
        {
            dropSlot = std::clamp (slotAt (d.localPosition.y), 0, plugins.size());
            repaint();
        }

        void itemDragExit (const SourceDetails&) override   { dropSlot = -1; repaint(); }

        void itemDropped (const SourceDetails& d) override
        {
            const auto slot = std::clamp (slotAt (d.localPosition.y), 0, plugins.size());
            dropSlot = -1;
            const auto text = d.description.toString();
            auto& edit = view.session.getEdit();

            if (text.startsWith (PluginBrowser::dragPrefix))
            {
                if (const auto* e = view.session.getPluginLibrary().findEntry (text.fromFirstOccurrenceOf (PluginBrowser::dragPrefix, false, false)))
                    view.addPlugin (track, slot, e->description);
            }
            else if (auto* plugin = view.draggedInsert (d.description))
            {
                editing::begin (edit, "Move " + plugin->getName());
                editing::moveInsert (*plugin, editing::pluginListOf (edit, track), slot);
            }

            refresh();
        }

    private:
        MixerView& view;
        tracktion::AudioTrack* track = nullptr;   // null: master
        juce::ReferenceCountedArray<tracktion::Plugin> plugins;
        OwnedArray<ValueSlider> mixes;
        int dragging = -1, dropSlot = -1;
        static constexpr int rowHeight = 28, rowGap = 4;

        Rectangle<int> rowBounds (int i) const   { return { 0, i * (rowHeight + rowGap), getWidth(), rowHeight }; }
        Rectangle<int> dotBounds (int i) const   { return rowBounds (i).removeFromLeft (rowHeight).withSizeKeepingCentre (10, 10); }
        Rectangle<int> nameBounds (int i) const  { return rowBounds (i).withTrimmedLeft (rowHeight).withTrimmedRight (rowHeight + theme::space1); }

        int rowAt (Point<int> p) const
        {
            for (int i = 0; i <= plugins.size(); ++i)
                if (rowBounds (i).contains (p))
                    return i;

            return -1;
        }

        int slotAt (int y) const   { return (y + rowHeight / 2) / (rowHeight + rowGap); }

        void showMenu (tracktion::Plugin& plugin)
        {
            auto& edit = view.session.getEdit();
            tracktion::Plugin::Ptr held (&plugin);
            auto* windowsPtr = &view.windows;

            PopupMenu m;
            m.addItem ("Open", [windowsPtr, held] { windowsPtr->open (*held); });
            m.addItem (editing::isBypassed (plugin) ? "Turn on" : "Bypass", [this, &edit, held]
            {
                editing::begin (edit, (editing::isBypassed (*held) ? "Turn on " : "Bypass ") + held->getName());
                editing::setBypassed (*held, ! editing::isBypassed (*held));
                repaint();
            });
            if (editing::canSidechain (plugin))
            {
                // Which channel keys it (FL's sidechain): any channel whose sound does not come from here.
                PopupMenu keys;
                auto* current = editing::sidechainSourceOf (plugin);
                auto& v = view;
                keys.addItem ("None", true, current == nullptr, [&edit, held, &v]
                {
                    editing::begin (edit, "Sidechain " + held->getName());
                    [[maybe_unused]] auto r = editing::setSidechainSource (*held, nullptr);
                    v.routesChanged();
                });
                keys.addSeparator();

                for (auto* ch : editing::channels (edit))
                    if (ch != track)
                        keys.addItem (ch->getName(), true, ch == current, [&edit, held, ch, &v]
                        {
                            editing::begin (edit, "Sidechain " + held->getName());
                            const auto r = editing::setSidechainSource (*held, ch);
                            if (v.onStatus) v.onStatus (r.failed() ? r.getErrorMessage() : ch->getName() + " keys " + held->getName());
                            v.routesChanged();
                        });

                m.addSubMenu ("Sidechain from", keys);
            }

            m.addSeparator();
            m.addItem ("Delete", [this, &edit, held, windowsPtr]
            {
                windowsPtr->close (*held);
                editing::begin (edit, "Delete " + held->getName());
                editing::removeInsert (*held);
                refresh();
            });
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition());
        }

    };

    //==============================================================================================
    MixerView::MixerView (engine::Session& s, PluginWindows& w)
        : session (s), windows (w),
          zoom (static_cast<float> (s.getEngine().getPropertyStorage().getPropertiesFile().getDoubleValue ("mixerZoom", 1.0)))
    {
        setOpaque (true);
        stripHolder = std::make_unique<StripHolder> (*this);
        viewport.setViewedComponent (stripHolder.get(), false);
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
        styleLabel (columnFromList, theme::textSm, theme::muted);
        columnFromList.setJustificationType (Justification::topLeft);
        line = std::make_unique<InsertLine> (*this);

        columnInto.setText ("Plays into", dontSendNotification);
        columnInput.setText ("Input", dontSendNotification);
        styleLabel (columnInput, theme::textSm, theme::text, true);
        input.setTooltip ("The interface input this channel takes: plug your guitar in there");
        input.onChange = [this]
        {
            if (auto* channel = selectedChannel())
            {
                session.setChannelInput (*channel, inputForId (input.getSelectedId()));
                repaint();
            }
        };
        hear.setTooltip ("Hear the input through this channel's plugins as you play");
        hear.onClick = [this]
        {
            if (auto* channel = selectedChannel())
            {
                session.setChannelMonitoring (*channel, hear.getToggleState());
                repaint();
            }
        };
        recordHere.setTooltip ("Takes are recorded from this channel's input, onto the recording lane");
        recordHere.onClick = [this]
        {
            if (auto* channel = selectedChannel(); channel != nullptr && recordHere.getToggleState())
            {
                session.setRecordChannel (*channel);

                if (onStatus)
                    onStatus ("Takes now record through " + channel->getName());
            }

            refreshColumn();
        };
        masterOutput.setTooltip ("Where you hear Carthage: the interface's outputs");
        masterOutput.onChange = [this]
        {
            auto& dm = session.getEngine().getDeviceManager();
            if (auto* wo = dm.getWaveOutDevice (masterOutput.getSelectedId() - 1))
                dm.setDefaultWaveOutDevice (wo->getDeviceID());
            repaint();
        };
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

        for (auto* c : std::initializer_list<Component*> { &columnName, &columnInput, &input, &hear, &recordHere, &masterOutput,
                                                           &columnInto, &output, &columnFrom, &columnFromList, &columnPlugins, line.get() })
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
        windows.closeAll();   // the old project's plugins go with it

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
        masterSelected = false;
        rebuild();
    }

    void MixerView::valueTreePropertyChanged (ValueTree& tree, const Identifier& id)
    {
        if (id == tracktion::IDs::enabled)   // a plugin bypassed or turned on (an undo, too)
            line->repaint();

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
        windows.closeMissing();   // a plugin deleted or undone away takes its window with it
        strips.clear();
        stripHolder->removeAllChildren();

        if (! editState.isValid())
            return;

        strips.add (std::make_unique<Strip> (*this, nullptr, -1));   // master
        addAndMakeVisible (*strips.getFirst());
        pinned = 1;

        // The Pre-Master, pinned beside master while it is on (owner).
        if (auto* pm = editing::preMaster (session.getEdit()); pm != nullptr && editing::usesPreMaster (session.getEdit()))
        {
            addAndMakeVisible (strips.add (std::make_unique<Strip> (*this, pm, -2)));
            pinned = 2;
        }

        int i = 0;

        for (auto* channel : editing::channels (session.getEdit()))
        {
            auto* strip = strips.add (std::make_unique<Strip> (*this, channel, i++));
            stripHolder->addAndMakeVisible (strip);
        }

        if (selectedChannel() == nullptr && ! masterSelected)
            if (const auto all = editing::channels (session.getEdit()); ! all.isEmpty())
                selected = all.getFirst()->itemID;

        resized();
        refreshColumn();
        routesChanged();
        repaint();
    }

    tracktion::AudioTrack* MixerView::selectedChannel() const
    {
        for (auto* ch : editing::channels (session.getEdit()))
            if (ch->itemID == selected)
                return ch;

        if (auto* pm = editing::preMaster (session.getEdit()); pm != nullptr && pm->itemID == selected)
            return pm;

        return nullptr;
    }

    void MixerView::selectMaster()
    {
        selected = {};
        masterSelected = true;
        routesChanged();
        refreshColumn();
    }

    void MixerView::addPlugin (tracktion::AudioTrack* channel, int slot, const PluginDescription& description)
    {
        auto& edit = session.getEdit();
        editing::begin (edit, "Add " + description.name);
        const auto r = session.insertPlugin (editing::pluginListOf (edit, channel), slot, description);

        if (onStatus)
            onStatus (r.failed() ? description.name + " could not be loaded: " + r.getErrorMessage()
                                 : description.name + " is on " + (channel != nullptr ? channel->getName() : String ("master")));

        channel != nullptr ? select (channel) : selectMaster();
    }

    void MixerView::addPluginToSelected (const PluginDescription& description)
    {
        auto* channel = selectedChannel();

        if (channel == nullptr && ! masterSelected)
            channel = editing::channels (session.getEdit()).getFirst();

        addPlugin (channel, -1, description);
    }

    MixerView::Route MixerView::routeTo (tracktion::AudioTrack* to)
    {
        auto* from = selectedChannel();

        if (from == nullptr || from == to)
            return Route::none;

        if (to == nullptr)   // master
            return editing::playsToOutput (*from) && editing::outputOf (*from) == nullptr ? Route::output : Route::none;

        if (editing::playsToOutput (*from) && editing::outputOf (*from) == to)
            return Route::output;

        for (const auto& send : editing::sendsOf (*from))
            if (send.to == to)
                return Route::send;

        for (auto* p : editing::insertsOf (*to))
            if (editing::sidechainSourceOf (*p) == from)
                return Route::sidechain;

        return Route::none;
    }

    void MixerView::zoomBy (float factor)
    {
        // Strip widths, as the lanes zoom in time (owner): around the strip under the pointer.
        const auto before = zoom;
        zoom = std::clamp (zoom * factor, 0.75f, 2.5f);

        if (std::abs (zoom - before) < 0.001f)
            return;

        const auto mouseX = viewport.getViewPositionX() + viewport.getMouseXYRelative().x;
        resized();
        viewport.setViewPosition (roundToInt (static_cast<float> (mouseX) * zoom / before) - viewport.getMouseXYRelative().x, 0);
        session.getEngine().getPropertyStorage().getPropertiesFile().setValue ("mixerZoom", static_cast<double> (zoom));
    }

    void MixerView::routesChanged()
    {
        for (auto* s : strips)
            s->refreshRoute();

        if (stripHolder != nullptr)
            stripHolder->repaint();
    }

    void MixerView::toggleRoute (tracktion::AudioTrack* to, bool only)
    {
        auto* from = selectedChannel();

        if (from == nullptr)
            return;

        auto& edit = session.getEdit();
        editing::begin (edit, "Route " + from->getName());
        auto result = Result::ok();
        const auto toName = to != nullptr ? to->getName() : String ("master");

        if (to == nullptr)
        {
            // Master's button: the main output to master, on or off.
            if (editing::outputOf (*from) != nullptr)
            {
                result = editing::setOutput (*from, nullptr);
                editing::setPlaysToOutput (*from, true);
            }
            else
            {
                editing::setPlaysToOutput (*from, ! editing::playsToOutput (*from));
            }
        }
        else if (only)
        {
            // Cmd-click (FL's "route to this track only"): its main output goes here alone.
            editing::removeSend (*from, *to);
            result = editing::setOutput (*from, to);
            if (result.wasOk()) editing::setPlaysToOutput (*from, true);
        }
        else
        {
            switch (routeTo (to))
            {
                case Route::output:    result = editing::setOutput (*from, nullptr); break;   // back to master
                case Route::send:      editing::removeSend (*from, *to); break;
                case Route::sidechain:
                case Route::none:      result = editing::addSend (*from, *to); break;
            }
        }

        if (onStatus)
            onStatus (result.failed() ? result.getErrorMessage() : from->getName() + ": routes changed (" + toName + ")");

        routesChanged();
        refreshColumn();
    }

    void MixerView::showRouteMenu (tracktion::AudioTrack& to, Component& fromComponent)
    {
        auto* from = selectedChannel();

        if (from == nullptr)
            return;

        auto& edit = session.getEdit();
        auto* held = &to;
        PopupMenu m;
        const auto kind = routeTo (&to);
        m.addItem ("Send here", kind != Route::send && kind != Route::output, kind == Route::send, [this, held] { toggleRoute (held, false); });
        m.addItem ("Play only here (main output)", true, kind == Route::output, [this, held] { toggleRoute (held, true); });

        PopupMenu keys;
        for (auto* p : editing::insertsOf (to))
            if (editing::canSidechain (*p))
            {
                tracktion::Plugin::Ptr plugin (p);
                const auto keyed = editing::sidechainSourceOf (*p) == from;
                keys.addItem (p->getName(), true, keyed, [this, &edit, plugin, from, keyed]
                {
                    editing::begin (edit, "Sidechain " + plugin->getName());
                    const auto r = editing::setSidechainSource (*plugin, keyed ? nullptr : from);

                    if (onStatus)
                        onStatus (r.failed() ? r.getErrorMessage()
                                             : keyed ? plugin->getName() + " has no sidechain now"
                                                     : from->getName() + " keys " + plugin->getName());
                    routesChanged();
                });
            }

        m.addSubMenu ("Sidechain into", keys, keys.getNumItems() > 0);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&fromComponent));
    }

    tracktion::Plugin* MixerView::firstPluginOfSelected()
    {
        auto* channel = selectedChannel();

        if (channel == nullptr && ! masterSelected)
            channel = &session.getTrack();

        const auto inserts = editing::insertsOf (editing::pluginListOf (session.getEdit(), channel));
        return inserts.isEmpty() ? nullptr : inserts.getFirst();
    }

    void MixerView::devicesChanged()
    {
        refreshColumn();
        repaint();
    }

    editing::ChannelInput MixerView::inputForId (int id)
    {
        if (id <= 0) return {};
        return { (id - 1) % 1000, id > 1000 };
    }

    int MixerView::idForInput (editing::ChannelInput in)
    {
        if (! in.isSet()) return 1 + 100000;   // "No input"
        return (in.stereo ? 1000 : 0) + in.firstChannel + 1;
    }

    String MixerView::inputName (editing::ChannelInput in) const
    {
        // The interface's own channel names when it has them ("Analogue 1"). A bare number, or
        // nothing (the built-in microphone calls its channel "1"), reads as "Input 1".
        StringArray names;
        if (auto* device = session.getDeviceManager().getCurrentAudioDevice())
            names = device->getInputChannelNames();

        const auto nameOf = [&] (int i)
        {
            const auto n = names[i].trim();
            return n.isEmpty() || n.containsOnly ("0123456789") ? "Input " + String (i + 1) : n;
        };

        return in.stereo ? nameOf (in.firstChannel) + " + " + nameOf (in.firstChannel + 1) : nameOf (in.firstChannel);
    }

    void MixerView::fillInputs (ComboBox& box) const
    {
        box.clear (dontSendNotification);
        box.addItem ("No input", idForInput ({}));
        const auto ins = session.getDeviceManager().getCurrentAudioDevice() != nullptr
                       ? session.getDeviceManager().getCurrentAudioDevice()->getInputChannelNames().size() : 0;

        for (int i = 0; i < ins; ++i)
            box.addItem (inputName ({ i, false }), idForInput ({ i, false }));

        if (ins > 1)
        {
            box.addSeparator();
            for (int i = 0; i + 1 < ins; i += 2)
                box.addItem (inputName ({ i, true }), idForInput ({ i, true }));
        }
    }

    void MixerView::fillInputs (PopupMenu& m, tracktion::AudioTrack& channel)
    {
        const auto current = editing::inputOf (channel);
        auto* held = &channel;
        const auto choose = [this, held] (editing::ChannelInput in) { session.setChannelInput (*held, in); select (held); repaint(); };
        const auto ins = session.getDeviceManager().getCurrentAudioDevice() != nullptr
                       ? session.getDeviceManager().getCurrentAudioDevice()->getInputChannelNames().size() : 0;

        m.addItem ("No input", true, ! current.isSet(), [choose] { choose ({}); });
        m.addSeparator();

        for (int i = 0; i < ins; ++i)
            m.addItem (inputName ({ i, false }), true, current == editing::ChannelInput { i, false }, [choose, i] { choose ({ i, false }); });

        if (ins > 1)
        {
            m.addSeparator();
            for (int i = 0; i + 1 < ins; i += 2)
                m.addItem (inputName ({ i, true }), true, current == editing::ChannelInput { i, true }, [choose, i] { choose ({ i, true }); });
        }

        m.addSeparator();
        m.addItem ("Hear the input", current.isSet(), editing::isMonitoring (channel), [this, held]
        {
            session.setChannelMonitoring (*held, ! editing::isMonitoring (*held));
            select (held);
            repaint();
        });
    }

    tracktion::Plugin* MixerView::draggedInsert (const var& description) const
    {
        const auto id = tracktion::EditItemID::fromVar (description.toString().fromFirstOccurrenceOf (insertDragPrefix, false, false));

        for (auto* p : tracktion::getAllPlugins (session.getEdit(), false))
            if (p->itemID == id)
                return p;

        return nullptr;
    }

    void MixerView::select (tracktion::AudioTrack* channel)
    {
        masterSelected = false;
        selected = channel != nullptr ? channel->itemID : tracktion::EditItemID();
        routesChanged();
        refreshColumn();
    }

    void MixerView::refreshColumn()
    {
        auto* channel = selectedChannel();
        auto& edit = session.getEdit();
        const auto any = channel != nullptr || masterSelected;

        for (auto* c : std::initializer_list<Component*> { &columnName, &columnInput, &input, &hear, &recordHere,
                                                           &columnInto, &output, &columnFrom, &columnFromList, &columnPlugins, line.get() })
            c->setVisible (any);

        masterOutput.setVisible (false);

        if (! any)
            return;

        line->setChannel (channel);   // null: master's Line

        if (channel == nullptr)
        {
            columnName.setText ("Master", dontSendNotification);

            for (auto* c : std::initializer_list<Component*> { &input, &hear, &recordHere, &columnFrom, &columnFromList, &output })
                c->setVisible (false);

            // Master's place in the column: where it goes, the interface's outputs.
            columnInput.setVisible (false);
            columnInto.setVisible (true);
            masterOutput.setVisible (true);
            masterOutput.clear (dontSendNotification);
            auto& dm = session.getEngine().getDeviceManager();

            for (int i = 0; i < dm.getNumWaveOutDevices(); ++i)
                if (auto* wo = dm.getWaveOutDevice (i))
                {
                    masterOutput.addItem (wo->getName(), i + 1);
                    if (wo->getDeviceID() == dm.getDefaultWaveOutDeviceID())
                        masterOutput.setSelectedId (i + 1, dontSendNotification);
                }

            return;
        }

        if (channel == editing::preMaster (edit))
        {
            // The Pre-Master: its Line, and where it goes (always master).
            columnName.setText ("Pre-Master", dontSendNotification);

            for (auto* c : std::initializer_list<Component*> { &columnInput, &input, &hear, &recordHere, &output, &columnFrom, &columnFromList })
                c->setVisible (false);

            columnInto.setVisible (false);
            return;
        }

        fillInputs (input);
        input.setSelectedId (idForInput (editing::inputOf (*channel)), dontSendNotification);
        hear.setToggleState (editing::isMonitoring (*channel), dontSendNotification);
        const auto records = channel == editing::recordChannel (edit);
        recordHere.setToggleState (records, dontSendNotification);
        recordHere.setEnabled (! records && ! session.isRecording());   // one channel records: choose another to move it

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

        if (const auto in = editing::inputOf (*channel); in.isSet())
            from.add (inputName (in) + (editing::isMonitoring (*channel) ? String (" (heard)") : String (" (not heard)")));

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

        // Master, then the Pre-Master close beside it, then a wider gap before the channels.
        for (int i = 0; i < std::min (pinned, strips.size()); ++i)
        {
            strips[i]->setBounds (r.removeFromLeft (masterWidth).withHeight (height - cableBand));
            r.removeFromLeft (i + 1 < pinned ? theme::space1 + 2 : theme::space4);
        }

        addButton.setBounds (bottom.withX (r.getX()).withWidth (120).reduced (0, 2));
        viewport.setBounds (r);
        const auto channelCount = strips.size() - pinned;
        const auto width = roundToInt (static_cast<float> (stripWidth) * zoom);
        stripHolder->setSize (std::max (r.getWidth(), channelCount * (width + stripGap)), height);

        // The strips leave a band under them for the cables (W51).
        for (int i = pinned; i < strips.size(); ++i)
            strips[i]->setBounds ((i - pinned) * (width + stripGap), 0, width, height - cableBand);

        columnName.setBounds (column.removeFromTop (28));
        column.removeFromTop (theme::space2);

        if (masterSelected)
        {
            columnInto.setBounds (column.removeFromTop (18));
            masterOutput.setBounds (column.removeFromTop (28));
            column.removeFromTop (theme::space4);
        }
        else
        {
            columnInput.setBounds (column.removeFromTop (18));
            input.setBounds (column.removeFromTop (28));
            column.removeFromTop (theme::space1);
            hear.setBounds (column.removeFromTop (24));
            recordHere.setBounds (column.removeFromTop (24));
            column.removeFromTop (theme::space3);
            columnInto.setBounds (column.removeFromTop (18));
            output.setBounds (column.removeFromTop (28));
        }
        column.removeFromTop (theme::space4);
        columnFrom.setBounds (column.removeFromTop (18));
        columnFromList.setBounds (column.removeFromTop (std::min (96, column.getHeight() / 3)));
        column.removeFromTop (theme::space3);
        columnPlugins.setBounds (column.removeFromTop (18));
        column.removeFromTop (theme::space1);
        line->setBounds (column);
    }
}
