#pragma once

namespace carthage::trace
{
    /** Signposts (W38): a scope shows as a named interval in Instruments (Points of Interest and
        os_signpost, subsystem "fi.carthage", category "Work"), so a slow frame or a slow open can be
        seen on a timeline. Free when nothing is recording. A no-op off macOS.

        Never on an audio thread: the real-time rules (F8) leave no room for system calls there. */
    class Scope
    {
    public:
        explicit Scope (const char* what) noexcept;
        ~Scope() noexcept;

        Scope (const Scope&) = delete;
        Scope& operator= (const Scope&) = delete;

    private:
        unsigned long long id = 0;
    };
}
