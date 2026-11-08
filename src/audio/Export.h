#pragma once

#include "core/Session.h"

namespace harmonia::audio
{
    /** Renders the whole song (from the start to the end of the last clip, plus a moment for
        notes to ring out) to a 24-bit WAV file. Blocks until done.
        Returns an error message, empty on success. */
    juce::String exportWav (te::Edit&, const juce::File&);

    /** How long the export lets notes ring out after the last clip ends. */
    inline constexpr double exportTailSeconds = 2.0;
}
