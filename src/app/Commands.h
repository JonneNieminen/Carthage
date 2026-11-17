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
        };

        inline juce::Array<juce::CommandID> all()
        {
            return { record, playStop, returnToStart, toggleMonitoring, choosePlugin,
                     openPluginEditor, bypassPlugin, audioSettings, measureLatency };
        }

        /** Commands the application itself performs, whatever window is in front. */
        inline juce::Array<juce::CommandID> application()
        {
            return { checkForUpdates };
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
                    info.setInfo ("Choose a plugin", "Choose the plugin the input plays through", "Plugin", 0);
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
                case checkForUpdates:
                    info.setInfo (juce::String::fromUTF8 ("Check for updates\xe2\x80\xa6"), "See whether a newer Carthage is out", "Carthage", 0);
                    break;
                case measureLatency:
                    info.setInfo ("Measure latency", "Measure the round trip through a cable from an output to an input", "Settings", 0);
                    break;
                default:
                    break;
            }
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
