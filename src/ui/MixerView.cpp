#include "MixerView.h"

#include "PluginBrowser.h"

#include <app/Commands.h>
#include <engine/Editing.h>
#include <engine/SampleEdits.h>

#include <map>

namespace carthage::ui
{
    using namespace juce;

    namespace
    {
        constexpr int stripWidth = 64, masterWidth = 80, stripGap = 2, columnWidth = 248;
        constexpr float minDb = -60.0f, maxDb = 6.0f;
        constexpr int sendsOnlyId = 100000;   // the "Plays into" menu's "Only its sends"

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
            fader.setDefault (track == nullptr ? editing::projectLoudness (edit) : 0.0);   // master: the project's loudness
            fader.setTooltip ("Volume. Drag the cap; double-click it for 0 dB. Scroll anywhere over the selected channel, Shift for fine steps.");
            fader.onlyThumbGrabs = true;   // a click on the strip selects it; only the cap moves the fader (owner)
            fader.onDragStart = [this]
            {
                if (! isSelected())
                    track != nullptr ? view.select (track) : view.selectMaster();

                editing::begin (view.session.getEdit(), nameForUndo() + " volume");
            };
            fader.onWheelStart = [this] { editing::begin (view.session.getEdit(), nameForUndo() + " volume"); };
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
            pan.onWheelStart = pan.onDragStart;
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
            sendLevel.onWheelStart = sendLevel.onDragStart;
            sendLevel.onValueChange = [this]
            {
                if (auto* from = view.selectedChannel(); from != nullptr && track != nullptr)
                    editing::setSendLevel (*from, *track, static_cast<float> (sendLevel.getValue()));

                view.repaintCables();   // the send's tag shows its level
            };
            addChildComponent (sendLevel);

            // The wheel adjusts only the selected channel's controls; over any other strip it
            // scrolls the mixer (owner, W51).
            for (auto* control : { &fader, &pan, &sendLevel })
                control->acceptsWheel = [this, control] { return control == &sendLevel || isSelected(); };

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
            else if (isSelected() && volume != nullptr)
                fader.mouseWheelMove (e.getEventRelativeTo (&fader), w);   // anywhere over the channel in context (owner)
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

            const auto bus = channelStrip() && view.isBus (index);
            g.setColour (! channelStrip() || bus ? theme::card : theme::bg2);   // a bus sits forward, as master does
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

            auto nameBox = nameArea;

            if (bus)
            {
                // "Bus" in a small tag in the channel's colour, beside the name.
                const auto tag = nameBox.removeFromRight (30).reduced (1, 1).toFloat();
                g.setColour (colourOf (*track, index));
                g.drawRoundedRectangle (tag.reduced (0.5f), theme::radiusXs, theme::stroke);
                g.setFont (theme::denseFont (theme::textXs, true));
                g.drawText ("Bus", tag, Justification::centred, false);
            }

            g.setColour (theme::text);
            g.setFont (theme::denseFont (theme::textSm, true));
            g.drawFittedText (track != nullptr ? shortName (track->getName()) : String ("Master"), nameBox, Justification::centred, 1, 0.7f);

            if (channelStrip())
            {
                g.setColour (outputArea.contains (getMouseXYRelative()) && isMouseOver() ? theme::text : theme::muted);
                g.setFont (theme::denseFont (theme::textXs));
                g.drawFittedText (String::fromUTF8 ("\xe2\x86\x92 ") + destinations (true), outputArea, Justification::centred, 1, 0.7f);

                // Its input: where it comes from, lit while heard (live).
                const auto in = editing::inputOf (*track);
                const auto heard = in.isSet() && editing::isMonitoring (*track);
                g.setColour (heard ? theme::accentText : in.isSet() ? theme::muted : theme::faint);
                g.setFont (theme::denseFont (theme::textXs, heard));
                g.drawFittedText ((in.isSet() ? String::fromUTF8 ("\xe2\x86\x90 ") + view.inputName (in) : String::fromUTF8 ("\xe2\x86\x90 no input")),
                                  inputArea, Justification::centred, 1, 0.8f);

                pill (g, muteArea, "M", editing::isChannelMuted (*track), theme::warn);
                pill (g, soloArea, "S", editing::isChannelSoloed (*track), theme::ok);
                pill (g, armArea, "R", editing::isArmed (*track), theme::clip);   // record arm (AGENTS.md: --clip)
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
                const auto button = routeArea.reduced (3, 2).toFloat();   // the strip's whole width (owner: a big target)
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
                    g.setFont (theme::uiFont (theme::textSm, true));
                    g.drawText ("SC", button, Justification::centred, false);
                }
                else
                {
                    const auto c = button.getCentre();
                    Path arrow;   // up into the strip
                    arrow.startNewSubPath (c.x, c.y + 7.0f);
                    arrow.lineTo (c.x, c.y - 7.0f);
                    arrow.startNewSubPath (c.x - 6.0f, c.y - 1.0f);
                    arrow.lineTo (c.x, c.y - 7.0f);
                    arrow.lineTo (c.x + 6.0f, c.y - 1.0f);
                    g.strokePath (arrow, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
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
                const auto third = pills.getWidth() / 3;
                muteArea = pills.removeFromLeft (third).reduced (1, 0);
                soloArea = pills.removeFromLeft (third).reduced (1, 0);
                armArea = pills.reduced (1, 0);
                r.removeFromTop (theme::space1);
            }

            if (track == nullptr)
                outputArea = r.removeFromBottom (14);   // master's output: the interface (owner)

            // The route button at the foot, the strip's full width and tall (owner: easy to hit), and
            // a send's level knob in its own row above it. Both rows are always kept, so nothing
            // moves when a route appears.
            routeArea = r.removeFromBottom (34);
            sendRow = r.removeFromBottom (26);
            sendLevel.setBounds (sendRow.withSizeKeepingCentre (26, 24));
            r.removeFromBottom (theme::space1);
            figureArea = r.removeFromBottom (16);
            r.removeFromBottom (theme::space1);
            meterArea = r.removeFromRight (channelStrip() ? 14 : 18);
            fader.setBounds (r);
        }

        void mouseDown (const MouseEvent& e) override
        {
            // While another channel is selected the foot of this strip is for routing it, and only
            // that: a click there routes and never changes which channel is selected (owner).
            if (showsRoute && routeZone().contains (e.getPosition()))
            {
                if (routeArea.expanded (0, 3).contains (e.getPosition()))
                {
                    if (e.mods.isPopupMenu() && track != nullptr)
                        view.showRouteMenu (*track, *this);
                    else
                        view.toggleRoute (track, e.mods.isAltDown());   // a click routes here or back; Option: a send
                }

                return;   // the send row around its knob does nothing; the source stays selected
            }

            view.grabKeyboardFocus();   // the arrow keys move between channels from here

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

            // As on the lanes: Cmd-click mutes every other channel, or solos this one alone.
            if (muteArea.contains (e.getPosition()))
                e.mods.isCommandDown() ? editing::muteOtherChannels (*track) : editing::setChannelMuted (*track, ! editing::isChannelMuted (*track));
            else if (armArea.contains (e.getPosition()))
            {
                if (view.session.isRecording())
                {
                    if (view.onStatus) view.onStatus ("Stop recording to arm or disarm a channel");
                }
                else
                {
                    view.session.setArmed (*track, ! editing::isArmed (*track));
                    view.refreshColumn();

                    if (! editing::inputOf (*track).isSet() && editing::isArmed (*track) && view.onStatus)
                        view.onStatus (track->getName() + " is armed but has no input: choose one above its name");
                }
            }
            else if (soloArea.contains (e.getPosition()))
                e.mods.isCommandDown() ? editing::soloChannelAlone (*track) : editing::setChannelSoloed (*track, ! editing::isChannelSoloed (*track));
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

            view.repaint();   // a Cmd-click on M or S changes every strip
        }

        void mouseDoubleClick (const MouseEvent& e) override
        {
            if (channelStrip() && nameArea.contains (e.getPosition()))
                view.showRename (*track, localAreaToGlobal (nameArea));
        }

        Rectangle<int> nameBounds() const   { return nameArea; }

        /** What the part under the pointer does (the M and S buttons take Cmd as the lanes do). */
        String getTooltip() override
        {
            const auto pos = getMouseXYRelative();
            const auto cmd = String (app::commands::commandName);

            if (showsRoute && routeArea.expanded (0, 3).contains (pos))
            {
                const auto* from = view.selectedChannel();
                const auto fromName = from != nullptr ? from->getName() : String ("The selected channel");
                const auto here = track != nullptr ? track->getName() : String ("master");
                const auto opt = String (app::commands::optionName);

                switch (route)
                {
                    case Route::output:    return fromName + " plays into " + here + ". Click to stop that.";
                    case Route::send:      return fromName + " also plays into " + here + " (its level is the knob beside this). Click to stop that.";
                    case Route::sidechain: return fromName + " drives a sidechain here. Click to play into " + here + " instead; right-click to change the sidechain.";
                    case Route::none:      break;
                }

                return track == nullptr ? fromName + " is not heard through master. Click to play into master again."
                                        : "Click to play " + fromName + " into " + here + " (as well as anywhere else it plays). " + opt + "-click: a send you set the level of. Right-click: sidechain and more.";
            }

            if (channelStrip())
            {
                if (muteArea.contains (pos))
                    return (editing::isChannelMuted (*track) ? "Unmute this channel" : "Mute this channel") + String (". ") + cmd + "-click: mute every other channel; again: every channel plays";
                if (armArea.contains (pos))
                    return (editing::isArmed (*track) ? "Disarm this channel" : "Arm this channel: R records its input, with every other armed channel, each to its own take");
                if (soloArea.contains (pos))
                    return (editing::isChannelSoloed (*track) ? "Stop soloing this channel" : "Solo this channel") + String (". ") + cmd + "-click: this channel alone; again: no solo";
                if (nameArea.contains (pos))
                    return (view.isBus (index) ? String ("A bus channel: route other channels into it to process them together. ") : String())
                           + "Double-click to rename. Right-click for its colour, to route, move or delete it.";
                if (inputArea.contains (pos))
                    return "The interface input it takes. Click to choose.";
                if (outputArea.contains (pos))
                    return "Plays into " + destinations (false) + ". Click to choose its main output; the route buttons at the strips' feet add more.";
            }

            return SettableTooltipClient::getTooltip();
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
        Rectangle<int> routeArea, sendRow;

        /** The foot that belongs to routing while another channel is selected: the button, the
            send row above it, and a little margin. */
        Rectangle<int> routeZone() const   { return routeArea.getUnion (sendRow).expanded (0, 3).withX (0).withRight (getWidth()); }
        Rectangle<int> colourBar, nameArea, outputArea, inputArea, muteArea, soloArea, armArea, meterArea, figureArea;

        String nameForUndo() const   { return track != nullptr ? track->getName() : String ("Master"); }

        /** Everywhere this channel plays: its main output, then every send (owner: "1, 2, 10").
            Short names for the strip, full ones (joined in words) for its tooltip. */
        String destinations (bool brief) const
        {
            StringArray names;

            if (editing::playsToOutput (*track))
            {
                auto* into = editing::outputOf (*track);

                // Master's route passes through the Pre-Master while it is on: the strip says so (owner).
                if (into != nullptr)
                    names.add (brief ? shortName (into->getName()) : into->getName());
                else if (editing::usesPreMaster (track->edit))
                    names.add (brief ? String ("Pre-Master") : String ("the Pre-Master (on to Master)"));
                else
                    names.add ("Master");
            }

            for (const auto& send : editing::sendsOf (*track))
                if (send.to != nullptr)
                    names.add (brief ? shortName (send.to->getName()) : send.to->getName());

            if (names.isEmpty())
                return brief ? String ("nowhere") : String ("nowhere: its main output is off and it has no sends");

            if (brief || names.size() == 1)
                return names.joinIntoString (", ");

            return names.joinIntoString (", ", 0, names.size() - 1) + " and " + names[names.size() - 1];
        }

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
    /** The selected channel's colour beside its name in the column; a click offers the others. */
    class MixerView::Swatch final : public Component, public SettableTooltipClient
    {
    public:
        explicit Swatch (MixerView& v) : view (v)
        {
            setTooltip ("This channel's colour. Click to choose another.");
            setMouseCursor (MouseCursor::PointingHandCursor);
        }

        void show (tracktion::AudioTrack* channel, Colour c)   { track = channel; colour = c; setVisible (channel != nullptr); repaint(); }

        void paint (Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat().withSizeKeepingCentre (14.0f, 14.0f);
            g.setColour (colour);
            g.fillEllipse (r);
            g.setColour (isMouseOver() ? theme::text : theme::lineStrong);
            g.drawEllipse (r.expanded (2.0f), theme::stroke);
        }

        void mouseEnter (const MouseEvent&) override   { repaint(); }
        void mouseExit (const MouseEvent&) override    { repaint(); }

        void mouseDown (const MouseEvent&) override
        {
            if (track != nullptr)
                view.colourMenu (*track).showMenuAsync (PopupMenu::Options().withTargetComponent (this));
        }

    private:
        MixerView& view;
        tracktion::AudioTrack* track = nullptr;
        Colour colour;
    };

    //==============================================================================================
    /** The channel strips side by side, scrolled by the viewport; the Routing section under them
        follows where each strip is. */
    class MixerView::StripHolder final : public Component
    {
    public:
        explicit StripHolder (MixerView& v) : view (v) {}

        // Scrolling moves the strips: the Routing section's lines follow.
        void moved() override   { view.repaintCables(); }

    private:
        MixerView& view;
    };

    //==============================================================================================
    /** The selected channel's cables (W51, refined at the owner's request) in a band under all the
        strips, master and the Pre-Master included, so a cable arrives in the strip it reaches. Two
        lines: the upper carries what leaves the selected channel (accent: it is the selection),
        the lower what comes into it (in the colour of the channel it comes from). A main output is
        a heavy line, a send a lighter one with its level, a sidechain a dashed one; each ends in
        an arrow into the strip it reaches (or at the edge, pointing on, when that strip is
        scrolled out of view), with a small tag saying what it is. Sound to master goes into the
        Pre-Master while it is on, and on from there into master. Hovering a cable lifts it and
        says in words what it does. */
    //==============================================================================================
    /** The selected channel's Line (5b, W51): every plugin on it in order and one empty slot
        after them, so it grows as plugins are added. A click on a plugin opens its window; its
        dot bypasses it (Ctrl- or Cmd-click: only it plays); its knob is its plugin volume; drag to reorder, or onto another channel's
        strip. Double-click the empty slot to choose a plugin; drop one from the browser anywhere. */
    class MixerView::InsertLine final : public Component, public DragAndDropTarget, public TooltipClient
    {
    public:
        explicit InsertLine (MixerView& v) : view (v) {}

        /** A plugin that delays its sound says by how much, and why it has no plugin volume knob (W56). */
        String getTooltip() override
        {
            const auto i = rowAt (getMouseXYRelative());

            if (i < 0 || i >= plugins.size())
                return {};

            const auto ms = editing::latencyOf (*plugins[i]) * 1000.0;

            if (view.session.isPausedForLowLatency (*plugins[i]))
                return plugins[i]->getName() + " is paused while you play through an armed channel (low-latency monitoring, in Settings > Recording): "
                       "its " + String (ms, 1) + " ms delay would hold back what you hear. It plays again when you stop hearing the input, and exports always use it.";

            if (ms <= 0.0)
                return {};

            return plugins[i]->getName() + " delays its sound by " + String (ms, 1) + " ms (it looks ahead). Carthage delays everything else "
                   "by the same, so the mix stays in time. It has no plugin volume knob, as Carthage would mix the sound before it in early: use its own mix control.";
        }

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
                knob->setTooltip ("Plugin volume: how much of " + p->getName() + "'s sound you hear, against the sound before it. Double-click: full.");
                tracktion::Plugin::Ptr held (p);
                knob->onDragStart = [&edit, held] { editing::begin (edit, held->getName() + " volume"); };
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

                auto name = nameBounds (i);

                if (const auto ms = editing::latencyOf (*p) * 1000.0; ms > 0.0 && ! editing::hasMix (*p))
                {
                    // In the knob's place: how late it is, in the time face.
                    g.setColour (theme::muted);
                    g.setFont (theme::timeFont (theme::textXs));
                    g.drawText (String (ms, ms < 10.0 ? 1 : 0) + " ms", rowBounds (i).removeFromRight (rowHeight + 14).reduced (2, 0),
                                Justification::centredRight, false);
                    name = name.withTrimmedRight (16);
                }

                const auto paused = view.session.isPausedForLowLatency (*p);
                g.setColour (bypassed || paused ? theme::muted : theme::text);
                g.setFont (theme::uiFont (theme::textSm, ! bypassed && ! paused));
                g.drawText (paused ? p->getName() + " (paused)" : p->getName(), name, Justification::centredLeft, true);

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

            // FL-style (owner): Ctrl- or Cmd-click on a plugin's dot or name leaves only it on; again,
            // every plugin on the Line plays. Before the menu: on a Mac Ctrl-click is a right-click.
            const auto onRow = dotBounds (i).expanded (4).contains (e.getPosition()) || nameBounds (i).contains (e.getPosition());

            if (onRow && (e.mods.isCtrlDown() || e.mods.isCommandDown()) && ! e.mods.isRightButtonDown())
            {
                soloPlugin (plugin);
                return;
            }

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

        void soloPlugin (tracktion::Plugin& chosen)
        {
            auto& edit = view.session.getEdit();
            const auto alone = ! editing::isBypassed (chosen)
                               && std::all_of (plugins.begin(), plugins.end(), [&chosen] (tracktion::Plugin* p) { return p == &chosen || editing::isBypassed (*p); });

            editing::begin (edit, alone ? String ("Turn on every plugin") : "Only " + chosen.getName());

            for (auto* p : plugins)
                editing::setBypassed (*p, alone ? false : p != &chosen);

            if (view.onStatus)
                view.onStatus (alone ? String ("Every plugin on this Line plays again")
                                     : "Only " + chosen.getName() + " plays on this Line; Ctrl-click it again for all");
            repaint();
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
        setWantsKeyboardFocus (true);   // Left and Right move between channels (owner)
        stripHolder = std::make_unique<StripHolder> (*this);
        viewport.setViewedComponent (stripHolder.get(), false);
        viewport.setScrollBarsShown (false, true);

        // Pulling an end of the scroll bar zooms the strips, as on the main grid (owner, W61).
        viewport.horizontalBar().onZoomStart = [this] { zoomAtGrab = zoom; };
        viewport.horizontalBar().onZoom = [this] (Range<double> pixels)
        {
            if (pixels.getLength() <= 0.0)
                return;

            zoom = std::clamp (zoomAtGrab * static_cast<float> (viewport.getViewWidth() / pixels.getLength()), 0.75f, 2.5f);
            resized();
            viewport.setViewPosition (roundToInt (pixels.getStart() * static_cast<double> (zoom / zoomAtGrab)), 0);
            session.getEngine().getPropertyStorage().getPropertiesFile().setValue ("mixerZoom", static_cast<double> (zoom));
        };
        addAndMakeVisible (viewport);
        // The Routing section under the strips (owner, W61), drawn as Settings > Mixer says.
        routingSection.setStyle (routing::styleFromId (session.getEngine().getPropertyStorage().getPropertiesFile().getValue ("routingStyle", "story")));
        routingSection.onRoute = [this] (int destination)
        {
            if (destination < 0 || destination >= static_cast<int> (routingTargets.size()))
                return;

            // The Pre-Master is how master is reached while it is on: its column means master.
            auto* to = routingTargets[static_cast<size_t> (destination)];
            toggleRoute (to != nullptr && to == editing::preMaster (session.getEdit()) ? nullptr : to, false);
        };
        addAndMakeVisible (routingSection);

        addButton.setTooltip ("Add a channel at the end");
        addButton.onClick = [this] { addChannels (nullptr, 1); };
        addButton.onRightClick = [this] { addChannelsMenu (nullptr).showMenuAsync (PopupMenu::Options().withTargetComponent (&addButton)); };
        addButton.setTooltip ("Add a channel at the end. Right-click to add several at once.");
        addAndMakeVisible (addButton);

        styleLabel (columnName, theme::textLg, theme::text, true);
        columnName.onTextChange = [this]
        {
            // The column's title renames the channel in place (double-click, as on the strip).
            if (auto* channel = selectedChannel(); channel != nullptr && channel != editing::preMaster (session.getEdit()))
                if (const auto name = columnName.getText().trim(); name.isNotEmpty() && name != channel->getName())
                {
                    editing::begin (session.getEdit(), "Rename channel");
                    channel->setName (name);
                }

            refreshColumn();
        };
        swatch = std::make_unique<Swatch> (*this);
        addChildComponent (*swatch);

        // Level: the strip's fader, here to type a figure into (owner).
        styleLabel (columnLevel, theme::textSm, theme::text, true);
        columnLevel.setText ("Level", dontSendNotification);
        level.setSliderStyle (Slider::LinearHorizontal);
        level.setTextBoxStyle (Slider::TextBoxRight, false, 64, 24);   // click the figure to type one
        level.setRange (minDb, maxDb, 0.1);
        level.setSkewFactorFromMidPoint (-12.0);
        level.setTextValueSuffix (" dB");
        level.setNumDecimalPlacesToDisplay (1);
        level.setTooltip ("This channel's level, as its fader. Drag, or click the figure and type one in dB (-60 to +6); "
                          "double-click returns to its default.");
        level.onDragStart = [this] { if (levelTarget() != nullptr) editing::begin (session.getEdit(), columnName.getText() + " volume"); };
        level.onWheelStart = level.onDragStart;
        level.onValueChange = [this]
        {
            auto* v = levelTarget();

            if (v == nullptr || std::abs (v->getVolumeDb() - static_cast<float> (level.getValue())) < 0.01f)
                return;

            if (! level.isMouseButtonDown())
                editing::begin (session.getEdit(), columnName.getText() + " volume");   // a typed figure: its own undo step

            v->setVolumeDb (static_cast<float> (level.getValue()));
            repaint();
        };
        for (auto* c : std::initializer_list<Component*> { &columnLevel, &level })
            addChildComponent (c);
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
        recordHere.setTooltip ("Armed channels record their input when you press R, each to its own take, on a lane with room for it. "
                               "Arm as many as you play into at once.");
        recordHere.onClick = [this]
        {
            if (auto* channel = selectedChannel())
            {
                session.setArmed (*channel, recordHere.getToggleState());

                if (onStatus)
                    onStatus (recordHere.getToggleState() ? channel->getName() + " is armed: R records its input"
                                                          : channel->getName() + " is no longer armed");
            }

            refreshColumn();
            repaint();
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

            if (id == sendsOnlyId)
            {
                editing::begin (session.getEdit(), "Main output off for " + channel->getName());
                editing::setPlaysToOutput (*channel, false);
                routesChanged();
                return;
            }

            const auto all = editing::channels (session.getEdit());
            editing::begin (session.getEdit(), "Main output on for " + channel->getName());
            editing::setPlaysToOutput (*channel, true);
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
        if (id == Identifier ("carthagePreMasterOn"))   // the Settings switch: the Pre-Master strip comes or goes
            triggerRebuild();

        if (id == tracktion::IDs::enabled)   // a plugin bypassed or turned on (an undo, too)
            line->repaint();

        // Names, colours, routes and mutes repaint; a track becoming a channel rebuilds.
        if (id == tracktion::IDs::name || id == colourProp || id == Identifier ("carthageRole") || id == tracktion::IDs::dst
            || id == tracktion::IDs::mute || id == Identifier ("carthageMuted") || id == Identifier ("carthageSoloed") || id == Identifier ("carthageArmed"))
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

    void MixerView::addChannels (tracktion::AudioTrack* after, int count)
    {
        auto& edit = session.getEdit();
        editing::begin (edit, count == 1 ? String ("Add channel") : "Add " + String (count) + " channels");
        tracktion::AudioTrack* first = nullptr;
        auto* previous = after;

        for (int i = 0; i < count; ++i)
            if (auto* added = editing::addChannel (edit, previous))
            {
                first = first != nullptr ? first : added;
                previous = added;
            }

        if (first == nullptr)
            return;

        select (first);

        if (onStatus)
            onStatus (count == 1 ? "Added " + first->getName() : "Added " + String (count) + " channels, from " + first->getName());
    }

    PopupMenu MixerView::addChannelsMenu (tracktion::AudioTrack* after)
    {
        PopupMenu m;

        for (const auto n : { 1, 2, 4, 8, 16 })
            m.addItem (n == 1 ? String ("1 channel") : String (n) + " channels", [this, after, n] { addChannels (after, n); });

        return m;
    }

    bool MixerView::keyPressed (const KeyPress& key)
    {
        if (key == KeyPress::leftKey || key == KeyPress::rightKey)
        {
            selectNeighbour (key == KeyPress::leftKey ? -1 : 1);
            return true;
        }

        return false;
    }

    void MixerView::selectNeighbour (int step)
    {
        // The strips in their order on screen: master, the Pre-Master while on, the channels.
        int current = -1;

        for (int i = 0; i < strips.size(); ++i)
        {
            auto* t = strips[i]->getTrack();
            if ((t == nullptr && masterSelected) || (t != nullptr && t->itemID == selected))
                current = i;
        }

        const auto next = std::clamp (current < 0 ? pinned : current + step, 0, strips.size() - 1);
        auto* strip = strips[next];
        strip->getTrack() != nullptr ? select (strip->getTrack()) : selectMaster();

        // Keep it in view.
        if (strip->getParentComponent() == stripHolder.get())
        {
            const auto b = strip->getBounds();
            const auto view = viewport.getViewArea();

            if (b.getX() < view.getX())
                viewport.setViewPosition (b.getX() - stripGap, 0);
            else if (b.getRight() > view.getRight())
                viewport.setViewPosition (b.getRight() - view.getWidth() + stripGap, 0);
        }
    }

    int MixerView::busInterval() const
    {
        auto& props = session.getEngine().getPropertyStorage().getPropertiesFile();
        return props.getBoolValue ("busChannels", false) ? std::clamp (props.getIntValue ("busEvery", 10), 2, 64) : 0;
    }

    bool MixerView::isBus (int channelIndex) const
    {
        const auto every = busInterval();
        return every > 0 && channelIndex >= 0 && (channelIndex + 1) % every == 0;
    }

    void MixerView::busesChanged()
    {
        resized();
        repaint();
    }

    void MixerView::repaintCables()
    {
        // The Routing section's picture of the mix: every channel, where its strip is, and where it plays.
        routing::Model m;
        routingTargets.clear();
        auto& edit = session.getEdit();
        auto* pre = editing::usesPreMaster (edit) ? editing::preMaster (edit) : nullptr;
        const auto all = editing::channels (edit);
        std::map<tracktion::AudioTrack*, int> index;
        auto& dm = session.getEngine().getDeviceManager();

        const auto viewLeft = static_cast<float> (routingSection.getLocalPoint (&viewport, Point<int>()).x);
        const auto viewRight = viewLeft + static_cast<float> (viewport.getWidth());

        for (auto* s : strips)
        {
            auto* track = s->getTrack();
            routing::Node n;
            n.x = routingSection.getLocalPoint (s, Point<float> (static_cast<float> (s->getWidth()) * 0.5f, 0.0f)).x;

            if (s->getParentComponent() == stripHolder.get())
            {
                if (n.x < viewLeft + 6.0f)        { n.x = viewLeft + 6.0f; n.beyond = -1; }
                else if (n.x > viewRight - 6.0f)  { n.x = viewRight - 6.0f; n.beyond = 1; }
            }

            if (track == nullptr)
            {
                n.name = "Master";
                n.kind = routing::Node::Kind::master;
                n.colour = theme::text;
                n.levelDb = edit.getMasterVolumePlugin() != nullptr ? edit.getMasterVolumePlugin()->getVolumeDb() : 0.0f;
                n.plugins = static_cast<int> (editing::insertsOf (edit.getMasterPluginList()).size());
            }
            else
            {
                const auto i = all.indexOf (track);
                n.name = track == pre ? String ("Pre-Master") : track->getName();
                n.kind = track == pre ? routing::Node::Kind::preMaster : isBus (i) ? routing::Node::Kind::bus : routing::Node::Kind::channel;
                n.colour = track == pre ? theme::text : colourOf (*track, i);
                n.muted = editing::isChannelMuted (*track);

                if (const auto in = editing::inputOf (*track); in.isSet())
                    n.input = inputName (in);

                if (auto* f = editing::faderOf (*track))
                    n.levelDb = f->getVolumeDb();

                n.plugins = static_cast<int> (editing::insertsOf (*track).size());
            }

            index[track] = static_cast<int> (m.nodes.size());
            m.nodes.push_back (std::move (n));
            routingTargets.push_back (track);

            if ((track == nullptr && masterSelected) || (track != nullptr && ! masterSelected && track->itemID == selected))
                m.selected = static_cast<int> (m.nodes.size()) - 1;
        }

        // Where each plays: a channel bound for master goes into the Pre-Master while it is on.
        const auto masterIndex = index.contains (nullptr) ? index[nullptr] : -1;
        const auto preIndex = pre != nullptr && index.contains (pre) ? index[pre] : -1;

        for (auto& [track, i] : index)
        {
            auto& n = m.nodes[static_cast<size_t> (i)];

            if (track == nullptr)
                continue;

            if (track == pre)
            {
                n.out = masterIndex;
                continue;
            }

            if (editing::playsToOutput (*track))
            {
                auto* into = editing::outputOf (*track);
                n.out = into != nullptr ? (index.contains (into) ? index[into] : -1) : (preIndex >= 0 ? preIndex : masterIndex);
            }

            for (const auto& send : editing::sendsOf (*track))
                if (send.to != nullptr && index.contains (send.to))
                    n.sends.push_back ({ index[send.to], send.gainDb });
        }

        if (auto* out = dm.getDefaultWaveOutDevice())
            m.masterOutput = out->getName();

        routingSection.setModel (std::move (m));
    }

    void MixerView::routesChanged()
    {
        for (auto* s : strips)
            s->refreshRoute();

        repaintCables();
    }

    void MixerView::toggleRoute (tracktion::AudioTrack* to, bool send)
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
        else if (send)
        {
            // Option-click: a copy at its own level, the main output untouched.
            switch (routeTo (to))
            {
                case Route::send:      editing::removeSend (*from, *to); break;
                case Route::output:    result = editing::setOutput (*from, nullptr); if (result.wasOk()) result = editing::addSend (*from, *to); break;
                case Route::sidechain:
                case Route::none:      result = editing::addSend (*from, *to); break;
            }
        }
        else
        {
            // A click (owner): the selected channel plays into this one too, at full level. The
            // first such route is its main output (master no longer hears it directly); more are
            // sends at 0 dB, after its fader, as the main output is. A click on one it already
            // plays into takes that route away; taking away the main one hands it to another
            // route, or back to master when none is left.
            const auto sends = editing::sendsOf (*from);

            switch (routeTo (to))
            {
                case Route::output:
                    if (const auto it = std::find_if (sends.begin(), sends.end(), [] (const editing::Send& sd) { return std::abs (sd.gainDb) < 0.05f; });
                        it != sends.end())
                    {
                        auto* next = it->to;
                        editing::removeSend (*from, *next);
                        result = editing::setOutput (*from, next);
                    }
                    else
                    {
                        result = editing::setOutput (*from, nullptr);
                    }
                    break;
                case Route::send:      editing::removeSend (*from, *to); break;
                case Route::sidechain:
                case Route::none:
                    if (editing::playsToOutput (*from) && editing::outputOf (*from) != nullptr)
                    {
                        result = editing::addSend (*from, *to, 0.0f);   // a second, third... destination
                    }
                    else
                    {
                        result = editing::setOutput (*from, to);
                        if (result.wasOk()) editing::setPlaysToOutput (*from, true);
                    }
                    break;
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
        m.addItem ("Play into this channel (main output)", true, kind == Route::output, [this, held] { toggleRoute (held, false); });
        m.addItem ("Send a copy here", kind != Route::output, kind == Route::send, [this, held] { toggleRoute (held, true); });

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
        if (id <= 0 || id == idForInput ({})) return {};   // "No input" (found by the computer-use test)
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
        const auto& names = deviceInputs();

        const auto nameOf = [&] (int i)
        {
            const auto n = names[i].trim();
            return n.isEmpty() || n.containsOnly ("0123456789") ? "Input " + String (i + 1) : n;
        };

        return in.stereo ? nameOf (in.firstChannel) + " + " + nameOf (in.firstChannel + 1) : nameOf (in.firstChannel);
    }

    const StringArray& MixerView::deviceInputs() const
    {
        // Asking the device for its channel names goes to the driver (CoreAudio): slow enough that
        // asking per strip, per paint and per menu item made moving between channels take 300 ms
        // (owner: the mixer lagged). Read once per device, and again after a few seconds.
        auto* device = session.getDeviceManager().getCurrentAudioDevice();
        const auto now = Time::getMillisecondCounterHiRes();

        if (device != inputNamesDevice || now - inputNamesReadAt > 3000.0)
        {
            inputNames = device != nullptr ? device->getInputChannelNames() : StringArray();
            inputNamesDevice = device;
            inputNamesReadAt = now;
        }

        return inputNames;
    }

    void MixerView::fillInputs (ComboBox& box) const
    {
        box.clear (dontSendNotification);
        box.addItem ("No input", idForInput ({}));
        const auto ins = deviceInputs().size();

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
        const auto ins = deviceInputs().size();

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
        const ScopeGuard relayout { [this] { resized(); } };   // what shows, and the list's height, set the layout
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
        readLevel();
        const auto renamable = channel != nullptr && channel != editing::preMaster (edit);
        columnName.setEditable (false, renamable, false);
        columnName.setTooltip (renamable ? "Double-click to rename this channel" : String());
        swatch->show (renamable ? channel : nullptr,
                      renamable ? colourOf (*channel, editing::channels (edit).indexOf (channel)) : Colour());

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
        recordHere.setToggleState (editing::isArmed (*channel), dontSendNotification);
        recordHere.setEnabled (! session.isRecording());   // what records is fixed while it records

        columnName.setText (channel->getName(), dontSendNotification);

        output.clear (dontSendNotification);
        output.addItem ("Master", 1);
        const auto all = editing::channels (edit);

        for (int i = 0; i < all.size(); ++i)
            if (all[i] != channel)
                output.addItem (all[i]->getName(), i + 2);

        output.addSeparator();
        output.addItem ("Only its sends (main output off)", sendsOnlyId);
        auto* into = editing::outputOf (*channel);
        output.setSelectedId (! editing::playsToOutput (*channel) ? sendsOnlyId : into != nullptr ? all.indexOf (into) + 2 : 1, dontSendNotification);

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
        m.addItem ("Guitar in here: input 1, heard and armed", ! session.isRecording(), false, [this, held]
        {
            session.playInHere (*held, { 0, false });
            select (held);

            if (onStatus)
                onStatus ("You hear input 1 through " + held->getName() + ", and R records it");
        });
        m.addSeparator();
        m.addItem (String::fromUTF8 ("Rename\xe2\x80\xa6"), [this, held]
        {
            for (auto* s : strips)
                if (s->getTrack() == held)
                    showRename (*held, s->localAreaToGlobal (s->nameBounds()));
        });
        m.addSubMenu ("Colour", colourMenu (channel));
        m.addItem ("Move left", index > 0, false, [&edit, held, index] { editing::begin (edit, "Move channel"); editing::moveChannel (*held, index - 1); });
        m.addItem ("Move right", index < editing::channels (edit).size() - 1, false, [&edit, held, index] { editing::begin (edit, "Move channel"); editing::moveChannel (*held, index + 1); });
        m.addSeparator();
        m.addItem ("Add a channel after it", [this, held] { addChannels (held, 1); });
        m.addSubMenu ("Add channels after it", addChannelsMenu (held));
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

    PopupMenu MixerView::colourMenu (tracktion::AudioTrack& channel)
    {
        auto& edit = session.getEdit();
        auto* held = &channel;
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

        return colours;
    }

    void MixerView::showRename (tracktion::AudioTrack& channel, Rectangle<int> screenArea)
    {
        auto* editor = new TextEditor();   // owned by the CallOutBox below
        editor->setText (channel.getName(), false);
        editor->setSize (180, 26);
        editor->setFont (theme::uiFont());
        editor->selectAll();
        auto* held = &channel;
        auto& edit = session.getEdit();
        editor->onReturnKey = [editor, held, &edit]
        {
            if (const auto name = editor->getText().trim(); name.isNotEmpty() && name != held->getName())
            {
                editing::begin (edit, "Rename channel");
                held->setName (name);
            }

            if (auto* box = editor->findParentComponentOfClass<CallOutBox>())
                box->dismiss();
        };
        editor->onEscapeKey = [editor] { if (auto* box = editor->findParentComponentOfClass<CallOutBox>()) box->dismiss(); };
        CallOutBox::launchAsynchronously (std::unique_ptr<Component> (editor), screenArea, nullptr);
        editor->grabKeyboardFocus();
    }

    tracktion::VolumeAndPanPlugin* MixerView::levelTarget() const
    {
        if (auto* channel = selectedChannel())
            return editing::faderOf (*channel);

        return masterSelected ? session.getEdit().getMasterVolumePlugin().get() : nullptr;
    }

    void MixerView::readLevel()
    {
        auto* v = levelTarget();

        if (level.isVisible() != (v != nullptr))
        {
            level.setVisible (v != nullptr);
            columnLevel.setVisible (v != nullptr);
            resized();
        }

        if (v == nullptr)
            return;

        level.setDefault (masterSelected ? editing::projectLoudness (session.getEdit()) : 0.0);

        // Follows the fader, unless the person is dragging it or typing into it here.
        if (! level.isMouseButtonDown() && ! level.hasKeyboardFocus (true))   // its figure is being typed into
            level.setValue (std::clamp (static_cast<double> (v->getVolumeDb()), static_cast<double> (minDb), static_cast<double> (maxDb)), dontSendNotification);

        // The Routing section shows the selected channel's level (the story): keep it current.
        if (std::abs (v->getVolumeDb() - shownLevel) > 0.05f)
        {
            shownLevel = v->getVolumeDb();
            repaintCables();
        }
    }

    void MixerView::setRoutingStyle (routing::Style style)
    {
        routingSection.setStyle (style);
        resized();   // each style wants its own height
    }

    void MixerView::timerCallback()
    {
        if (rebuildPending)
            rebuild();

        readLevel();

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
        drawViewName (g, viewNameArea, "Mixer", theme::bg);
    }

    void MixerView::resized()
    {
        auto r = getLocalBounds();
        auto column = r.removeFromRight (columnWidth).reduced (theme::space4, theme::space3);
        r = r.reduced (theme::space2);

        // Add channel along the bottom; master and the strips share one height above it.
        auto bottom = r.removeFromBottom (28);
        r.removeFromBottom (theme::space1);
        // The Routing section under the strips, as tall as its drawing wants, within reason.
        const auto sectionHeight = std::clamp (routingSection.idealHeight(), 140, std::max (140, r.getHeight() * 45 / 100));
        routingSection.setBounds (r.removeFromBottom (sectionHeight));
        r.removeFromBottom (theme::space2);
        const auto height = r.getHeight() - viewport.getScrollBarThickness();

        // Master, then the Pre-Master close beside it, then a wider gap before the channels.
        for (int i = 0; i < std::min (pinned, strips.size()); ++i)
        {
            strips[i]->setBounds (r.removeFromLeft (masterWidth).withHeight (height));
            r.removeFromLeft (i + 1 < pinned ? theme::space1 + 2 : theme::space4);
        }

        addButton.setBounds (bottom.withX (r.getX()).withWidth (120).reduced (0, 2));
        viewNameArea = bottom;
        viewport.setBounds (r);
        const auto width = roundToInt (static_cast<float> (stripWidth) * zoom);
        const auto busWidth = roundToInt (static_cast<float> (width) * 1.5f);   // a bus stands out by its size

        // The strips leave a band under them for the cables (W51).
        int x = 0;

        for (int i = pinned; i < strips.size(); ++i)
        {
            const auto w = isBus (i - pinned) ? busWidth : width;
            strips[i]->setBounds (x, 0, w, height);
            x += w + stripGap;
        }

        stripHolder->setSize (std::max (r.getWidth(), x), height);

        // The column reads as stacked sections: a heading, a fixed gap, its field; the same gap
        // between every heading and its field, and the same wider gap between sections (owner).
        constexpr int headingHeight = 16, fieldHeight = 28, switchHeight = 26;
        const auto heading = [&column] (Component& label)
        {
            label.setBounds (column.removeFromTop (headingHeight));
            column.removeFromTop (theme::space2);
        };
        const auto nextSection = [&column] { column.removeFromTop (theme::space6); };

        {
            auto title = column.removeFromTop (fieldHeight);

            if (swatch->isVisible())
            {
                swatch->setBounds (title.removeFromLeft (22));
                title.removeFromLeft (theme::space2);
            }
            else
            {
                swatch->setBounds ({});
            }

            columnName.setBounds (title);
        }
        nextSection();

        if (level.isVisible())
        {
            heading (columnLevel);
            level.setBounds (column.removeFromTop (fieldHeight));
            nextSection();
        }

        if (masterSelected)
        {
            heading (columnInto);
            masterOutput.setBounds (column.removeFromTop (fieldHeight));
            nextSection();
        }
        else if (input.isVisible())
        {
            heading (columnInput);
            input.setBounds (column.removeFromTop (fieldHeight));
            column.removeFromTop (theme::space2);
            hear.setBounds (column.removeFromTop (switchHeight));
            recordHere.setBounds (column.removeFromTop (switchHeight));
            nextSection();
            heading (columnInto);
            output.setBounds (column.removeFromTop (fieldHeight));
            nextSection();
        }

        if (columnFromList.isVisible())
        {
            // As tall as its lines, wrapped to the column's width: no gap under a short list.
            const auto font = columnFromList.getFont();
            const auto listWidth = static_cast<float> (column.getWidth());
            int rows = 0;

            for (const auto& lineText : StringArray::fromLines (columnFromList.getText()))
                rows += std::max (1, static_cast<int> (std::ceil (GlyphArrangement::getStringWidth (font, lineText) / listWidth)));

            heading (columnFrom);
            const auto rowHeight = roundToInt (font.getHeight() * 1.25f);
            columnFromList.setBounds (column.removeFromTop (std::min (rowHeight * std::max (1, rows), column.getHeight() / 3)));
            nextSection();
        }

        heading (columnPlugins);
        line->setBounds (column);
        repaintCables();   // strips moved or the selection changed: the Routing section follows
    }
}
