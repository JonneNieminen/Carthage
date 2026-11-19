#include "../Motion.h"

#import <AppKit/AppKit.h>

namespace carthage::ui::motion
{
    bool reduced()
    {
        return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion] == YES;
    }
}
