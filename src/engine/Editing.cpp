#include "Editing.h"

#include <map>

namespace carthage::editing
{
    using namespace tracktion;

    namespace
    {
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
                static auto formats = [] { auto f = std::make_unique<juce::AudioFormatManager>(); f->registerBasicFormats(); return f; }();

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
            const auto asWritten = editFile.getChildFile (path);

            if (asWritten.exists())
                return asWritten;

            if (const auto besideEdit = editFile.getSiblingFile (path); besideEdit.exists())
                return besideEdit;

            return asWritten;
        };
    }

    void useConsistentFilePaths (Edit& edit)
    {
        edit.filePathResolver = consistentFilePaths ([&edit] { return edit.editFileRetriever ? edit.editFileRetriever() : juce::File(); });
    }

    void begin (Edit& edit, const juce::String& name)
    {
        edit.getUndoManager().beginNewTransaction (name);
    }

    juce::String named (const juce::String& verb, int count, const juce::String& noun)
    {
        return count == 1 ? verb + " " + noun : verb + " " + juce::String (count) + " " + noun + "s";
    }

    double startBeat (const Clip& c)   { return tempo (c).toBeats (c.getPosition().getStart()).inBeats(); }
    double endBeat (const Clip& c)     { return tempo (c).toBeats (c.getPosition().getEnd()).inBeats(); }

    juce::Array<AudioTrack*> lanes (Edit& edit)   { return getAudioTracks (edit); }

    int laneOf (const Clip& c)
    {
        return lanes (c.edit).indexOf (dynamic_cast<AudioTrack*> (c.getTrack()));
    }

    void move (const Clips& clips, double deltaBeats, int laneDelta)
    {
        if (clips.isEmpty())
            return;

        // Never before zero: the earliest clip stops there and the rest keep their spacing.
        auto earliest = startBeat (*clips.getFirst());
        for (auto* c : clips) earliest = std::min (earliest, startBeat (*c));
        deltaBeats = std::max (deltaBeats, -earliest);

        auto all = lanes (clips.getFirst()->edit);

        for (auto* c : clips)
        {
            const auto length = c->getPosition().getLength();
            c->setStart (timeAt (*c, startBeat (*c) + deltaBeats), false, true);
            juce::ignoreUnused (length);

            if (laneDelta != 0)
            {
                const auto target = std::clamp (laneOf (*c) + laneDelta, 0, all.size() - 1);

                if (auto* t = all[target]; t != nullptr && t != c->getTrack())
                    c->moveTo (*t);
            }

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

        // A longer clip plays slower: speed scales by old / new length.
        c.setSpeedRatio (c.getSpeedRatio() * oldLength / newLength);
        c.setEnd (newEnd, true);
        crossfade (c);
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

    Clip* placeCopy (const Clip& source, AudioTrack& lane, double beat)
    {
        auto copy = duplicateClip (source);

        if (copy == nullptr)
            return nullptr;

        if (copy->getTrack() != &lane)
            copy->moveTo (lane);

        copy->setStart (timeAt (source, beat), false, true);
        crossfade (*copy);
        return copy.get();
    }

    void remove (const Clips& clips)
    {
        for (auto* c : clips)
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

    void setLoopRange (Edit& edit, double startBeat, double endBeat)
    {
        auto& t = edit.tempoSequence;
        edit.getTransport().setLoopRange ({ t.toTime (BeatPosition::fromBeats (std::min (startBeat, endBeat))),
                                            t.toTime (BeatPosition::fromBeats (std::max (startBeat, endBeat))) });
    }

    MarkerClip* addMarker (Edit& edit, double beat)
    {
        auto& markers = edit.getMarkerManager();
        auto marker = markers.createMarker (-1, edit.tempoSequence.toTime (BeatPosition::fromBeats (beat)),
                                            TimeDuration::fromSeconds (0.0), nullptr);
        return dynamic_cast<MarkerClip*> (marker.get());
    }
}
