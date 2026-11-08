#include "ui/PhysicalKeys.h"

#ifdef _WIN32
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace harmonia::ui::physical_keys
{
   #ifdef _WIN32
    bool isSupported()
    {
        return true;
    }

    bool isDown (int scanCode)
    {
        // The scan code names the physical key; the current layout says which virtual key it
        // produces, and the virtual key's state is what Windows tracks.
        const auto virtualKey = MapVirtualKeyW ((UINT) scanCode, MAPVK_VSC_TO_VK);
        return virtualKey != 0 && (GetAsyncKeyState ((int) virtualKey) & 0x8000) != 0;
    }
   #else
    bool isSupported()
    {
        return false;
    }

    bool isDown (int)
    {
        return false;
    }
   #endif
}
