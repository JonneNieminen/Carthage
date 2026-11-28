// Linux and elsewhere: nothing stops a process bringing its window forward.
#include "SharedMemory.h"

namespace carthage::engine::platform
{
    void letComeForward (int) {}
}
