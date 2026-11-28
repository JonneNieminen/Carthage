// macOS: carthage-plughost runs as an accessory app, with no Dock icon, no menu bar of its own and
// no place in the app switcher; its plugin windows still take the keyboard when clicked.
#include "../PluginWindow.h"

#import <AppKit/AppKit.h>

namespace carthage::plughost
{
    void becomeAccessory()
    {
        [NSApp setActivationPolicy: NSApplicationActivationPolicyAccessory];
    }
}

namespace carthage::plughost
{
    // macOS shows no dialog for a helper without a bundle that crashes; nothing to do.
    void dieQuietly() {}

    void comeForward()
    {
        // macOS 14 and later: Carthage yields its activation to this process, which takes it.
        if (@available (macOS 14.0, *))
            [NSApp activate];
        else
            [NSApp activateIgnoringOtherApps: YES];
    }

    void* enterProAudio()       { return nullptr; }
    void leaveProAudio (void*)  {}
}
