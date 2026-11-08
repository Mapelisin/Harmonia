#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace harmonia::ui
{
    /** Picks the renderer for a top-level window. All renderer choices live here.
        Currently: the software renderer on Windows, because JUCE's Direct2D path
        is capped at 30 fps for continuous animation (docs/research/spike-02.md).
        Call after the window is visible (it needs a native peer). */
    void applyPreferredRenderer (juce::Component& topLevelWindow);

    /** Name of the renderer the window is using, for diagnostics. */
    juce::String getCurrentRendererName (juce::Component& topLevelWindow);
}
