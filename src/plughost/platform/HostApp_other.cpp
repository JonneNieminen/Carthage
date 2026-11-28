// Windows and elsewhere: a console program with windows already has no taskbar entry of its own
// beyond its windows; nothing to change.
#include "../PluginWindow.h"

namespace carthage::plughost
{
    void becomeAccessory() {}
}
