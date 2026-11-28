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
