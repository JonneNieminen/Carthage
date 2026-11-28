// macOS: Carthage hands its activation to a plugin host about to show a window (macOS 14 and
// later make activation cooperative; before, the host simply activates itself).
#include "SharedMemory.h"

#import <AppKit/AppKit.h>

namespace carthage::engine::platform
{
    void letComeForward (int pid)
    {
        if (pid <= 0)
            return;

        if (@available (macOS 14.0, *))
            if (NSRunningApplication* app = [NSRunningApplication runningApplicationWithProcessIdentifier: pid])
                [NSApp yieldActivationToApplication: app];
    }
}
