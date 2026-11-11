#pragma once

#include "core/Tracktion.h"

/** The plugin windows of a song together: which are open, closing them all, keeping them above
    everything else. One window is ui::PluginWindow (or the plugin's own process's window). */
namespace harmonia::ui::plugin_windows
{
    /** The song's instruments and effects whose windows are open, in the song's order. */
    juce::Array<te::Plugin*> getOpen (te::Edit&);

    /** "Lead: Surge XT", "Master: EQ". */
    juce::String describe (te::Plugin&);

    /** Opens the plugin's window if it is closed and brings it to the front. */
    void bringToFront (te::Plugin&);

    /** Closes them all. Returns how many there were. */
    int closeAll (te::Edit&);

    /** Plugin windows stay above every other window, Harmonia's and other programs' (a setting
        of the program, off at first). Setting it changes the windows that are open too. */
    bool staysOnTop (te::Engine&);
    void setStaysOnTop (te::Engine&, bool);
}
