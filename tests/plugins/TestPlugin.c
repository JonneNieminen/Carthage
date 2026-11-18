/* A deliberately broken "VST3" for the scanner's tests (docs/TESTING.md). It has no plugin in it:
   loading the binary runs this constructor, which crashes (abort) or never returns (hang), the way
   a broken plugin does at load. Built into build/<preset>/test-plugins only, never installed. */

#include <stdlib.h>
#include <unistd.h>

__attribute__((constructor)) static void carthageTestOnLoad (void)
{
#if CARTHAGE_TEST_HANG
    for (;;)
        sleep (1);
#else
    abort();
#endif
}

/* The VST3 entry point a host looks for; never reached. */
__attribute__((visibility ("default"))) void* GetPluginFactory (void)
{
    return 0;
}
