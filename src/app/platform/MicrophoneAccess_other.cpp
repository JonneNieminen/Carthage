#include "MicrophoneAccess.h"

namespace carthage::app
{
    void requestMicrophoneAccess (std::function<void (bool)> onResult)
    {
        onResult (true);
    }
}
