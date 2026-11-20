#pragma once

#include "Controls.h"
#include "PluginWindows.h"

#include <engine/Session.h>

namespace carthage::ui
{
    /** The mixer (phase 5a, W47, W48): master pinned on the left, a narrow strip for every
        channel (colour, name, output, pan, mute, solo, fader with a peak and RMS meter), and the
        selected channel's column on the right (where it plays into, what plays into it; its
        insert Line arrives in 5b). It reads the edit and listens; it keeps no copy. F9 shows it in
        place of the lanes, and it can move to a window of its own like the other views.

        Meters are the only thing in it that moves during playback (AGENTS.md, UI). */
    class MixerView final : public juce::Component, public juce::DragAndDropContainer,
                            private juce::ValueTree::Listener, private juce::Timer
    {
    public:
        MixerView (engine::Session&, PluginWindows&);
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
        class InsertLine;

        /** A plugin from the browser, or an insert dragged from a Line (drag descriptions). */
        static constexpr const char* insertDragPrefix = "carthage-insert:";

        /** Loads a plugin into a channel's Line (null: master) at `slot` (-1: the end), and selects it. */
        void addPlugin (tracktion::AudioTrack* channel, int slot, const juce::PluginDescription&);

        /** The plugin browser's double-click: onto the selected channel's Line (channel 1 if none). */
        void addPluginToSelected (const juce::PluginDescription&);

        /** The selected channel's (or master's) first plugin: the commands that open or bypass one. */
        tracktion::Plugin* firstPluginOfSelected();

        /** The interface changed: input and output choices again. */
        void devicesChanged();

        /** The choices for an input picker, and their ids: channel + 1 mono, 1000 + channel + 1 a pair. */
        void fillInputs (juce::ComboBox&) const;
        void fillInputs (juce::PopupMenu&, tracktion::AudioTrack&);
        static editing::ChannelInput inputForId (int id);
        static int idForInput (editing::ChannelInput);
        juce::String inputName (editing::ChannelInput) const;

        /** How the selected channel reaches `to` (null: master): what the route buttons and cables show. */
        enum class Route { none, output, send, sidechain };
        Route routeTo (tracktion::AudioTrack* to);

        /** A click on a strip's route button (FL): send here or stop; with Cmd, play only here. */
        void toggleRoute (tracktion::AudioTrack* to, bool only);
        void showRouteMenu (tracktion::AudioTrack& to, juce::Component& from);
        void routesChanged();

        /** Strip width, as the lanes zoom in time: Cmd+wheel or pinch over the strips (W51). */
        void zoomBy (float factor);

        /** The plugin dragged from a Line, by its drag description. */
        tracktion::Plugin* draggedInsert (const juce::var& description) const;

    private:
        engine::Session& session;
        PluginWindows& windows;
        juce::ValueTree editState;
        juce::OwnedArray<Strip> strips;   // master first
        juce::Viewport viewport;
        class StripHolder;
        std::unique_ptr<StripHolder> stripHolder;
        juce::TextButton addButton { "Add channel" };
        tracktion::EditItemID selected;
        bool masterSelected = false;
        int pinned = 1;
        float zoom = 1.0f;   // strips left of the scrolling channels: master, and the Pre-Master while on
        bool rebuildPending = false;

        // The selected channel's column.
        juce::Label columnName, columnInto, columnFrom, columnFromList, columnPlugins, columnInput;
        juce::ComboBox input, masterOutput;
        Switch hear { "Hear the input" }, recordHere { "Takes record here" };
        std::unique_ptr<InsertLine> line;
        juce::ComboBox output;
        void refreshColumn();

        void rebuild();
        tracktion::AudioTrack* selectedChannel() const;
        void selectMaster();
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
