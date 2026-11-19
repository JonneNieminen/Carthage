#include "Timeline.h"
#include <app/Commands.h>
#include <engine/AudioFormats.h>
#include "SampleBrowser.h"

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

            const auto firstBar = static_cast<long> (std::floor (first / bar));
            const auto lastBar = static_cast<long> (std::floor (last / bar));

            for (auto i = firstBar; i <= lastBar; ++i)
            {
                const auto b = static_cast<double> (i) * bar;
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
                    for (int beat = 1; beat < static_cast<int> (bar); ++beat)
                    {
                        g.setColour (theme::line);
                        g.fillRect (x + static_cast<float> (beat * ppb), r.getHeight() * 0.8f, 1.0f, r.getHeight());
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
    class Timeline::Headers final : public Component, public TooltipClient
    {
    public:
        explicit Headers (Timeline& t) : timeline (t)
        {
            nameEditor.setFont (theme::uiFont (theme::textMd, true));
            nameEditor.setJustification (Justification::centredLeft);
            nameEditor.setIndents (4, 0);
            nameEditor.onReturnKey = [this] { finishRename (true); };
            nameEditor.onEscapeKey = [this] { finishRename (false); };
            nameEditor.onFocusLost = [this] { finishRename (true); };
            addChildComponent (nameEditor);
        }

        void paint (Graphics& g) override
        {
            g.fillAll (theme::bg2);
            g.setColour (theme::line);
            g.fillRect (getWidth() - 1, 0, 1, getHeight());

            const auto tracks = timeline.laneTracks();

            for (int i = 0; i < tracks.size(); ++i)
            {
                const auto row = rowBounds (i);

                if (row.getBottom() < 0 || row.getY() > getHeight())
                    continue;

                auto* lane = tracks[i];

                if (lane->itemID == timeline.currentLane)
                {
                    g.setColour (theme::hover);   // the current lane: ⌥⌘N adds below it, ⌘↑/↓ move it
                    g.fillRect (row);
                }

                g.setColour (borderUnderPointer == i ? theme::lineStrong : theme::line);
                g.fillRect (row.withTop (row.getBottom() - (borderUnderPointer == i ? 2 : 1)));

                const auto parts = partsOf (row);
                g.setColour (timeline.laneColour (i).withMultipliedAlpha (lane->isMuted (false) ? 0.35f : 1.0f));
                g.fillRoundedRectangle (parts.colourBar.toFloat(), 2.0f);

                if (! (renaming == lane->itemID && nameEditor.isVisible()))
                {
                    g.setColour (lane->isMuted (false) ? theme::muted : theme::text);
                    g.setFont (theme::uiFont (theme::textMd, true));
                    g.drawText (lane->getName(), parts.name, Justification::centredLeft, true);
                }

                pill (g, parts.mute, "M", lane->isMuted (false), theme::warn);
                pill (g, parts.solo, "S", lane->isSolo (false), theme::ok);

                if (parts.detail.getHeight() < 14)
                    continue;   // short lanes show the name only

                g.setColour (theme::muted);
                g.setFont (theme::uiFont (theme::textSm));
                const auto count = lane->getClips().size();
                const auto recordsHere = lane == &timeline.session.getTrack();
                auto detail = count == 0 ? String ("Empty") : String (count) + (count == 1 ? " clip" : " clips");
                if (recordsHere) detail << ", records";
                g.drawText (detail, parts.detail, Justification::centredLeft, true);
            }
        }

        void mouseDown (const MouseEvent& e) override
        {
            resizing = -1;
            moving = {};
            dropIndex = -1;

            if (const auto border = borderAt (e.y); border >= 0 && ! e.mods.isPopupMenu())
            {
                resizing = border;
                heightAtStart = timeline.heightOfLane (border);
                return;
            }

            auto* lane = laneAt (e.y);

            if (lane == nullptr)
                return;

            timeline.currentLane = lane->itemID;
            repaint();
            const auto parts = partsOf (rowBounds (timeline.laneTracks().indexOf (lane)));

            if (e.mods.isPopupMenu())
                showMenu (*lane);
            else if (parts.mute.contains (e.getPosition()))
                e.mods.isCommandDown() ? editing::muteOthers (*lane) : editing::setLaneMuted (*lane, ! lane->isMuted (false));
            else if (parts.solo.contains (e.getPosition()))
                e.mods.isCommandDown() ? editing::soloAlone (*lane) : editing::setLaneSoloed (*lane, ! lane->isSolo (false));
            else
                moving = lane->itemID;   // a drag moves the lane
        }

        void mouseDrag (const MouseEvent& e) override
        {
            if (resizing >= 0)
            {
                if (auto* lane = timeline.laneTracks()[resizing])
                    timeline.setLaneOwnHeight (*lane, heightAtStart + e.getDistanceFromDragStartY());
                return;
            }

            if (moving == tracktion::EditItemID() || std::abs (e.getDistanceFromDragStartY()) < 4)
                return;

            // The gap the lane would go into: before the lane under the pointer's upper half,
            // after it for the lower half.
            const auto tracks = timeline.laneTracks();
            const auto contentY = e.y + timeline.scrollY;
            const auto under = std::clamp (timeline.laneIndexAt (contentY), 0, std::max (0, tracks.size() - 1));
            const auto mid = timeline.laneTop (under) + timeline.heightOfLane (under) / 2;
            dropIndex = std::clamp (contentY < mid ? under : under + 1, 0, tracks.size());
            setMouseCursor (MouseCursor::DraggingHandCursor);
            repaint();
        }

        void mouseUp (const MouseEvent&) override
        {
            if (dropIndex >= 0)
            {
                const auto tracks = timeline.laneTracks();

                for (auto* t : tracks)
                {
                    if (t->itemID == moving)
                    {
                        const auto from = tracks.indexOf (t);
                        editing::moveLane (*t, dropIndex > from ? dropIndex - 1 : dropIndex);   // the gap counts the lane itself
                        break;
                    }
                }
            }

            resizing = -1;
            moving = {};
            dropIndex = -1;
            setMouseCursor (MouseCursor::NormalCursor);
            repaint();
        }

        void mouseDoubleClick (const MouseEvent& e) override
        {
            if (const auto border = borderAt (e.y); border >= 0)
            {
                if (auto* lane = timeline.laneTracks()[border])
                    timeline.setLaneOwnHeight (*lane, std::nullopt);   // back to the default
                return;
            }

            if (auto* lane = laneAt (e.y))
            {
                const auto parts = partsOf (rowBounds (timeline.laneTracks().indexOf (lane)));

                if (! parts.mute.contains (e.getPosition()) && ! parts.solo.contains (e.getPosition()))
                    startRename (*lane);
            }
        }

        void mouseMove (const MouseEvent& e) override
        {
            const auto border = borderAt (e.y);
            setMouseCursor (border >= 0 ? MouseCursor::UpDownResizeCursor : MouseCursor::NormalCursor);
            borderUnderPointer = border;
            repaint();
        }

        void mouseExit (const MouseEvent&) override   { borderUnderPointer = -1; repaint(); }

        void paintOverChildren (Graphics& g) override
        {
            if (dropIndex < 0)
                return;

            // Where the dragged lane goes.
            const auto y = timeline.laneTop (dropIndex) - timeline.scrollY;
            g.setColour (theme::accent);
            g.fillRect (0, std::max (0, y - 1), getWidth() - 1, 2);
        }

        String getTooltip() override
        {
            const auto pos = getMouseXYRelative();
            auto* lane = laneAt (pos.y);

            if (lane == nullptr)
                return {};

            const auto parts = partsOf (rowBounds (timeline.laneTracks().indexOf (lane)));

            const auto cmd = String (app::commands::commandName);

            if (borderAt (pos.y) >= 0)      return "Drag to make this lane taller or shorter; double-click for the default height";
            if (parts.mute.contains (pos))  return (lane->isMuted (false) ? "Unmute this lane" : "Mute this lane") + String (". ") + cmd + "-click: mute every other lane; again: every lane plays";
            if (parts.solo.contains (pos))  return (lane->isSolo (false) ? "Stop soloing this lane" : "Solo this lane") + String (". ") + cmd + "-click: this lane alone; again: no solo";
            return "Click to make it the current lane, drag to move it, double-click to rename; right-click for more";
        }

        void startRename (tracktion::AudioTrack& lane)
        {
            renaming = lane.itemID;
            const auto parts = partsOf (rowBounds (timeline.laneTracks().indexOf (&lane)));
            nameEditor.setBounds (parts.name.expanded (2, 1));
            nameEditor.setText (lane.getName(), false);
            nameEditor.setVisible (true);
            nameEditor.grabKeyboardFocus();
            nameEditor.selectAll();
            repaint();
        }

        /** Rows move when the lanes scroll; an open name editor goes with its row. */
        void resized() override   { finishRename (true); }

    private:
        Timeline& timeline;
        TextEditor nameEditor;
        tracktion::EditItemID renaming;

        struct Parts { Rectangle<int> colourBar, name, mute, solo, detail; };

        int resizing = -1, heightAtStart = 0, dropIndex = -1, borderUnderPointer = -1;
        tracktion::EditItemID moving;

        /** The lane whose bottom border is within 3 px of `y`, or -1. */
        int borderAt (int y) const
        {
            const auto contentY = y + timeline.scrollY;
            const auto count = timeline.laneTracks().size();

            for (int i = 0; i < count; ++i)
                if (std::abs (timeline.laneTop (i + 1) - contentY) <= 3)
                    return i;

            return -1;
        }

        Rectangle<int> rowBounds (int index) const
        {
            return { 0, timeline.laneTop (index) - timeline.scrollY, getWidth() - 1, timeline.heightOfLane (index) };
        }

        static Parts partsOf (Rectangle<int> row)
        {
            Parts p;
            auto inner = row.reduced (theme::space3, theme::space2);
            p.colourBar = inner.removeFromLeft (4);
            inner.removeFromLeft (theme::space2);
            auto top = inner.removeFromTop (20);
            p.solo = top.removeFromRight (20).withSizeKeepingCentre (20, 18);
            top.removeFromRight (theme::space1);
            p.mute = top.removeFromRight (20).withSizeKeepingCentre (20, 18);
            top.removeFromRight (theme::space1);
            p.name = top;
            p.detail = inner.removeFromTop (18);
            return p;
        }

        void pill (Graphics& g, Rectangle<int> r, const String& letter, bool on, Colour onColour) const
        {
            const auto over = isMouseOver() && r.contains (getMouseXYRelative());
            const auto f = r.toFloat();

            if (on)
            {
                g.setColour (onColour);
                g.fillRoundedRectangle (f, theme::radiusXs);
            }
            else
            {
                g.setColour (over ? theme::hover : theme::well);
                g.fillRoundedRectangle (f, theme::radiusXs);
                g.setColour (theme::line);
                g.drawRoundedRectangle (f.reduced (0.5f), theme::radiusXs, theme::stroke);
            }

            g.setColour (on ? theme::onAccent : over ? theme::text : theme::muted);
            g.setFont (theme::uiFont (theme::textSm, true));
            g.drawText (letter, r, Justification::centred, false);
        }

        tracktion::AudioTrack* laneAt (int y) const
        {
            const auto tracks = timeline.laneTracks();
            const auto index = timeline.laneIndexAt (y + timeline.scrollY);
            return y >= 0 && index < tracks.size() ? tracks[index] : nullptr;
        }

        tracktion::AudioTrack* renamingLane() const
        {
            for (auto* t : timeline.laneTracks())
                if (t->itemID == renaming)
                    return t;

            return nullptr;
        }

        void finishRename (bool keep)
        {
            if (! nameEditor.isVisible())
                return;

            nameEditor.setVisible (false);   // first: committing repaints, and focus moves on

            if (auto* lane = renamingLane(); lane != nullptr && keep)
                editing::renameLane (*lane, nameEditor.getText());

            renaming = {};
            repaint();
        }

        void showMenu (tracktion::AudioTrack& lane)
        {
            auto& recordLane = timeline.session.getTrack();
            const auto deletable = editing::canDeleteLane (lane, recordLane);
            const tracktion::AudioTrack::Ptr held (&lane);

            PopupMenu m;
            m.addItem ("Rename", [this, held] { startRename (*held); });
            m.addItem (lane.isMuted (false) ? "Unmute" : "Mute", [held] { editing::setLaneMuted (*held, ! held->isMuted (false)); });
            m.addItem (lane.isSolo (false) ? "Stop soloing" : "Solo", [held] { editing::setLaneSoloed (*held, ! held->isSolo (false)); });
            m.addSeparator();
            PopupMenu add;
            for (int n : { 1, 2, 4, 8 })
                add.addItem (n == 1 ? String ("1 lane") : String (n) + " lanes", [this, held, n] { timeline.addLane (held.get(), n); });
            m.addSubMenu ("Add lanes below", add);
            m.addItem (&lane == &recordLane ? "Delete lane (the input records here)" : "Delete lane", deletable, false, [this, held]
            {
                const auto name = held->getName();
                timeline.selected.clearQuick();
                editing::deleteLane (*held);

                if (timeline.onStatus)
                    timeline.onStatus ("Deleted " + name + ". Undo brings it back.");
            });
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition());
        }
    };

    //==============================================================================================
    /** The lanes: grid lines, then each visible clip with its waveform. */
    class Timeline::Lanes final : public Component, public DragAndDropTarget, public FileDragAndDropTarget,
                                  private ChangeListener
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

            // Shading inside the beat (W41): every other step tinted, so a half beat or a 16th
            // is easy to find when programming drums. Only when the steps are wide enough to see.
            if (const auto shading = timeline.session.getBeatShading(); shading > 0)
            {
                const auto step = 1.0 / static_cast<double> (shading);   // 1: whole beats; 2: halves…

                if (step * ppb >= 6.0)
                {
                    g.setColour (theme::text.withAlpha (0.035f));
                    const auto firstStep = static_cast<long> (std::floor (first / step));
                    const auto lastStep = static_cast<long> (std::ceil (last / step));

                    for (auto k = firstStep; k <= lastStep; ++k)
                    {
                        if ((k & 1) == 0)
                            continue;

                        const auto x0 = timeline.xAtBeat (static_cast<double> (k) * step) - static_cast<float> (getX());
                        const auto x1 = timeline.xAtBeat (static_cast<double> (k + 1) * step) - static_cast<float> (getX());
                        g.fillRect (x0, static_cast<float> (clip.getY()), x1 - x0, static_cast<float> (clip.getHeight()));
                    }

                    // The half beat gets a line of its own.
                    if (shading == 2 || shading == 4)
                    {
                        g.setColour (theme::line);
                        for (auto beat = static_cast<long> (std::floor (first)); static_cast<double> (beat) <= last; ++beat)
                            g.fillRect (timeline.xAtBeat (static_cast<double> (beat) + 0.5) - static_cast<float> (getX()), static_cast<float> (clip.getY()), 1.0f,
                                        static_cast<float> (clip.getHeight()));
                    }
                }
            }

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
                const auto y = timeline.laneTop (i) - timeline.scrollY;
                const auto h = timeline.heightOfLane (i);

                if (y + h < clip.getY() || y > clip.getBottom())
                    continue;   // lane out of view

                g.setColour (theme::line);
                g.fillRect (clip.getX(), y + h - 1, clip.getWidth(), 1);

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
            const Rectangle<float> body (x0, static_cast<float> (laneY + 3), std::max (2.0f, x1 - x0), static_cast<float> (timeline.heightOfLane (lane) - 7));

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

        Rectangle<int> sliceLine;   // where the slice tool will cut, while it is over a clip

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
        int laneAt (int y) const      { return timeline.laneIndexAt (y + timeline.scrollY); }

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

            // The slice tool shows where it will cut: a line through the clip under the pointer,
            // exactly there, free of the grid (W41).
            auto line = Rectangle<int>();

            if (timeline.tool == Tool::slice && hit.clip != nullptr)
            {
                const auto lane = editing::laneOf (*hit.clip);
                const auto top = timeline.laneTop (lane) - timeline.scrollY + 3;
                line = { e.x - 1, top - 3, 3, timeline.heightOfLane (lane) - 1 };
            }

            if (line != sliceLine)
            {
                repaint (sliceLine.expanded (4, 0));
                sliceLine = line;
                repaint (sliceLine.expanded (4, 0));
            }
        }

        void mouseExit (const MouseEvent&) override
        {
            repaint (sliceLine.expanded (4, 0));
            sliceLine = {};
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

            // FL-style right-click delete (a Settings switch, W38): any tool, and a right-drag
            // deletes every clip it crosses, as the erase tool does.
            if (e.mods.isPopupMenu() && timeline.session.getRightClickDeletes())
            {
                begin ("Delete clips");
                drag = Drag::erase;
                eraseOrMute (hit);
                return;
            }

            switch (timeline.tool)
            {
                case Tool::select:
                case Tool::stretch:
                {
                    // FL-style paste by click (a Settings switch, W38): an empty spot gets a copy
                    // of the last clip used; Shift still draws a selection box.
                    if (hit.clip == nullptr && timeline.tool == Tool::select && ! e.mods.isShiftDown()
                         && timeline.session.getClickPlacesClip() && anchorLane >= 0 && anchorLane < lanesList.size())
                    {
                        if (auto* source = clipFor (timeline.brush))
                        {
                            begin ("Place clip");
                            if (auto* placed = editing::placeCopy (*source, *lanesList[anchorLane], std::max (0.0, snapped (anchorBeat, bypass, {}))))
                                timeline.selectOnly ({ placed });
                            used();
                            return;
                        }
                    }

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
                        // Where you click, never on the grid: drag the new edges to the grid after (W41).
                        begin ("Slice clip");
                        editing::split (edit, { hit.clip }, anchorBeat);
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
                const auto y = timeline.laneTop (i) - timeline.scrollY;

                if (y + timeline.heightOfLane (i) < marquee.getY() || y > marquee.getBottom())
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

        //==========================================================================================
        // Dropping sounds: from the sample browser, or audio files from the Finder/Explorer (W30).
        // A ghost shows where the sound lands, snapped like any edit; the drop is one undo step.
        static std::optional<File> sampleIn (const var& description)
        {
            const auto text = description.toString();

            if (! text.startsWith (SampleBrowser::dragPrefix))
                return std::nullopt;

            return File (text.fromFirstOccurrenceOf (SampleBrowser::dragPrefix, false, false));
        }

        static bool isSound (const String& path)
        {
            return engine::SamplePreview::extensions().contains (File (path).getFileExtension().toLowerCase());
        }

        bool isInterestedInDragSource (const SourceDetails& d) override   { return sampleIn (d.description).has_value(); }
        void itemDragEnter (const SourceDetails& d) override              { if (auto f = sampleIn (d.description)) startGhost ({ *f }); itemDragMove (d); }
        void itemDragMove (const SourceDetails& d) override               { moveGhost (d.localPosition); }
        void itemDragExit (const SourceDetails&) override                 { endGhost(); }
        void itemDropped (const SourceDetails& d) override                { moveGhost (d.localPosition); drop(); }

        bool isInterestedInFileDrag (const StringArray& files) override
        {
            return std::any_of (files.begin(), files.end(), [] (const String& f) { return isSound (f); });
        }

        void fileDragEnter (const StringArray& files, int x, int y) override
        {
            Array<File> sounds;

            for (const auto& f : files)
                if (isSound (f))
                    sounds.add (File (f));

            startGhost (sounds);
            moveGhost ({ x, y });
        }

        void fileDragMove (const StringArray&, int x, int y) override     { moveGhost ({ x, y }); }
        void fileDragExit (const StringArray&) override                   { endGhost(); }
        void filesDropped (const StringArray&, int x, int y) override     { moveGhost ({ x, y }); drop(); }

        Array<File> dropping;
        std::vector<double> droppingSeconds;
        double dropBeat = 0.0;
        int dropLane = -1;

        void startGhost (const Array<File>& files)
        {
            dropping = files;
            droppingSeconds.clear();

            for (const auto& f : files)
            {
                // The analysed length if there is one, else the file's header, read once per drag.
                double seconds = 0.0;

                if (const auto a = timeline.session.getSampleLibrary().getAnalysis (f); a && a->seconds > 0.0)
                    seconds = a->seconds;
                else if (std::unique_ptr<AudioFormatReader> r (timeline.formats.createReaderFor (f)); r != nullptr && r->sampleRate > 0)
                    seconds = static_cast<double> (r->lengthInSamples) / r->sampleRate;

                droppingSeconds.push_back (seconds);
            }
        }

        void moveGhost (Point<int> p)
        {
            const auto tracks = timeline.laneTracks();
            const auto lane = std::clamp (laneAt (p.y), 0, std::max (0, tracks.size() - 1));
            const auto beat = std::max (0.0, snapped (beatAt (p.x), ModifierKeys::currentModifiers.isCommandDown(), {}));

            if (lane != dropLane || beat != dropBeat)
            {
                dropLane = lane;
                dropBeat = beat;
                repaint();
            }
        }

        void endGhost()
        {
            dropping.clear();
            dropLane = -1;
            repaint();
        }

        /** The sounds one after another on the lane, from the drop point. */
        void drop()
        {
            const auto tracks = timeline.laneTracks();

            if (dropping.isEmpty() || dropLane < 0 || dropLane >= tracks.size())
            {
                endGhost();
                return;
            }

            auto& edit = timeline.session.getEdit();
            editing::begin (edit, dropping.size() == 1 ? String ("Add sample") : "Add " + String (dropping.size()) + " samples");
            editing::Clips added;
            auto beat = dropBeat;

            for (const auto& f : dropping)
                if (auto* c = editing::insertFile (*tracks[dropLane], f, beat))
                {
                    // A loop of known tempo can follow the project's (Settings > Browser).
                    if (timeline.session.getFitLoopsToTempo())
                        if (const auto a = timeline.session.getSampleLibrary().getAnalysis (f); a && a->bpm)
                            editing::fitToTempo (*c, *a->bpm);

                    added.add (c);
                    beat = editing::endBeat (*c);
                }

            endGhost();
            timeline.selectOnly (added);

            if (added.isEmpty() && timeline.onStatus)
                timeline.onStatus ("Carthage could not read that file");
            else if (timeline.session.getAlwaysCopySamples())
                timeline.session.collectSamples();   // W30: the project keeps its own copy
        }

        void paintOverChildren (Graphics& g) override
        {
            if (! sliceLine.isEmpty())
            {
                const auto x = static_cast<float> (sliceLine.getCentreX());
                g.setColour (theme::accent);
                g.fillRect (Rectangle<float> (x - 0.75f, static_cast<float> (sliceLine.getY()), 1.5f, static_cast<float> (sliceLine.getHeight())));
                Path notch;   // a small mark at the top, so the line reads as "cut here"
                notch.addTriangle (x - 4.0f, static_cast<float> (sliceLine.getY()), x + 4.0f, static_cast<float> (sliceLine.getY()), x, static_cast<float> (sliceLine.getY()) + 5.0f);
                g.fillPath (notch);
            }

            if (! marquee.isEmpty())
            {
                g.setColour (theme::accent.withAlpha (0.12f));
                g.fillRect (marquee);
                g.setColour (theme::accent.withAlpha (0.6f));
                g.drawRect (marquee, 1);
            }

            // The sound being dragged in: where it lands and how long it is.
            if (! dropping.isEmpty() && dropLane >= 0)
            {
                auto start = dropBeat;
                const auto y = static_cast<float> (timeline.laneTop (dropLane) - timeline.scrollY + 3);

                for (size_t i = 0; i < static_cast<size_t> (dropping.size()); ++i)
                {
                    const auto seconds = std::max (0.25, droppingSeconds[i]);
                    const auto startSeconds = timeline.session.getEdit().tempoSequence.toTime (tracktion::BeatPosition::fromBeats (start)).inSeconds();
                    const auto end = timeline.secondsToBeats (startSeconds + seconds);
                    const auto x0 = timeline.xAtBeat (start) - static_cast<float> (getX());
                    const auto x1 = timeline.xAtBeat (end) - static_cast<float> (getX());
                    const Rectangle<float> ghost (x0, y, std::max (4.0f, x1 - x0), static_cast<float> (timeline.heightOfLane (dropLane) - 7));

                    g.setColour (theme::accent.withAlpha (0.10f));
                    g.fillRoundedRectangle (ghost, theme::radiusXs);
                    g.setColour (theme::accent);
                    g.drawRoundedRectangle (ghost.reduced (0.5f), theme::radiusXs, 1.5f);
                    g.setFont (theme::uiFont (theme::textXs, true));
                    g.drawText (dropping[static_cast<int> (i)].getFileNameWithoutExtension(), ghost.reduced (6.0f, 2.0f).withHeight (14.0f),
                                Justification::centredLeft, true);
                    start = end;
                }
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
        : session (s), editState (s.getEdit().state),
          thumbnailCache (64, s.getFolders().settings.getChildFile ("Overviews"))
    {
        carthage::audio::registerReadable (formats);
        ruler = std::make_unique<Ruler> (*this);
        headers = std::make_unique<Headers> (*this);
        lanes = std::make_unique<Lanes> (*this);
        playhead = std::make_unique<Playhead>();
        recording = std::make_unique<RecordingRegion>();

        for (auto* c : std::initializer_list<Component*> { ruler.get(), headers.get(), lanes.get(), &hScroll, &vScroll })
            addAndMakeVisible (c);

        addChildComponent (*recording);
        addAndMakeVisible (*playhead);

        addLaneButton.setTooltip ("Add a lane at the bottom. Right-click a lane's header to add one below it.");
        addLaneButton.onClick = [this] { addLane(); };
        addAndMakeVisible (addLaneButton);

        headerEdge.setTooltip ("Drag to widen the lane names; double-click to fit the longest name");
        headerEdge.getCurrentWidth = [this] { return headerWidth; };
        headerEdge.onResize = [this] (int w) { setHeaderWidth (w); };
        headerEdge.onFit = [this] { setHeaderWidth (fittingHeaderWidth()); };
        addAndMakeVisible (headerEdge);

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
        v.setProperty ("laneHeight", laneHeight, nullptr);
        v.setProperty ("headerWidth", headerWidth, nullptr);
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
        laneHeight = std::clamp (static_cast<int> (v.getProperty ("laneHeight", defaultLaneHeight)), minLaneHeight, maxLaneHeight);
        tops.clear();
        headerWidth = std::clamp (static_cast<int> (v.getProperty ("headerWidth", defaultHeaderWidth)), minHeaderWidth, maxHeaderWidth);
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

    tracktion::AudioTrack* Timeline::findCurrentLane() const
    {
        for (auto* t : laneTracks())
            if (t->itemID == currentLane)
                return t;

        return nullptr;
    }

    void Timeline::addLaneBelowCurrent()   { addLane (findCurrentLane()); }

    void Timeline::moveCurrentLane (int delta)
    {
        auto* lane = findCurrentLane();

        if (lane == nullptr)
        {
            if (onStatus) onStatus ("Click a lane's name first, then move it");
            return;
        }

        editing::moveLane (*lane, laneTracks().indexOf (lane) + delta);
    }

    void Timeline::addLane (tracktion::AudioTrack* after, int count)
    {
        auto* lane = editing::addLanes (session.getEdit(), count, after);

        if (lane == nullptr)
            return;

        // Bring the new lane into view.
        const auto index = laneTracks().indexOf (lane);
        const auto visible = lanesArea().getHeight();
        const auto top = laneTop (index);

        if (top < scrollY || top + heightOfLane (index) > scrollY + visible)
        {
            scrollY = std::max (0, top + heightOfLane (index) - visible);
            repaintLanes();
            resized();
        }

        currentLane = lane->itemID;

        if (onStatus)
            onStatus (count == 1 ? "Added " + lane->getName() + ". Double-click its name to rename it."
                                 : "Added " + String (count) + " lanes");
    }

    void Timeline::repaintLanes()
    {
        tops.clear();   // a lane added, moved, removed or resized
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

    void Timeline::deleteSelection (bool cut)
    {
        const auto clips = getSelectedClips();

        if (clips.isEmpty())
            return;

        editing::begin (session.getEdit(), editing::named (cut ? "Cut" : "Delete", clips.size()));
        selected.clearQuick();
        editing::remove (clips);
    }

    void Timeline::copySelection()
    {
        const auto clips = getSelectedClips();

        if (clips.isEmpty())
            return;

        copied = editing::copy (clips);

        if (onStatus)
            onStatus (clips.size() == 1 ? String ("Copied a clip") : "Copied " + String (clips.size()) + " clips");
    }

    void Timeline::paste()
    {
        if (! canPaste())
            return;

        auto& edit = session.getEdit();
        editing::begin (edit, editing::named ("Paste", copied->count()));
        selectOnly (editing::paste (edit, *copied, secondsToBeats (session.getPositionSeconds())));

        if (session.getAlwaysCopySamples())
            session.collectSamples();   // clips from another project bring their files along (W30)
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

        const auto content = lanesHeight();
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
        if (e.mods.isAltDown())
        {
            // Lane height, keeping the lane under the pointer where it is.
            const auto step = w.deltaY > 0 ? laneHeightStep / 2 : w.deltaY < 0 ? -laneHeightStep / 2 : 0;
            setLaneHeight (laneHeight + step, e.getEventRelativeTo (this).y - lanesArea().getY());
            return;
        }

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
            const auto maxY = std::max (0, lanesHeight() - lanesArea().getHeight());
            scrollY = std::clamp (scrollY - static_cast<int> (delta * 400.0f), 0, maxY);
            repaintLanes();
        }
    }

    namespace
    {
        const Identifier laneHeightProp ("carthageHeight");   // a lane's own height, in its state
    }

    const std::vector<int>& Timeline::laneTops() const
    {
        if (tops.empty())
        {
            const auto tracks = laneTracks();
            tops.reserve (static_cast<size_t> (tracks.size()) + 1);
            tops.push_back (0);

            for (auto* t : tracks)
            {
                const auto& state = t->state;
                const auto h = state.hasProperty (laneHeightProp) ? std::clamp (static_cast<int> (state[laneHeightProp]), minLaneHeight, maxLaneHeight)
                                                                  : laneHeight;
                tops.push_back (tops.back() + h);
            }
        }

        return tops;
    }

    int Timeline::heightOfLane (int index) const
    {
        const auto& t = laneTops();
        const auto count = static_cast<int> (t.size()) - 1;
        return index >= 0 && index < count ? t[static_cast<size_t> (index) + 1] - t[static_cast<size_t> (index)] : laneHeight;
    }

    int Timeline::laneTop (int index) const
    {
        const auto& t = laneTops();
        const auto count = static_cast<int> (t.size()) - 1;

        if (index <= 0)
            return index * laneHeight;

        return index <= count ? t[static_cast<size_t> (index)] : t.back() + (index - count) * laneHeight;
    }

    int Timeline::lanesHeight() const   { return laneTops().back(); }

    int Timeline::laneIndexAt (int contentY) const
    {
        if (contentY < 0)
            return -1;

        const auto& t = laneTops();
        const auto count = static_cast<int> (t.size()) - 1;

        if (contentY >= t.back())
            return count + (contentY - t.back()) / laneHeight;   // below the lanes, in default-height steps

        const auto above = std::upper_bound (t.begin(), t.end(), contentY);
        return static_cast<int> (std::distance (t.begin(), above)) - 1;
    }

    void Timeline::setLaneOwnHeight (tracktion::AudioTrack& lane, std::optional<int> height)
    {
        if (height)
            lane.state.setProperty (laneHeightProp, std::clamp (*height, minLaneHeight, maxLaneHeight), nullptr);
        else
            lane.state.removeProperty (laneHeightProp, nullptr);

        updateScrollBars();
        repaintLanes();
        lastPlayheadSeconds = -1.0;
    }

    int Timeline::fittingHeaderWidth() const
    {
        // The row's fixed parts (paddings, colour bar, M and S) plus the longest name.
        int widest = 0;

        for (auto* t : laneTracks())
            widest = std::max (widest, GlyphArrangement::getStringWidthInt (theme::uiFont (theme::textMd, true), t->getName()));

        return widest + 2 * theme::space3 + 4 + theme::space2 + 2 * 20 + 2 * theme::space1 + 2;
    }

    void Timeline::setHeaderWidth (int w)
    {
        headerWidth = std::clamp (w, minHeaderWidth, maxHeaderWidth);
        resized();
        repaintLanes();
        ruler->repaint();
    }

    void Timeline::setLaneHeight (int newHeight, std::optional<int> keepLaneAtY)
    {
        newHeight = std::clamp (newHeight, minLaneHeight, maxLaneHeight);

        if (newHeight == laneHeight)
            return;

        // Every lane changes by the same step, its own height kept relative; the point under the
        // pointer (or the top) stays put.
        const auto anchor = keepLaneAtY.value_or (0);
        const auto before = std::max (1, lanesHeight());
        const auto contentY = static_cast<double> (scrollY + anchor) / before;
        const auto step = newHeight - laneHeight;
        laneHeight = newHeight;
        tops.clear();

        for (auto* lane : laneTracks())
            if (lane->state.hasProperty (laneHeightProp))
                lane->state.setProperty (laneHeightProp, std::clamp (static_cast<int> (lane->state[laneHeightProp]) + step, minLaneHeight, maxLaneHeight), nullptr);

        scrollY = std::max (0, static_cast<int> (std::lround (contentY * lanesHeight())) - anchor);
        updateScrollBars();
        scrollY = std::clamp (scrollY, 0, std::max (0, lanesHeight() - lanesArea().getHeight()));
        resized();
        repaintLanes();
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
            const auto y = area.getY() + laneTop (lane) - scrollY;
            recording->setBounds (Rectangle<float> (x0, static_cast<float> (y + 3), std::max (2.0f, x - x0), static_cast<float> (heightOfLane (lane) - 7))
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
        headerEdge.setBounds (headerWidth - PanelEdge::thickness / 2, area.getY(), PanelEdge::thickness, area.getHeight());
        headerEdge.toFront (false);
        addLaneButton.setBounds (Rectangle<int> (0, 0, headerWidth, rulerHeight).reduced (theme::space2, 3).removeFromRight (88));   // the pop-out button sits on the left
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
