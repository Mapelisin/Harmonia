#pragma once

#include "core/Tracktion.h"

/** Presets: a plugin's settings kept in a file of their own, to use again in any song.

    Each plugin has a folder under the presets folder (Documents/Harmonia/Presets/<plugin>), and a
    preset is a .harmoniapreset file in it: copied elsewhere, it works there too. Presets work for
    Harmonia's built-in synth, drums and effects and for plugins (VST3), those in a process of their
    own too: a plugin's preset holds what the plugin itself saves (as a song does).
    Loading a preset is one undo step. */
namespace harmonia::audio::presets
{
    inline constexpr const char* fileExtension = ".harmoniapreset";

    /** Documents/Harmonia/Presets. */
    juce::File getDefaultFolder();

    /** Whether the plugin's settings can be kept as presets: the built-in synth, drums and effects,
        and plugins. */
    bool canHavePresets (te::Plugin&);

    /** The name the plugin's presets go under ("Compressor", "Surge XT"). */
    juce::String getPluginName (te::Plugin&);

    /** The folder of the plugin's presets (it may not exist yet). */
    juce::File getFolderFor (te::Plugin&, const juce::File& presetsFolder = getDefaultFolder());

    /** The plugin's presets, sorted by name. Files there for another plugin of the same name are left out. */
    juce::Array<juce::File> list (te::Plugin&, const juce::File& presetsFolder = getDefaultFolder());

    /** A preset's name: its file's name without the extension. */
    juce::String getName (const juce::File& preset);

    /** The file a preset of this name has (characters Windows doesn't allow in names left out). */
    juce::File getFileFor (te::Plugin&, const juce::String& name, const juce::File& presetsFolder = getDefaultFolder());

    /** Saves the plugin's settings as a preset of this name, replacing one of the same name.
        Returns an empty string, or the reason it couldn't. */
    juce::String save (te::Plugin&, const juce::String& name, const juce::File& presetsFolder = getDefaultFolder());

    /** Puts a preset's settings into the plugin: one undo step. Returns an empty string, or the
        reason it couldn't (another plugin's preset, not a preset). */
    juce::String load (te::Plugin&, const juce::File& preset);

    /** The name of the preset last loaded into the plugin or saved from it (kept in the song);
        empty if none. */
    juce::String getLastUsed (te::Plugin&);

    /** The built-in plugins can go back to their first settings (one undo step); plugins can't. */
    bool canReset (te::Plugin&);
    void reset (te::Plugin&);

    /** The presets a plugin comes with (its programs): empty when it has fewer than two that are named. */
    juce::StringArray getOwnPresets (te::Plugin&);

    /** The plugin's own preset in use, or -1. */
    int getCurrentOwnPreset (te::Plugin&);

    /** Switches the plugin to one of its own presets: one undo step. */
    void useOwnPreset (te::Plugin&, int index);
}
