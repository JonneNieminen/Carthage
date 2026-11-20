#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace carthage::editing
{
    /** Phase 5a's model (W47, W49): lanes and mixer channels are separate (F5).

        - A **channel** is one of Tracktion's audio tracks, marked as a channel. It holds no clips:
          only its plugins, fader, pan and meter. Its output goes to master or to another channel.
        - A **lane** is only where clips are drawn: a name, colour, height, mute and solo kept in
          the edit (CARTHAGELANES), with undo where the timeline had it.
        - A **carrier** is a hidden audio track holding one lane's clips that play through one
          route (a channel, or master). It outputs into that channel. Carriers keep Tracktion's
          crossfades within a lane, and lane mute and solo act on them. They are made as needed
          and never shown.

        A clip's lane and route are its carrier's. Routing is per sample: every clip of a sample
        goes the same way (routeSample). Message thread. */
    class Lane
    {
    public:
        Lane() = default;
        explicit Lane (juce::ValueTree s) : state (std::move (s)) {}

        bool isValid() const noexcept           { return state.isValid(); }
        int id() const;
        juce::String getName() const;
        bool isMuted() const;
        bool isSoloed() const;

        bool operator== (const Lane& other) const noexcept   { return state == other.state; }

        juce::ValueTree state;   // its own properties; the timeline reads colour and height here
    };

    /** Lanes in order. */
    juce::Array<Lane> lanes (const tracktion::Edit&);
    Lane laneWithID (const tracktion::Edit&, int id);
    int indexOf (const tracktion::Edit&, const Lane&);

    /** The lane takes are recorded on (the first lane until another is chosen). */
    Lane recordingLane (const tracktion::Edit&);

    /** A clip's lane, and its index among the lanes (-1 if it has none). */
    Lane laneOfClip (const tracktion::Clip&);
    int laneOf (const tracktion::Clip&);

    /** Every clip on a lane, whatever its route; every clip on every lane. */
    juce::Array<tracktion::Clip*> clipsOn (tracktion::Edit&, const Lane&);
    juce::Array<tracktion::Clip*> allClips (tracktion::Edit&);

    /** The clips of every lane in one pass, indexed as lanes() (for painting). */
    std::vector<juce::Array<tracktion::Clip*>> clipsByLane (tracktion::Edit&);

    /** Lane edits. Each of these is one undo step (it begins its own), except mute and solo,
        which stay outside undo as mixers keep them. */
    juce::String nextLaneName (const tracktion::Edit&);

    /** Adds a lane below `after` (or at the end), named by nextLaneName. */
    Lane addLane (tracktion::Edit&, const Lane& after = {});

    /** Adds `count` lanes below `after` (or at the end); returns the first. */
    Lane addLanes (tracktion::Edit&, int count, const Lane& after = {});

    /** Moves a lane to `newIndex` among the lanes, clips and all. */
    void moveLane (tracktion::Edit&, const Lane&, int newIndex);

    /** Deletes a lane and its clips. The caller keeps the recording lane and the last lane. */
    void deleteLane (tracktion::Edit&, const Lane&);
    bool canDeleteLane (const tracktion::Edit&, const Lane&);

    /** An empty or blank name keeps the old one. */
    void renameLane (tracktion::Edit&, const Lane&, const juce::String&);

    void setLaneMuted (tracktion::Edit&, const Lane&, bool);
    void setLaneSoloed (tracktion::Edit&, const Lane&, bool);

    /** Cmd-click on M: every other lane muted and this one playing; if that is already so, every
        lane plays again. Cmd-click on S: this lane soloed alone; again, no lane is soloed. */
    void muteOthers (tracktion::Edit&, const Lane&);
    void soloAlone (tracktion::Edit&, const Lane&);

    //==============================================================================================
    /** Channels in mixer order (master is not one of them: it is the edit's own). */
    juce::Array<tracktion::AudioTrack*> channels (const tracktion::Edit&);
    bool isChannel (const tracktion::Track&);
    bool isCarrier (const tracktion::Track&);

    /** The channel a clip plays through, or null for master. */
    tracktion::AudioTrack* routeOf (const tracktion::Clip&);

    /** The carrier for `lane` playing into `channel` (null: master); made if there is none,
        inside the caller's undo step. */
    tracktion::AudioTrack* carrierFor (tracktion::Edit&, const Lane&, tracktion::AudioTrack* channel);

    /** Moves a clip to another lane, keeping its route. */
    void moveToLane (tracktion::Clip&, const Lane&);

    /** Routes every clip of `clip`'s sample to `channel` (null: master), each on its own lane. */
    void routeSample (tracktion::Clip&, tracktion::AudioTrack* channel);

    /** Where a channel's sound goes: another channel, or null for master. */
    tracktion::AudioTrack* outputOf (const tracktion::AudioTrack& channel);

    /** Sends a channel's output to `destination` (null: master). A route that would come back to
        the channel (a feedback loop) is refused with a reason. One undo step's worth. */
    juce::Result setOutput (tracktion::AudioTrack& channel, tracktion::AudioTrack* destination);

    /** Sends (W51, FL-style routing): besides its main output a channel plays into any number of
        other channels, each at its own level, after its fader. Implemented as Tracktion aux sends
        on the source and an aux return on each destination (a bus per destination channel). A send
        that would bring the sound back to where it started is refused. */
    struct Send
    {
        tracktion::AudioTrack* to = nullptr;
        float gainDb = 0.0f;
    };

    std::vector<Send> sendsOf (tracktion::AudioTrack& from);
    juce::Result addSend (tracktion::AudioTrack& from, tracktion::AudioTrack& to, float gainDb = 0.0f);
    void removeSend (tracktion::AudioTrack& from, tracktion::AudioTrack& to);
    void setSendLevel (tracktion::AudioTrack& from, tracktion::AudioTrack& to, float gainDb);

    /** A channel's fader: its own volume and pan (never Tracktion's getVolumePlugin(), which
        returns the last such stage, and a channel may end in a "main output off" gate). */
    tracktion::VolumeAndPanPlugin* faderOf (tracktion::AudioTrack&);

    /** Whether the main output plays at all (FL: a channel can go only to its sends). */
    bool playsToOutput (const tracktion::AudioTrack&);
    void setPlaysToOutput (tracktion::AudioTrack&, bool);

    /** True if sound leaving `from` reaches `to`, through outputs and sends. */
    bool reaches (tracktion::AudioTrack& from, tracktion::AudioTrack& to);

    /** Sidechain: a plugin that takes a second input hears `source` there (null: none). Refused if
        the plugin's channel feeds `source` (the loop would go through the plugin). */
    bool canSidechain (tracktion::Plugin&);
    tracktion::AudioTrack* sidechainSourceOf (tracktion::Plugin&);
    juce::Result setSidechainSource (tracktion::Plugin&, tracktion::AudioTrack* source);

    /** "Channel N" with the lowest N no channel uses. */
    juce::String nextChannelName (const tracktion::Edit&);

    /** A new channel after `after` (or at the end); inside the caller's undo step. */
    tracktion::AudioTrack* addChannel (tracktion::Edit&, tracktion::AudioTrack* after = nullptr);

    /** Deletes a channel: its clips play to master, channels that went into it go to master. */
    void deleteChannel (tracktion::AudioTrack&);

    /** Moves a channel to `newIndex` among the channels. */
    void moveChannel (tracktion::AudioTrack&, int newIndex);

    /** Channel mute and solo (W50): Carthage's own, outside undo as lane mute is. A soloed
        channel plays, with what plays into it and what it plays into; everything else is silent,
        clips playing straight to master too. (Tracktion's own solo would silence the carriers
        feeding a soloed channel.) */
    bool isChannelMuted (const tracktion::AudioTrack&);
    bool isChannelSoloed (const tracktion::AudioTrack&);
    void setChannelMuted (tracktion::AudioTrack&, bool);
    void setChannelSoloed (tracktion::AudioTrack&, bool);

    /** A channel's input (W51): the interface input it takes, mono or a stereo pair, or none;
        and whether it is heard (monitored). Kept with the channel, inside the caller's undo step
        (every edit is undoable); the session puts it into effect (Session::applyInputRouting),
        and again after an undo. No input also turns hearing it off. */
    struct ChannelInput
    {
        int firstChannel = -1;   // the interface's input, from 0; -1: none
        bool stereo = false;

        bool isSet() const noexcept   { return firstChannel >= 0; }
        bool operator== (const ChannelInput&) const = default;
    };

    ChannelInput inputOf (const tracktion::AudioTrack&);
    void setInput (tracktion::AudioTrack&, ChannelInput);
    bool isMonitoring (const tracktion::AudioTrack&);
    void setMonitoring (tracktion::AudioTrack&, bool);

    /** The channel takes are recorded through (one at a time until 5d). */
    tracktion::AudioTrack* recordChannel (const tracktion::Edit&);
    void setRecordChannel (tracktion::Edit&, tracktion::AudioTrack&);

    /** A channel's meters: the fader's peak meter, and the RMS meter Carthage adds beside it
        (Tracktion's measurer reports one or the other). Null on a channel without them. For
        master: the master's pair, added by prepareMix. */
    tracktion::LevelMeterPlugin* peakMeterOf (tracktion::AudioTrack&);
    tracktion::LevelMeterPlugin* rmsMeterOf (tracktion::AudioTrack&);
    tracktion::LevelMeterPlugin* masterPeakMeter (tracktion::Edit&);
    tracktion::LevelMeterPlugin* masterRMSMeter (tracktion::Edit&);

    /** The plugins a person put on a channel: not its fader or meters. */
    juce::Array<tracktion::Plugin*> insertsOf (tracktion::AudioTrack&);

    /** Insert Lines (5b, W51): the plugins on a channel, or on master (channel null), in order,
        before its own fader and meters. Each of these changes the model inside the caller's undo
        step. */
    tracktion::PluginList& pluginListOf (tracktion::Edit&, tracktion::AudioTrack* channel);
    juce::Array<tracktion::Plugin*> insertsOf (tracktion::PluginList&);

    /** Where in the plugin list the slot at `slot` goes: before that insert, or after the last. */
    int listIndexForSlot (tracktion::PluginList&, int slot);

    /** Puts `plugin` into a slot (it must not be in a list yet). */
    void placeInsert (tracktion::PluginList&, int slot, const tracktion::Plugin::Ptr& plugin);

    /** Moves an insert to another slot, in its own Line or another channel's. In its own Line it
        keeps running (its window stays open); across channels it is rebuilt from its saved state. */
    void moveInsert (tracktion::Plugin&, tracktion::PluginList& to, int slot);
    void removeInsert (tracktion::Plugin&);

    bool isBypassed (const tracktion::Plugin&);
    void setBypassed (tracktion::Plugin&, bool);

    /** Wet/dry, 0 (dry) to 1 (wet, the default), for plugins that offer it. */
    bool hasMix (const tracktion::Plugin&);
    float mixOf (const tracktion::Plugin&);
    void setMix (tracktion::Plugin&, float);

    /** The Pre-Master (W51): a channel between everything that plays to master and master
        itself, on unless the setting turns it off. Routes still name master; while it is on they
        pass through it. It is not one of channels(). */
    tracktion::AudioTrack* preMaster (const tracktion::Edit&);
    bool usesPreMaster (const tracktion::Edit&);
    void setUsesPreMaster (tracktion::Edit&, bool);

    //==============================================================================================
    /** Sets up a project's lanes and channels: a new one gets 8 lanes and 50 channels; one saved
        before 5a keeps its sound, lane n's clips playing through channel n (W47). No undo step. */
    void prepareMix (tracktion::Edit&, bool isNew, int newLanes = 8);

    /** After any edit or undo: lane mute and solo onto the carriers, and only what could sound
        in the audio graph (an empty channel costs nothing, W49). `inputChannel` is processing
        while it has the live input (and so does any channel monitoring an input). Not an undo step. */
    void syncMix (tracktion::Edit&, const tracktion::AudioTrack* inputChannel);

    /** Channels created by default (brief, feature 3). */
    constexpr int defaultChannels = 50;
}
