#include "SampleEditor.h"

#include <engine/AudioFormats.h>
#include <engine/SampleAnalysis.h>

namespace carthage::ui
{
    using namespace juce;

    namespace
    {
        constexpr int rowHeight = 28, sliderRow = 24, headingHeight = 16, labelWidth = 56, controlsWidth = 380;

        /** "Key: A minor   Tempo: 120 bpm" (owner: the key clearly apart from the clip count). */
        String factsText (const engine::SampleLibrary::Analysis& a)
        {
            return "Key: " + (a.key.isNotEmpty() ? a.key : String ("none found")) + (a.bpm ? "   Tempo: " + String (*a.bpm, 1) + " bpm" : String());
        }
    }

    SampleEditor::SampleEditor (engine::Session& s) : session (s)
    {
        audio::registerReadable (formats);
        thumbnail.addChangeListener (this);
        setWantsKeyboardFocus (true);   // Escape and Cmd+W close it while it is in front (W42)

        styleLabel (name, theme::textLg, theme::text, true);
        name.setEditable (false, true, false);   // double-click to rename the sample (owner)
        name.setTooltip ("Double-click to rename this sample; every clip of it takes the name");
        name.setColour (Label::textWhenEditingColourId, theme::text);
        name.setColour (TextEditor::backgroundColourId, theme::well);
        name.onTextChange = [this]
        {
            auto* c = clip();

            if (c == nullptr)
                return;

            if (auto r = editing::renameSample (*c, name.getText()); r.failed())
            {
                if (onStatus) onStatus ("Not renamed: " + r.getErrorMessage());
                name.setText (editing::sampleName (*c), dontSendNotification);
            }
            else if (onStatus)
            {
                onStatus ("Renamed to " + editing::sampleName (*c));
            }
        };
        styleLabel (shared, theme::textSm, theme::muted);
        styleLabel (facts, theme::textSm, theme::muted);

        const auto setUp = [this] (ValueSlider& slider, Label& label, const String& text, double lo, double hi, double step,
                                   const String& suffix, const String& tip)
        {
            styleLabel (label, theme::textSm, theme::muted);
            label.setText (text, dontSendNotification);
            slider.setSliderStyle (Slider::LinearHorizontal);
            slider.setTextBoxStyle (Slider::TextBoxRight, false, 64, 22);
            slider.setRange (lo, hi, step);
            slider.setTextValueSuffix (suffix);
            slider.setTooltip (tip + " Every clip of this sample follows; double-click returns to 0.");
            slider.onDragStart = [this, text] { beginGesture (text); };
            addAndMakeVisible (label);
            addAndMakeVisible (slider);
        };

        setUp (gain, gainLabel, "Gain", -48.0, 12.0, 0.1, " dB", "How loud the sample plays.");
        setUp (pitch, pitchLabel, "Pitch", -24.0, 24.0, 0.01, " st", "Pitch in semitones, keeping the length.");
        setUp (formant, formantLabel, "Formant", -12.0, 12.0, 0.01, " st",
               "The sound's colour without its pitch: up sounds smaller and brighter, down bigger and darker.");
        setUp (fadeIn, fadeInLabel, "Fade in", 0.0, 2000.0, 1.0, " ms", "A fade at the start.");
        setUp (fadeOut, fadeOutLabel, "Fade out", 0.0, 2000.0, 1.0, " ms", "A fade at the end.");
        fadeInLabel.setText ("In", dontSendNotification);    // under the "Fades" heading
        fadeOutLabel.setText ("Out", dontSendNotification);

        for (auto [heading, text] : { std::pair { &soundHeading, "Sound" }, { &fadesHeading, "Fades" } })
        {
            styleLabel (*heading, theme::textSm, theme::text, true);
            heading->setText (text, dontSendNotification);
            addAndMakeVisible (heading);
        }

        playButton.setTooltip ("Play the sample from where it starts. Click the waveform to play from that point.");
        playButton.onClick = [this]
        {
            if (file.existsAsFile())
                session.getPreview().play (file, sound().start);
        };
        addAndMakeVisible (playButton);

        gain.onValueChange = [this] { apply ([this] (auto& s2) { s2.gainDb = static_cast<float> (gain.getValue()); }); };
        pitch.onValueChange = [this] { apply ([this] (auto& s2) { s2.pitch = static_cast<float> (pitch.getValue()); }); };
        formant.onValueChange = [this] { apply ([this] (auto& s2) { s2.formant = static_cast<float> (formant.getValue()); }); };
        fadeIn.onValueChange = [this] { apply ([this] (auto& s2) { s2.fadeIn = fadeIn.getValue() / 1000.0; }); };
        fadeOut.onValueChange = [this] { apply ([this] (auto& s2) { s2.fadeOut = fadeOut.getValue() / 1000.0; }); };

        for (auto [box, isIn] : { std::pair { &fadeInShape, true }, { &fadeOutShape, false } })
        {
            box->addItem ("Linear", tracktion::AudioFadeCurve::linear);
            box->addItem ("Convex", tracktion::AudioFadeCurve::convex);
            box->addItem ("Concave", tracktion::AudioFadeCurve::concave);
            box->addItem ("S-curve", tracktion::AudioFadeCurve::sCurve);
            box->setTooltip (String ("The shape of the fade ") + (isIn ? "in" : "out") + ": convex sounds fuller, concave quicker, S-curve smooth at both ends.");
            box->onChange = [this, box, isIn]
            {
                beginGesture ("Fade shape");
                const auto shape = static_cast<tracktion::AudioFadeCurve::Type> (box->getSelectedId());
                apply ([shape, isIn] (auto& s2) { (isIn ? s2.fadeInShape : s2.fadeOutShape) = shape; });
            };
            addAndMakeVisible (box);
        }

        reverse.setTooltip ("Play the sample backwards. Every clip of it follows.");
        reverse.onClick = [this]
        {
            beginGesture (reverse.getToggleState() ? "Reverse sample" : "Play sample forwards");
            apply ([this] (auto& s2) { s2.reversed = reverse.getToggleState(); });
        };

        fit.setTooltip ("Stretch every clip of this sample to the project's tempo, keeping its pitch (needs the sample's own tempo).");
        fit.onClick = [this]
        {
            auto* c = clip();

            if (c == nullptr || ! bpm)
                return;

            editing::begin (c->edit, "Fit to tempo");
            editing::fitSampleToTempo (*c, *bpm);

            if (onStatus)
                onStatus ("Fitted " + editing::sampleName (*c) + " from " + String (*bpm, 1) + " bpm to the project's tempo");

            refresh();
        };

        unique.setTooltip ("Make this clip a sample of its own, so edits here no longer change the other clips.");
        unique.onClick = [this]
        {
            if (auto* c = clip())
            {
                const auto newName = editing::makeUnique (*c);

                if (onStatus)
                    onStatus ("This clip is now " + newName + "; edits here leave the others alone");

                refresh();
            }
        };

        shapeButton.setClickingTogglesState (true);
        shapeButton.setTooltip ("Draw the sample's volume shape: click the waveform to add a point, drag a point to move it, right-click to remove it, "
                                "drag the small handle between two points to bend the line, double-click it to straighten. Every clip of the sample follows.");
        shapeButton.onClick = [this]
        {
            shaping = shapeButton.getToggleState();
            clearShape.setVisible (shaping);
            repaint();
        };
        clearShape.setTooltip ("Remove every point: the sample plays at its full level again");
        clearShape.onClick = [this]
        {
            beginGesture ("Clear shape");
            envelope.clear();
            storeEnvelope();
        };
        addChildComponent (clearShape);
        addAndMakeVisible (shapeButton);

        closeButton.setTooltip ("Close the sample editor (Escape)");
        closeButton.onClick = [this] { if (onClose) onClose(); };

        for (auto* c : std::initializer_list<Component*> { &name, &shared, &facts, &reverse, &fit, &unique, &closeButton })
            addAndMakeVisible (c);
    }

    SampleEditor::~SampleEditor()
    {
        thumbnail.removeChangeListener (this);
    }

    tracktion::Clip* SampleEditor::clip() const
    {
        return clipID.isValid() ? tracktion::findClipForID (session.getEdit(), clipID) : nullptr;
    }

    editing::SampleSound SampleEditor::sound() const
    {
        auto* c = clip();
        return c != nullptr ? editing::soundOf (*c) : editing::SampleSound();
    }

    void SampleEditor::show (tracktion::Clip& c)
    {
        clipID = c.itemID;
        const auto* audio = dynamic_cast<tracktion::AudioClipBase*> (&c);
        const auto newFile = audio != nullptr ? audio->getOriginalFile() : File();

        if (newFile != file)
        {
            file = newFile;
            thumbnail.setSource (file.existsAsFile() ? new FileInputSource (file) : nullptr);

            std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (file));
            fileSeconds = reader != nullptr && reader->sampleRate > 0.0 ? static_cast<double> (reader->lengthInSamples) / reader->sampleRate : 0.0;
            analyse();
        }

        refresh();
        grabKeyboardFocus();
    }

    void SampleEditor::analyse()
    {
        bpm.reset();
        facts.setText ({}, dontSendNotification);

        // What the browser already found, else worked out in the background.
        if (const auto known = session.getSampleLibrary().getAnalysis (file))
        {
            bpm = known->bpm;
            facts.setText (factsText (*known), dontSendNotification);
            fit.setEnabled (bpm.has_value());
            return;
        }

        fit.setEnabled (false);
        facts.setText (String::fromUTF8 ("Finding key and tempo\xe2\x80\xa6"), dontSendNotification);

        Thread::launch ([safe = SafePointer<SampleEditor> (this), f = file]
        {
            const auto found = engine::analysis::analyseFile (f);

            MessageManager::callAsync ([safe, f, found]
            {
                if (safe == nullptr || safe->file != f)
                    return;

                safe->bpm = found ? found->bpm : std::nullopt;
                safe->facts.setText (found ? factsText (*found) : String ("Key: none found"),
                                     dontSendNotification);
                safe->fit.setEnabled (safe->bpm.has_value());
            });
        });
    }

    void SampleEditor::refresh()
    {
        auto* c = clip();

        if (c == nullptr)
        {
            if (onClose)
                onClose();   // the clip went (deleted, undone, another project)
            return;
        }

        name.setText (editing::sampleName (*c), dontSendNotification);
        const auto count = editing::clipsOfSample (*c).size();
        shared.setText (count == 1 ? String ("1 clip") : String (count) + " clips share this sample", dontSendNotification);
        resized();   // the clip count sets where the key starts
        readControls();
        repaint();
    }

    void SampleEditor::readControls()
    {
        const auto s = sound();
        gain.setValue (s.gainDb, dontSendNotification);
        pitch.setValue (s.pitch, dontSendNotification);
        formant.setValue (s.formant, dontSendNotification);
        fadeIn.setValue (s.fadeIn * 1000.0, dontSendNotification);
        fadeOut.setValue (s.fadeOut * 1000.0, dontSendNotification);
        reverse.setToggleState (s.reversed, dontSendNotification);

        if (auto* c = clip(); c != nullptr && dragPoint < 0 && dragBend < 0)
            envelope = editing::envelopeOf (*c);
        fadeInShape.setSelectedId (s.fadeInShape, dontSendNotification);
        fadeOutShape.setSelectedId (s.fadeOutShape, dontSendNotification);
    }

    void SampleEditor::beginGesture (const String& what)
    {
        if (auto* c = clip())
            editing::begin (c->edit, what + " (" + editing::sampleName (*c) + ")");
    }

    void SampleEditor::apply (const std::function<void (editing::SampleSound&)>& change)
    {
        auto* c = clip();

        if (c == nullptr)
            return;

        auto s = editing::soundOf (*c);
        change (s);
        editing::setSound (*c, s);
        repaint (waveArea);
    }

    //==============================================================================================
    float SampleEditor::xAt (double seconds) const
    {
        const auto span = std::max (0.001, fileSeconds);
        return static_cast<float> (waveArea.getX()) + static_cast<float> (seconds / span) * static_cast<float> (waveArea.getWidth());
    }

    double SampleEditor::secondsAt (float x) const
    {
        const auto t = (x - static_cast<float> (waveArea.getX())) / static_cast<float> (std::max (1, waveArea.getWidth()));
        return std::clamp (static_cast<double> (t), 0.0, 1.0) * fileSeconds;
    }

    float SampleEditor::yForGain (float level) const
    {
        const auto area = waveArea.toFloat().reduced (0.0f, 6.0f);
        return area.getBottom() - std::clamp (level, 0.0f, 1.0f) * area.getHeight();
    }

    float SampleEditor::gainForY (float y) const
    {
        const auto area = waveArea.toFloat().reduced (0.0f, 6.0f);
        return std::clamp ((area.getBottom() - y) / std::max (1.0f, area.getHeight()), 0.0f, 1.0f);
    }

    Point<float> SampleEditor::pointXY (const editing::EnvelopePoint& p) const
    {
        return { xAt (p.time), yForGain (p.gainDb <= -60.0f ? 0.0f : Decibels::decibelsToGain (p.gainDb)) };
    }

    Point<float> SampleEditor::handleXY (int segment)
    {
        // Halfway along the line in time, on the line as it plays.
        const auto& a = envelope[static_cast<size_t> (segment)];
        const auto& b = envelope[static_cast<size_t> (segment) + 1];
        const auto t = (a.time + b.time) * 0.5;
        auto* c = clip();
        return { xAt (t), yForGain (c != nullptr ? editing::envelopeGainAt (*c, t) : 1.0f) };
    }

    int SampleEditor::pointAt (Point<int> p) const
    {
        for (size_t i = 0; i < envelope.size(); ++i)
            if (pointXY (envelope[i]).getDistanceFrom (p.toFloat()) <= 7.0f)
                return static_cast<int> (i);

        return -1;
    }

    int SampleEditor::handleAt (Point<int> p)
    {
        for (int i = 0; i + 1 < static_cast<int> (envelope.size()); ++i)
            if (handleXY (i).getDistanceFrom (p.toFloat()) <= 6.0f)
                return i;

        return -1;
    }

    void SampleEditor::storeEnvelope()
    {
        if (auto* c = clip())
            editing::setEnvelope (*c, envelope);

        repaint (waveArea);
    }

    SampleEditor::Grab SampleEditor::grabAt (Point<int> p) const
    {
        if (! waveArea.contains (p))
            return Grab::none;

        const auto s = sound();
        const auto x = static_cast<float> (p.x);

        if (std::abs (x - xAt (s.start)) <= 5.0f)              return Grab::start;
        if (std::abs (x - xAt (s.start + s.length)) <= 5.0f)   return Grab::end;
        return Grab::none;
    }

    void SampleEditor::mouseMove (const MouseEvent& e)
    {
        if (shaping && waveArea.contains (e.getPosition()))
        {
            setMouseCursor (pointAt (e.getPosition()) >= 0 ? MouseCursor::DraggingHandCursor
                            : handleAt (e.getPosition()) >= 0 ? MouseCursor::UpDownResizeCursor : MouseCursor::CrosshairCursor);
            return;
        }

        setMouseCursor (grabAt (e.getPosition()) != Grab::none ? MouseCursor::LeftRightResizeCursor
                        : waveArea.contains (e.getPosition()) ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
    }

    void SampleEditor::mouseDown (const MouseEvent& e)
    {
        grabKeyboardFocus();
        dragPoint = dragBend = -1;
        lastDrag = e.position;

        if (shaping && waveArea.contains (e.getPosition()))
        {
            const auto hit = pointAt (e.getPosition());

            if (e.mods.isPopupMenu())
            {
                if (hit >= 0)
                {
                    beginGesture ("Remove point");
                    envelope.erase (envelope.begin() + hit);
                    storeEnvelope();
                }
                return;
            }

            if (hit >= 0)
            {
                beginGesture ("Move point");
                dragPoint = hit;
                return;
            }

            if (const auto handle = handleAt (e.getPosition()); handle >= 0)
            {
                beginGesture ("Bend shape");
                dragBend = handle;
                bendAtStart = envelope[static_cast<size_t> (handle)].bend;
                return;
            }

            // A new point. The first one also puts points at both ends, at full level, so the
            // shape covers the whole sound.
            beginGesture ("Add point");
            const auto s = sound();
            const auto level = gainForY (static_cast<float> (e.y));
            const editing::EnvelopePoint added { secondsAt (static_cast<float> (e.x)), level <= 0.001f ? -60.0f : Decibels::gainToDecibels (level), 0.0f };

            if (envelope.empty())
            {
                envelope.push_back ({ s.start, 0.0f, 0.0f });
                envelope.push_back ({ s.start + s.length, 0.0f, 0.0f });
            }

            envelope.push_back (added);
            std::sort (envelope.begin(), envelope.end(), [] (const auto& a, const auto& b) { return a.time < b.time; });

            for (size_t i = 0; i < envelope.size(); ++i)
                if (std::abs (envelope[i].time - added.time) < 1.0e-9)
                    dragPoint = static_cast<int> (i);

            storeEnvelope();
            return;
        }

        grab = grabAt (e.getPosition());

        if (grab != Grab::none)
        {
            const auto s = sound();
            grabStart = s.start;
            grabLength = s.length;
            beginGesture (grab == Grab::start ? "Sample start" : "Sample end");
            return;
        }

        // A click plays from there, as in the browser.
        if (waveArea.contains (e.getPosition()) && file.existsAsFile())
            session.getPreview().play (file, secondsAt (static_cast<float> (e.x)));
    }

    void SampleEditor::mouseDrag (const MouseEvent& e)
    {
        // Shape drags move by the mouse's steps; Ctrl halves them, for fine work (owner).
        const auto step = (e.position - lastDrag) * (e.mods.isCtrlDown() ? 0.5f : 1.0f);
        lastDrag = e.position;

        if (dragPoint >= 0)
        {
            // Between its neighbours in time; the level from the height.
            auto& p = envelope[static_cast<size_t> (dragPoint)];
            const auto lo = dragPoint > 0 ? envelope[static_cast<size_t> (dragPoint) - 1].time + 0.001 : 0.0;
            const auto hi = dragPoint + 1 < static_cast<int> (envelope.size()) ? envelope[static_cast<size_t> (dragPoint) + 1].time - 0.001 : fileSeconds;
            const auto secondsPerPixel = fileSeconds / std::max (1, waveArea.getWidth());
            p.time = std::clamp (p.time + static_cast<double> (step.x) * secondsPerPixel, lo, std::max (lo, hi));
            const auto y = pointXY (p).y + step.y;
            const auto level = gainForY (y);
            p.gainDb = level <= 0.001f ? -60.0f : Decibels::gainToDecibels (level);
            storeEnvelope();
            return;
        }

        if (dragBend >= 0)
        {
            // Up bends the line up, down bends it down. Tracktion bends a line relative to its
            // direction, so a rising line takes the opposite sign to a falling one (owner).
            const auto& from = envelope[static_cast<size_t> (dragBend)];
            const auto& to = envelope[static_cast<size_t> (dragBend) + 1];
            const auto rising = to.gainDb > from.gainDb;
            auto& bend = envelope[static_cast<size_t> (dragBend)].bend;
            bend = std::clamp (bend + (rising ? -step.y : step.y) / 80.0f, -1.0f, 1.0f);
            storeEnvelope();
            return;
        }

        if (grab == Grab::none)
            return;

        const auto at = secondsAt (static_cast<float> (e.x));
        const auto end = grabStart + grabLength;

        apply ([&] (editing::SampleSound& s)
        {
            if (grab == Grab::start)
            {
                s.start = std::clamp (at, 0.0, end - 0.01);
                s.length = end - s.start;
            }
            else
            {
                s.start = grabStart;
                s.length = std::clamp (at - grabStart, 0.01, fileSeconds - grabStart);
            }
        });
    }

    void SampleEditor::mouseUp (const MouseEvent&)
    {
        if (dragPoint >= 0 || dragBend >= 0)
        {
            dragPoint = dragBend = -1;
            refresh();
            return;
        }

        if (grab != Grab::none)
            refresh();

        grab = Grab::none;
    }

    void SampleEditor::mouseDoubleClick (const MouseEvent& e)
    {
        if (! shaping || ! waveArea.contains (e.getPosition()))
            return;

        // A double-click on a line's handle straightens it; on a point, the lines either side.
        auto straighten = [this] (int segment)
        {
            if (segment >= 0 && segment + 1 < static_cast<int> (envelope.size()))
                envelope[static_cast<size_t> (segment)].bend = 0.0f;
        };

        const auto handle = handleAt (e.getPosition());
        const auto point = pointAt (e.getPosition());

        if (handle < 0 && point < 0)
            return;

        beginGesture ("Straighten");
        dragPoint = dragBend = -1;

        if (handle >= 0)
            straighten (handle);
        else
        {
            straighten (point - 1);
            straighten (point);
        }

        storeEnvelope();
        refresh();
    }

    bool SampleEditor::keyPressed (const KeyPress& k)
    {
        if (k == KeyPress::escapeKey)
        {
            if (onClose) onClose();
            return true;
        }

        return false;
    }

    //==============================================================================================
    void SampleEditor::paint (Graphics& g)
    {
        g.fillAll (theme::bg2);
        g.setColour (theme::line);
        g.fillRect (0, 0, getWidth(), 1);

        if (factsDividerX >= 0 && facts.getText().isNotEmpty())
            g.fillRect (factsDividerX, facts.getY() + 4, 1, facts.getHeight() - 8);

        // The waveform: the whole file faint, the part that plays bright, with its fades.
        g.setColour (theme::well);
        g.fillRoundedRectangle (waveArea.toFloat(), theme::radiusSm);

        if (thumbnail.getTotalLength() > 0.0)
        {
            const auto s = sound();
            // Drawn to its own loudest peak (up to 8x), so a quiet take is still readable, then
            // grown or shrunk by the sample's gain, so the gain can be seen (owner).
            const auto zoom = std::clamp (0.9f / std::max (0.001f, thumbnail.getApproximatePeak()), 1.0f, 8.0f) * Decibels::decibelsToGain (s.gainDb);
            g.saveState();
            g.reduceClipRegion (waveArea.reduced (2));
            g.setColour (theme::faint);
            thumbnail.drawChannels (g, waveArea.reduced (2), 0.0, thumbnail.getTotalLength(), zoom);
            g.restoreState();

            const auto x0 = xAt (s.start), x1 = xAt (s.start + s.length);
            const auto playing = Rectangle<float> (x0, static_cast<float> (waveArea.getY()), x1 - x0, static_cast<float> (waveArea.getHeight()));
            g.saveState();
            g.reduceClipRegion (playing.toNearestInt());
            g.setColour (theme::text.withAlpha (0.85f));
            thumbnail.drawChannels (g, waveArea.reduced (2), 0.0, thumbnail.getTotalLength(), zoom);
            g.restoreState();

            // The edges to drag, and the fades as lines from silence.
            g.setColour (theme::accent);
            g.fillRect (playing.withWidth (2.0f));
            g.fillRect (playing.withX (x1 - 2.0f).withWidth (2.0f));

            // The fades in their own shapes, from silence (bottom) to full (top).
            Path fades;
            const auto top = playing.getY() + 2.0f, bottom = playing.getBottom() - 2.0f;
            const auto curve = [&] (double from, double length, tracktion::AudioFadeCurve::Type shape, bool rising)
            {
                constexpr int steps = 24;

                for (int i = 0; i <= steps; ++i)
                {
                    const auto alpha = static_cast<double> (i) / steps;
                    const auto level = tracktion::AudioFadeCurve::alphaToGainForType (shape, static_cast<float> (rising ? alpha : 1.0 - alpha));
                    const Point<float> p (xAt (from + alpha * length), bottom - level * (bottom - top));

                    if (i == 0) fades.startNewSubPath (p); else fades.lineTo (p);
                }
            };

            if (s.fadeIn > 0.0)  curve (s.start, s.fadeIn, s.fadeInShape, true);
            if (s.fadeOut > 0.0) curve (s.start + s.length - s.fadeOut, s.fadeOut, s.fadeOutShape, false);
            g.setColour (theme::accent.withAlpha (0.6f));
            g.strokePath (fades, PathStrokeType (1.25f));

            // The envelope as it plays: read from the clip's own curve, so bends are drawn true.
            if (! envelope.empty())
            {
                if (auto* c = clip())
                {
                    Path line;
                    const auto from = waveArea.getX(), to = waveArea.getRight();

                    for (int x = from; x <= to; x += 2)
                    {
                        const Point<float> p (static_cast<float> (x), yForGain (editing::envelopeGainAt (*c, secondsAt (static_cast<float> (x)))));
                        if (x == from) line.startNewSubPath (p); else line.lineTo (p);
                    }

                    g.setColour (shaping ? theme::warn : theme::warn.withAlpha (0.45f));
                    g.strokePath (line, PathStrokeType (shaping ? 2.0f : 1.25f));

                    if (shaping)
                    {
                        for (const auto& p : envelope)
                        {
                            const auto at = pointXY (p);
                            g.setColour (theme::bg2);
                            g.fillEllipse (Rectangle<float> (9.0f, 9.0f).withCentre (at));
                            g.setColour (theme::warn);
                            g.drawEllipse (Rectangle<float> (9.0f, 9.0f).withCentre (at), 2.0f);
                        }

                        for (int i = 0; i + 1 < static_cast<int> (envelope.size()); ++i)
                        {
                            g.setColour (theme::warn.withAlpha (0.7f));
                            g.fillEllipse (Rectangle<float> (6.0f, 6.0f).withCentre (handleXY (i)));
                        }
                    }
                }
            }
            else if (shaping)
            {
                g.setColour (theme::warn);
                g.setFont (theme::uiFont (theme::textSm, true));
                g.drawText ("Click to add the first point", waveArea.reduced (theme::space2), Justification::topLeft, false);
            }

            if (s.reversed)
            {
                g.setColour (theme::accentText);
                g.setFont (theme::uiFont (theme::textSm, true));
                g.drawText ("Plays reversed", waveArea.reduced (theme::space2), Justification::topRight, false);
            }
        }
        else
        {
            g.setColour (theme::muted);
            g.setFont (theme::uiFont (theme::textSm));
            g.drawText (file.existsAsFile() ? String::fromUTF8 ("Reading the waveform\xe2\x80\xa6") : String ("The audio file is missing"),
                        waveArea, Justification::centred, false);
        }
    }

    void SampleEditor::resized()
    {
        // The agreed layout (4f): the name and close across the top; the clip count and the key
        // under it; the waveform with its tools above and its actions below; the sound and the
        // fades in a column on the right.
        auto r = getLocalBounds().reduced (theme::space4, theme::space3);

        auto top = r.removeFromTop (rowHeight);
        closeButton.setBounds (top.removeFromRight (rowHeight));
        top.removeFromRight (theme::space2);
        name.setBounds (top);

        auto info = r.removeFromTop (20);
        const auto sharedWidth = GlyphArrangement::getStringWidthInt (shared.getFont(), shared.getText()) + 2;
        shared.setBounds (info.removeFromLeft (std::min (sharedWidth, info.getWidth() / 2)));
        info.removeFromLeft (theme::space3);
        factsDividerX = info.getX();
        info.removeFromLeft (theme::space3);
        facts.setBounds (info);
        r.removeFromTop (theme::space3);

        // The right column: Sound (gain, pitch, formant), then Fades (in and out with shapes).
        auto controls = r.removeFromRight (controlsWidth);
        r.removeFromRight (theme::space6);

        const auto row = [&controls] (Label& label, Slider& slider, ComboBox* shape)
        {
            auto line = controls.removeFromTop (sliderRow);
            label.setBounds (line.removeFromLeft (labelWidth));

            if (shape != nullptr)   // the shape next to its name, so every figure ends on one edge
            {
                shape->setBounds (line.removeFromLeft (96));
                line.removeFromLeft (theme::space2);
            }

            slider.setBounds (line);
            controls.removeFromTop (theme::space1);
        };

        soundHeading.setBounds (controls.removeFromTop (headingHeight));
        controls.removeFromTop (theme::space1);
        row (gainLabel, gain, nullptr);
        row (pitchLabel, pitch, nullptr);
        row (formantLabel, formant, nullptr);
        controls.removeFromTop (theme::space2);
        fadesHeading.setBounds (controls.removeFromTop (headingHeight));
        controls.removeFromTop (theme::space1);
        row (fadeInLabel, fadeIn, &fadeInShape);
        row (fadeOutLabel, fadeOut, &fadeOutShape);

        // The waveform's tools above it, its actions below it.
        auto tools = r.removeFromTop (rowHeight);
        playButton.setBounds (tools.removeFromLeft (rowHeight));
        tools.removeFromLeft (theme::space2);
        shapeButton.setBounds (tools.removeFromLeft (104));
        tools.removeFromLeft (theme::space1);
        clearShape.setBounds (tools.removeFromLeft (104));
        r.removeFromTop (theme::space2);

        auto actions = r.removeFromBottom (rowHeight);
        r.removeFromBottom (theme::space2);
        reverse.setBounds (actions.removeFromLeft (96));
        actions.removeFromLeft (theme::space2);
        fit.setBounds (actions.removeFromLeft (110));
        actions.removeFromLeft (theme::space1);
        unique.setBounds (actions.removeFromLeft (110));

        waveArea = r;
    }
}
