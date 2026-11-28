#include "SettingsPanel.h"

#include "ExportWindow.h"

#include <app/Commands.h>

#include <numeric>
#include <utility>

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

        constexpr int rowHeight = 28, labelWidth = 168, contentWidth = 600, pageRow = 32;
        constexpr int pictureWidth = 280, pictureHeight = 104;   // the loopback drawing
        constexpr int minBusEvery = 2, maxBusEvery = 64;          // as the mixer reads it
    }

    //==============================================================================================
    /** The pages down the left. A click or the arrow keys choose one; while searching, each page
        shows how many settings on it match, and pages with none fade. */
    class SettingsPanel::PageList final : public Component
    {
    public:
        explicit PageList (SettingsPanel& p) : panel (p)
        {
            setWantsKeyboardFocus (true);
            setMouseClickGrabsKeyboardFocus (true);
        }

        void paint (Graphics& g) override
        {
            const auto searching = panel.find.getText().trim().isNotEmpty();

            for (int i = 0; i < pageCount; ++i)
            {
                const auto r = rowBounds (i);
                const auto chosen = ! searching && static_cast<int> (panel.page) == i;
                const auto count = panel.matches[static_cast<size_t> (i)];
                const auto faded = searching && count == 0;

                if (chosen || (i == hover && ! faded))
                {
                    g.setColour (chosen ? theme::card : theme::hover.withAlpha (0.5f));
                    g.fillRoundedRectangle (r.toFloat(), theme::radiusSm);
                }

                if (chosen)
                {
                    g.setColour (theme::accent);   // the page in view: live
                    g.fillRoundedRectangle (r.toFloat().reduced (4.0f, 8.0f).removeFromLeft (3.0f), 1.5f);
                }

                auto text = r.reduced (theme::space3, 0);
                g.setFont (theme::uiFont (theme::textMd, chosen));
                g.setColour (faded ? theme::faint : chosen ? theme::text : theme::muted);

                if (searching && count > 0)
                {
                    g.setFont (theme::denseFont (theme::textXs));
                    g.setColour (theme::accentText);
                    g.drawText (String (count), text.removeFromRight (28), Justification::centredRight, false);
                    g.setFont (theme::uiFont (theme::textMd, false));
                    g.setColour (theme::text);
                }

                g.drawText (nameOf (static_cast<Page> (i)), text, Justification::centredLeft, true);
            }

            if (hasKeyboardFocus (false) && ! searching)
            {
                g.setColour (theme::ring);
                g.drawRoundedRectangle (rowBounds (static_cast<int> (panel.page)).toFloat().reduced (0.5f), theme::radiusSm, theme::stroke);
            }
        }

        void mouseMove (const MouseEvent& e) override   { setHover (rowAt (e.y)); }
        void mouseExit (const MouseEvent&) override     { setHover (-1); }

        void mouseDown (const MouseEvent& e) override
        {
            if (const auto i = rowAt (e.y); i >= 0)
                panel.showPage (static_cast<Page> (i));
        }

        bool keyPressed (const KeyPress& key) override
        {
            const auto step = key == KeyPress::upKey ? -1 : key == KeyPress::downKey ? 1 : 0;

            if (step == 0)
                return false;

            panel.showPage (static_cast<Page> (std::clamp (static_cast<int> (panel.page) + step, 0, pageCount - 1)));
            return true;
        }

        void focusGained (FocusChangeType) override   { repaint(); }
        void focusLost (FocusChangeType) override     { repaint(); }

        String getTooltip() { return {}; }

        int idealHeight() const   { return pageCount * pageRow; }

    private:
        SettingsPanel& panel;
        int hover = -1;

        Rectangle<int> rowBounds (int i) const   { return { 0, i * pageRow, getWidth(), pageRow - 2 }; }
        int rowAt (int y) const                  { const auto i = y / pageRow; return y >= 0 && i < pageCount ? i : -1; }
        void setHover (int i)                    { if (i != hover) { hover = i; repaint(); } }
    };

    String SettingsPanel::nameOf (Page p)
    {
        switch (p)
        {
            case Page::audio:     return "Audio";
            case Page::recording: return "Recording";
            case Page::projects:  return "Projects";
            case Page::timeline:  return "Timeline";
            case Page::mixer:     return "Mixer";
            case Page::browser:   return "Browser";
            case Page::display:   return "Display";
            case Page::shortcuts: return "Shortcuts";
            case Page::updates:   return "Updates";
        }

        return {};
    }

    String SettingsPanel::introOf (Page p)
    {
        switch (p)
        {
            case Page::audio:     return "Your interface, how quickly what you play comes back, and how hard the computer is working.";
            case Page::recording: return "How new takes are saved, what an export (" + String (app::commands::commandName) + "+R) is made as, and the metronome.";
            case Page::projects:  return "What Carthage opens with, what tempo changes do to clips, and where samples are kept.";
            case Page::timeline:  return "How the lanes look and what the mouse does on them.";
            case Page::mixer:     return "How channels reach master, and which channels are buses.";
            case Page::browser:   return "The samples and plugins panel beside the lanes, and where plugins run.";
            case Page::display:   return "How large everything is, and the window layouts you can recall.";
            case Page::shortcuts: return "Every command and the keys that run it. Click a key to change it.";
            case Page::updates:   return "Which version this is, and which new versions you get.";
        }

        return {};
    }

    void SettingsPanel::showPage (Page p)
    {
        if (find.getText().isNotEmpty())
            find.clear();   // choosing a page leaves the search

        page = p;
        session.getEngine().getPropertyStorage().getPropertiesFile().setValue ("settingsPage", static_cast<int> (p));
        viewport.setViewPosition (0, 0);
        resized();
        pageList->repaint();
    }

    /** How to measure latency, drawn (owner): a generic audio interface with a cable from one of
        its outputs back into one of its inputs. The cable takes the accent while a measurement runs. */
    class SettingsPanel::LoopbackPicture final : public Component
    {
    public:
        LoopbackPicture()
        {
            setInterceptsMouseClicks (false, false);
        }

        void setLive (bool on)
        {
            if (std::exchange (live, on) != on)
                repaint();
        }

        void paint (Graphics& g) override
        {
            const auto area = getLocalBounds().toFloat().reduced (1.0f);
            const auto box = area.withTrimmedTop (area.getHeight() * 0.42f);

            // The interface: a flat box with its jacks, a big knob and two meters.
            g.setColour (theme::card);
            g.fillRoundedRectangle (box, theme::radiusSm);
            g.setColour (theme::lineStrong);
            g.drawRoundedRectangle (box, theme::radiusSm, theme::stroke);

            const auto jackY = box.getCentreY();
            const auto jackRadius = box.getHeight() * 0.17f;
            const float inX[] = { box.getX() + box.getWidth() * 0.10f, box.getX() + box.getWidth() * 0.22f };
            const float outX[] = { box.getRight() - box.getWidth() * 0.22f, box.getRight() - box.getWidth() * 0.10f };
            const auto knobX = box.getX() + box.getWidth() * 0.44f;

            const auto jack = [&] (float x)
            {
                g.setColour (theme::well);
                g.fillEllipse (x - jackRadius, jackY - jackRadius, jackRadius * 2.0f, jackRadius * 2.0f);
                g.setColour (theme::mist);
                g.drawEllipse (x - jackRadius, jackY - jackRadius, jackRadius * 2.0f, jackRadius * 2.0f, 1.2f);
            };

            for (auto x : inX)  jack (x);
            for (auto x : outX) jack (x);

            const auto knobRadius = box.getHeight() * 0.28f;
            const auto knob = Point<float> (knobX, jackY);
            g.setColour (theme::hover);
            g.fillEllipse (Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (knob));
            g.setColour (theme::mist);
            g.drawEllipse (Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (knob), 1.2f);
            g.drawLine (Line<float> (knob, knob.getPointOnCircumference (knobRadius * 0.75f, -0.8f)), 1.6f);

            for (int i = 0; i < 2; ++i)
            {
                const auto x = knob.x + knobRadius + 12.0f + static_cast<float> (i) * 6.0f;
                const auto meter = Rectangle<float> (x, jackY - knobRadius, 2.5f, knobRadius * 2.0f);
                g.setColour (theme::well);
                g.fillRoundedRectangle (meter, 1.0f);
                g.setColour (theme::ok);
                g.fillRoundedRectangle (meter.withTrimmedTop (meter.getHeight() * (i == 0 ? 0.45f : 0.6f)), 1.0f);
            }

            g.setFont (theme::uiFont (theme::textXs));
            g.setColour (theme::muted);
            const auto captionY = box.getBottom() - box.getHeight() * 0.30f;
            g.drawText ("In", Rectangle<float> (inX[0] - 12.0f, captionY, inX[1] - inX[0] + 24.0f, box.getHeight() * 0.28f), Justification::centred);
            g.drawText ("Out", Rectangle<float> (outX[0] - 12.0f, captionY, outX[1] - outX[0] + 24.0f, box.getHeight() * 0.28f), Justification::centred);

            // The cable (owner): straight up out of the first output, across, and down into the
            // second input, with arrows along it for the way the sound goes.
            const auto from = Point<float> (outX[0], jackY);
            const auto to = Point<float> (inX[1], jackY);
            const auto top = area.getY() + 6.0f;
            Path cable;
            cable.startNewSubPath (from);
            cable.lineTo (from.x, top);
            cable.lineTo (to.x, top);
            cable.lineTo (to);
            cable = cable.createPathWithRoundedCorners (6.0f);
            const auto ink = live ? theme::accent : theme::mist;
            g.setColour (ink);
            g.strokePath (cable, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));

            // Its plugs.
            for (auto p : { from, to })
                g.fillRoundedRectangle (Rectangle<float> (jackRadius * 1.2f, jackRadius * 2.6f).withCentre (p.translated (0.0f, -jackRadius * 1.2f)), 1.5f);

            // Arrows: up out of the output, across to the left, down into the input.
            const auto arrow = [&g] (Point<float> tip, float angle)
            {
                Path head;
                head.addTriangle (0.0f, -5.0f, -4.5f, 3.0f, 4.5f, 3.0f);   // pointing up
                g.fillPath (head, AffineTransform::rotation (angle).translated (tip));
            };
            const auto riseMid = (top + jackY - jackRadius * 2.4f) * 0.5f;
            arrow ({ from.x, riseMid }, 0.0f);
            arrow ({ (from.x + to.x) * 0.5f, top }, -MathConstants<float>::halfPi);
            arrow ({ to.x, riseMid }, MathConstants<float>::pi);
        }

    private:
        bool live = false;
    };

    void SettingsPanel::chooseRoutingStyle (routing::Style style)
    {
        session.getEngine().getPropertyStorage().getPropertiesFile().setValue ("routingStyle", routing::idOf (style));

        for (auto& p : routingPreviews)
            p->setToggleState (p->getStyleShown() == style, dontSendNotification);

        if (onRoutingStyleChanged)
            onRoutingStyleChanged (style);
    }

    bool SettingsPanel::keyPressed (const KeyPress& key)
    {
        // Escape anywhere in Settings closes it, back to the view it covered (owner). The search
        // box takes Escape first, to clear what was typed.
        if (key != KeyPress::escapeKey || ! onClose)
            return false;

        onClose();
        return true;
    }

    SettingsPanel::SettingsPanel (engine::Session& s, ApplicationCommandManager& commands, Array<CommandID> shortcutCommands)
        : session (s),
          page (static_cast<Page> (jlimit (0, pageCount - 1, s.getEngine().getPropertyStorage().getPropertiesFile().getIntValue ("settingsPage", 0)))),
          shortcuts (commands, std::move (shortcutCommands))
    {
        styleLabel (title, theme::textLg, theme::text, true);
        title.setText ("Settings", dontSendNotification);

        find.setTextToShowWhenEmpty (String::fromUTF8 ("Find a setting\xe2\x80\xa6"), theme::faint);
        find.setFont (theme::uiFont (theme::textMd));
        find.setIndents (theme::space2, 6);
        find.onTextChange = [this] { viewport.setViewPosition (0, 0); resized(); pageList->repaint(); };
        find.onEscapeKey = [this]
        {
            if (find.getText().isEmpty())
            {
                if (onClose) onClose();   // Escape again: Settings closes
                return;
            }

            find.clear();
            resized();
            pageList->repaint();
        };
        find.setTooltip ("Looks through every page. Escape clears it.");
        addAndMakeVisible (find);

        pageList = std::make_unique<PageList> (*this);
        addAndMakeVisible (*pageList);
        for (int i = 0; i < pageCount; ++i)
        {
            auto& heading = pageHeadings[static_cast<size_t> (i)];
            heading = std::make_unique<Label>();
            styleLabel (*heading, theme::textLg, theme::text, true);
            heading->setText (nameOf (static_cast<Page> (i)), dontSendNotification);
            body.addChildComponent (*heading);
        }

        styleLabel (pageTitle, theme::textXl, theme::text, true);
        styleLabel (pageIntro, theme::textMd, theme::muted);
        pageIntro.setJustificationType (Justification::topLeft);
        addAndMakeVisible (pageTitle);
        addAndMakeVisible (pageIntro);
        styleLabel (noMatch, theme::textMd, theme::muted);
        noMatch.setJustificationType (Justification::topLeft);
        body.addChildComponent (noMatch);

        for (auto [label, text] : { std::pair { &interfaceHeading, "Interface" }, { &recordingHeading, "Recording" },
                                    { &latencyHeading, "Latency" }, { &loadHeading, "Load" }, { &projectsHeading, "Projects" }, { &browserHeading, "Browser" }, { &timelineHeading, "Timeline" }, { &mixerHeading, "Mixer" }, { &routingHeading, "Routing" }, { &metronomeHeading, "Metronome" }, { &windowsHeading, "Windows" }, { &processesHeading, "Plugin processes" },
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
        loopPicture = std::make_unique<LoopbackPicture>();
        loopPicture->setTitle ("A cable from an output of the interface back into one of its inputs");
        body.addAndMakeVisible (*loopPicture);

        closeButton.setTooltip ("Close Settings and go back to the lanes (" + String (app::commands::commandName) + "+W or Escape)");
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

        // Count-in (5d): Off, 1 or 2 bars before a recording that starts from a stop.
        styleLabel (countInLabel, theme::textMd, theme::muted);
        countInLabel.setText ("Count-in", dontSendNotification);
        countIn.addItem ("Off", 1);
        countIn.addItem ("1 bar", 2);
        countIn.addItem ("2 bars", 3);
        countIn.setSelectedId (session.getCountInBars() + 1, dontSendNotification);
        countIn.setTooltip ("Before recording from a stop, play this much earlier with the click, so you come in on the beat. The take starts where you pressed record.");
        countIn.onChange = [this] { session.setCountInBars (countIn.getSelectedId() - 1); };
        body.addAndMakeVisible (countInLabel);
        body.addAndMakeVisible (countIn);

        // Low-latency monitoring (brief, feature 8; W57).
        lowLatency.setToggleState (session.getLowLatencyMonitoring(), dontSendNotification);
        lowLatency.setTooltip ("While you play through an armed channel you hear, plugins elsewhere that delay their sound (lookahead "
                               "limiters, linear-phase EQs) are paused, so what you hear is not held back to line up with them. The armed "
                               "channels keep theirs. They come back when you stop hearing the input; an export always uses every plugin.");
        lowLatency.onClick = [this] { session.setLowLatencyMonitoring (lowLatency.getToggleState()); };
        body.addAndMakeVisible (lowLatency);

        // Export (brief, feature 8): the format and rate Cmd+R uses; its window shows the same.
        styleLabel (exportHeading, theme::textSm, theme::muted, true);
        exportHeading.setText ("Export", dontSendNotification);
        for (auto [l, text] : { std::pair { &exportFormatLabel, "Format" }, { &exportRateLabel, "Sample rate" } })
        {
            styleLabel (*l, theme::textMd, theme::muted);
            l->setText (text, dontSendNotification);
        }
        exportFormat.onChange = [this] { session.getEngine().getPropertyStorage().getPropertiesFile().setValue (ExportWindow::formatKey, exportFormat.getSelectedId() - 1); };
        exportRate.onChange = [this] { session.getEngine().getPropertyStorage().getPropertiesFile().setValue (ExportWindow::rateKey, exportRate.getSelectedId() == 1 ? 0 : exportRate.getSelectedId()); };

        for (auto* c : std::initializer_list<Component*> { &exportHeading, &exportFormatLabel, &exportFormat, &exportRateLabel, &exportRate })
            body.addAndMakeVisible (c);

        // Bus channels (owner): off by default; every Nth channel wider and marked "Bus".
        busChannels.setToggleState (props.getBoolValue ("busChannels", false), dontSendNotification);
        busChannels.setTooltip ("Every few channels, one is drawn wider and marked Bus: a place to route a group of channels "
                                "(drums, guitars) and process them together. Only how the mixer looks; routing stays yours.");
        styleLabel (busEveryLabel, theme::textMd, theme::muted);
        busEveryLabel.setText ("Bus every", dontSendNotification);

        // A few common intervals to pick, or any number typed in (owner).
        const auto ordinal = [] (int n)
        {
            const auto teen = n % 100 >= 11 && n % 100 <= 13;
            const auto suffix = teen ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
            return String (n) + suffix + " channel";
        };
        const auto showBusEvery = [this, ordinal] (int n)
        {
            if (busEvery.indexOfItemId (n) >= 0)
                busEvery.setSelectedId (n, dontSendNotification);
            else
                busEvery.setText (ordinal (n), dontSendNotification);
        };

        for (const auto n : { 4, 5, 8, 10, 12, 16 })
            busEvery.addItem (ordinal (n), n);

        busEvery.setEditableText (true);
        showBusEvery (std::clamp (props.getIntValue ("busEvery", 10), minBusEvery, maxBusEvery));
        busEvery.setTooltip ("Which channels are buses: with every 10th, channels 10, 20, 30 and so on. Pick one, or click "
                             "and type any number from " + String (minBusEvery) + " to " + String (maxBusEvery) + ".");
        busEvery.setEnabled (busChannels.getToggleState());

        const auto busChanged = [this, showBusEvery]
        {
            // A typed figure ("7", "7th") counts by its number; anything else goes back to what it was.
            auto& p = session.getEngine().getPropertyStorage().getPropertiesFile();
            const auto typed = busEvery.getText().trim().getIntValue();
            const auto n = typed > 0 ? std::clamp (typed, minBusEvery, maxBusEvery) : p.getIntValue ("busEvery", 10);
            showBusEvery (n);
            p.setValue ("busChannels", busChannels.getToggleState());
            p.setValue ("busEvery", n);
            busEvery.setEnabled (busChannels.getToggleState());
            if (onBusChannelsChanged) onBusChannelsChanged();
        };
        // How the Routing section under the mixer draws the mix: five ways, each shown as it looks (owner).
        styleLabel (routingHint, theme::textSm, theme::muted);
        routingHint.setText ("How the Routing section under the mixer draws where sound goes. Each picture is the same project "
                             "with the Drum bus selected. The selected channel's story is the default.", dontSendNotification);
        routingHint.setJustificationType (Justification::topLeft);
        body.addAndMakeVisible (routingHint);
        const auto currentRouting = routing::styleFromId (props.getValue ("routingStyle", "story"));

        for (size_t i = 0; i < routingPreviews.size(); ++i)
        {
            const auto style = routing::styles[i];
            routingPreviews[i] = std::make_unique<routing::Preview> (style);
            routingPreviews[i]->setToggleState (style == currentRouting, dontSendNotification);
            routingPreviews[i]->onClick = [this, style] { chooseRoutingStyle (style); };
            body.addAndMakeVisible (*routingPreviews[i]);
        }

        busChannels.onClick = busChanged;
        busEvery.onChange = busChanged;

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

        for (auto* c : std::initializer_list<Component*> { &browserHeading, &panelWidthLabel, &panelWidth, &textSizeLabel, &textSize, &playWhileHeld, &fitLoops, &preMaster, &busChannels, &busEveryLabel, &busEvery })
            body.addAndMakeVisible (c);

        // Project loudness (owner): where a new project's master starts; channels start at 0 dB.
        styleLabel (loudnessLabel, theme::textMd, theme::muted);
        loudnessLabel.setText ("New projects at", dontSendNotification);
        for (const auto db : { 0, -3, -6, -9, -12 })
            loudness.addItem (db == 0 ? String ("0 dB (full level)") : String::fromUTF8 ("\xe2\x88\x92") + String (-db) + " dB", 100 + db);
        loudness.setSelectedId (100 + roundToInt (session.getNewProjectLoudness()), dontSendNotification);
        if (loudness.getSelectedId() == 0)
            loudness.setSelectedId (100, dontSendNotification);
        loudness.setTooltip ("The project loudness: where a new project's master fader starts, and where a double-click on it returns. "
                             "Mix at -3 or -6 dB to keep room above. Channels always start at 0 dB.");
        loudness.onChange = [this] { session.setNewProjectLoudness (static_cast<float> (loudness.getSelectedId() - 100)); };
        body.addAndMakeVisible (loudnessLabel);
        body.addAndMakeVisible (loudness);

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
        clickLevel.setTooltip ("How loud the metronome is, for this project. Double-click: 60 %.");
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

        // Plugins in their own processes (5f-4, W69).
        styleLabel (processesHint, theme::textSm, theme::muted);
        processesHint.setJustificationType (Justification::topLeft);
        processesHint.setText ("A plugin that crashes then stops only its own process: Carthage keeps playing and starts it "
                               "again. Plugins you hear live, through a channel whose input is on, always run inside Carthage.",
                               dontSendNotification);
        pluginProcesses.setToggleState (session.getPluginProcesses(), dontSendNotification);
        pluginProcesses.setTooltip ("On: each plugin runs outside Carthage, so a crashing plugin can not take Carthage down. "
                                    "Off: every plugin runs inside Carthage. Changing it reloads the plugins, with their settings.");
        pluginProcesses.onClick = [this]
        {
            session.setPluginProcesses (pluginProcesses.getToggleState());
            groupAutomatically.setEnabled (pluginProcesses.getToggleState());
        };
        groupAutomatically.setToggleState (session.getAutomaticPluginGrouping(), dontSendNotification);
        groupAutomatically.setEnabled (session.getPluginProcesses());
        groupAutomatically.setTooltip ("On: plugins by the same maker share a process, and a plugin that has crashed gets one of its own. "
                                       "Off: right-click a plugin > Runs in to choose where each one runs.");
        groupAutomatically.onClick = [this] { session.setAutomaticPluginGrouping (groupAutomatically.getToggleState()); };
        forgetCrashes.setTooltip ("Plugins that have crashed share their maker's process again, from the next time they load");
        forgetCrashes.onClick = [this] { session.getProcesses().forgetCrashes(); };

        for (auto* c : std::initializer_list<Component*> { &processesHeading, &processesHint, &pluginProcesses, &groupAutomatically, &forgetCrashes })
            body.addAndMakeVisible (c);

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
        {
            auto& props = session.getEngine().getPropertyStorage().getPropertiesFile();
            auto* device = session.getDeviceManager().getCurrentAudioDevice();
            ExportWindow::fillFormats (exportFormat, props);
            ExportWindow::fillRates (exportRate, props, device != nullptr ? device->getCurrentSampleRate() : 48000.0);
        }

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

        loopPicture->setLive (isMeasuring());   // the cable lights while the probe plays
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
        // The pages on the panel's surface, the page itself on the lanes' background.
        g.fillAll (theme::bg);
        g.setColour (theme::bg2);
        g.fillRect (0, 0, sidebarWidth, getHeight());
        g.setColour (theme::line);
        g.fillRect (sidebarWidth, 0, 1, getHeight());
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
        // The shortcut list reports a new height from inside this layout; the layout in hand
        // already places it at that height.
        if (layingOut)
            return;

        const ScopedValueSetter<bool> laying (layingOut, true);

        // The pages on the left: the title, the search, the list.
        auto side = getLocalBounds().removeFromLeft (sidebarWidth).reduced (theme::space3, theme::space4);
        title.setBounds (side.removeFromTop (rowHeight).withTrimmedLeft (theme::space1));
        side.removeFromTop (theme::space2);
        find.setBounds (side.removeFromTop (rowHeight));
        side.removeFromTop (theme::space3);
        pageList->setBounds (side.removeFromTop (pageList->idealHeight()));

        // The page: its name and what it is for, with the window buttons; below, its settings.
        auto main = getLocalBounds().withTrimmedLeft (sidebarWidth + 1);
        auto head = main.removeFromTop (theme::space4 + rowHeight + theme::space1 + 22 + theme::space3).reduced (theme::space8, 0).withTrimmedTop (theme::space4);
        auto buttons = head.removeFromTop (rowHeight);
        closeButton.setBounds (buttons.removeFromRight (closeButton.isVisible() ? rowHeight : 0));
        popButton.setBounds (buttons.removeFromRight (rowHeight));
        pageTitle.setBounds (buttons.withWidth (std::min (buttons.getWidth(), contentWidth)));
        head.removeFromTop (theme::space1);
        pageIntro.setBounds (head.removeFromTop (22).withWidth (std::min (head.getWidth(), contentWidth)));

        const auto query = find.getText().trim();
        const auto searching = query.isNotEmpty();
        main.removeFromTop (1);

        viewport.setBounds (main);
        const auto bodyWidth = viewport.getWidth() - viewport.getScrollBarThickness();
        auto r = Rectangle<int> (theme::space8, 0, std::min (bodyWidth - 2 * theme::space8, contentWidth), 10000);
        const auto startY = r.getY();

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
            Page on;
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

        // Every word of the search must be somewhere: in the setting's own words, or in its page's
        // or group's name ("recording count" finds Count-in). Words that only ask for a change
        // ("set", "change", "turn"...) are not needed to match (owner: "recording set count").
        const auto lineMatches = [&] (const Line& line, const String& context)
        {
            if (query.isEmpty())
                return true;

            static const StringArray filler { "set", "sets", "setting", "settings", "change", "changes", "turn", "on", "off", "the", "a", "an",
                                              "to", "of", "for", "my", "how", "do", "i", "can", "where", "is", "make", "use", "choose", "pick" };
            String words (context + " ");
            for (auto* c : line.parts) words << wordsOf (c);

            for (const auto& word : StringArray::fromTokens (query, true))
                if (! filler.contains (word, true) && ! words.containsIgnoreCase (word))
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
        sections.push_back ({ Page::audio, &interfaceHeading, { row (inputDeviceLabel, inputDevice), row (outputDeviceLabel, outputDevice),
                                                   row (rateLabel, rate), row (bufferLabel, buffer),
                                                   Line { { &testButton, &testHint }, rowHeight + 20 + theme::space1, [this] (Rectangle<int> b)
                                                          {
                                                              testButton.setBounds (b.removeFromTop (rowHeight).removeFromLeft (150));
                                                              testHint.setBounds (b.removeFromTop (20));
                                                          } } } });
        sections.push_back ({ Page::recording, &recordingHeading, { row (formatLabel, format), row (countInLabel, countIn), whole (lowLatency, rowHeight) } });
        sections.push_back ({ Page::recording, &exportHeading, { row (exportFormatLabel, exportFormat), row (exportRateLabel, exportRate) } });
        sections.push_back ({ Page::audio, &latencyHeading, { row (reportedLabel, reportedValue), row (measuredLabel, measuredValue),
                                                 Line { { loopPicture.get(), &loopHint, &loopOutLabel, &loopOut, &loopInLabel, &loopIn, &measureButton },
                                                        pictureHeight + theme::space2 + 48 + 2 * (rowHeight + theme::space1) + rowHeight + theme::space2,
                                                        [this] (Rectangle<int> b)
                                                        {
                                                            loopPicture->setBounds (b.removeFromTop (pictureHeight).removeFromLeft (pictureWidth));
                                                            b.removeFromTop (theme::space2);
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
        sections.push_back ({ Page::audio, &loadHeading, { row (cpuLabel, cpuValue), row (threadLabel, threadValue),
                                              Line { { &dropoutsLabel, &dropoutsValue, &resetDropouts }, rowHeight, [this] (Rectangle<int> line)
                                                     {
                                                         dropoutsLabel.setBounds (line.removeFromLeft (labelWidth));
                                                         resetDropouts.setBounds (line.removeFromRight (64));
                                                         dropoutsValue.setBounds (line);
                                                     } },
                                              whole (showLoad, rowHeight) } });
        sections.push_back ({ Page::projects, &projectsHeading, { row (atLaunchLabel, atLaunch), row (loudnessLabel, loudness), row (tempoFollowLabel, tempoFollow),
                                                  Line { { &copySamples, &copySamplesHint }, rowHeight + 50, [this] (Rectangle<int> b)
                                                         {
                                                             copySamples.setBounds (b.removeFromTop (rowHeight));
                                                             copySamplesHint.setBounds (b);
                                                         } } } });
        sections.push_back ({ Page::display, &windowsHeading, { row (slotsLabel, slots), row (uiScaleLabel, uiScale) } });
        sections.push_back ({ Page::browser, &browserHeading, { row (panelWidthLabel, panelWidth), row (textSizeLabel, textSize),
                                                 whole (playWhileHeld, rowHeight), whole (fitLoops, rowHeight), whole (checkPlugins, rowHeight) } });
        sections.push_back ({ Page::browser, &processesHeading, { whole (pluginProcesses, rowHeight), whole (groupAutomatically, rowHeight),
                                                   whole (processesHint, 50),
                                                   Line { { &forgetCrashes }, rowHeight, [this] (Rectangle<int> line)
                                                          {
                                                              forgetCrashes.setBounds (line.removeFromLeft (160));
                                                          } } } });
        sections.push_back ({ Page::recording, &metronomeHeading, { row (clickSoundLabel, clickSound), row (clickLevelLabel, clickLevel),
                                                   whole (clickAccent, rowHeight), whole (clickWhileRecording, rowHeight) } });
        sections.push_back ({ Page::timeline, &timelineHeading, { row (beatShadingLabel, beatShading), whole (clickPlaces, rowHeight), whole (rightClickDeletes, rowHeight) } });
        sections.push_back ({ Page::mixer, &mixerHeading, { whole (preMaster, rowHeight), whole (busChannels, rowHeight), row (busEveryLabel, busEvery) } });

        // Routing: the hint, then the five pictures two to a row.
        {
            std::vector<Component*> parts { &routingHint };
            for (auto& p : routingPreviews) parts.push_back (p.get());
            constexpr int hintHeight = 40, card = 150, gap = 12;
            const auto rows = static_cast<int> ((routingPreviews.size() + 1) / 2);

            sections.push_back ({ Page::mixer, &routingHeading, { Line { parts, hintHeight + rows * card + (rows - 1) * gap, [this] (Rectangle<int> b)
                                  {
                                      routingHint.setBounds (b.removeFromTop (hintHeight));
                                      const auto w = (b.getWidth() - gap) / 2;

                                      for (size_t i = 0; i < routingPreviews.size(); ++i)
                                      {
                                          const auto col = static_cast<int> (i % 2), row = static_cast<int> (i / 2);
                                          routingPreviews[i]->setBounds (b.getX() + col * (w + gap), b.getY() + row * (card + gap), w, card);
                                      }
                                  } } } });
        }
        const auto hintHeight = updatesHint.getText().isEmpty() ? 0 : 36;
        sections.push_back ({ Page::updates, &updatesHeading, { Line { { &versionLabel, &versionValue, &checkNow }, rowHeight + theme::space1, [this] (Rectangle<int> line)
                                                        {
                                                            line = line.withHeight (rowHeight);
                                                            versionLabel.setBounds (line.removeFromLeft (labelWidth));
                                                            checkNow.setBounds (line.removeFromRight (96));
                                                            versionValue.setBounds (line);
                                                        } },
                                                 whole (betaUpdates, rowHeight), whole (updatesHint, hintHeight) } });

        // The chosen page, or while searching every page that has a match, each under its name.
        matches.fill (0);
        bool anyShown = false;

        const auto sectionsOn = [&] (Page p)
        {
            std::vector<Section*> out;
            for (auto& section : sections)
                if (section.on == p)
                    out.push_back (&section);
            return out;
        };

        for (int pi = 0; pi < pageCount; ++pi)
        {
            const auto p = static_cast<Page> (pi);
            const auto pageShown = searching || p == page;
            const auto pageMatches = searching && nameOf (p).containsIgnoreCase (query);
            auto onPage = sectionsOn (p);
            bool pageHeaded = false;

            const auto headPage = [&]
            {
                // While searching, each page's results sit under its name.
                if (! searching || pageHeaded)
                    return;

                pageHeaded = true;
                r.removeFromTop (anyShown ? theme::space8 : theme::space2);
                auto& label = *pageHeadings[static_cast<size_t> (pi)];
                label.setVisible (true);
                label.setBounds (r.removeFromTop (24));
            };

            pageHeadings[static_cast<size_t> (pi)]->setVisible (false);

            for (auto* section : onPage)
            {
                const auto headingMatches = searching && (pageMatches || section->heading->getText().containsIgnoreCase (query));
                std::vector<const Line*> shown;

                for (const auto& line : section->lines)
                {
                    const auto keep = pageShown && (headingMatches || lineMatches (line, nameOf (p) + " " + section->heading->getText()));

                    for (auto* c : line.parts)
                        c->setVisible (keep);

                    if (keep)
                        shown.push_back (&line);
                }

                matches[static_cast<size_t> (pi)] += static_cast<int> (shown.size());

                // A page with one group needs no heading for it: the page's name says it.
                const auto headed = ! shown.empty() && (searching || onPage.size() > 1);
                section->heading->setVisible (headed);

                if (shown.empty())
                    continue;

                headPage();
                anyShown = true;
                r.removeFromTop (headed ? theme::space6 : theme::space2);

                if (headed)
                {
                    section->heading->setBounds (r.removeFromTop (20));
                    r.removeFromTop (theme::space1);
                }

                for (const auto* line : shown)
                    line->place (r.removeFromTop (line->height));
            }

            if (p == Page::shortcuts)
            {
                // The shortcuts take the same search into their own list.
                const auto shortcutsShown = pageShown && (! searching || pageMatches || shortcuts.showMatching (query)
                                                          || shortcutsHeading.getText().containsIgnoreCase (query));

                if (! searching)
                    shortcuts.showMatching ({});

                shortcutsHeading.setVisible (shortcutsShown && searching);
                shortcuts.setVisible (shortcutsShown);

                if (shortcutsShown)
                {
                    matches[static_cast<size_t> (pi)] += 1;
                    headPage();
                    anyShown = true;
                    r.removeFromTop (theme::space2);
                    shortcuts.setBounds (r.withHeight (shortcuts.getIdealHeight()));   // it reports a new height if it needs one
                    r.removeFromTop (shortcuts.getIdealHeight());
                }
            }
        }

        const auto total = std::accumulate (matches.begin(), matches.end(), 0);
        pageTitle.setText (searching ? String::fromUTF8 ("Results for \xe2\x80\x9c") + query + String::fromUTF8 ("\xe2\x80\x9d") : nameOf (page), dontSendNotification);
        pageIntro.setText (searching ? (total == 0 ? String ("Nothing matches. Try another word, or clear the search with Escape.")
                                                   : String ("Matching settings from every page. Choose a page on the left to leave the search."))
                                     : introOf (page),
                           dontSendNotification);

        noMatch.setVisible (false);
        body.setSize (bodyWidth, std::max (r.getY() - startY + theme::space8, 80));
        juce::ignoreUnused (anyShown);
    }

    SettingsPanel::~SettingsPanel() = default;

    void SettingsPanel::setInWindow (bool inWindow)
    {
        popButton.setGlyph (inWindow ? IconButton::Glyph::popIn : IconButton::Glyph::popOut);
        popButton.setTooltip (inWindow ? "Settings back into the main window" : "Settings in a window of its own");
        closeButton.setVisible (! inWindow);   // its window has its own close button
    }
}
