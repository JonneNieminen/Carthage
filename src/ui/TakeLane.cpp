#include "TakeLane.h"

namespace carthage::ui
{
    using namespace juce;

    namespace
    {
        // Phase 1 has one track; it takes the first lane colour (design/README.md, "Lanes").
        const auto& laneColour = theme::laneSky;
    }

    //==============================================================================================
    class TakeLane::ClipLayer final : public Component, private ChangeListener
    {
    public:
        explicit ClipLayer (TakeLane& l) : lane (l)
        {
            setInterceptsMouseClicks (false, false);
            setBufferedToImage (true);   // the playhead moves over this without repainting it
        }

        ~ClipLayer() override
        {
            for (auto* t : watched)
                t->removeChangeListener (this);
        }

        void watch (AudioThumbnail& t)
        {
            if (! watched.contains (&t))
            {
                watched.add (&t);
                t.addChangeListener (this);
            }
        }

        void paint (Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            const auto lanes = r.withTrimmedTop (static_cast<float> (rulerHeight));

            // Ruler and second lines.
            const auto range = lane.visible;
            const auto pixelsPerSecond = r.getWidth() / static_cast<float> (range.getLength());
            const double step = pixelsPerSecond > 60.0f ? 1.0 : (pixelsPerSecond > 12.0f ? 5.0 : 10.0);

            g.setFont (theme::timeFont (theme::textXs));

            for (double t = std::ceil (range.getStart() / step) * step; t < range.getEnd(); t += step)
            {
                const auto x = lane.xFor (t);
                const bool major = std::fmod (t, step * 5.0) < 1.0e-6;
                g.setColour (major ? theme::lineStrong : theme::line);
                g.fillRect (x, lanes.getY(), 1.0f, lanes.getHeight());

                if (major || pixelsPerSecond * static_cast<float> (step) > 48.0f)
                {
                    g.setColour (theme::muted);
                    const auto secs = static_cast<int> (t);
                    g.drawText (String (secs / 60) + ":" + String (secs % 60).paddedLeft ('0', 2),
                                Rectangle<float> (x + 4.0f, 0.0f, 60.0f, static_cast<float> (rulerHeight)),
                                Justification::centredLeft, false);
                }
            }

            g.setColour (theme::line);
            g.fillRect (0.0f, lanes.getY() - 1.0f, r.getWidth(), 1.0f);

            // Clips: tinted body, a waveform in the lane colour, the name on top.
            const auto clipArea = lanes.reduced (0.0f, static_cast<float> (theme::space3));

            for (auto* clip : lane.track.getClips())
            {
                auto* wave = dynamic_cast<tracktion::WaveAudioClip*> (clip);

                if (wave == nullptr)
                    continue;

                const auto pos = wave->getPosition();
                const auto x0 = lane.xFor (pos.getStart().inSeconds());
                const auto x1 = lane.xFor (pos.getEnd().inSeconds());

                if (x1 < 0.0f || x0 > r.getWidth())
                    continue;   // culled to the viewport

                const Rectangle<float> body (x0, clipArea.getY(), std::max (2.0f, x1 - x0), clipArea.getHeight());
                g.setColour (laneColour.withAlpha (0.16f));
                g.fillRoundedRectangle (body, theme::radiusXs);
                g.setColour (laneColour.withAlpha (0.55f));
                g.drawRoundedRectangle (body.reduced (0.5f), theme::radiusXs, theme::stroke);

                const auto file = wave->getOriginalFile();
                auto& thumb = lane.thumbnailFor (file);
                watch (thumb);

                const auto waveArea = body.withTrimmedTop (18.0f).reduced (1.0f, 4.0f);
                const auto from = pos.getOffset().inSeconds();
                g.setColour (laneColour);

                if (thumb.getTotalLength() > 0.0)
                    thumb.drawChannels (g, waveArea.toNearestInt(), from, from + pos.getLength().inSeconds(), 1.0f);

                g.setColour (theme::text);
                g.setFont (theme::uiFont (theme::textXs, true));
                g.drawText (wave->getName(), body.reduced (6.0f, 3.0f).withHeight (14.0f), Justification::centredLeft, true);
            }

            if (lane.track.getClips().isEmpty())
            {
                g.setColour (theme::muted);
                g.setFont (theme::uiFont());
                g.drawText ("Takes you record appear here, at the time you played them.", lanes, Justification::centred, true);
            }
        }

    private:
        TakeLane& lane;
        Array<AudioThumbnail*> watched;

        void changeListenerCallback (ChangeBroadcaster*) override   { repaint(); }
    };

    //==============================================================================================
    class TakeLane::Playhead final : public Component
    {
    public:
        Playhead()  { setInterceptsMouseClicks (false, false); }

        void paint (Graphics& g) override
        {
            g.setColour (theme::accent);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 1.0f);
        }
    };

    class TakeLane::RecordingRegion final : public Component
    {
    public:
        RecordingRegion()   { setInterceptsMouseClicks (false, false); }

        void paint (Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (theme::clip.withAlpha (0.14f));
            g.fillRoundedRectangle (r, theme::radiusXs);
            g.setColour (theme::clip.withAlpha (0.7f));
            g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusXs, theme::stroke);
        }
    };

    //==============================================================================================
    TakeLane::TakeLane (tracktion::AudioTrack& t)
        : track (t), trackState (t.state)
    {
        formats.registerBasicFormats();
        clips = std::make_unique<ClipLayer> (*this);
        playhead = std::make_unique<Playhead>();
        recording = std::make_unique<RecordingRegion>();

        addAndMakeVisible (*clips);
        addChildComponent (*recording);
        addAndMakeVisible (*playhead);

        trackState.addListener (this);
        updateVisibleRange();
    }

    TakeLane::~TakeLane()
    {
        trackState.removeListener (this);
    }

    AudioThumbnail& TakeLane::thumbnailFor (const File& file)
    {
        auto& slot = thumbnails[file.getFullPathName()];

        if (slot == nullptr)
        {
            slot = std::make_unique<AudioThumbnail> (256, formats, thumbnailCache);
            slot->setSource (new FileInputSource (file));
        }

        return *slot;
    }

    void TakeLane::modelChanged()
    {
        updateVisibleRange();
        clips->repaint();
    }

    void TakeLane::updateVisibleRange()
    {
        double end = 30.0;

        for (auto* clip : track.getClips())
            end = std::max (end, clip->getPosition().getEnd().inSeconds() + 5.0);

        end = std::max (end, lastPlayhead + 5.0);

        if (end > visible.getEnd() || end < visible.getEnd() - 1.0)
        {
            visible = { 0.0, end };
            clips->repaint();
        }
    }

    float TakeLane::xFor (double seconds) const noexcept
    {
        return static_cast<float> ((seconds - visible.getStart()) / visible.getLength()) * static_cast<float> (getWidth());
    }

    void TakeLane::setPlayhead (double seconds, std::optional<double> recordingFrom)
    {
        lastPlayhead = seconds;
        lastRecordingFrom = recordingFrom;

        if (seconds > visible.getEnd() - 1.0)
            updateVisibleRange();

        const auto x = roundToInt (xFor (seconds));
        playhead->setBounds (x - 1, 0, 2, getHeight());

        recording->setVisible (recordingFrom.has_value());

        if (recordingFrom)
        {
            const auto lanes = getLocalBounds().withTrimmedTop (rulerHeight).reduced (0, theme::space3);
            const auto x0 = roundToInt (xFor (*recordingFrom));
            recording->setBounds (lanes.withX (x0).withWidth (std::max (2, x - x0)));
        }
    }

    void TakeLane::resized()
    {
        clips->setBounds (getLocalBounds());
        setPlayhead (lastPlayhead, lastRecordingFrom);
    }

    void TakeLane::paint (Graphics& g)
    {
        g.setColour (theme::well);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), theme::radiusMd);
    }
}
