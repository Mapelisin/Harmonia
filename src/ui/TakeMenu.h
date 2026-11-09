#pragma once

#include "core/Project.h"

namespace harmonia::ui::take_menu
{
    /** "Take 2 of 4" for a clip with takes (recorded round a loop), else empty. */
    juce::String describe (const te::Clip&);

    /** The clip's takes, the one it plays ticked (choosing one is an undo step), and
        "Keep only this take". Empty for a clip without takes. */
    juce::PopupMenu make (core::Project&, te::AudioClipBase&);

    /** The clip plays this take (one undo step). False if it can't (the file is gone). */
    bool choose (core::Project&, te::AudioClipBase&, int index);
}
