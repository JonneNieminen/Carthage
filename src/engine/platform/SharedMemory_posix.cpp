// macOS (and other POSIX systems): shm_open and mmap for the memory, a named semaphore for the
// wake signal (macOS has no unnamed semaphores between processes).
#include "SharedMemory.h"

#include <atomic>
#include <cerrno>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <unistd.h>

namespace carthage::engine::platform
{
    struct SharedMemory::Impl
    {
        juce::String name;
        void* address = nullptr;
        size_t bytes = 0;
        bool named = true;

        ~Impl()
        {
            if (address != nullptr)
                munmap (address, bytes);
        }
    };

    namespace
    {
        std::unique_ptr<SharedMemory> map (const juce::String& name, size_t bytes, bool creating)
        {
            const int flags = creating ? (O_CREAT | O_EXCL | O_RDWR) : O_RDWR;
            const int fd = shm_open (name.toRawUTF8(), flags, 0600);

            if (fd < 0)
                return {};

            if (creating && ftruncate (fd, static_cast<off_t> (bytes)) != 0)
            {
                close (fd);
                shm_unlink (name.toRawUTF8());
                return {};
            }

            void* address = mmap (nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            close (fd);

            if (address == MAP_FAILED)
            {
                if (creating)
                    shm_unlink (name.toRawUTF8());

                return {};
            }

            auto impl = std::make_unique<SharedMemory::Impl>();
            impl->name = name;
            impl->address = address;
            impl->bytes = bytes;
            impl->named = creating;   // only the creator removes the name
            return std::make_unique<SharedMemory> (std::move (impl));
        }
    }

    SharedMemory::SharedMemory (std::unique_ptr<Impl> i) : impl (std::move (i)) {}

    SharedMemory::~SharedMemory()
    {
        removeName();
    }

    std::unique_ptr<SharedMemory> SharedMemory::create (const juce::String& name, size_t bytes)   { return map (name, bytes, true); }
    std::unique_ptr<SharedMemory> SharedMemory::open (const juce::String& name, size_t bytes)     { return map (name, bytes, false); }

    void* SharedMemory::data() const noexcept   { return impl->address; }
    size_t SharedMemory::size() const noexcept  { return impl->bytes; }

    void SharedMemory::removeName()
    {
        if (impl->named)
            shm_unlink (impl->name.toRawUTF8());

        impl->named = false;
    }

    struct WakeSignal::Impl
    {
        juce::String name;
        sem_t* semaphore = SEM_FAILED;
        bool named = true;

        ~Impl()
        {
            if (semaphore != SEM_FAILED)
                sem_close (semaphore);
        }
    };

    namespace
    {
        std::unique_ptr<WakeSignal> semaphore (const juce::String& name, bool creating)
        {
            auto* s = creating ? sem_open (name.toRawUTF8(), O_CREAT | O_EXCL, 0600, 0U)
                               : sem_open (name.toRawUTF8(), 0);

            if (s == SEM_FAILED)
                return {};

            auto impl = std::make_unique<WakeSignal::Impl>();
            impl->name = name;
            impl->semaphore = s;
            impl->named = creating;
            return std::make_unique<WakeSignal> (std::move (impl));
        }
    }

    WakeSignal::WakeSignal (std::unique_ptr<Impl> i) : impl (std::move (i)) {}

    WakeSignal::~WakeSignal()
    {
        removeName();
    }

    std::unique_ptr<WakeSignal> WakeSignal::create (const juce::String& name)   { return semaphore (name, true); }
    std::unique_ptr<WakeSignal> WakeSignal::open (const juce::String& name)     { return semaphore (name, false); }

    void WakeSignal::post() noexcept   { sem_post (impl->semaphore); }

    void WakeSignal::wait() noexcept
    {
        while (sem_wait (impl->semaphore) != 0 && errno == EINTR) {}
    }

    void WakeSignal::removeName()
    {
        if (impl->named)
            sem_unlink (impl->name.toRawUTF8());

        impl->named = false;
    }

    int currentProcessID()   { return static_cast<int> (getpid()); }

    juce::String uniqueName (char suffix)
    {
        static std::atomic<int> counter { 0 };
        return "/cth" + juce::String::toHexString (static_cast<int> (getpid())) + "-"
             + juce::String (++counter) + juce::String::charToString (static_cast<juce::juce_wchar> (suffix));
    }
}
