#pragma once

#include "core/Tracktion.h"

/** A plugin's node as the song keeps it, with the settings the plugin has right now. */
namespace harmonia::core::plugin_state
{
    /** What Tracktion's Plugin::flushPluginStateToValueTree writes, written into a copy of the
        plugin's node instead: the plugin's own state (VST settings, changed in its window
        without going through undo), and the values set by hand of parameters the automation
        is moving. Tracktion writes them into the open edit as undoable steps (the built-in
        synth even replaces a node each time), which made Ctrl+Z after saving undo nothing
        visible and dropped the redo steps. */
    void write (te::Plugin&, juce::ValueTree& node);

    /** A copy of the plugin's node with its settings as they are now. */
    juce::ValueTree snapshot (te::Plugin&);
}
