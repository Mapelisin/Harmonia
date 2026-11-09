#pragma once

#include "core/Tracktion.h"

/** Effect chains kept in files: every effect of a track (or the master), in order, each with its
    settings and whether it's bypassed, to use on another track or in another song.

    A chain is a .harmoniachain file in the chains folder (Documents/Harmonia/Chains); copied
    elsewhere, it works there too. Loading is one undo step. */
namespace harmonia::audio::chains
{
    inline constexpr const char* fileExtension = ".harmoniachain";

    /** Documents/Harmonia/Chains. */
    juce::File getDefaultFolder();

    /** The chains saved, sorted by name. */
    juce::Array<juce::File> list (const juce::File& folder = getDefaultFolder());

    /** A chain's name: its file's name without the extension. */
    juce::String getName (const juce::File& chain);

    /** The file a chain of this name has (characters Windows doesn't allow in names left out). */
    juce::File getFileFor (const juce::String& name, const juce::File& folder = getDefaultFolder());

    /** Saves the chain's effects under this name, replacing a chain of the same name. Returns an
        empty string, or the reason it couldn't (no effects, no name, the file can't be written). */
    juce::String save (te::PluginList&, const juce::String& name, const juce::File& folder = getDefaultFolder());

    /** What loading did. */
    struct Loaded
    {
        int added = 0;
        juce::StringArray problems;     ///< effects left out and why (a plugin not installed, no room)
        juce::String error;             ///< nothing was loaded: why (not a chain file)
    };

    /** Adds the file's effects after those the chain has, or in place of them: one undo step. */
    Loaded load (te::PluginList&, const juce::File& chain, bool replace);
}
