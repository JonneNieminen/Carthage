#pragma once

#include <functional>

namespace carthage::app
{
    /** Asks the operating system for access to audio inputs, without blocking.

        On macOS the first open of an input device otherwise blocks the message thread inside
        CoreAudio until the user answers the system's microphone prompt, with no window on screen.
        Asking first, asynchronously, lets the app open its window and then the device. The callback
        runs on the message thread. On other platforms it is called at once with true. */
    void requestMicrophoneAccess (std::function<void (bool granted)> onResult);
}
