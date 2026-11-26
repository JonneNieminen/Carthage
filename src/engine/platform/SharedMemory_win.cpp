// Windows: a file mapping backed by the paging file for the memory, a named semaphore for the wake
// signal. Both go away with the last handle, so removing a name only stops further opens.
#include "SharedMemory.h"

#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <climits>
#include <string>

namespace carthage::engine::platform
{
    namespace
    {
        /** "/cth…" (the portable form) as a session-local Windows name. */
        std::wstring windowsName (const juce::String& name)
        {
            return ("Local\\carthage-" + name.trimCharactersAtStart ("/")).toWideCharPointer();
        }
    }

    struct SharedMemory::Impl
    {
        HANDLE mapping = nullptr;
        void* address = nullptr;
        size_t bytes = 0;

        ~Impl()
        {
            if (address != nullptr)
                UnmapViewOfFile (address);

            if (mapping != nullptr)
                CloseHandle (mapping);
        }
    };

    namespace
    {
        std::unique_ptr<SharedMemory> map (const juce::String& name, size_t bytes, bool creating)
        {
            const auto wide = windowsName (name);
            const auto size = static_cast<unsigned long long> (bytes);
            HANDLE mapping = creating
                ? CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                      static_cast<DWORD> (size >> 32), static_cast<DWORD> (size & 0xffffffffULL), wide.c_str())
                : OpenFileMappingW (FILE_MAP_ALL_ACCESS, FALSE, wide.c_str());

            if (mapping == nullptr)
                return {};

            if (creating && GetLastError() == ERROR_ALREADY_EXISTS)
            {
                CloseHandle (mapping);
                return {};
            }

            void* address = MapViewOfFile (mapping, FILE_MAP_ALL_ACCESS, 0, 0, bytes);

            if (address == nullptr)
            {
                CloseHandle (mapping);
                return {};
            }

            auto impl = std::make_unique<SharedMemory::Impl>();
            impl->mapping = mapping;
            impl->address = address;
            impl->bytes = bytes;
            return std::make_unique<SharedMemory> (std::move (impl));
        }
    }

    SharedMemory::SharedMemory (std::unique_ptr<Impl> i) : impl (std::move (i)) {}
    SharedMemory::~SharedMemory() = default;

    std::unique_ptr<SharedMemory> SharedMemory::create (const juce::String& name, size_t bytes)   { return map (name, bytes, true); }
    std::unique_ptr<SharedMemory> SharedMemory::open (const juce::String& name, size_t bytes)     { return map (name, bytes, false); }

    void* SharedMemory::data() const noexcept   { return impl->address; }
    size_t SharedMemory::size() const noexcept  { return impl->bytes; }
    void SharedMemory::removeName()             {}

    struct WakeSignal::Impl
    {
        HANDLE semaphore = nullptr;

        ~Impl()
        {
            if (semaphore != nullptr)
                CloseHandle (semaphore);
        }
    };

    namespace
    {
        std::unique_ptr<WakeSignal> semaphore (const juce::String& name, bool creating)
        {
            const auto wide = windowsName (name);
            HANDLE s = creating ? CreateSemaphoreW (nullptr, 0, LONG_MAX, wide.c_str())
                                : OpenSemaphoreW (SEMAPHORE_MODIFY_STATE | SYNCHRONIZE, FALSE, wide.c_str());

            if (s == nullptr)
                return {};

            if (creating && GetLastError() == ERROR_ALREADY_EXISTS)
            {
                CloseHandle (s);
                return {};
            }

            auto impl = std::make_unique<WakeSignal::Impl>();
            impl->semaphore = s;
            return std::make_unique<WakeSignal> (std::move (impl));
        }
    }

    WakeSignal::WakeSignal (std::unique_ptr<Impl> i) : impl (std::move (i)) {}
    WakeSignal::~WakeSignal() = default;

    std::unique_ptr<WakeSignal> WakeSignal::create (const juce::String& name)   { return semaphore (name, true); }
    std::unique_ptr<WakeSignal> WakeSignal::open (const juce::String& name)     { return semaphore (name, false); }

    void WakeSignal::post() noexcept   { ReleaseSemaphore (impl->semaphore, 1, nullptr); }
    void WakeSignal::wait() noexcept   { WaitForSingleObject (impl->semaphore, INFINITE); }
    void WakeSignal::removeName()      {}

    juce::String uniqueName (char suffix)
    {
        static std::atomic<int> counter { 0 };
        return "/cth" + juce::String::toHexString (static_cast<int> (GetCurrentProcessId())) + "-"
             + juce::String (++counter) + juce::String::charToString (static_cast<juce::juce_wchar> (suffix));
    }
}
