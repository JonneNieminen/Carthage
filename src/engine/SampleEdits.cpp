#include "SampleEdits.h"

#include <set>

namespace carthage::editing
{
    using namespace tracktion;

    namespace
    {
        const juce::Identifier sampleProp ("carthageSample");

        const AudioClipBase* audioOf (const Clip& c)   { return dynamic_cast<const AudioClipBase*> (&c); }
    }

    juce::String sampleKey (const Clip& c)
    {
        if (const auto own = c.state[sampleProp].toString(); own.isNotEmpty())
            return "name:" + own;

        const auto* audio = audioOf (c);
        return audio != nullptr ? "file:" + audio->getOriginalFile().getFullPathName() : "clip:" + c.itemID.toString();
    }

    juce::String sampleName (const Clip& c)
    {
        if (const auto own = c.state[sampleProp].toString(); own.isNotEmpty())
            return own;

        const auto* audio = audioOf (c);
        return audio != nullptr ? audio->getOriginalFile().getFileNameWithoutExtension() : c.getName();
    }

    Clips clipsOfSample (const Clip& clip)
    {
        Clips same;
        const auto key = sampleKey (clip);

        for (auto* lane : lanes (clip.edit))
            for (auto* c : lane->getClips())
                if (audioOf (*c) != nullptr && sampleKey (*c) == key)
                    same.add (c);

        return same;
    }

    juce::String makeUnique (Clip& clip)
    {
        // The file's name and the lowest number no sample in the project uses.
        const auto* audio = audioOf (clip);
        const auto base = audio != nullptr ? audio->getOriginalFile().getFileNameWithoutExtension() : clip.getName();
        std::set<juce::String> taken;

        for (auto* lane : lanes (clip.edit))
            for (auto* c : lane->getClips())
                taken.insert (sampleName (*c));

        int n = 2;
        while (taken.contains (base + " (" + juce::String (n) + ")"))
            ++n;

        const auto name = base + " (" + juce::String (n) + ")";
        begin (clip.edit, "Make unique");
        clip.state.setProperty (sampleProp, name, &clip.edit.getUndoManager());
        return name;
    }

    SampleSound soundOf (const Clip& c)
    {
        SampleSound s;
        s.start = c.getPosition().getOffset().inSeconds();
        s.length = c.getPosition().getLength().inSeconds();

        if (const auto* audio = audioOf (c))
        {
            s.gainDb = audio->getGainDB();
            s.fadeIn = audio->getFadeIn().inSeconds();
            s.fadeOut = audio->getFadeOut().inSeconds();
            s.fadeInShape = audio->getFadeInType();
            s.fadeOutShape = audio->getFadeOutType();
            s.reversed = audio->getIsReversed();
            s.pitch = audio->getPitchChange();
        }

        return s;
    }

    void setSound (const Clip& model, const SampleSound& s)
    {
        for (auto* c : clipsOfSample (model))
        {
            auto* audio = dynamic_cast<AudioClipBase*> (c);

            if (audio == nullptr)
                continue;

            audio->setGainDB (s.gainDb);
            audio->setIsReversed (s.reversed);

            // Pitch plays through the time-stretcher, keeping the length (W36).
            if (std::abs (s.pitch) > 0.001f && audio->getTimeStretchMode() == TimeStretcher::disabled)
                audio->setTimeStretchMode (TimeStretcher::defaultMode);

            audio->setPitchChange (std::clamp (s.pitch, -24.0f, 24.0f));

            // Which part of the file plays: the offset into it and the length, the clip's start
            // on the timeline kept.
            const auto length = std::max (0.01, s.length);
            c->setOffset (TimeDuration::fromSeconds (std::max (0.0, s.start)));
            c->setLength (TimeDuration::fromSeconds (length), true);

            audio->setFadeIn (TimeDuration::fromSeconds (std::clamp (s.fadeIn, 0.0, length)));
            audio->setFadeOut (TimeDuration::fromSeconds (std::clamp (s.fadeOut, 0.0, length)));
            audio->setFadeInType (s.fadeInShape);
            audio->setFadeOutType (s.fadeOutShape);
        }
    }

    void fitSampleToTempo (const Clip& model, double bpm)
    {
        for (auto* c : clipsOfSample (model))
            fitToTempo (*c, bpm);
    }
}
