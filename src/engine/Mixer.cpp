#include "Mixer.h"
#include "SampleEdits.h"

#include <set>

namespace carthage::editing
{
    using namespace tracktion;

    namespace
    {
        const juce::Identifier lanesTag ("CARTHAGELANES"), laneTag ("LANE");
        const juce::Identifier idProp ("id"), nameProp ("name"), mutedProp ("muted"), soloedProp ("soloed");
        const juce::Identifier recordingProp ("recording"), nextIDProp ("nextID");
        const juce::Identifier roleProp ("carthageRole"), carrierLaneProp ("carthageLane");
        const juce::Identifier colourProp ("carthageColour"), heightProp ("carthageHeight");   // the timeline's own (4d)
        const juce::Identifier channelMutedProp ("carthageMuted"), channelSoloedProp ("carthageSoloed");
        const juce::Identifier meterProp ("carthageMeter");   // "peak" or "rms" on the meters Carthage adds
        constexpr const char* channelRole = "channel";
        constexpr const char* carrierRole = "carrier";

        juce::ValueTree lanesState (const Edit& edit)   { return edit.state.getChildWithName (lanesTag); }

        juce::ValueTree lanesStateForWriting (Edit& edit)
        {
            return edit.state.getOrCreateChildWithName (lanesTag, nullptr);
        }

        juce::UndoManager* undoOf (Edit& edit)   { return &edit.getUndoManager(); }

        juce::Array<AudioTrack*> carriers (const Edit& edit)
        {
            juce::Array<AudioTrack*> out;

            for (auto* t : getAudioTracks (const_cast<Edit&> (edit)))
                if (isCarrier (*t))
                    out.add (t);

            return out;
        }

        int carrierLaneID (const Track& t)   { return static_cast<int> (t.state[carrierLaneProp]); }

        Lane newLaneState (Edit& edit, const juce::String& name)
        {
            auto all = lanesStateForWriting (edit);
            const auto id = std::max (1, static_cast<int> (all[nextIDProp]));
            all.setProperty (nextIDProp, id + 1, undoOf (edit));

            juce::ValueTree lane (laneTag);
            lane.setProperty (idProp, id, nullptr);
            lane.setProperty (nameProp, name, nullptr);
            return Lane (lane);
        }

        bool flag (const Track& t, const juce::Identifier& p)   { return static_cast<bool> (t.state[p]); }

        /** Lane and channel mute and solo onto Tracktion's track mute, which is outside undo.
            A carrier plays unless its lane is silenced (muted, or another lane soloed), or a
            channel is soloed and the carrier plays straight to master. A channel plays unless it
            is muted, or some channel is soloed and it is neither that channel, nor upstream of it
            (feeding it), nor downstream (fed by it). Tracktion's own solo is not used (W50). */
        void applyMutes (Edit& edit)
        {
            const auto all = lanes (edit);
            const auto anyLaneSolo = std::any_of (all.begin(), all.end(), [] (const Lane& l) { return l.isSoloed(); });
            const auto chs = channels (edit);
            std::set<const AudioTrack*> soloPath;

            for (auto* ch : chs)
                if (flag (*ch, channelSoloedProp))
                {
                    for (auto* d = ch; d != nullptr && ! soloPath.contains (d); d = d->getOutput().getDestinationTrack())
                        soloPath.insert (d);   // it and what it plays into

                    for (auto* other : chs)
                        if (other->getOutput().feedsInto (ch))
                            soloPath.insert (other);   // what plays into it
                }

            const auto anyChannelSolo = ! soloPath.empty();

            for (auto* c : carriers (edit))
            {
                const auto lane = laneWithID (edit, carrierLaneID (*c));
                const auto laneSilent = lane.isValid() && (lane.isMuted() || (anyLaneSolo && ! lane.isSoloed()));
                const auto toMaster = c->getOutput().getDestinationTrack() == nullptr;
                const auto silent = laneSilent || (anyChannelSolo && toMaster);

                if (c->isMuted (false) != silent)
                    c->setMute (silent);
            }

            for (auto* ch : chs)
            {
                const auto silent = flag (*ch, channelMutedProp) || (anyChannelSolo && ! soloPath.contains (ch));

                if (ch->isMuted (false) != silent)
                    ch->setMute (silent);
            }
        }

        LevelMeterPlugin* meterIn (const PluginList& list, const char* kind)
        {
            for (auto* p : list)
                if (auto* m = dynamic_cast<LevelMeterPlugin*> (p); m != nullptr && p->state[meterProp].toString() == kind)
                    return m;

            return nullptr;
        }

        /** The RMS meter after the fader's own (peak) meter, and the master's pair. */
        void addMeters (PluginList& list, bool withPeak)
        {
            for (const auto* kind : { "peak", "rms" })
            {
                if (! withPeak && juce::String (kind) == "peak")
                    continue;

                if (meterIn (list, kind) != nullptr)
                    continue;

                auto plugin = list.getEdit().getPluginCache().createNewPlugin (LevelMeterPlugin::xmlTypeName, {});

                if (plugin == nullptr)
                    continue;

                plugin->state.setProperty (meterProp, kind, nullptr);
                list.insertPlugin (plugin, -1, nullptr);
            }
        }

        bool hasOwnPlugins (AudioTrack& t)   { return ! insertsOf (t).isEmpty(); }

        /** In or out of the audio graph, outside undo. Tracktion's setters for a track and for its
            plugins are undo steps. When a track's flag changes, Tracktion copies it onto its
            plugins, but reads the track's flag before it has updated, so it writes the old value;
            stored on the plugins beforehand, it is equal and nothing is written. Then the plugins get the new value
            here, outside undo (found by MixerTests). */
        void setProcessing (AudioTrack& t, bool on)
        {
            // JUCE writes even an equal value when none is stored, so store the old one first.
            const auto was = t.isProcessing (false);

            for (auto* p : t.pluginList)
                p->state.setProperty (IDs::process, was, nullptr);

            t.state.setProperty (IDs::process, on, nullptr);

            for (auto* p : t.pluginList)
                p->state.setProperty (IDs::process, on, nullptr);
        }

        AudioTrack* insertTrack (Edit& edit, Track* preceding, bool withFader)
        {
            auto t = edit.insertNewAudioTrack (TrackInsertPoint (nullptr, preceding), nullptr, withFader);
            return t.get();
        }
    }

    //==============================================================================================
    int Lane::id() const                     { return static_cast<int> (state[idProp]); }
    juce::String Lane::getName() const       { return state[nameProp].toString(); }
    bool Lane::isMuted() const               { return static_cast<bool> (state[mutedProp]); }
    bool Lane::isSoloed() const              { return static_cast<bool> (state[soloedProp]); }

    juce::Array<Lane> lanes (const Edit& edit)
    {
        juce::Array<Lane> out;

        for (auto child : lanesState (edit))
            if (child.hasType (laneTag))
                out.add (Lane (child));

        return out;
    }

    Lane laneWithID (const Edit& edit, int id)
    {
        for (auto child : lanesState (edit))
            if (child.hasType (laneTag) && static_cast<int> (child[idProp]) == id)
                return Lane (child);

        return {};
    }

    int indexOf (const Edit& edit, const Lane& lane)
    {
        return lanes (edit).indexOf (lane);
    }

    Lane recordingLane (const Edit& edit)
    {
        if (const auto chosen = laneWithID (edit, static_cast<int> (lanesState (edit)[recordingProp])); chosen.isValid())
            return chosen;

        return lanes (edit).getFirst();
    }

    Lane laneOfClip (const Clip& c)
    {
        if (const auto* t = c.getTrack(); t != nullptr && isCarrier (*t))
            return laneWithID (c.edit, carrierLaneID (*t));

        return {};
    }

    int laneOf (const Clip& c)
    {
        return indexOf (c.edit, laneOfClip (c));
    }

    juce::Array<Clip*> clipsOn (Edit& edit, const Lane& lane)
    {
        juce::Array<Clip*> out;

        for (auto* c : carriers (edit))
            if (carrierLaneID (*c) == lane.id())
                out.addArray (c->getClips());

        return out;
    }

    juce::Array<Clip*> allClips (Edit& edit)
    {
        juce::Array<Clip*> out;

        for (auto* c : carriers (edit))
            out.addArray (c->getClips());

        return out;
    }

    std::vector<juce::Array<Clip*>> clipsByLane (Edit& edit)
    {
        const auto all = lanes (edit);
        std::vector<juce::Array<Clip*>> out (static_cast<size_t> (all.size()));

        for (auto* c : carriers (edit))
            for (int i = 0; i < all.size(); ++i)
                if (all.getReference (i).id() == carrierLaneID (*c))
                    out[static_cast<size_t> (i)].addArray (c->getClips());

        return out;
    }

    //==============================================================================================
    juce::String nextLaneName (const Edit& edit)
    {
        std::set<int> used;

        for (const auto& l : lanes (edit))
            if (const auto name = l.getName(); name.startsWith ("Lane ") && name.substring (5).containsOnly ("0123456789"))
                used.insert (name.substring (5).getIntValue());

        int n = 1;
        while (used.contains (n))
            ++n;

        return "Lane " + juce::String (n);
    }

    namespace
    {
        Lane insertLane (Edit& edit, const Lane& after)
        {
            auto tree = lanesStateForWriting (edit);
            auto lane = newLaneState (edit, nextLaneName (edit));
            const auto at = after.isValid() ? tree.indexOf (after.state) + 1 : -1;
            tree.addChild (lane.state, at, undoOf (edit));
            return lane;
        }
    }

    Lane addLane (Edit& edit, const Lane& after)
    {
        edit.getUndoManager().beginNewTransaction ("Add lane");
        return insertLane (edit, after);
    }

    Lane addLanes (Edit& edit, int count, const Lane& after)
    {
        edit.getUndoManager().beginNewTransaction (count == 1 ? juce::String ("Add lane") : "Add " + juce::String (count) + " lanes");
        Lane first, previous = after;

        for (int i = 0; i < count; ++i)
        {
            previous = insertLane (edit, previous);

            if (! first.isValid())
                first = previous;
        }

        return first;
    }

    void moveLane (Edit& edit, const Lane& lane, int newIndex)
    {
        auto tree = lanesStateForWriting (edit);
        const auto from = tree.indexOf (lane.state);
        newIndex = std::clamp (newIndex, 0, tree.getNumChildren() - 1);

        if (from < 0 || from == newIndex)
            return;

        edit.getUndoManager().beginNewTransaction ("Move lane");
        tree.moveChild (from, newIndex, undoOf (edit));
    }

    bool canDeleteLane (const Edit& edit, const Lane& lane)
    {
        return lane.isValid() && ! (lane == recordingLane (edit)) && lanes (edit).size() > 1;
    }

    void deleteLane (Edit& edit, const Lane& lane)
    {
        edit.getUndoManager().beginNewTransaction ("Delete lane");

        for (auto* c : carriers (edit))
            if (carrierLaneID (*c) == lane.id())
                edit.deleteTrack (c);

        lanesStateForWriting (edit).removeChild (lane.state, undoOf (edit));
    }

    void renameLane (Edit& edit, const Lane& lane, const juce::String& name)
    {
        const auto trimmed = name.trim();

        if (trimmed.isEmpty() || trimmed == lane.getName())
            return;

        edit.getUndoManager().beginNewTransaction ("Rename lane");
        Lane (lane).state.setProperty (nameProp, trimmed, undoOf (edit));
    }

    void setLaneMuted (Edit& edit, const Lane& lane, bool muted)
    {
        Lane (lane).state.setProperty (mutedProp, muted, nullptr);
        applyMutes (edit);
    }

    void setLaneSoloed (Edit& edit, const Lane& lane, bool soloed)
    {
        Lane (lane).state.setProperty (soloedProp, soloed, nullptr);
        applyMutes (edit);
    }

    void muteOthers (Edit& edit, const Lane& lane)
    {
        const auto all = lanes (edit);
        auto alreadyAlone = ! lane.isMuted();

        for (const auto& l : all)
            if (! (l == lane) && ! l.isMuted())
                alreadyAlone = false;

        for (const auto& l : all)
            Lane (l).state.setProperty (mutedProp, alreadyAlone ? false : ! (l == lane), nullptr);

        applyMutes (edit);
    }

    void soloAlone (Edit& edit, const Lane& lane)
    {
        const auto all = lanes (edit);
        auto alreadyAlone = lane.isSoloed();

        for (const auto& l : all)
            if (! (l == lane) && l.isSoloed())
                alreadyAlone = false;

        for (const auto& l : all)
            Lane (l).state.setProperty (soloedProp, alreadyAlone ? false : l == lane, nullptr);

        applyMutes (edit);
    }

    //==============================================================================================
    bool isChannel (const Track& t)   { return t.state[roleProp].toString() == channelRole; }
    bool isCarrier (const Track& t)   { return t.state[roleProp].toString() == carrierRole; }

    juce::Array<AudioTrack*> channels (const Edit& edit)
    {
        juce::Array<AudioTrack*> out;

        for (auto* t : getAudioTracks (const_cast<Edit&> (edit)))
            if (isChannel (*t))
                out.add (t);

        return out;
    }

    AudioTrack* routeOf (const Clip& c)
    {
        if (auto* t = dynamic_cast<AudioTrack*> (c.getTrack()))
            return t->getOutput().getDestinationTrack();

        return nullptr;
    }

    AudioTrack* carrierFor (Edit& edit, const Lane& lane, AudioTrack* channel)
    {
        if (! lane.isValid())
            return nullptr;

        for (auto* c : carriers (edit))
            if (carrierLaneID (*c) == lane.id() && c->getOutput().getDestinationTrack() == channel)
                return c;

        // A new carrier, kept after the last track so it never sits between channels.
        auto* carrier = insertTrack (edit, getAllTracks (edit).getLast(), false);

        if (carrier == nullptr)
            return nullptr;

        auto* um = undoOf (edit);
        carrier->state.setProperty (roleProp, carrierRole, um);
        carrier->state.setProperty (carrierLaneProp, lane.id(), um);
        carrier->setName ("Lane " + juce::String (lane.id()) + (channel != nullptr ? " > " + channel->getName() : juce::String (" > Master")));

        if (channel != nullptr)
            carrier->getOutput().setOutputToTrack (channel);

        applyMutes (edit);
        return carrier;
    }

    void moveToLane (Clip& clip, const Lane& lane)
    {
        if (laneOfClip (clip) == lane)
            return;

        if (auto* carrier = carrierFor (clip.edit, lane, routeOf (clip)))
            clip.moveTo (*carrier);
    }

    void routeSample (Clip& clip, AudioTrack* channel)
    {
        for (auto* c : clipsOfSample (clip))
            if (routeOf (*c) != channel)
                if (auto* carrier = carrierFor (c->edit, laneOfClip (*c), channel))
                    c->moveTo (*carrier);
    }

    AudioTrack* outputOf (const AudioTrack& channel)
    {
        return channel.getOutput().getDestinationTrack();
    }

    juce::Result setOutput (AudioTrack& channel, AudioTrack* destination)
    {
        if (destination == &channel)
            return juce::Result::fail (channel.getName() + " can not play into itself");

        if (destination != nullptr && destination->getOutput().feedsInto (&channel))
            return juce::Result::fail (destination->getName() + " already plays into " + channel.getName()
                                       + ", so this would feed the sound back into itself");

        if (destination == nullptr)
            channel.getOutput().setOutputToDefaultDevice (false);
        else
            channel.getOutput().setOutputToTrack (destination);

        return juce::Result::ok();
    }

    juce::String nextChannelName (const Edit& edit)
    {
        std::set<int> used;

        for (auto* t : channels (edit))
            if (const auto name = t->getName(); name.startsWith ("Channel ") && name.substring (8).containsOnly ("0123456789"))
                used.insert (name.substring (8).getIntValue());

        int n = 1;
        while (used.contains (n))
            ++n;

        return "Channel " + juce::String (n);
    }

    AudioTrack* addChannel (Edit& edit, AudioTrack* after)
    {
        const auto name = nextChannelName (edit);
        const auto all = channels (edit);
        Track* preceding = after != nullptr ? after : all.getLast();

        if (preceding == nullptr)   // the first channel: after Tracktion's own tracks, before carriers
        {
                for (auto* t : getAllTracks (edit))
                if (dynamic_cast<AudioTrack*> (t) == nullptr)
                    preceding = t;
        }

        auto* channel = insertTrack (edit, preceding, true);

        if (channel == nullptr)
            return nullptr;

        channel->state.setProperty (roleProp, channelRole, undoOf (edit));
        channel->setName (name);

        // The fader's meter reads peaks; Carthage adds one for RMS beside it (brief, feature 3).
        for (auto* p : channel->pluginList)
            if (dynamic_cast<LevelMeterPlugin*> (p) != nullptr && ! p->state.hasProperty (meterProp))
            {
                p->state.setProperty (meterProp, "peak", undoOf (edit));
                break;
            }

        addMeters (channel->pluginList, true);   // adds the peak one too if Tracktion made none
        return channel;
    }

    void deleteChannel (AudioTrack& channel)
    {
        auto& edit = channel.edit;

        // Whatever played into it now plays to master: its lanes' clips and other channels.
        for (auto* t : getAudioTracks (edit))
            if (t != &channel && t->getOutput().getDestinationTrack() == &channel)
            {
                if (isCarrier (*t))
                {
                    const auto lane = laneWithID (edit, carrierLaneID (*t));

                    if (auto* toMaster = carrierFor (edit, lane, nullptr))
                        for (auto* c : juce::Array<Clip*> (t->getClips()))
                            c->moveTo (*toMaster);

                    edit.deleteTrack (t);
                }
                else
                {
                    t->getOutput().setOutputToDefaultDevice (false);
                }
            }

        edit.deleteTrack (&channel);
    }

    void moveChannel (AudioTrack& channel, int newIndex)
    {
        auto& edit = channel.edit;
        auto others = channels (edit);
        others.removeFirstMatchingValue (&channel);
        newIndex = std::clamp (newIndex, 0, others.size());

        if (channels (edit).indexOf (&channel) == newIndex)
            return;

        Track* preceding = nullptr;

        if (newIndex > 0)
        {
            preceding = others[newIndex - 1];
        }
        else if (! others.isEmpty())
        {
            const auto all = getAllTracks (edit);
            const auto first = all.indexOf (others.getFirst());
            preceding = first > 0 ? all[first - 1] : nullptr;
        }

        edit.moveTrack (&channel, TrackInsertPoint (nullptr, preceding));
    }

    bool isChannelMuted (const AudioTrack& t)    { return flag (t, channelMutedProp); }
    bool isChannelSoloed (const AudioTrack& t)   { return flag (t, channelSoloedProp); }

    void setChannelMuted (AudioTrack& t, bool on)
    {
        t.state.setProperty (channelMutedProp, on, nullptr);
        applyMutes (t.edit);
    }

    void setChannelSoloed (AudioTrack& t, bool on)
    {
        t.state.setProperty (channelSoloedProp, on, nullptr);
        applyMutes (t.edit);
    }

    LevelMeterPlugin* peakMeterOf (AudioTrack& t)   { return meterIn (t.pluginList, "peak"); }

    LevelMeterPlugin* rmsMeterOf (AudioTrack& t)
    {
        auto* m = meterIn (t.pluginList, "rms");

        if (m != nullptr && m->measurer.getMode() != LevelMeasurer::RMSMode)
            m->measurer.setMode (LevelMeasurer::RMSMode);   // the mode is not saved with the plugin

        return m;
    }

    LevelMeterPlugin* masterPeakMeter (Edit& edit)   { return meterIn (edit.getMasterPluginList(), "peak"); }

    LevelMeterPlugin* masterRMSMeter (Edit& edit)
    {
        auto* m = meterIn (edit.getMasterPluginList(), "rms");

        if (m != nullptr && m->measurer.getMode() != LevelMeasurer::RMSMode)
            m->measurer.setMode (LevelMeasurer::RMSMode);

        return m;
    }

    juce::Array<Plugin*> insertsOf (PluginList& list)
    {
        juce::Array<Plugin*> out;

        for (auto* p : list)
            if (dynamic_cast<VolumeAndPanPlugin*> (p) == nullptr && dynamic_cast<LevelMeterPlugin*> (p) == nullptr)
                out.add (p);

        return out;
    }

    juce::Array<Plugin*> insertsOf (AudioTrack& t)   { return insertsOf (t.pluginList); }

    PluginList& pluginListOf (Edit& edit, AudioTrack* channel)
    {
        return channel != nullptr ? channel->pluginList : edit.getMasterPluginList();
    }

    int listIndexForSlot (PluginList& list, int slot)
    {
        const auto inserts = insertsOf (list);

        if (slot >= 0 && slot < inserts.size())
            return list.indexOf (inserts[slot]);

        // After the last insert: before the first of the channel's own (fader, meters), so the
        // fader and meters always see the processed sound.
        for (int i = 0; i < list.size(); ++i)
            if (dynamic_cast<VolumeAndPanPlugin*> (list[i]) != nullptr || dynamic_cast<LevelMeterPlugin*> (list[i]) != nullptr)
                return i;

        return list.size();
    }

    void placeInsert (PluginList& list, int slot, const Plugin::Ptr& plugin)
    {
        list.insertPlugin (plugin, listIndexForSlot (list, slot), nullptr);
    }

    void moveInsert (Plugin& plugin, PluginList& to, int slot)
    {
        auto* from = plugin.getOwnerList();

        if (from == &to)
        {
            // Within its Line: the state moves, the plugin keeps running.
            auto parent = plugin.state.getParent();
            const auto oldIndex = parent.indexOf (plugin.state);
            auto newIndex = parent.indexOf (to[listIndexForSlot (to, slot)] != nullptr ? to[listIndexForSlot (to, slot)]->state : juce::ValueTree());

            if (newIndex < 0)
                newIndex = parent.getNumChildren() - 1;
            else if (newIndex > oldIndex)
                --newIndex;   // its own place counts until it moves

            if (oldIndex >= 0 && newIndex != oldIndex)
                parent.moveChild (oldIndex, newIndex, &plugin.edit.getUndoManager());

            return;
        }

        // To another channel: rebuilt there from its state, flushed first so nothing is lost.
        plugin.flushPluginStateToValueTree();
        auto state = plugin.state.createCopy();
        EditItemID::remapIDs (state, nullptr, plugin.edit);
        plugin.deleteFromParent();

        if (auto newPlugin = plugin.edit.getPluginCache().getOrCreatePluginFor (state))
            to.insertPlugin (newPlugin, listIndexForSlot (to, slot), nullptr);
    }

    void removeInsert (Plugin& plugin)   { plugin.deleteFromParent(); }

    bool isBypassed (const Plugin& p)    { return ! p.isEnabled(); }
    void setBypassed (Plugin& p, bool b) { p.setEnabled (! b); }

    bool hasMix (const Plugin& p)        { return dynamic_cast<const ExternalPlugin*> (&p) != nullptr; }

    float mixOf (const Plugin& p)
    {
        if (const auto* ext = dynamic_cast<const ExternalPlugin*> (&p))
            return std::clamp (ext->wetValue.get(), 0.0f, 1.0f);

        return 1.0f;
    }

    void setMix (Plugin& p, float mix)
    {
        if (auto* ext = dynamic_cast<ExternalPlugin*> (&p))
        {
            mix = std::clamp (mix, 0.0f, 1.0f);
            ext->wetValue = mix;          // the plugin's sound
            ext->dryValue = 1.0f - mix;   // and the sound before it, crossfaded
        }
    }

    //==============================================================================================
    void prepareMix (Edit& edit, bool isNew, int newLanes)
    {
        if (lanesState (edit).isValid())
            return;   // already a 5a project

        auto lanesTree = lanesStateForWriting (edit);
        const auto oldTracks = isNew ? juce::Array<AudioTrack*>() : getAudioTracks (edit);

        // New: the audio tracks Tracktion made are not wanted; channels and carriers replace them.
        if (isNew)
            for (auto* t : getAudioTracks (edit))
                edit.deleteTrack (t);

        // Before 5a each lane was a track: it becomes lane n, its track the carrier of lane n's
        // clips, playing into channel n (W47). Mute and solo move to the lane.
        for (auto* t : oldTracks)
        {
            auto lane = newLaneState (edit, t->getName());

            if (t->state.hasProperty (colourProp)) lane.state.setProperty (colourProp, t->state[colourProp], nullptr);
            if (t->state.hasProperty (heightProp)) lane.state.setProperty (heightProp, t->state[heightProp], nullptr);

            lane.state.setProperty (mutedProp, t->isMuted (false), nullptr);
            lane.state.setProperty (soloedProp, t->isSolo (false), nullptr);
            lanesTree.appendChild (lane.state, nullptr);

            t->setMute (false);
            t->setSolo (false);
            t->state.setProperty (roleProp, carrierRole, nullptr);
            t->state.setProperty (carrierLaneProp, lane.id(), nullptr);
        }

        if (isNew)
            for (int i = 0; i < newLanes; ++i)
                lanesTree.appendChild (newLaneState (edit, "Lane " + juce::String (i + 1)).state, nullptr);

        // The channels: 50, or one for every old lane if there were more.
        const auto count = std::max (defaultChannels, oldTracks.size());

        for (int i = 0; i < count; ++i)
            addChannel (edit);

        const auto all = channels (edit);

        for (int i = 0; i < oldTracks.size(); ++i)
        {
            auto* t = oldTracks[i];
            t->getOutput().setOutputToTrack (all[i]);
            t->setName ("Lane " + juce::String (i + 1) + " > " + all[i]->getName());

            // The input chain (phase 1) was the first lane's plugins: it moves to channel 1.
            if (i == 0)
                for (auto& p : t->pluginList.getPlugins())   // a copy: removing changes the list
                    if (dynamic_cast<VolumeAndPanPlugin*> (p) == nullptr && dynamic_cast<LevelMeterPlugin*> (p) == nullptr)
                    {
                        auto state = p->state.createCopy();
                        p->removeFromParent();
                        all[0]->pluginList.insertPlugin (state, all[0]->pluginList.size() > 0 ? 0 : -1);
                    }

            // Carriers keep no fader of their own: the channel has it.
            for (auto& p : t->pluginList.getPlugins())
                p->removeFromParent();
        }

        // Carriers after the channels, so the channels come first among the tracks.
        for (auto* t : oldTracks)
            edit.moveTrack (t, TrackInsertPoint (nullptr, getAllTracks (edit).getLast()));

        addMeters (edit.getMasterPluginList(), true);
    }

    void syncMix (Edit& edit, const AudioTrack* inputChannel)
    {
        applyMutes (edit);

        // Only what could sound stays in the graph: carriers with clips; channels with something
        // playing into them, plugins of their own, or the live input (W49).
        std::set<const AudioTrack*> fed;

        for (auto* c : carriers (edit))
        {
            const auto sounds = ! c->getClips().isEmpty();

            if (c->isProcessing (false) != sounds)
                setProcessing (*c, sounds);

            if (sounds)
                for (auto* dest = c->getOutput().getDestinationTrack(); dest != nullptr && ! fed.contains (dest);
                     dest = dest->getOutput().getDestinationTrack())
                    fed.insert (dest);
        }

        const auto all = channels (edit);

        for (auto* ch : all)
            if (ch == inputChannel || hasOwnPlugins (*ch))
                for (auto* dest = ch; dest != nullptr && ! fed.contains (dest); dest = dest->getOutput().getDestinationTrack())
                    fed.insert (dest);

        for (auto* ch : all)
        {
            const auto sounds = fed.contains (ch);

            if (ch->isProcessing (false) != sounds)
                setProcessing (*ch, sounds);
        }
    }
}
