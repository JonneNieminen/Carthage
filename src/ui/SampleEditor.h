#pragma once

#include "Controls.h"

#include <engine/SampleEdits.h>
#include <engine/Session.h>

namespace carthage::ui
{
    /** The sample editor (phase 4e, W42): a panel under the lanes for the sample of the clip that
        was double-clicked. Its waveform plays from where it is clicked, as in the browser; its
        bright part is what plays, and its edges drag. Gain, pitch, fades, reverse, Fit to tempo
        and Make unique act on every clip of the sample at once, one undo step per gesture, and
        never change the file. */
    class SampleEditor final : public juce::Component, private juce::ChangeListener
    {
    public:
        explicit SampleEditor (engine::Session&);
        ~SampleEditor() override;

        /** Shows `clip`'s sample. */
        void show (tracktion::Clip&);

        /** The edit changed (an undo, another tab): reads the clip again, or closes if it is gone. */
        void refresh();

        tracktion::EditItemID getClipID() const noexcept   { return clipID; }

        /** Escape, the close button, or the clip gone. */
        std::function<void()> onClose;

        /** A line for the status bar ("Fitted Kick to 120 bpm"). */
        std::function<void (const juce::String&)> onStatus;

        static constexpr int defaultHeight = 230, minHeight = 160, maxHeight = 520;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

    private:
        engine::Session& session;
        tracktion::EditItemID clipID;
        juce::File file;
        double fileSeconds = 0.0;

        juce::AudioFormatManager formats;
        juce::AudioThumbnailCache thumbCache { 4 };
        juce::AudioThumbnail thumbnail { 256, formats, thumbCache };

        juce::Label name, shared, facts;
        ValueSlider gain { 0.0 }, pitch { 0.0 }, fadeIn { 0.0 }, fadeOut { 0.0 };
        juce::Label gainLabel, pitchLabel, fadeInLabel, fadeOutLabel;
        juce::ToggleButton reverse { "Reverse" };
        juce::ComboBox fadeInShape, fadeOutShape;   // linear, convex, concave, S-curve
        juce::TextButton fit { "Fit to tempo" }, unique { "Make unique" };

        // The envelope (owner): Draw shape turns the waveform's clicks into envelope editing,
        // FL-style: click adds a point, drag moves it, right-click removes it, the handle halfway
        // along a line bends it (drag up: the line bulges up), and a double-click straightens it.
        juce::TextButton shapeButton { "Draw shape" }, clearShape { "Clear shape" };
        bool shaping = false;
        std::vector<editing::EnvelopePoint> envelope;
        int dragPoint = -1, dragBend = -1;
        juce::Point<float> lastDrag;   // drags move by steps, so Ctrl (half speed) can change mid-drag
        float bendAtStart = 0.0f;

        float yForGain (float gain) const;
        float gainForY (float y) const;
        juce::Point<float> pointXY (const editing::EnvelopePoint&) const;
        juce::Point<float> handleXY (int segment);
        int pointAt (juce::Point<int>) const;
        int handleAt (juce::Point<int>);
        void storeEnvelope();
        IconButton closeButton { IconButton::Glyph::close };
        std::optional<double> bpm;   // the sample's own tempo, when known

        juce::Rectangle<int> waveArea;
        enum class Grab { none, start, end } grab = Grab::none;
        double grabStart = 0.0, grabLength = 0.0;

        tracktion::Clip* clip() const;
        editing::SampleSound sound() const;
        void apply (const std::function<void (editing::SampleSound&)>& change);
        void beginGesture (const juce::String& what);
        void readControls();
        void analyse();

        float xAt (double seconds) const;
        double secondsAt (float x) const;
        Grab grabAt (juce::Point<int>) const;

        void changeListenerCallback (juce::ChangeBroadcaster*) override   { repaint (waveArea); }
    };
}
