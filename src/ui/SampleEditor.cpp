#include "SampleEditor.h"

#include <engine/AudioFormats.h>
#include <engine/SampleAnalysis.h>

namespace carthage::ui
{
    using namespace juce;

    namespace
    {
        constexpr int rowHeight = 28, labelWidth = 72, controlsWidth = 300;
    }

    SampleEditor::SampleEditor (engine::Session& s) : session (s)
    {
        audio::registerReadable (formats);
        thumbnail.addChangeListener (this);
        setWantsKeyboardFocus (true);   // Escape and Cmd+W close it while it is in front (W42)

        styleLabel (name, theme::textLg, theme::text, true);
        styleLabel (shared, theme::textSm, theme::muted);
        styleFigure (facts, theme::textSm, theme::muted);

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
        setUp (fadeIn, fadeInLabel, "Fade in", 0.0, 2000.0, 1.0, " ms", "A fade at the start.");
        setUp (fadeOut, fadeOutLabel, "Fade out", 0.0, 2000.0, 1.0, " ms", "A fade at the end.");

        gain.onValueChange = [this] { apply ([this] (auto& s2) { s2.gainDb = static_cast<float> (gain.getValue()); }); };
        pitch.onValueChange = [this] { apply ([this] (auto& s2) { s2.pitch = static_cast<float> (pitch.getValue()); }); };
        fadeIn.onValueChange = [this] { apply ([this] (auto& s2) { s2.fadeIn = fadeIn.getValue() / 1000.0; }); };
        fadeOut.onValueChange = [this] { apply ([this] (auto& s2) { s2.fadeOut = fadeOut.getValue() / 1000.0; }); };

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
            facts.setText ((known->key.isNotEmpty() ? known->key : String ("No key")) + (known->bpm ? "   " + String (*known->bpm, 1) + " bpm" : String()),
                           dontSendNotification);
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
                safe->facts.setText (found ? (found->key.isNotEmpty() ? found->key : String ("No key")) + (found->bpm ? "   " + String (*found->bpm, 1) + " bpm" : String())
                                           : String ("Key and tempo not found"),
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
        readControls();
        repaint();
    }

    void SampleEditor::readControls()
    {
        const auto s = sound();
        gain.setValue (s.gainDb, dontSendNotification);
        pitch.setValue (s.pitch, dontSendNotification);
        fadeIn.setValue (s.fadeIn * 1000.0, dontSendNotification);
        fadeOut.setValue (s.fadeOut * 1000.0, dontSendNotification);
        reverse.setToggleState (s.reversed, dontSendNotification);
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
        setMouseCursor (grabAt (e.getPosition()) != Grab::none ? MouseCursor::LeftRightResizeCursor
                        : waveArea.contains (e.getPosition()) ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
    }

    void SampleEditor::mouseDown (const MouseEvent& e)
    {
        grabKeyboardFocus();
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
        if (grab != Grab::none)
            refresh();

        grab = Grab::none;
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

        // The waveform: the whole file faint, the part that plays bright, with its fades.
        g.setColour (theme::well);
        g.fillRoundedRectangle (waveArea.toFloat(), theme::radiusSm);

        if (thumbnail.getTotalLength() > 0.0)
        {
            const auto s = sound();
            // Drawn to its own loudest peak (up to 8x), so a quiet take is still readable.
            const auto zoom = std::clamp (0.9f / std::max (0.001f, thumbnail.getApproximatePeak()), 1.0f, 8.0f);
            g.setColour (theme::faint);
            thumbnail.drawChannels (g, waveArea.reduced (2), 0.0, thumbnail.getTotalLength(), zoom);

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

            Path fades;
            const auto top = playing.getY() + 2.0f, bottom = playing.getBottom() - 2.0f;
            fades.startNewSubPath (x0, bottom);
            fades.lineTo (xAt (s.start + s.fadeIn), top);
            fades.startNewSubPath (xAt (s.start + s.length - s.fadeOut), top);
            fades.lineTo (x1, bottom);
            g.setColour (theme::accent.withAlpha (0.6f));
            g.strokePath (fades, PathStrokeType (1.25f));

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
        auto r = getLocalBounds().reduced (theme::space4, theme::space3);

        auto top = r.removeFromTop (rowHeight);
        closeButton.setBounds (top.removeFromRight (rowHeight));
        top.removeFromRight (theme::space2);
        unique.setBounds (top.removeFromRight (110));
        top.removeFromRight (theme::space1);
        fit.setBounds (top.removeFromRight (110));
        top.removeFromRight (theme::space2);
        reverse.setBounds (top.removeFromRight (96));
        name.setBounds (top.removeFromLeft (std::min (260, top.getWidth() / 2)));
        shared.setBounds (top.removeFromLeft (std::min (200, top.getWidth() / 2)));
        facts.setBounds (top);
        r.removeFromTop (theme::space3);

        auto controls = r.removeFromRight (controlsWidth);
        r.removeFromRight (theme::space4);
        waveArea = r;

        for (auto [label, slider] : { std::pair<Label*, Slider*> { &gainLabel, &gain }, { &pitchLabel, &pitch }, { &fadeInLabel, &fadeIn }, { &fadeOutLabel, &fadeOut } })
        {
            auto line = controls.removeFromTop (rowHeight);
            label->setBounds (line.removeFromLeft (labelWidth));
            slider->setBounds (line);
            controls.removeFromTop (theme::space1);
        }
    }
}
