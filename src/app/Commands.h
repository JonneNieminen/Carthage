#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace carthage::app
{
    /** The command table (AGENTS.md: one table for menus, shortcuts, the shortcut editor, tooltips
        and the help page). Each command has an id, a name, a description and default keys; the menu
        bar, the keyboard and every tooltip read them from here. Keys use JUCE's commandModifier, which
        is Cmd on macOS and Ctrl on Windows, and are never written into UI text by hand.

        Tool keys E P B C D T Z S are reserved for the timeline tools (F7, phase 4). */
    namespace commands
    {
        enum ID : juce::CommandID
        {
            record = 0x2001,
            playStop,
            returnToStart,
            toggleMonitoring,
            choosePlugin,
            openPluginEditor,
            bypassPlugin,
            audioSettings,
            measureLatency,
            checkForUpdates,   // handled by the application, not the window (app menu)
            pluginBrowser,

            // Timeline tools (F7). In this order: tools() maps them to the timeline's Tool values.
            toolSelect, toolDraw, toolPaint, toolSlice, toolDelete, toolMute, toolZoom, toolStretch,

            // Timeline edits
            undo, redo, toggleGrid, splitAtPlayhead, duplicateClips, deleteClips,
            nudgeLeft, nudgeRight, addMarker, loopSelection,
            keyboardShortcuts,

            // Projects (handled by the application: they can replace the window's edit)
            newProject, openProject, saveProject, saveProjectAs,
        };

        /** The tool commands, in Timeline::Tool order. */
        inline juce::Array<juce::CommandID> tools()
        {
            return { toolSelect, toolDraw, toolPaint, toolSlice, toolDelete, toolMute, toolZoom, toolStretch };
        }

        inline juce::Array<juce::CommandID> all()
        {
            return { record, playStop, returnToStart, toggleMonitoring, choosePlugin,
                     openPluginEditor, bypassPlugin, audioSettings, measureLatency, pluginBrowser,
                     toolSelect, toolDraw, toolPaint, toolSlice, toolDelete, toolMute, toolZoom, toolStretch,
                     undo, redo, toggleGrid, splitAtPlayhead, duplicateClips, deleteClips,
                     nudgeLeft, nudgeRight, addMarker, loopSelection, keyboardShortcuts };
        }

        /** Commands the application itself performs, whatever window is in front. */
        inline juce::Array<juce::CommandID> application()
        {
            return { checkForUpdates, newProject, openProject, saveProject, saveProjectAs };
        }

        /** Name, description, category and default keys. Flags (enabled, ticked) are added by the
            command target, which knows the current state. */
        inline void describe (juce::CommandID id, juce::ApplicationCommandInfo& info)
        {
            using KP = juce::KeyPress;
            const auto primary = juce::ModifierKeys::commandModifier;

            switch (id)
            {
                case record:
                    info.setInfo ("Record", "Record a take from the input", "Transport", 0);
                    info.addDefaultKeypress ('r', 0);
                    break;
                case playStop:
                    info.setInfo ("Play or stop", "Play from the playhead, or stop", "Transport", 0);
                    info.addDefaultKeypress (KP::spaceKey, 0);
                    break;
                case returnToStart:
                    info.setInfo ("Return to start", "Move the playhead to the start", "Transport", 0);
                    info.addDefaultKeypress (KP::homeKey, 0);
                    break;
                case toggleMonitoring:
                    info.setInfo ("Hear the input", "Hear the input through the plugin while you play", "Input", 0);
                    info.addDefaultKeypress ('m', 0);
                    break;
                case choosePlugin:
                    info.setInfo ("Choose a plugin", "Search the plugin browser for the plugin the input plays through", "Plugin", 0);
                    info.addDefaultKeypress ('i', primary);
                    break;
                case openPluginEditor:
                    info.setInfo ("Open plugin", "Show the plugin's own window", "Plugin", 0);
                    info.addDefaultKeypress ('o', primary | juce::ModifierKeys::shiftModifier);
                    break;
                case bypassPlugin:
                    info.setInfo ("Bypass plugin", "Pass the input through without the plugin", "Plugin", 0);
                    info.addDefaultKeypress ('b', primary | juce::ModifierKeys::shiftModifier);
                    break;
                case audioSettings:
                    info.setInfo ("Settings", "Audio interface, latency and updates", "Settings", 0);
                    info.addDefaultKeypress (',', primary);
                    break;
                // Tools: a tap switches; holding the key switches until it is released (F7).
                case toolSelect:  info.setInfo ("Select", "Select, move and trim clips; drag an edge to trim, with Option/Alt to stretch, with Cmd/Ctrl to slip", "Tools", juce::ApplicationCommandInfo::wantsKeyUpDownCallbacks); info.addDefaultKeypress ('e', 0); break;
                case toolDraw:    info.setInfo ("Draw", "Place a copy of the last clip you used where you click", "Tools", juce::ApplicationCommandInfo::wantsKeyUpDownCallbacks); info.addDefaultKeypress ('p', 0); break;
                case toolPaint:   info.setInfo ("Paint", "Drag to place copies of the last clip one after another", "Tools", juce::ApplicationCommandInfo::wantsKeyUpDownCallbacks); info.addDefaultKeypress ('b', 0); break;
                case toolSlice:   info.setInfo ("Slice", "Click a clip to cut it in two there", "Tools", juce::ApplicationCommandInfo::wantsKeyUpDownCallbacks); info.addDefaultKeypress ('c', 0); break;
                case toolDelete:  info.setInfo ("Delete", "Click or drag over clips to delete them", "Tools", juce::ApplicationCommandInfo::wantsKeyUpDownCallbacks); info.addDefaultKeypress ('d', 0); break;
                case toolMute:    info.setInfo ("Mute", "Click or drag over clips to mute or unmute them", "Tools", juce::ApplicationCommandInfo::wantsKeyUpDownCallbacks); info.addDefaultKeypress ('t', 0); break;
                case toolZoom:    info.setInfo ("Zoom", "Click to zoom in, Option/Alt-click to zoom out, drag across a range to fill the view with it", "Tools", juce::ApplicationCommandInfo::wantsKeyUpDownCallbacks); info.addDefaultKeypress ('z', 0); break;
                case toolStretch: info.setInfo ("Stretch", "Drag a clip's edge to time-stretch it", "Tools", juce::ApplicationCommandInfo::wantsKeyUpDownCallbacks); info.addDefaultKeypress ('s', 0); break;

                case undo:            info.setInfo ("Undo", "Undo the last edit", "Edit", 0); info.addDefaultKeypress ('z', primary); break;
                case redo:            info.setInfo ("Redo", "Redo the edit you undid", "Edit", 0); info.addDefaultKeypress ('z', primary | juce::ModifierKeys::shiftModifier); break;
                case toggleGrid:      info.setInfo ("Snap to grid", "Turn the grid on or off", "Edit", 0); info.addDefaultKeypress ('g', 0); break;
                case splitAtPlayhead: info.setInfo ("Split at playhead", "Cut the selected clips (or every clip under the playhead) in two at the playhead", "Edit", 0); info.addDefaultKeypress ('e', primary); break;
                case duplicateClips:  info.setInfo ("Duplicate", "Copy the selected clips right after themselves", "Edit", 0); info.addDefaultKeypress ('d', primary); break;
                case deleteClips:     info.setInfo ("Delete clips", "Delete the selected clips", "Edit", 0); info.addDefaultKeypress (KP::deleteKey, 0); info.addDefaultKeypress (KP::backspaceKey, 0); break;
                case nudgeLeft:       info.setInfo ("Nudge left", "Move the selected clips one grid step earlier", "Edit", 0); info.addDefaultKeypress (KP::leftKey, 0); break;
                case nudgeRight:      info.setInfo ("Nudge right", "Move the selected clips one grid step later", "Edit", 0); info.addDefaultKeypress (KP::rightKey, 0); break;
                case addMarker:       info.setInfo ("Add marker", "Add a marker at the playhead", "Edit", 0); info.addDefaultKeypress ('m', juce::ModifierKeys::shiftModifier); break;
                case loopSelection:   info.setInfo ("Loop selection", "Loop the range the selected clips cover", "Edit", 0); info.addDefaultKeypress ('l', primary); break;

                case newProject:      info.setInfo ("New project", "Start an empty project", "File", 0); info.addDefaultKeypress ('n', primary); break;
                case openProject:     info.setInfo (juce::String::fromUTF8 ("Open\xe2\x80\xa6"), "Open a saved project", "File", 0); info.addDefaultKeypress ('o', primary); break;
                case saveProject:     info.setInfo ("Save", "Save the project", "File", 0); info.addDefaultKeypress ('s', primary); break;
                case saveProjectAs:   info.setInfo (juce::String::fromUTF8 ("Save as\xe2\x80\xa6"), "Save the project in a new folder, with its recordings", "File", 0); info.addDefaultKeypress ('s', primary | juce::ModifierKeys::shiftModifier); break;

                case pluginBrowser:
                    info.setInfo ("Show plugins", "Show or hide the plugin browser", "Plugin", 0);
                    info.addDefaultKeypress (KP::F8Key, 0);
                    break;
                case checkForUpdates:
                    info.setInfo (juce::String::fromUTF8 ("Check for updates\xe2\x80\xa6"), "See whether a newer Carthage is out", "Carthage", 0);
                    break;
                case keyboardShortcuts:
                    info.setInfo (juce::String::fromUTF8 ("Keyboard shortcuts\xe2\x80\xa6"), "See and change every shortcut", "Settings", 0);
                    break;
                case measureLatency:
                    info.setInfo ("Measure latency", "Measure the round trip through a cable from an output to an input", "Settings", 0);
                    break;
                default:
                    break;
            }
        }

        /** Every command, grouped as the menus are (the shortcut editor lists them this way). */
        inline juce::Array<juce::CommandID> inMenuOrder()
        {
            auto ids = all();
            ids.addArray (application());

            const juce::StringArray order { "File", "Transport", "Input", "Plugin", "Edit", "Tools", "Settings", "Carthage" };
            const auto rank = [&] (juce::CommandID id)
            {
                juce::ApplicationCommandInfo info (id);
                describe (id, info);
                const auto i = order.indexOf (info.categoryName);
                return i < 0 ? order.size() : i;
            };

            std::stable_sort (ids.begin(), ids.end(), [&] (auto a, auto b) { return rank (a) < rank (b); });
            return ids;
        }

        /** "Record (R)": the tooltip for a control, with its current shortcut from the key mappings. */
        inline juce::String tooltip (juce::ApplicationCommandManager& manager, juce::CommandID id)
        {
            juce::ApplicationCommandInfo info (id);
            describe (id, info);
            auto tip = info.description;

            if (auto* mappings = manager.getKeyMappings())
            {
                const auto keys = mappings->getKeyPressesAssignedToCommand (id);

                if (! keys.isEmpty())
                    tip << " (" << keys.getFirst().getTextDescriptionWithIcons() << ")";
            }

            return tip;
        }
    }
}
