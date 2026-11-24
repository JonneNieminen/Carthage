#include "Editing.h"
#include "AudioFormats.h"

#include <limits>
#include <set>
#include <vector>

#include <map>

namespace carthage::editing
{
    using namespace tracktion;

    namespace
    {
        const juce::Identifier copiedRouteProp ("carthageCopiedRoute");

        TempoSequence& tempo (const Clip& c)            { return c.edit.tempoSequence; }
        TimePosition timeAt (const Clip& c, double beat) { return tempo (c).toTime (BeatPosition::fromBeats (std::max (0.0, beat))); }

        /** How long a clip's audio is, in seconds of the file. Tracktion's figure when it has one
            (it fills it in from its own background work); otherwise the file's header, read once
            per file and remembered, so a drag never reads files. */
        double sourceSeconds (AudioClipBase& audio)
        {
            if (const auto known = audio.getSourceLength().inSeconds(); known > 0.0)
                return known;

            static std::map<juce::String, double> lengths;
            const auto file = audio.getAudioFile().getFile();
            auto& cached = lengths[file.getFullPathName()];

            if (cached <= 0.0)
            {
                static auto formats = [] { auto f = std::make_unique<juce::AudioFormatManager>(); carthage::audio::registerReadable (*f); return f; }();

                std::unique_ptr<juce::AudioFormatReader> reader (formats->createReaderFor (file));

                if (reader != nullptr && reader->sampleRate > 0.0)
                    cached = static_cast<double> (reader->lengthInSamples) / reader->sampleRate;
            }

            return cached;
        }

        /** Overlapping audio clips crossfade automatically (brief, feature 2). */
        void crossfade (Clip& c)
        {
            if (auto* audio = dynamic_cast<AudioClipBase*> (&c))
                audio->setAutoCrossfade (true);
        }
    }

    Edit::FilePathResolver consistentFilePaths (std::function<juce::File()> editFileOf)
    {
        return [editFileOf = std::move (editFileOf)] (const juce::String& path) -> juce::File
        {
            if (juce::File::isAbsolutePath (path))
                return path;

            const auto editFile = editFileOf ? editFileOf() : juce::File();

            if (editFile == juce::File())
                return {};

            // As written: relative to the edit file taken as a folder. Older files, if any, may be
            // relative to the edit's folder; take whichever exists.
            auto asWritten = editFile.getChildFile (path);

            if (asWritten.exists())
                return asWritten;

            if (auto besideEdit = editFile.getSiblingFile (path); besideEdit.exists())
                return besideEdit;

            return asWritten;
        };
    }

    void useConsistentFilePaths (Edit& edit)
    {
        edit.filePathResolver = consistentFilePaths ([&edit] { return edit.editFileRetriever ? edit.editFileRetriever() : juce::File(); });
    }

    std::shared_ptr<const Copied> copy (const Clips& clips)
    {
        auto out = std::make_shared<Copied>();

        if (clips.isEmpty())
            return out;

        out->from = &clips.getFirst()->edit;

        auto first = std::numeric_limits<double>::max();
        out->firstLane = std::numeric_limits<int>::max();

        for (auto* c : clips)
        {
            first = std::min (first, c->getPosition().getStart().inSeconds());
            out->firstLane = std::min (out->firstLane, laneOf (*c));
        }

        for (auto* c : clips)
        {
            auto state = c->state.createCopy();

            // The file as it resolves now, absolute: the clip can land in another project.
            if (auto* audio = dynamic_cast<AudioClipBase*> (c))
                if (const auto file = audio->getOriginalFile(); file != juce::File())
                    state.setProperty (IDs::source, file.getFullPathName(), nullptr);

            state.setProperty (IDs::start, c->getPosition().getStart().inSeconds() - first, nullptr);

            // Its route, by the channel's ID: kept when pasted into the same project (W47).
            if (auto* channel = routeOf (*c))
                state.setProperty (copiedRouteProp, channel->itemID.toString(), nullptr);

            Clipboard::Clips::ClipInfo info;
            info.state = state;
            info.trackOffset = laneOf (*c) - out->firstLane;
            out->clips.clips.push_back (std::move (info));
        }

        return out;
    }

    Clips paste (Edit& edit, const Copied& copied, double atBeat)
    {
        const auto all = lanes (edit);

        if (copied.count() == 0 || all.isEmpty())
            return {};

        // Lanes the edit lacks: the clips go onto its last lanes instead.
        int deepest = 0;

        for (const auto& c : copied.clips.clips)
            deepest = std::max (deepest, c.trackOffset);

        const auto firstLane = std::clamp (copied.firstLane, 0, std::max (0, all.size() - 1 - deepest));
        const auto sameEdit = copied.from == &edit;

        // Each clip onto the carrier of its lane and route; Tracktion pastes one carrier at a time.
        std::map<AudioTrack*, Clipboard::Clips> byCarrier;

        for (const auto& c : copied.clips.clips)
        {
            const auto lane = all[std::min (firstLane + c.trackOffset, all.size() - 1)];
            AudioTrack* route = nullptr;

            if (sameEdit)
                if (const auto id = c.state[copiedRouteProp].toString(); id.isNotEmpty())
                    for (auto* ch : channels (edit))
                        if (ch->itemID.toString() == id)
                            route = ch;

            if (auto* carrier = carrierFor (edit, lane, route))
            {
                auto info = c;
                info.trackOffset = 0;
                info.state = c.state.createCopy();
                info.state.removeProperty (copiedRouteProp, nullptr);
                byCarrier[carrier].clips.push_back (std::move (info));
            }
        }

        std::set<EditItemID> before;

        for (auto* c : allClips (edit))
            before.insert (c->itemID);

        for (auto& [carrier, content] : byCarrier)
        {
            EditInsertPoint insertPoint (edit);
            Clipboard::ContentType::EditPastingOptions options (edit, insertPoint);
            options.startTrack = carrier;
            options.startTime = edit.tempoSequence.toTime (tracktion::BeatPosition::fromBeats (std::max (0.0, atBeat)));
            options.silent = true;
            content.pasteIntoEdit (options);
        }

        Clips added;

        for (auto* c : allClips (edit))
            if (! before.contains (c->itemID))
            {
                crossfade (*c);
                added.add (c);
            }

        return added;
    }

    void begin (Edit& edit, const juce::String& name)
    {
        edit.getUndoManager().beginNewTransaction (name);
    }

    namespace
    {
        /** The values Carthage edits (fader level and pan, send level) back to what the tree
            now holds, where they differ. Setting a parameter writes its value into the tree as
            well, and when the undo has just taken that property away (it was at its default) the
            write is recorded as a new change, which empties the redo list. So the write goes
            through no undo manager: it puts back the very value the tree holds. */
        void parametersFollowTree (Edit& edit)
        {
            auto* um = &edit.getUndoManager();

            const auto follow = [um] (tracktion::AutomatableParameter* param, juce::CachedValue<float>& stored)
            {
                if (param == nullptr || std::abs (param->getCurrentValue() - stored.get()) <= 1.0e-6f)
                    return;

                stored.referTo (stored.getValueTree(), stored.getPropertyID(), nullptr, stored.getDefault());
                param->setParameter (stored.get(), juce::dontSendNotification);
                stored.referTo (stored.getValueTree(), stored.getPropertyID(), um, stored.getDefault());
            };

            for (auto* p : tracktion::getAllPlugins (edit, true))
            {
                if (auto* v = dynamic_cast<tracktion::VolumeAndPanPlugin*> (p))
                {
                    follow (v->volParam.get(), v->volume);
                    follow (v->panParam.get(), v->pan);
                }
                else if (auto* send = dynamic_cast<tracktion::AuxSendPlugin*> (p))
                {
                    follow (send->gain.get(), send->gainLevel);
                }
            }
        }
    }

    bool undo (Edit& edit)
    {
        const auto done = edit.getUndoManager().undo();
        parametersFollowTree (edit);
        return done;
    }

    bool redo (Edit& edit)
    {
        const auto done = edit.getUndoManager().redo();
        parametersFollowTree (edit);
        return done;
    }

    juce::String named (const juce::String& verb, int count, const juce::String& noun)
    {
        return count == 1 ? verb + " " + noun : verb + " " + juce::String (count) + " " + noun + "s";
    }

    double startBeat (const Clip& c)   { return tempo (c).toBeats (c.getPosition().getStart()).inBeats(); }
    double endBeat (const Clip& c)     { return tempo (c).toBeats (c.getPosition().getEnd()).inBeats(); }

    namespace
    {
        const juce::Identifier notesTag ("CARTHAGENOTES"), notesText ("text");
    }

    juce::String notes (Edit& edit)
    {
        return edit.state.getChildWithName (notesTag)[notesText].toString();
    }

    void setNotes (Edit& edit, const juce::String& text)
    {
        if (text == notes (edit))
            return;

        edit.state.getOrCreateChildWithName (notesTag, nullptr).setProperty (notesText, text, nullptr);
    }

    void move (const Clips& clips, double deltaBeats, int laneDelta)
    {
        if (clips.isEmpty())
            return;

        // Never before zero: the earliest clip stops there and the rest keep their spacing.
        auto earliest = startBeat (*clips.getFirst());
        for (auto* c : clips) earliest = std::min (earliest, startBeat (*c));
        deltaBeats = std::max (deltaBeats, -earliest);

        const auto all = lanes (clips.getFirst()->edit);

        for (auto* c : clips)
        {
            c->setStart (timeAt (*c, startBeat (*c) + deltaBeats), false, true);

            if (laneDelta != 0 && ! all.isEmpty())
                moveToLane (*c, all[std::clamp (laneOf (*c) + laneDelta, 0, all.size() - 1)]);   // its route stays

            crossfade (*c);
        }
    }

    void trimStart (Clip& c, double newStartBeat)
    {
        const auto end = c.getPosition().getEnd();
        auto start = timeAt (c, newStartBeat);

        // The content stays put: as the start moves right, the offset into the audio grows.
        const auto offset = c.getPosition().getOffset() + (start - c.getPosition().getStart());

        if (offset < TimeDuration())
            start = start - offset;   // can not start before the audio does

        start = std::min (start, end - TimeDuration::fromSeconds (0.001));
        c.setPosition ({ { start, end }, std::max (TimeDuration(), c.getPosition().getOffset() + (start - c.getPosition().getStart())) });
        crossfade (c);
    }

    void trimEnd (Clip& c, double newEndBeat)
    {
        auto end = std::max (timeAt (c, newEndBeat), c.getPosition().getStart() + TimeDuration::fromSeconds (0.001));

        // Not past the end of the audio.
        if (auto* audio = dynamic_cast<AudioClipBase*> (&c))
        {
            const auto seconds = sourceSeconds (*audio);

            if (seconds > 0.0)
                end = std::min (end, c.getPosition().getStart() - c.getPosition().getOffset()
                                       + TimeDuration::fromSeconds (seconds / audio->getSpeedRatio()));
        }

        // preserveSync = true keeps the content in place (Tracktion's false would slide it with the end).
        c.setEnd (end, true);
        crossfade (c);
    }

    void stretchEnd (Clip& c, double newEndBeat)
    {
        const auto oldLength = c.getPosition().getLength().inSeconds();
        const auto newEnd = std::max (timeAt (c, newEndBeat), c.getPosition().getStart() + TimeDuration::fromSeconds (0.01));
        const auto newLength = (newEnd - c.getPosition().getStart()).inSeconds();

        if (oldLength <= 0.0 || newLength <= 0.0)
            return;

        // A longer clip plays slower: speed scales by old / new length. With a stretch mode set,
        // Tracktion time-stretches it, so the pitch stays where it was (W36).
        if (auto* audio = dynamic_cast<AudioClipBase*> (&c); audio != nullptr && audio->getTimeStretchMode() == TimeStretcher::disabled)
            audio->setTimeStretchMode (TimeStretcher::defaultMode);

        c.setSpeedRatio (c.getSpeedRatio() * oldLength / newLength);
        c.setEnd (newEnd, true);
        crossfade (c);
    }

    void fitToTempo (Clip& c, double bpm)
    {
        auto* audio = dynamic_cast<AudioClipBase*> (&c);

        if (audio == nullptr || bpm <= 0.0)
            return;

        const auto info = audio->getWaveInfo();
        const auto seconds = info.getLengthInSeconds();

        if (seconds <= 0.0)
            return;

        audio->getLoopInfo().setBpm (bpm, info);
        audio->setTimeStretchMode (TimeStretcher::defaultMode);
        audio->setAutoTempo (true);

        // As many beats as the loop holds, at the project's tempo from where it starts.
        const auto beats = seconds * bpm / 60.0;
        c.setEnd (timeAt (c, startBeat (c) + beats), true);
        crossfade (c);
    }

    std::vector<BeatPlace> placesInBeats (Edit& edit)
    {
        std::vector<BeatPlace> places;

        for (auto* c : allClips (edit))
            {
                const auto* audio = dynamic_cast<AudioClipBase*> (c);
                places.push_back ({ c->itemID, startBeat (*c), endBeat (*c), audio != nullptr && audio->getAutoTempo() });
            }

        return places;
    }

    void setTempo (Edit& edit, double bpm)
    {
        auto& first = *edit.tempoSequence.getTempo (0);
        first.set (first.getStartBeat(), bpm, first.getCurve(), false);
    }

    int followTempo (Edit& edit, const std::vector<BeatPlace>& before, TempoFollow how)
    {
        auto& t = edit.tempoSequence;
        int changed = 0;

        for (const auto& place : before)
        {
            auto* c = findClipForID (edit, place.clip);

            if (c == nullptr || (how == TempoFollow::leaveInTime && ! place.fitted))
                continue;

            const auto start = t.toTime (BeatPosition::fromBeats (place.start));
            const auto end = t.toTime (BeatPosition::fromBeats (place.end));
            const auto oldLength = c->getPosition().getLength().inSeconds();
            const auto newLength = (end - start).inSeconds();

            if (std::abs ((c->getPosition().getStart() - start).inSeconds()) < 1.0e-9
                 && (how == TempoFollow::moveWithBeat || std::abs (oldLength - newLength) < 1.0e-9))
                continue;   // already there

            c->setStart (start, false, true);   // moves it, keeping its length

            if (place.fitted)
            {
                c->setEnd (end, true);   // a fitted loop stretches itself to its beats
            }
            else if (how == TempoFollow::stretch && oldLength > 0.0 && newLength > 0.0)
            {
                if (auto* audio = dynamic_cast<AudioClipBase*> (c); audio != nullptr && audio->getTimeStretchMode() == TimeStretcher::disabled)
                    audio->setTimeStretchMode (TimeStretcher::defaultMode);

                c->setSpeedRatio (c->getSpeedRatio() * oldLength / newLength);
                c->setEnd (end, true);
            }

            crossfade (*c);
            ++changed;
        }

        return changed;
    }

    void slip (Clip& c, double deltaBeats)
    {
        const auto seconds = (timeAt (c, startBeat (c) + std::abs (deltaBeats)) - c.getPosition().getStart()).inSeconds();
        const auto delta = TimeDuration::fromSeconds (deltaBeats < 0 ? -seconds : seconds);

        // Dragging right shows earlier content: the offset shrinks. It stays within the audio.
        auto offset = c.getPosition().getOffset() - delta;
        offset = std::max (TimeDuration(), offset);
        c.setOffset (offset);
    }

    Clips split (Edit& edit, const Clips& clips, double beat)
    {
        Clips right;

        for (auto* c : clips)
        {
            if (beat <= startBeat (*c) || beat >= endBeat (*c))
                continue;

            if (auto* t = dynamic_cast<ClipTrack*> (c->getTrack()))
                    if (auto* r = t->splitClip (*c, edit.tempoSequence.toTime (BeatPosition::fromBeats (beat))))
                        right.add (r);
        }

        return right;
    }

    Clips duplicate (const Clips& clips)
    {
        Clips copies;

        if (clips.isEmpty())
            return copies;

        auto first = startBeat (*clips.getFirst()), last = endBeat (*clips.getFirst());

        for (auto* c : clips)
        {
            first = std::min (first, startBeat (*c));
            last = std::max (last, endBeat (*c));
        }

        for (auto* c : clips)
        {
            if (auto copy = duplicateClip (*c))
            {
                copy->setStart (timeAt (*c, startBeat (*c) + (last - first)), false, true);
                crossfade (*copy);
                copies.add (copy.get());
            }
        }

        return copies;
    }

    Clip* placeCopy (const Clip& source, const Lane& lane, double beat)
    {
        auto copy = duplicateClip (source);   // on the source's carrier: the sample's route

        if (copy == nullptr)
            return nullptr;

        moveToLane (*copy, lane);

        copy->setStart (timeAt (source, beat), false, true);
        crossfade (*copy);
        return copy.get();
    }

    Clip* insertFile (Edit& edit, const Lane& lane, const juce::File& file, double beat, AudioTrack* channel)
    {
        const AudioFile audio (edit.engine, file);
        const auto seconds = audio.getLength();
        auto* carrier = carrierFor (edit, lane, channel);   // to master until routed (W47), unless given one

        if (! audio.isValid() || seconds <= 0.0 || carrier == nullptr)
            return nullptr;

        const auto start = edit.tempoSequence.toTime (BeatPosition::fromBeats (std::max (0.0, beat)));
        auto clip = insertWaveClip (*carrier, file.getFileNameWithoutExtension(), file,
                                    { { start, TimeDuration::fromSeconds (seconds) } }, DeleteExistingClips::no);

        if (clip == nullptr)
            return nullptr;

        crossfade (*clip);
        return clip.get();
    }

    Clips insertFilesOnOwnLanes (Edit& edit, const Lane& firstLane, const juce::Array<juce::File>& files, double beat)
    {
        Clips added;
        auto free = freeChannels (edit);
        auto lane = firstLane;

        for (const auto& file : files)
        {
            if (! AudioFile (edit.engine, file).isValid())
                continue;

            if (! lane.isValid())
                lane = insertLaneAfter (edit, lanes (edit).getLast());   // past the last lane: a new one

            auto* channel = free.isEmpty() ? addChannel (edit) : free.removeAndReturn (0);

            if (auto* c = insertFile (edit, lane, file, beat, channel))
                added.add (c);

            // The next lane down, or none (a new one next time).
            const auto all = lanes (edit);
            const auto next = indexOf (edit, lane) + 1;
            lane = next < all.size() ? all.getReference (next) : Lane {};
        }

        return added;
    }

    void remove (const Clips& clips)
    {
        // Hold every clip first: removing one changes its lane's clip list, which may be the very
        // list passed in (a lane's getClips()), and frees the clip (found by ASan, phase 4d).
        std::vector<tracktion::Clip::Ptr> held (clips.begin(), clips.end());

        for (auto& c : held)
            c->removeFromParent();
    }

    void toggleMute (const Clips& clips)
    {
        bool anyPlaying = false;

        for (auto* c : clips)
            anyPlaying = anyPlaying || ! c->disabled.get();

        for (auto* c : clips)
            c->disabled = anyPlaying;
    }

    namespace
    {
        const juce::Identifier loopFromProp ("carthageLoopFrom"), loopToProp ("carthageLoopTo");
        constexpr double shortestLoop = 1.0 / 960.0;   // a tick
    }

    std::optional<LoopRegion> loopRegion (const Edit& edit)
    {
        const auto& state = edit.state;

        if (! state.hasProperty (loopFromProp) || ! state.hasProperty (loopToProp))
            return std::nullopt;

        const LoopRegion r { static_cast<double> (state[loopFromProp]), static_cast<double> (state[loopToProp]) };
        return r.to - r.from >= shortestLoop ? std::optional (r) : std::nullopt;
    }

    void setLoopRegion (Edit& edit, double fromBeat, double toBeat)
    {
        const auto from = std::max (0.0, std::min (fromBeat, toBeat));
        const auto to = std::max (fromBeat, toBeat);

        if (to - from < shortestLoop)
            return;

        auto* um = &edit.getUndoManager();
        edit.state.setProperty (loopFromProp, from, um);
        edit.state.setProperty (loopToProp, to, um);
        syncLoop (edit);
        edit.getTransport().looping.setValue (true, nullptr);
    }

    void clearLoopRegion (Edit& edit)
    {
        auto* um = &edit.getUndoManager();
        edit.state.removeProperty (loopFromProp, um);
        edit.state.removeProperty (loopToProp, um);
        syncLoop (edit);
    }

    void setLooping (Edit& edit, bool on)
    {
        edit.getTransport().looping.setValue (on && loopRegion (edit).has_value(), nullptr);
    }

    void setPunch (Edit& edit, bool on)
    {
        if (on)
            edit.state.setProperty ("carthagePunch", true, nullptr);
        else
            edit.state.removeProperty ("carthagePunch", nullptr);
    }

    bool isPunchOn (const Edit& edit)
    {
        return static_cast<bool> (edit.state["carthagePunch"]);
    }

    bool isLooping (const Edit& edit)
    {
        return loopRegion (edit).has_value() && const_cast<Edit&> (edit).getTransport().looping.get();
    }

    void syncLoop (Edit& edit)
    {
        auto& transport = edit.getTransport();
        const auto had = transport.getLoopRange().getLength() > TimeDuration();

        if (const auto r = loopRegion (edit))
        {
            auto& t = edit.tempoSequence;
            const TimeRange times (t.toTime (BeatPosition::fromBeats (r->from)), t.toTime (BeatPosition::fromBeats (r->to)));

            if (transport.getLoopRange() != times)
                transport.setLoopRange (times);

            if (! had)
                transport.looping.setValue (true, nullptr);   // an undone clear brings the loop back
        }
        else if (had || transport.looping.get())
        {
            transport.setLoopRange ({});
            transport.looping.setValue (false, nullptr);
        }
    }

    void adoptTransportLoop (Edit& edit)
    {
        if (edit.state.hasProperty (loopFromProp))
            return;

        const auto range = edit.getTransport().getLoopRange();

        if (range.getLength() <= TimeDuration())
            return;

        auto& t = edit.tempoSequence;
        edit.state.setProperty (loopFromProp, t.toBeats (range.getStart()).inBeats(), nullptr);
        edit.state.setProperty (loopToProp, t.toBeats (range.getEnd()).inBeats(), nullptr);
    }

    MarkerClip* addMarker (Edit& edit, double beat)
    {
        auto& markers = edit.getMarkerManager();
        auto marker = markers.createMarker (-1, edit.tempoSequence.toTime (BeatPosition::fromBeats (beat)),
                                            TimeDuration::fromSeconds (0.0), nullptr);
        return dynamic_cast<MarkerClip*> (marker.get());
    }
}
