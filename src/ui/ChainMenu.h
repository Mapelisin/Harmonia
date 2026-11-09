#pragma once

#include "audio/EffectChains.h"

/** The menu of effect chains of a mixer strip: save the strip's effects as a chain, add a saved
    chain to them, or put one in their place. */
namespace harmonia::ui::chain_menu
{
    /** Tells what happened ("Chain: Vocal"), or a problem (true). */
    using Tell = std::function<void (const juce::String& text, bool isProblem)>;

    juce::PopupMenu make (te::PluginList&, Tell, const juce::File& folder = audio::chains::getDefaultFolder());

    /** Asks for a name, then saves the effects as a chain of that name (asking before it
        replaces one). */
    void askToSave (te::PluginList&, Tell, const juce::File& folder = audio::chains::getDefaultFolder());

    /** Loads a chain and tells how it went (the effects left out, if any). */
    void load (te::PluginList&, const juce::File& chain, bool replace, const Tell&);
}
