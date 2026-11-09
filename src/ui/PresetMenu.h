#pragma once

#include "audio/Presets.h"

/** The presets menu of a plugin, the same wherever it is: an effect's slot in the mixer, the
    instrument's row, Harmonia's knobs window. */
namespace harmonia::ui::preset_menu
{
    /** Tells what happened ("Preset: Warm pad"), or a problem (true). */
    using Tell = std::function<void (const juce::String& text, bool isProblem)>;

    /** Save the settings as a preset, the plugin's presets to load (the last one used ticked), the
        presets it comes with, back to its first settings, the presets' folder. */
    juce::PopupMenu make (te::Plugin&, Tell, const juce::File& presetsFolder = audio::presets::getDefaultFolder());

    /** Asks for a name, then saves the plugin's settings as a preset of that name (asking before
        it replaces one). */
    void askToSave (te::Plugin&, Tell, const juce::File& presetsFolder = audio::presets::getDefaultFolder());
}
