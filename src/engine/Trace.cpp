#include "Trace.h"

#if defined (__APPLE__)
 #include <os/signpost.h>
#endif

namespace carthage::trace
{
   #if defined (__APPLE__)
    namespace
    {
        os_log_t handle()
        {
            static const auto log = os_log_create ("fi.carthage", "Work");
            return log;
        }
    }

    Scope::Scope (const char* what) noexcept
    {
        auto* log = handle();

        if (! os_signpost_enabled (log))
            return;

        id = os_signpost_id_generate (log);
        os_signpost_interval_begin (log, id, "Work", "%{public}s", what);
    }

    Scope::~Scope() noexcept
    {
        if (id != 0)
            os_signpost_interval_end (handle(), id, "Work");
    }
   #else
    Scope::Scope (const char*) noexcept {}
    Scope::~Scope() noexcept {}
   #endif
}
