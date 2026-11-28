// Windows: a crash ends the process at once. Without this, Windows Error Reporting can hold a
// crashed process open behind "carthage-plughost has stopped working" until someone answers it,
// and the app would not hear that the process went.
#include "../PluginWindow.h"

#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <werapi.h>

#include <cstdlib>

namespace carthage::plughost
{
    void becomeAccessory() {}

    void dieQuietly()
    {
        SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        WerSetFlags (WER_FAULT_REPORTING_NO_UI);
        _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    }
}
