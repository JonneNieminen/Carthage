#pragma once

#include "Controls.h"

#include <engine/Session.h>

namespace carthage::ui
{
    /** The mixer (phase 5a, W47, W48): master pinned on the left, a narrow strip for every
        channel (colour, name, output, pan, mute, solo, fader with a peak and RMS meter), and the
        selected channel's column on the right (where it plays into, what plays into it; its
        insert Line arrives in 5b). It reads the edit and listens; it keeps no copy. F9 shows it in
        place of the lanes, and it can move to a window of its own like the other views.

        Meters are the only thing in it that moves during playback (AGENTS.md, UI). */
    class MixerView final : public juce::Component, private juce::ValueTree::Listener, private juce::Timer
    {
    public:
        explicit MixerView (engine::Session&);
        ~MixerView() override;

        /** The front project changed: strips for its channels. Before it closes: let go. */
        void projectChanged();
        void projectClosing();

        /** Selects a channel's strip and shows it in the column. */
        void select (tracktion::AudioTrack*);

        /** A line for the status bar ("Channel 3 can not play into itself"). */
        std::function<void (const juce::String&)> onStatus;

        void paint (juce::Graphics&) override;
        void resized() override;

        class Strip;

    private:
        engine::Session& session;
        juce::ValueTree editState;
        juce::OwnedArray<Strip> strips;   // master first
        juce::Viewport viewport;
        juce::Component stripHolder;
        juce::TextButton addButton { "Add channel" };
        tracktion::EditItemID selected;
        bool rebuildPending = false;

        // The selected channel's column.
        juce::Label columnName, columnInto, columnFrom, columnFromList, columnPlugins, columnPluginList;
        juce::ComboBox output;
        void refreshColumn();

        void rebuild();
        tracktion::AudioTrack* selectedChannel() const;
        void showMenu (tracktion::AudioTrack&);
        void showOutputMenu (tracktion::AudioTrack&, juce::Component& from);
        void chooseOutput (tracktion::AudioTrack&, tracktion::AudioTrack* destination);

        void timerCallback() override;
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override             { triggerRebuild(); }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override      { triggerRebuild(); }
        void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override              { triggerRebuild(); }
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
        void triggerRebuild();

        friend class Strip;
    };
}
