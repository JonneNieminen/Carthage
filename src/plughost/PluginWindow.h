#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace carthage::plughost
{
    /** A plugin's window in carthage-plughost (5f-3, W68), made to match the app's own plugin
        windows (ui/PluginWindows): the native title bar with the plugin's name, Carthage's
        background behind the plugin's own editor (or JUCE's generic one), the editor's own size,
        centred on the main display. Nothing of Carthage's is drawn inside it: the plugin looks
        the same wherever it runs.

        Cmd/Ctrl+W closes it, as in Carthage; any other key the plugin does not use goes to
        Carthage (`onKey`), so its commands (play, save, undo) still work from here. */
    class PluginWindow final : public juce::DocumentWindow
    {
    public:
        PluginWindow (juce::AudioPluginInstance&, const juce::String& title, juce::Colour background,
                      std::function<void()> closed, std::function<void (const juce::KeyPress&)> key);
        ~PluginWindow() override;

        void closeButtonPressed() override;
        bool keyPressed (const juce::KeyPress&) override;

        /** At the plugin's own size, as in Carthage (W38). */
        float getDesktopScaleFactor() const override   { return 1.0f; }

    private:
        std::function<void()> onClosed;
        std::function<void (const juce::KeyPress&)> onKey;
    };

    /** macOS: the host is an accessory, so it has no Dock icon or menu of its own and its windows
        sit with Carthage's (platform/HostApp_mac.mm); elsewhere nothing to do. */
    void becomeAccessory();
}
