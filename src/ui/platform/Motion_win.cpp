#include "../Motion.h"

#include <windows.h>

namespace carthage::ui::motion
{
    bool reduced()
    {
        // Settings > Accessibility > Visual effects > Animation effects.
        BOOL animations = TRUE;
        return SystemParametersInfoW (SPI_GETCLIENTAREAANIMATION, 0, &animations, 0) != 0 && animations == FALSE;
    }
}
