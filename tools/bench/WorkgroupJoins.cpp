// Counts the threads that join an audio workgroup, at the operating system call itself.
//
// Tracktion Engine joins its render threads to the device's workgroup (DECISIONS W14) but does not
// check the result, and no system tool lists a workgroup's members. On macOS this file interposes
// os_workgroup_join/os_workgroup_leave in carthage-bench only. It is built as a shared library the
// bench links: dyld applies the __interpose section of a loaded library, and ignored it when this
// file was part of the executable (the probe's own successful joins counted 0). So a run can report how many threads joined and whether each join
// succeeded. The app itself is untouched. Joins happen when a thread starts, not per block.

#include "WorkgroupJoins.h"

#if defined(__APPLE__)

#include <os/workgroup.h>

#include <atomic>

namespace
{
    std::atomic<int> attempts { 0 }, joined { 0 }, failed { 0 }, left { 0 };
}

extern "C" int carthage_bench_os_workgroup_join (os_workgroup_t wg, os_workgroup_join_token_t token)
{
    const auto result = os_workgroup_join (wg, token);
    attempts.fetch_add (1, std::memory_order_relaxed);
    (result == 0 ? joined : failed).fetch_add (1, std::memory_order_relaxed);
    return result;
}

extern "C" void carthage_bench_os_workgroup_leave (os_workgroup_t wg, os_workgroup_join_token_t token)
{
    os_workgroup_leave (wg, token);
    left.fetch_add (1, std::memory_order_relaxed);
}

namespace
{
    struct Interposer
    {
        const void* replacement;
        const void* original;
    };

    [[gnu::used, gnu::section ("__DATA,__interpose")]] const Interposer interposers[] = {
        { reinterpret_cast<const void*> (&carthage_bench_os_workgroup_join), reinterpret_cast<const void*> (&os_workgroup_join) },
        { reinterpret_cast<const void*> (&carthage_bench_os_workgroup_leave), reinterpret_cast<const void*> (&os_workgroup_leave) },
    };
}

namespace carthage::bench
{
    WorkgroupJoins getWorkgroupJoins() noexcept
    {
        return { true,
                 attempts.load (std::memory_order_relaxed), joined.load (std::memory_order_relaxed),
                 failed.load (std::memory_order_relaxed), left.load (std::memory_order_relaxed) };
    }
}

#else

namespace carthage::bench
{
    WorkgroupJoins getWorkgroupJoins() noexcept   { return {}; }
}

#endif
