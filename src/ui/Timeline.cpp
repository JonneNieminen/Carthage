#include "Timeline.h"

namespace carthage::ui
{
    using namespace juce;

    namespace
    {
        const Colour* laneColours[] = { &theme::laneSky, &theme::laneTide, &theme::laneKelp, &theme::laneSand,
                                        &theme::laneCoral, &theme::laneMurex, &theme::laneRose, &theme::laneSlate };
    }

    //==============================================================================================
    /** Bars and beats, labelled where there is room; a click moves the playhead. */
    class Timeline::Ruler final : public Component
    {
    public:
        explicit Ruler (Timeline& t) : timeline (t) {}

        void paint (Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.fillAll (theme::bg2);
            g.setColour (theme::line);
            g.fillRect (0.0f, r.getBottom() - 1.0f, r.getWidth(), 1.0f);

            const auto bar = timeline.beatsPerBar();
            const auto ppb = timeline.pixelsPerBeat;
            const auto first = timeline.beatAtX (static_cast<float> (getX()));
            const auto last = timeline.beatAtX (static_cast<float> (getRight()));

            // A label every bar, or every few bars when bars are narrow.
            auto labelEvery = 1.0;
            while (labelEvery * bar * ppb < 48.0)
                labelEvery *= 2.0;

            g.setFont (theme::timeFont (theme::textXs));

            for (auto b = std::floor (first / bar) * bar; b <= last; b += bar)
            {
                const auto x = timeline.xAtBeat (b) - static_cast<float> (getX());
                const auto barNumber = static_cast<int> (std::round (b / bar)) + 1;
                const bool labelled = std::fmod (barNumber - 1, labelEvery) == 0.0;

                g.setColour (labelled ? theme::lineStrong : theme::line);
                g.fillRect (x, labelled ? r.getHeight() * 0.45f : r.getHeight() * 0.7f, 1.0f, r.getHeight());

                if (labelled)
                {
                    g.setColour (theme::muted);
                    g.drawText (String (barNumber), Rectangle<float> (x + 4.0f, 2.0f, 48.0f, r.getHeight() * 0.5f),
                                Justification::centredLeft, false);
                }

                if (bar * ppb >= 40.0)   // beats inside the bar, when there is room
                    for (int i = 1; i < static_cast<int> (bar); ++i)
                    {
                        g.setColour (theme::line);
                        g.fillRect (x + static_cast<float> (i * ppb), r.getHeight() * 0.8f, 1.0f, r.getHeight());
                    }
            }
        }

        void mouseDown (const MouseEvent& e) override   { moveTo (e); }
        void mouseDrag (const MouseEvent& e) override   { moveTo (e); }

    private:
        Timeline& timeline;

        void moveTo (const MouseEvent& e)
        {
            const auto beat = std::max (0.0, timeline.beatAtX (static_cast<float> (e.x + getX())));
            timeline.session.getEdit().getTransport().setPosition (tracktion::TimePosition::fromSeconds (timeline.beatsToSeconds (beat)));
        }
    };

    //==============================================================================================
    /** The lane names, with each lane's colour. */
    class Timeline::Headers final : public Component
    {
    public:
        explicit Headers (Timeline& t) : timeline (t) {}

        void paint (Graphics& g) override
        {
            g.fillAll (theme::bg2);
            g.setColour (theme::line);
            g.fillRect (getWidth() - 1, 0, 1, getHeight());

            const auto tracks = timeline.laneTracks();

            for (int i = 0; i < tracks.size(); ++i)
            {
                const auto y = i * laneHeight - timeline.scrollY;

                if (y + laneHeight < 0 || y > getHeight())
                    continue;

                auto row = Rectangle<int> (0, y, getWidth() - 1, laneHeight);
                g.setColour (theme::line);
                g.fillRect (row.removeFromBottom (1));

                auto inner = row.reduced (theme::space3, theme::space2);
                g.setColour (timeline.laneColour (i));
                g.fillRoundedRectangle (inner.removeFromLeft (4).toFloat(), 2.0f);
                inner.removeFromLeft (theme::space2);

                g.setColour (theme::text);
                g.setFont (theme::uiFont (theme::textMd, true));
                g.drawText (tracks[i]->getName(), inner.removeFromTop (20), Justification::centredLeft, true);

                g.setColour (theme::muted);
                g.setFont (theme::uiFont (theme::textSm));
                const auto count = tracks[i]->getClips().size();
                g.drawText (count == 0 ? String ("Empty") : String (count) + (count == 1 ? " clip" : " clips"),
                            inner.removeFromTop (18), Justification::centredLeft, true);
            }
        }

    private:
        Timeline& timeline;
    };

    //==============================================================================================
    /** The lanes: grid lines, then each visible clip with its waveform. */
    class Timeline::Lanes final : public Component, private ChangeListener
    {
    public:
        explicit Lanes (Timeline& t) : timeline (t) { setOpaque (true); }

        ~Lanes() override
        {
            for (auto* t : watched)
                t->removeChangeListener (this);
        }

        void paint (Graphics& g) override
        {
            g.fillAll (theme::well);

            const auto clip = g.getClipBounds();
            const auto first = timeline.beatAtX (static_cast<float> (getX() + clip.getX()));
            const auto last = timeline.beatAtX (static_cast<float> (getX() + clip.getRight()));
            const auto bar = timeline.beatsPerBar();
            const auto ppb = timeline.pixelsPerBeat;

            // Grid lines: bars stronger than the grid's own steps. Drawn at least 10 px apart (the
            // grid still snaps at its own step); closer lines read as texture, not as a grid.
            auto shown = timeline.grid;
            while (shown.stepBeats (bar, ppb) * ppb < 10.0 && shown.division != time::Division::bar)
                shown.division = static_cast<time::Division> (static_cast<int> (shown.division) - 1);

            for (auto b : shown.lines (first, last, bar, ppb))
            {
                const bool isBar = std::abs (std::fmod (b, bar)) < 1.0e-9;
                g.setColour (isBar ? theme::lineStrong : theme::line);
                g.fillRect (timeline.xAtBeat (b) - static_cast<float> (getX()), static_cast<float> (clip.getY()), 1.0f,
                            static_cast<float> (clip.getHeight()));
            }

            const auto tracks = timeline.laneTracks();

            for (int i = 0; i < tracks.size(); ++i)
            {
                const auto y = i * laneHeight - timeline.scrollY;

                if (y + laneHeight < clip.getY() || y > clip.getBottom())
                    continue;   // lane out of view

                g.setColour (theme::line);
                g.fillRect (clip.getX(), y + laneHeight - 1, clip.getWidth(), 1);

                for (auto* c : tracks[i]->getClips())
                    paintClip (g, *c, i, y, first, last);
            }

            if (tracks.size() == 1 && tracks.getFirst()->getClips().isEmpty())
            {
                g.setColour (theme::muted);
                g.setFont (theme::uiFont());
                g.drawText ("Takes you record appear here, at the time you played them.", getLocalBounds(), Justification::centred, true);
            }
        }

    private:
        Timeline& timeline;
        Array<AudioThumbnail*> watched;

        void paintClip (Graphics& g, tracktion::Clip& c, int lane, int laneY, double firstBeat, double lastBeat)
        {
            const auto pos = c.getPosition();
            const auto startBeat = timeline.secondsToBeats (pos.getStart().inSeconds());
            const auto endBeat = timeline.secondsToBeats (pos.getEnd().inSeconds());

            if (endBeat < firstBeat || startBeat > lastBeat)
                return;   // culled to the viewport

            const auto colour = timeline.laneColour (lane);
            const auto x0 = timeline.xAtBeat (startBeat) - static_cast<float> (getX());
            const auto x1 = timeline.xAtBeat (endBeat) - static_cast<float> (getX());
            const Rectangle<float> body (x0, static_cast<float> (laneY + 3), std::max (2.0f, x1 - x0), static_cast<float> (laneHeight - 7));

            const bool muted = c.disabled.get();
            g.setColour (colour.withAlpha (muted ? 0.06f : 0.16f));
            g.fillRoundedRectangle (body, theme::radiusXs);
            g.setColour (colour.withAlpha (muted ? 0.3f : 0.55f));
            g.drawRoundedRectangle (body.reduced (0.5f), theme::radiusXs, theme::stroke);

            if (auto* wave = dynamic_cast<tracktion::WaveAudioClip*> (&c))
            {
                auto& thumb = timeline.thumbnailFor (wave->getOriginalFile());

                if (! watched.contains (&thumb))
                {
                    watched.add (&thumb);
                    thumb.addChangeListener (this);
                }

                const auto waveArea = body.withTrimmedTop (16.0f).reduced (1.0f, 3.0f);
                const auto from = pos.getOffset().inSeconds() * wave->getSpeedRatio();
                const auto to = from + pos.getLength().inSeconds() * wave->getSpeedRatio();

                // Only the visible part of a long clip is drawn.
                const auto visible = waveArea.getIntersection (Rectangle<float> (0.0f, waveArea.getY(), static_cast<float> (getWidth()), waveArea.getHeight()));

                if (thumb.getTotalLength() > 0.0 && ! visible.isEmpty())
                {
                    const auto secondsPerPixel = (to - from) / static_cast<double> (waveArea.getWidth());
                    const auto visFrom = from + (visible.getX() - waveArea.getX()) * secondsPerPixel;
                    const auto visTo = visFrom + visible.getWidth() * secondsPerPixel;
                    g.setColour (colour.withAlpha (muted ? 0.35f : 1.0f));
                    thumb.drawChannels (g, visible.toNearestInt(), visFrom, visTo, 1.0f);
                }
            }

            if (body.getWidth() > 24.0f)
            {
                g.setColour (muted ? theme::muted : theme::text);
                g.setFont (theme::uiFont (theme::textXs, true));
                g.drawText (c.getName(), body.reduced (6.0f, 2.0f).withHeight (14.0f), Justification::centredLeft, true);
            }
        }

        void changeListenerCallback (ChangeBroadcaster*) override   { repaint(); }
    };

    //==============================================================================================
    class Timeline::Playhead final : public Component
    {
    public:
        Playhead()  { setInterceptsMouseClicks (false, false); }

        void paint (Graphics& g) override
        {
            g.setColour (theme::accent);
            g.fillRect (getLocalBounds().toFloat().withSizeKeepingCentre (2.0f, static_cast<float> (getHeight())));
        }
    };

    class Timeline::RecordingRegion final : public Component
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
    Timeline::Timeline (engine::Session& s)
        : session (s), editState (s.getEdit().state)
    {
        formats.registerBasicFormats();
        ruler = std::make_unique<Ruler> (*this);
        headers = std::make_unique<Headers> (*this);
        lanes = std::make_unique<Lanes> (*this);
        playhead = std::make_unique<Playhead>();
        recording = std::make_unique<RecordingRegion>();

        for (auto* c : std::initializer_list<Component*> { ruler.get(), headers.get(), lanes.get(), &hScroll, &vScroll })
            addAndMakeVisible (c);

        addChildComponent (*recording);
        addAndMakeVisible (*playhead);

        hScroll.setAutoHide (false);
        vScroll.setAutoHide (true);
        hScroll.addListener (this);
        vScroll.addListener (this);
        editState.addListener (this);
    }

    Timeline::~Timeline()
    {
        editState.removeListener (this);
        hScroll.removeListener (this);
        vScroll.removeListener (this);
    }

    //==============================================================================================
    Array<tracktion::AudioTrack*> Timeline::laneTracks() const
    {
        return tracktion::getAudioTracks (session.getEdit());
    }

    Colour Timeline::laneColour (int laneIndex) const
    {
        return *laneColours[static_cast<size_t> (laneIndex) % std::size (laneColours)];
    }

    double Timeline::secondsToBeats (double seconds) const
    {
        return session.getEdit().tempoSequence.toBeats (tracktion::TimePosition::fromSeconds (seconds)).inBeats();
    }

    double Timeline::beatsToSeconds (double beats) const
    {
        return session.getEdit().tempoSequence.toTime (tracktion::BeatPosition::fromBeats (beats)).inSeconds();
    }

    double Timeline::beatsPerBar() const
    {
        const auto& sig = session.getEdit().tempoSequence.getTimeSigAt (tracktion::TimePosition());
        return static_cast<double> (sig.numerator.get()) * 4.0 / static_cast<double> (sig.denominator.get());
    }

    Rectangle<int> Timeline::lanesArea() const
    {
        return getLocalBounds().withTrimmedLeft (headerWidth).withTrimmedTop (rulerHeight).withTrimmedBottom (12).withTrimmedRight (12);
    }

    double Timeline::beatAtX (float x) const noexcept
    {
        return scrollBeat + (static_cast<double> (x) - headerWidth) / pixelsPerBeat;
    }

    float Timeline::xAtBeat (double beat) const noexcept
    {
        return static_cast<float> (headerWidth + (beat - scrollBeat) * pixelsPerBeat);
    }

    AudioThumbnail& Timeline::thumbnailFor (const File& file)
    {
        auto& slot = thumbnails[file.getFullPathName()];

        if (slot == nullptr)
        {
            slot = std::make_unique<AudioThumbnail> (256, formats, thumbnailCache);
            slot->setSource (new FileInputSource (file));
        }

        return *slot;
    }

    void Timeline::repaintLanes()
    {
        lanes->repaint();
        headers->repaint();
        updateScrollBars();
    }

    //==============================================================================================
    void Timeline::setZoom (double newPixelsPerBeat, double aroundBeat)
    {
        const auto x = xAtBeat (aroundBeat);
        pixelsPerBeat = std::clamp (newPixelsPerBeat, minPixelsPerBeat, maxPixelsPerBeat);
        scrollBeat = std::max (0.0, aroundBeat - (static_cast<double> (x) - headerWidth) / pixelsPerBeat);
        ruler->repaint();
        repaintLanes();
        lastPlayheadSeconds = -1.0;   // reposition on the next frame
    }

    void Timeline::setScrollBeat (double beat)
    {
        scrollBeat = std::max (0.0, beat);
        ruler->repaint();
        repaintLanes();
        lastPlayheadSeconds = -1.0;
    }

    void Timeline::updateScrollBars()
    {
        // Time: the content runs to the last clip plus some room, or at least what is in view.
        double endBeat = 0.0;

        for (auto* t : laneTracks())
            for (auto* c : t->getClips())
                endBeat = std::max (endBeat, secondsToBeats (c->getPosition().getEnd().inSeconds()));

        const auto viewBeats = lanesArea().getWidth() / pixelsPerBeat;
        hScroll.setRangeLimits (0.0, std::max (endBeat + viewBeats * 0.5, scrollBeat + viewBeats), dontSendNotification);
        hScroll.setCurrentRange (scrollBeat, viewBeats, dontSendNotification);

        const auto content = laneTracks().size() * laneHeight;
        vScroll.setRangeLimits (0.0, std::max (content, lanesArea().getHeight()), dontSendNotification);
        vScroll.setCurrentRange (scrollY, lanesArea().getHeight(), dontSendNotification);
    }

    void Timeline::scrollBarMoved (ScrollBar* bar, double newStart)
    {
        if (bar == &hScroll)
            setScrollBeat (newStart);
        else
        {
            scrollY = static_cast<int> (newStart);
            repaintLanes();
        }
    }

    void Timeline::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
    {
        if (e.mods.isCommandDown())
        {
            // Zoom in time around the pointer.
            const auto factor = std::pow (1.15, static_cast<double> (w.deltaY) * 8.0);
            setZoom (pixelsPerBeat * factor, beatAtX (static_cast<float> (e.getEventRelativeTo (this).x)));
            return;
        }

        const bool sideways = e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY);
        const auto delta = sideways ? (e.mods.isShiftDown() ? w.deltaY : w.deltaX) : w.deltaY;

        if (sideways)
            setScrollBeat (scrollBeat - static_cast<double> (delta) * 400.0 / pixelsPerBeat);
        else
        {
            const auto maxY = std::max (0, laneTracks().size() * laneHeight - lanesArea().getHeight());
            scrollY = std::clamp (scrollY - static_cast<int> (delta * 400.0f), 0, maxY);
            repaintLanes();
        }
    }

    void Timeline::mouseMagnify (const MouseEvent& e, float scale)
    {
        setZoom (pixelsPerBeat * static_cast<double> (scale), beatAtX (static_cast<float> (e.getEventRelativeTo (this).x)));
    }

    //==============================================================================================
    void Timeline::setPlayhead (double seconds, std::optional<double> recordingFrom)
    {
        if (seconds == lastPlayheadSeconds && recordingFrom == lastRecordingFrom)
            return;

        lastPlayheadSeconds = seconds;
        lastRecordingFrom = recordingFrom;

        const auto area = lanesArea();
        const auto x = xAtBeat (secondsToBeats (seconds));
        playhead->setVisible (x >= static_cast<float> (area.getX()) && x <= static_cast<float> (area.getRight()));
        playhead->setBounds (roundToInt (x) - 2, 0, 4, area.getBottom());

        if (recordingFrom)
        {
            const auto x0 = std::max (static_cast<float> (area.getX()), xAtBeat (secondsToBeats (*recordingFrom)));
            const auto lane = laneTracks().indexOf (&session.getTrack());
            const auto y = area.getY() + lane * laneHeight - scrollY;
            recording->setBounds (Rectangle<float> (x0, static_cast<float> (y + 3), std::max (2.0f, x - x0), static_cast<float> (laneHeight - 7))
                                      .toNearestInt().getIntersection (area));
            recording->setVisible (true);
        }
        else
        {
            recording->setVisible (false);
        }
    }

    void Timeline::resized()
    {
        const auto area = lanesArea();
        ruler->setBounds (area.getX(), 0, area.getWidth(), rulerHeight);
        headers->setBounds (0, area.getY(), headerWidth, area.getHeight());
        lanes->setBounds (area);
        hScroll.setBounds (area.getX(), area.getBottom(), area.getWidth(), getHeight() - area.getBottom());
        vScroll.setBounds (area.getRight(), area.getY(), getWidth() - area.getRight(), area.getHeight());
        updateScrollBars();
        lastPlayheadSeconds = -1.0;
    }

    void Timeline::paint (Graphics& g)
    {
        g.fillAll (theme::bg);
        g.setColour (theme::bg2);
        g.fillRect (0, 0, headerWidth, rulerHeight);
    }
}
