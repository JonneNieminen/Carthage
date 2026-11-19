#include "SampleEdits.h"

#include <set>

namespace carthage::editing
{
    using namespace tracktion;

    namespace
    {
        const juce::Identifier sampleProp ("carthageSample");

        const AudioClipBase* audioOf (const Clip& c)   { return dynamic_cast<const AudioClipBase*> (&c); }

        const juce::Identifier envelopeTag ("CARTHAGEENVELOPE"), pointTag ("POINT"), timeProp ("time"), gainProp ("db"), bendProp ("bend"),
                               envelopePluginProp ("carthageEnvelope"), syncedProp ("carthageEnvelopeSynced");

        /** The clip's envelope plugin, if it has one (marked, so a user's own plugin is never it). */
        VolumeAndPanPlugin* envelopePlugin (Clip& c)
        {
            if (auto* list = c.getPluginList())
                for (auto* p : *list)
                    if (p->state.getProperty (envelopePluginProp, false))
                        return dynamic_cast<VolumeAndPanPlugin*> (p);

            return nullptr;
        }
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

    std::vector<EnvelopePoint> envelopeOf (const Clip& c)
    {
        std::vector<EnvelopePoint> points;

        for (const auto& p : c.state.getChildWithName (envelopeTag))
            points.push_back ({ static_cast<double> (p[timeProp]), static_cast<float> (p[gainProp]), static_cast<float> (p[bendProp]) });

        return points;
    }

    void setEnvelope (const Clip& model, std::vector<EnvelopePoint> points)
    {
        std::sort (points.begin(), points.end(), [] (const auto& a, const auto& b) { return a.time < b.time; });

        for (auto* c : clipsOfSample (model))
        {
            auto* um = &c->edit.getUndoManager();

            if (auto old = c->state.getChildWithName (envelopeTag); old.isValid())
                c->state.removeChild (old, um);

            if (! points.empty())
            {
                juce::ValueTree envelope (envelopeTag);

                for (const auto& p : points)
                    envelope.appendChild (juce::ValueTree (pointTag, { { timeProp, p.time }, { gainProp, juce::jlimit (-60.0f, 6.0f, p.gainDb) },
                                                                       { bendProp, juce::jlimit (-1.0f, 1.0f, p.bend) } }), nullptr);

                c->state.appendChild (envelope, um);
            }

            syncEnvelope (*c);
        }
    }

    void syncEnvelope (Clip& c)
    {
        auto* audio = dynamic_cast<AudioClipBase*> (&c);

        if (audio == nullptr)
            return;

        const auto points = envelopeOf (c);
        const auto pos = c.getPosition();
        const auto speed = std::max (0.0001, c.getSpeedRatio());
        const auto fileSeconds = audio->getWaveInfo().getLengthInSeconds();

        // What the curve depends on: when it is the same as last time, nothing to do.
        juce::String key;
        key << pos.getStart().inSeconds() << ";" << pos.getOffset().inSeconds() << ";" << pos.getLength().inSeconds() << ";" << speed
            << ";" << (audio->getIsReversed() ? 1 : 0) << ";" << c.state.getChildWithName (envelopeTag).toXmlString().hashCode64();

        auto* plugin = envelopePlugin (c);

        // An undo can take the plugin away without changing anything above: then sync again.
        if (c.state[syncedProp].toString() == key && (plugin != nullptr) == ! points.empty())
            return;

        c.state.setProperty (syncedProp, key, nullptr);

        if (points.empty())
        {
            if (plugin != nullptr)
                plugin->deleteFromParent();
            return;
        }

        if (plugin == nullptr)
        {
            auto created = c.edit.getPluginCache().createNewPlugin (VolumeAndPanPlugin::xmlTypeName, {});
            created->state.setProperty (envelopePluginProp, true, nullptr);
            c.getPluginList()->insertPlugin (created, 0, nullptr);
            plugin = dynamic_cast<VolumeAndPanPlugin*> (created.get());

            if (plugin == nullptr)
                return;
        }

        // At unity this law leaves a clip as loud as without the plugin (measured by render).
        plugin->setPanLaw (PanLaw6dBCenter);

        // File time to timeline time: past the clip's offset, at its speed, mirrored if reversed.
        auto& curve = plugin->volParam->getCurve();
        curve.clear (nullptr);

        for (const auto& p : points)
        {
            const auto inFile = audio->getIsReversed() ? fileSeconds - p.time : p.time;
            const auto at = pos.getStart().inSeconds() + (inFile - pos.getOffset().inSeconds() * speed) / speed;
            const auto value = p.gainDb <= -60.0f ? 0.0f : decibelsToVolumeFaderPosition (p.gainDb);
            curve.addPoint (TimePosition::fromSeconds (at), value, audio->getIsReversed() ? -p.bend : p.bend, nullptr);
        }
    }

    float envelopeGainAt (Clip& c, double fileTime)
    {
        auto* audio = dynamic_cast<AudioClipBase*> (&c);
        auto* plugin = envelopePlugin (c);

        if (audio == nullptr || plugin == nullptr || plugin->volParam->getCurve().getNumPoints() == 0)
            return 1.0f;

        const auto pos = c.getPosition();
        const auto speed = std::max (0.0001, c.getSpeedRatio());
        const auto inFile = audio->getIsReversed() ? audio->getWaveInfo().getLengthInSeconds() - fileTime : fileTime;
        const auto at = pos.getStart().inSeconds() + (inFile - pos.getOffset().inSeconds() * speed) / speed;
        const auto position = plugin->volParam->getCurve().getValueAt (TimePosition::fromSeconds (at), plugin->volParam->getCurrentBaseValue());
        return position <= 0.0f ? 0.0f : juce::Decibels::decibelsToGain (volumeFaderPositionToDB (position));
    }

    void syncEnvelopes (Edit& edit)
    {
        for (auto* lane : lanes (edit))
            for (auto* c : lane->getClips())
                if (c->state.getChildWithName (envelopeTag).isValid() || envelopePlugin (*c) != nullptr)
                    syncEnvelope (*c);
    }

    void fitSampleToTempo (const Clip& model, double bpm)
    {
        for (auto* c : clipsOfSample (model))
            fitToTempo (*c, bpm);
    }
}
