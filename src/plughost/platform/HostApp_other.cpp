// Elsewhere (Linux): nothing to change.
#include "../PluginWindow.h"

namespace carthage::plughost
{
    void becomeAccessory() {}
    void dieQuietly() {}
    void comeForward() {}
    void* enterProAudio()       { return nullptr; }
    void leaveProAudio (void*)  {}
}
