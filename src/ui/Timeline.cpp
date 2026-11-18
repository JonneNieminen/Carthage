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
            // Solid, so a clip laid over another hides it (its name and waveform would read
            // through a see-through body); the same tint the lane's well would show through it.
            g.setColour (theme::well.interpolatedWith (colour, muted ? 0.06f : 0.16f));
            g.fillRoundedRectangle (body, theme::radiusXs);
            g.setColour (colour.withAlpha (muted ? 0.3f : 0.55f));
            g.drawRoundedRectangle (body.reduced (0.5f), theme::radiusXs, theme::stroke);

            if (timeline.isSelected (c))
            {
                // Selection is live: the accent.
                g.setColour (theme::accent);
                g.drawRoundedRectangle (body.reduced (0.5f), theme::radiusXs, 2.0f);
            }

            if (auto* wave = dynamic_cast<tracktion::WaveAudioClip*> (&c))
            {
                auto& thumb = timeline.thumbnailFor (timeline.fileOf (*wave));

                if (! watched.contains (&thumb))
                {
                    watched.add (&thumb);
                    thumb.addChangeListener (this);
                }

                const auto waveArea = body.withTrimmedTop (16.0f).reduced (1.0f, 3.0f);
                const auto from = pos.getOffset().inSeconds() * wave->getSpeedRatio();
                const auto to = from + pos.getLength().inSeconds() * wave->getSpeedRatio();

                // Only the part being repainted is drawn: a moving playhead repaints a narrow strip,
                // and a long clip's whole waveform would otherwise be traced for it every frame.
                const auto repainted = g.getClipBounds().toFloat();
                const auto visible = waveArea.getIntersection (Rectangle<float> (std::floor (repainted.getX()) - 1.0f, waveArea.getY(),
                                                                                 repainted.getWidth() + 2.0f, waveArea.getHeight()));

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

        //==========================================================================================
        // Tools. Every gesture opens one named transaction on mouse-down; the changes until
        // mouse-up join it, so a whole drag undoes in one step.
        struct Hit
        {
            tracktion::Clip* clip = nullptr;
            enum class Zone { none, body, leftEdge, rightEdge } zone = Zone::none;
        };

        struct Original { tracktion::EditItemID id; double start = 0.0, end = 0.0; };

        enum class Drag { none, move, trimStart, trimEnd, stretch, slip, marquee, paint, erase, mute, zoomRange };

        Drag drag = Drag::none;
        double anchorBeat = 0.0, appliedBeats = 0.0, lastPaintEnd = 0.0;
        int anchorLane = 0, appliedLanes = 0;
        tracktion::EditItemID primary;
        std::vector<Original> originals;
        Array<tracktion::EditItemID> touched;
        Rectangle<int> marquee;
        Point<int> downAt;

        static constexpr int edgePixels = 6;

        double beatAt (int x) const   { return timeline.beatAtX (static_cast<float> (x + getX())); }
        int laneAt (int y) const      { return (y + timeline.scrollY) / laneHeight; }

        Hit clipAt (Point<int> p) const
        {
            const auto lanesList = timeline.laneTracks();
            const auto lane = laneAt (p.y);

            if (lane < 0 || lane >= lanesList.size())
                return {};

            for (auto* c : lanesList[lane]->getClips())
            {
                const auto x0 = timeline.xAtBeat (editing::startBeat (*c)) - static_cast<float> (getX());
                const auto x1 = timeline.xAtBeat (editing::endBeat (*c)) - static_cast<float> (getX());

                const auto px = static_cast<float> (p.x);

                if (px < x0 - 1.0f || px > x1 + 1.0f)
                    continue;

                const auto width = x1 - x0;
                const auto edge = std::min (static_cast<float> (edgePixels), width / 3.0f);

                if (px <= x0 + edge) return { c, Hit::Zone::leftEdge };
                if (px >= x1 - edge) return { c, Hit::Zone::rightEdge };
                return { c, Hit::Zone::body };
            }

            return {};
        }

        double snapped (double beat, bool bypass, const editing::Clips& excluding) const
        {
            return time::snap (beat, timeline.grid, timeline.snapTargets (excluding), timeline.beatsPerBar(),
                               timeline.pixelsPerBeat, bypass).beat;
        }

        tracktion::Clip* clipFor (tracktion::EditItemID id) const   { return tracktion::findClipForID (timeline.session.getEdit(), id); }

        void used()   { if (timeline.onToolUsed) timeline.onToolUsed(); }

        void begin (const String& name)   { editing::begin (timeline.session.getEdit(), name); }

        void mouseMove (const MouseEvent& e) override
        {
            const auto hit = clipAt (e.getPosition());
            const bool edge = hit.zone == Hit::Zone::leftEdge || hit.zone == Hit::Zone::rightEdge;

            switch (timeline.tool)
            {
                case Tool::select:
                case Tool::stretch: setMouseCursor (edge ? MouseCursor::LeftRightResizeCursor
                                                         : hit.clip != nullptr ? MouseCursor::DraggingHandCursor : MouseCursor::NormalCursor); break;
                case Tool::slice:   setMouseCursor (MouseCursor::IBeamCursor); break;
                case Tool::zoom:    setMouseCursor (MouseCursor::CrosshairCursor); break;
                default:            setMouseCursor (MouseCursor::PointingHandCursor); break;
            }
        }

        void mouseDown (const MouseEvent& e) override
        {
            drag = Drag::none;
            downAt = e.getPosition();
            anchorBeat = beatAt (e.x);
            anchorLane = laneAt (e.y);
            appliedBeats = 0.0;
            appliedLanes = 0;
            touched.clear();
            originals.clear();

            const auto hit = clipAt (e.getPosition());
            const bool bypass = e.mods.isCommandDown();
            auto& edit = timeline.session.getEdit();
            const auto lanesList = timeline.laneTracks();

            switch (timeline.tool)
            {
                case Tool::select:
                case Tool::stretch:
                {
                    if (hit.clip == nullptr)
                    {
                        if (! e.mods.isShiftDown())
                            timeline.clearSelection();

                        drag = Drag::marquee;
                        marquee = {};
                        return;
                    }

                    if (e.mods.isShiftDown())
                    {
                        auto clips = timeline.getSelectedClips();
                        if (clips.contains (hit.clip)) clips.removeFirstMatchingValue (hit.clip); else clips.add (hit.clip);
                        timeline.selectOnly (clips);
                    }
                    else if (! timeline.isSelected (*hit.clip))
                    {
                        timeline.selectOnly ({ hit.clip });
                    }

                    timeline.brush = hit.clip->itemID;
                    primary = hit.clip->itemID;
                    const auto edge = hit.zone == Hit::Zone::leftEdge || hit.zone == Hit::Zone::rightEdge;

                    if (edge && e.mods.isCommandDown())          drag = Drag::slip;
                    else if (hit.zone == Hit::Zone::rightEdge && (e.mods.isAltDown() || timeline.tool == Tool::stretch)) drag = Drag::stretch;
                    else if (hit.zone == Hit::Zone::leftEdge)    drag = Drag::trimStart;
                    else if (hit.zone == Hit::Zone::rightEdge)   drag = Drag::trimEnd;
                    else                                         drag = Drag::move;

                    const auto clips = drag == Drag::move ? timeline.getSelectedClips() : editing::Clips { hit.clip };

                    for (auto* c : clips)
                        originals.push_back ({ c->itemID, editing::startBeat (*c), editing::endBeat (*c) });

                    const auto n = static_cast<int> (clips.size());
                    begin (drag == Drag::move ? editing::named ("Move", n) : drag == Drag::slip ? String ("Slip clip")
                           : drag == Drag::stretch ? String ("Stretch clip") : String ("Trim clip"));
                    return;
                }

                case Tool::draw:
                case Tool::paint:
                {
                    if (hit.clip != nullptr)
                    {
                        timeline.brush = hit.clip->itemID;   // choose what to draw
                        timeline.selectOnly ({ hit.clip });
                        return;
                    }

                    auto* source = clipFor (timeline.brush);

                    if (source == nullptr || anchorLane < 0 || anchorLane >= lanesList.size())
                    {
                        if (timeline.onStatus) timeline.onStatus ("Click a clip first to choose what to draw");
                        return;
                    }

                    begin (timeline.tool == Tool::draw ? "Draw clip" : "Paint clips");
                    const auto at = std::max (0.0, snapped (anchorBeat, bypass, {}));

                    if (auto* placed = editing::placeCopy (*source, *lanesList[anchorLane], at))
                    {
                        lastPaintEnd = editing::endBeat (*placed);
                        timeline.selectOnly ({ placed });
                    }

                    drag = timeline.tool == Tool::paint ? Drag::paint : Drag::none;
                    used();
                    return;
                }

                case Tool::slice:
                    if (hit.clip != nullptr)
                    {
                        begin ("Slice clip");
                        editing::split (edit, { hit.clip }, snapped (anchorBeat, bypass, { hit.clip }));
                        used();
                    }
                    return;

                case Tool::erase:
                    begin ("Delete clips");
                    drag = Drag::erase;
                    eraseOrMute (hit);
                    return;

                case Tool::mute:
                    begin ("Mute clips");
                    drag = Drag::mute;
                    eraseOrMute (hit);
                    return;

                case Tool::zoom:
                    drag = Drag::zoomRange;
                    marquee = {};
                    return;
            }
        }

        void eraseOrMute (const Hit& hit)
        {
            if (hit.clip == nullptr || touched.contains (hit.clip->itemID))
                return;

            touched.add (hit.clip->itemID);

            if (drag == Drag::erase)
            {
                timeline.selected.removeFirstMatchingValue (hit.clip->itemID);
                editing::remove ({ hit.clip });
            }
            else
            {
                editing::toggleMute ({ hit.clip });
            }

            used();
        }

        void mouseDrag (const MouseEvent& e) override
        {
            const bool bypass = e.mods.isCommandDown();
            const auto beat = beatAt (e.x);

            switch (drag)
            {
                case Drag::move:
                {
                    auto* lead = clipFor (primary);
                    if (lead == nullptr) return;

                    const auto& o = *std::find_if (originals.begin(), originals.end(), [this] (const Original& x) { return x.id == primary; });
                    editing::Clips moving;
                    for (const auto& x : originals) if (auto* c = clipFor (x.id)) moving.add (c);

                    const auto wanted = snapped (o.start + (beat - anchorBeat), bypass, moving) - o.start;
                    const auto laneShift = std::clamp (laneAt (e.y) - anchorLane, -anchorLane, timeline.laneTracks().size() - 1 - anchorLane);

                    if (std::abs (wanted - appliedBeats) > 1.0e-9 || laneShift != appliedLanes)
                    {
                        editing::move (moving, wanted - appliedBeats, laneShift - appliedLanes);
                        appliedBeats = editing::startBeat (*lead) - o.start;
                        appliedLanes = laneShift;
                        used();
                    }
                    return;
                }

                case Drag::trimStart:
                case Drag::trimEnd:
                case Drag::stretch:
                case Drag::slip:
                {
                    auto* c = clipFor (primary);
                    if (c == nullptr || originals.empty()) return;

                    const auto& o = originals.front();

                    if (drag == Drag::slip)
                    {
                        const auto delta = beat - anchorBeat;
                        editing::slip (*c, delta - appliedBeats);
                        appliedBeats = delta;
                    }
                    else
                    {
                        const auto edge = drag == Drag::trimStart ? o.start : o.end;
                        const auto to = snapped (edge + (beat - anchorBeat), bypass, { c });

                        if (drag == Drag::trimStart)     editing::trimStart (*c, to);
                        else if (drag == Drag::trimEnd)  editing::trimEnd (*c, to);
                        else                             editing::stretchEnd (*c, to);
                    }

                    used();
                    return;
                }

                case Drag::paint:
                {
                    auto* source = clipFor (timeline.brush);
                    const auto lane = std::clamp (anchorLane, 0, timeline.laneTracks().size() - 1);

                    // Copies one after another, as far as the pointer has gone.
                    while (source != nullptr && beat >= lastPaintEnd)
                    {
                        auto* placed = editing::placeCopy (*source, *timeline.laneTracks()[lane], lastPaintEnd);
                        if (placed == nullptr) break;
                        lastPaintEnd = editing::endBeat (*placed);
                        used();
                    }
                    return;
                }

                case Drag::erase:
                case Drag::mute:
                    eraseOrMute (clipAt (e.getPosition()));
                    return;

                case Drag::marquee:
                case Drag::zoomRange:
                    marquee = Rectangle<int> (downAt, e.getPosition());
                    if (drag == Drag::zoomRange) marquee = marquee.withY (0).withHeight (getHeight());
                    selectInMarquee (e.mods.isShiftDown());
                    repaint();
                    return;

                case Drag::none:
                    return;
            }
        }

        void selectInMarquee (bool add)
        {
            if (drag != Drag::marquee)
                return;

            editing::Clips inside = add ? timeline.getSelectedClips() : editing::Clips{};
            const auto lanesList = timeline.laneTracks();

            for (int i = 0; i < lanesList.size(); ++i)
            {
                const auto y = i * laneHeight - timeline.scrollY;

                if (y + laneHeight < marquee.getY() || y > marquee.getBottom())
                    continue;

                for (auto* c : lanesList[i]->getClips())
                {
                    const auto x0 = timeline.xAtBeat (editing::startBeat (*c)) - static_cast<float> (getX());
                    const auto x1 = timeline.xAtBeat (editing::endBeat (*c)) - static_cast<float> (getX());

                    if (x1 >= static_cast<float> (marquee.getX()) && x0 <= static_cast<float> (marquee.getRight()))
                        inside.addIfNotAlreadyThere (c);
                }
            }

            timeline.selectOnly (inside);
        }

        void mouseUp (const MouseEvent& e) override
        {
            if (drag == Drag::zoomRange)
            {
                if (marquee.getWidth() > 8)
                {
                    // Fill the view with the dragged range.
                    const auto from = beatAt (marquee.getX()), to = beatAt (marquee.getRight());
                    timeline.pixelsPerBeat = std::clamp (getWidth() / std::max (to - from, 1.0e-3), minPixelsPerBeat, maxPixelsPerBeat);
                    timeline.setScrollBeat (from);
                }
                else
                {
                    timeline.setZoom (timeline.pixelsPerBeat * (e.mods.isAltDown() ? 0.5 : 2.0), beatAt (e.x));
                }

                used();
            }

            drag = Drag::none;
            marquee = {};
            repaint();
        }

        void paintOverChildren (Graphics& g) override
        {
            if (! marquee.isEmpty())
            {
                g.setColour (theme::accent.withAlpha (0.12f));
                g.fillRect (marquee);
                g.setColour (theme::accent.withAlpha (0.6f));
                g.drawRect (marquee, 1);
            }
        }
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

    void Timeline::projectChanged()
    {
        editState.removeListener (this);
        editState = session.getEdit().state;
        editState.addListener (this);
        selected.clearQuick();
        brush = {};
        // Thumbnails are kept: they belong to audio files, not projects, so a tab coming back
        // draws at once and a sample used in two projects is read once. Resolved paths belong to
        // a project file and are keyed by it (fileOf).
        lastPlayheadSeconds = -1.0;
        lastRecordingFrom.reset();
        setViewState (session.getOpenedView());
        headers->repaint();
        ruler->repaint();
        repaintLanes();

        if (onSelectionChanged)
            onSelectionChanged();
    }

    juce::ValueTree Timeline::getViewState() const
    {
        juce::ValueTree v ("VIEW");
        v.setProperty ("pixelsPerBeat", pixelsPerBeat, nullptr);
        v.setProperty ("scrollBeat", scrollBeat, nullptr);
        v.setProperty ("scrollY", scrollY, nullptr);
        v.setProperty ("gridDivision", static_cast<int> (grid.division), nullptr);
        v.setProperty ("gridFeel", static_cast<int> (grid.feel), nullptr);
        v.setProperty ("gridAdaptive", grid.adaptive, nullptr);
        v.setProperty ("snap", grid.enabled, nullptr);
        return v;
    }

    void Timeline::setViewState (const juce::ValueTree& v)
    {
        const time::Grid defaults;

        // Anything missing or out of range falls back to the defaults: an older file still opens.
        const auto division = static_cast<int> (v.getProperty ("gridDivision", static_cast<int> (defaults.division)));
        const auto feel = static_cast<int> (v.getProperty ("gridFeel", static_cast<int> (defaults.feel)));
        grid.division = division >= 0 && division <= static_cast<int> (time::Division::sixtyFourth) ? static_cast<time::Division> (division) : defaults.division;
        grid.feel = feel >= 0 && feel <= static_cast<int> (time::Feel::dotted) ? static_cast<time::Feel> (feel) : defaults.feel;
        grid.adaptive = v.getProperty ("gridAdaptive", defaults.adaptive);
        grid.enabled = v.getProperty ("snap", defaults.enabled);

        pixelsPerBeat = std::clamp (static_cast<double> (v.getProperty ("pixelsPerBeat", 24.0)), minPixelsPerBeat, maxPixelsPerBeat);
        scrollY = std::max (0, static_cast<int> (v.getProperty ("scrollY", 0)));
        setScrollBeat (v.getProperty ("scrollBeat", 0.0));
        resized();
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

    File Timeline::fileOf (tracktion::WaveAudioClip& clip)
    {
        if (clip.hasAnyTakes())
            return clip.getOriginalFile();   // a comp's file; takes are not made yet (phase 5)

        const auto key = session.getProjectFile().getFullPathName() + "\n" + clip.getSourceFileReference().source.get();
        auto found = resolvedFiles.find (key);

        if (found == resolvedFiles.end())
            found = resolvedFiles.emplace (key, clip.getOriginalFile()).first;

        return found->second;
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
    void Timeline::setTool (Tool t)
    {
        tool = t;
        lanes->setMouseCursor (MouseCursor::NormalCursor);
    }

    bool Timeline::isSelected (const tracktion::Clip& c) const   { return selected.contains (c.itemID); }

    editing::Clips Timeline::getSelectedClips() const
    {
        editing::Clips clips;

        for (const auto& id : selected)
            if (auto* c = tracktion::findClipForID (session.getEdit(), id))
                clips.add (c);

        return clips;
    }

    void Timeline::selectOnly (const editing::Clips& clips)
    {
        selected.clearQuick();

        for (auto* c : clips)
            selected.add (c->itemID);

        lanes->repaint();

        if (onSelectionChanged)
            onSelectionChanged();
    }

    void Timeline::clearSelection()   { selectOnly ({}); }

    double Timeline::playheadBeat() const
    {
        return secondsToBeats (session.getEdit().getTransport().getPosition().inSeconds());
    }

    time::SnapTargets Timeline::snapTargets (const editing::Clips& excluding) const
    {
        time::SnapTargets t;

        for (auto* track : laneTracks())
            for (auto* c : track->getClips())
                if (! excluding.contains (c))
                {
                    t.clipEdges.push_back (editing::startBeat (*c));
                    t.clipEdges.push_back (editing::endBeat (*c));
                }

        for (auto* m : session.getEdit().getMarkerManager().getMarkers())
            t.markers.push_back (editing::startBeat (*m));

        t.playhead = playheadBeat();
        return t;
    }

    void Timeline::splitAtPlayhead()
    {
        auto clips = getSelectedClips();

        if (clips.isEmpty())   // nothing selected: every clip under the playhead
            for (auto* t : laneTracks())
                for (auto* c : t->getClips())
                    if (editing::startBeat (*c) < playheadBeat() && editing::endBeat (*c) > playheadBeat())
                        clips.add (c);

        if (clips.isEmpty())
            return;

        editing::begin (session.getEdit(), editing::named ("Split", clips.size()));
        editing::split (session.getEdit(), clips, playheadBeat());
    }

    void Timeline::duplicateSelection()
    {
        const auto clips = getSelectedClips();

        if (clips.isEmpty())
            return;

        editing::begin (session.getEdit(), editing::named ("Duplicate", clips.size()));
        selectOnly (editing::duplicate (clips));
    }

    void Timeline::deleteSelection()
    {
        const auto clips = getSelectedClips();

        if (clips.isEmpty())
            return;

        editing::begin (session.getEdit(), editing::named ("Delete", clips.size()));
        selected.clearQuick();
        editing::remove (clips);
    }

    void Timeline::nudgeSelection (int steps)
    {
        const auto clips = getSelectedClips();

        if (clips.isEmpty())
            return;

        editing::begin (session.getEdit(), editing::named ("Nudge", clips.size()));
        editing::move (clips, steps * grid.stepBeats (beatsPerBar(), pixelsPerBeat), 0);
    }

    void Timeline::addMarkerAtPlayhead()
    {
        editing::begin (session.getEdit(), "Add marker");
        editing::addMarker (session.getEdit(), playheadBeat());
    }

    void Timeline::loopSelection()
    {
        const auto clips = getSelectedClips();

        if (clips.isEmpty())
            return;

        auto from = editing::startBeat (*clips.getFirst()), to = editing::endBeat (*clips.getFirst());

        for (auto* c : clips)
        {
            from = std::min (from, editing::startBeat (*c));
            to = std::max (to, editing::endBeat (*c));
        }

        editing::begin (session.getEdit(), "Loop selection");
        editing::setLoopRange (session.getEdit(), from, to);
        session.getEdit().getTransport().looping = true;
        ruler->repaint();
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
