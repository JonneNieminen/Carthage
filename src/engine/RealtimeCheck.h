#pragma once

// Real-time safety checking (docs/TESTING.md, "Real-time safety").
//
// In an RTSan build (preset `rtsan`, CARTHAGE_RTSAN=1) every scope that opens a ScopedRealtime is
// checked by Clang's RealTimeSanitizer: any allocation, free, lock, blocking system call or file I/O
// made from inside it — by Carthage, JUCE, Tracktion Engine or a plugin — is reported with a stack
// trace. Carthage opens one around the whole device callback (AudioCallback), so the check covers
// everything the audio thread does after JUCE hands it a block. In every other build these are no-ops.

#if CARTHAGE_RTSAN
 #include <sanitizer/rtsan_interface.h>

 // The runtime entry points the compiler calls around a [[clang::nonblocking]] function. They are
 // exported by the RTSan runtime but not declared in its public header (checked against LLVM 23's
 // libclang_rt.rtsan_osx_dynamic.dylib). Calling them directly marks a scope as real-time without
 // having to annotate the third-party code it calls.
 extern "C" void __rtsan_realtime_enter();
 extern "C" void __rtsan_realtime_exit();
#endif

// Marks a function that runs on an audio thread. RTSan checks everything it does at run time.
#if defined (__clang__)
 #define CARTHAGE_NONBLOCKING [[clang::nonblocking]]
#else
 #define CARTHAGE_NONBLOCKING
#endif

namespace carthage::rt
{
    struct ScopedRealtime
    {
       #if CARTHAGE_RTSAN
        ScopedRealtime() noexcept   { __rtsan_realtime_enter(); }
        ~ScopedRealtime() noexcept  { __rtsan_realtime_exit(); }
       #else
        ScopedRealtime() noexcept {}
        ~ScopedRealtime() noexcept {}
       #endif

        ScopedRealtime (const ScopedRealtime&) = delete;
        ScopedRealtime& operator= (const ScopedRealtime&) = delete;
    };

    /** True in a build where the checks above are live. */
    constexpr bool isChecked() noexcept
    {
       #if CARTHAGE_RTSAN
        return true;
       #else
        return false;
       #endif
    }
}
