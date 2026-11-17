#include "MicrophoneAccess.h"

#include <juce_events/juce_events.h>

#import <AVFoundation/AVFoundation.h>

namespace carthage::app
{
    void requestMicrophoneAccess (std::function<void (bool)> onResult)
    {
        const auto status = [AVCaptureDevice authorizationStatusForMediaType: AVMediaTypeAudio];

        if (status == AVAuthorizationStatusAuthorized)    { onResult (true);  return; }
        if (status != AVAuthorizationStatusNotDetermined) { onResult (false); return; }

        auto shared = std::make_shared<std::function<void (bool)>> (std::move (onResult));

        [AVCaptureDevice requestAccessForMediaType: AVMediaTypeAudio
                                 completionHandler: ^(BOOL granted)
                                 {
                                     juce::MessageManager::callAsync ([shared, granted] { (*shared) (granted == YES); });
                                 }];
    }
}
