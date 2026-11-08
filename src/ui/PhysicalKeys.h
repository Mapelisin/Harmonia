#pragma once

// No JUCE here on purpose: the Windows implementation includes <windows.h>.

namespace harmonia::ui::physical_keys
{
    /** True if keys can be read by their place on the keyboard (Windows). Elsewhere callers fall
        back to characters. */
    bool isSupported();

    /** Whether the key at this place is held down right now, whatever the keyboard layout
        (English, Russian, ...). `scanCode` is the PC set-1 scan code of the key, e.g. 0x2C for
        the key labelled Z on a US keyboard (Я on a Russian one). */
    bool isDown (int scanCode);
}
