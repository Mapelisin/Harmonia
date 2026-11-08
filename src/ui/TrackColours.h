#pragma once

#include "core/Tracktion.h"
#include <juce_gui_basics/juce_gui_basics.h>

/** The colours tracks and clips are drawn in. A track is coloured by its place in the song
    unless the user chose a colour for it; a clip takes its track's colour unless it has its own. */
namespace harmonia::ui::track_colours
{
    struct NamedColour
    {
        juce::String name;
        juce::Colour colour;
    };

    /** The colours offered in the menus, in order. */
    const std::vector<NamedColour>& getPalette();

    /** The colour of a track with no colour of its own, at this place in the song. */
    juce::Colour getAutomatic (int trackIndex);

    juce::Colour getTrackColour (const te::Track&, int trackIndex);

    /** The clip's own colour, or else its track's (`trackIndex` is that track's place). */
    juce::Colour getClipColour (const te::Clip&, int trackIndex);

    /** A menu to choose a colour: `noneText` ("Automatic", "Same as the track") first, then the
        palette with swatches; the one in use is ticked. */
    juce::PopupMenu createMenu (std::optional<juce::Colour> current, const juce::String& noneText,
                                std::function<void (std::optional<juce::Colour>)> choose);
}
