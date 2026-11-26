#pragma once

#include <juce_core/juce_core.h>

#include <memory>

namespace carthage::engine::platform
{
    /** A block of memory two processes share by name (5f, W65): the app creates it, the plugin host
        opens it. Zeroed when created. The platform files hold the only OS calls
        (SharedMemory_posix.cpp, SharedMemory_win.cpp). */
    class SharedMemory final
    {
    public:
        static std::unique_ptr<SharedMemory> create (const juce::String& name, size_t bytes);
        static std::unique_ptr<SharedMemory> open (const juce::String& name, size_t bytes);
        ~SharedMemory();

        void* data() const noexcept;
        size_t size() const noexcept;

        /** Takes the name away so no other process can open it; the memory stays for those that
            have it. The creator calls this once the other side has opened it (macOS keeps a name
            until it is removed or the computer restarts; Windows removes it with the last user). */
        void removeName();

        struct Impl;
        explicit SharedMemory (std::unique_ptr<Impl>);

    private:
        std::unique_ptr<Impl> impl;
        JUCE_DECLARE_NON_COPYABLE (SharedMemory)
    };

    /** A counting signal between two processes. `post` never waits, so the audio thread may call
        it; `wait` blocks until a post (the plugin host's render thread). */
    class WakeSignal final
    {
    public:
        static std::unique_ptr<WakeSignal> create (const juce::String& name);
        static std::unique_ptr<WakeSignal> open (const juce::String& name);
        ~WakeSignal();

        void post() noexcept;
        void wait() noexcept;

        /** As SharedMemory::removeName. */
        void removeName();

        struct Impl;
        explicit WakeSignal (std::unique_ptr<Impl>);

    private:
        std::unique_ptr<Impl> impl;
        JUCE_DECLARE_NON_COPYABLE (WakeSignal)
    };

    /** A name no other object on this computer has, short enough for every platform (macOS allows
        31 characters): the process id, a counter and `suffix` (one letter). */
    juce::String uniqueName (char suffix);
}
