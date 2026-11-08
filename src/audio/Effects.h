#pragma once

#include "core/Session.h"

/** Effect chains (inserts) of tracks and of the master.

    A track's plugin list is [instrument, effects..., volume/pan, level meter]: effects sit after the
    instrument and before the fader, so the track's meter shows what the effects made of the sound.
    The master list holds only effects (the master volume is a separate plugin, after them).
    Every change is one undo step. */
namespace harmonia::audio::effects
{
    /** Something that can be added to an effect chain: a built-in Tracktion effect or a scanned plugin. */
    struct EffectType
    {
        juce::String name;
        juce::String builtInType;               ///< Tracktion's type name; empty for an external plugin
        juce::PluginDescription description;    ///< the external plugin

        bool isBuiltIn() const noexcept         { return builtInType.isNotEmpty(); }
    };

    /** The effects that come with Harmonia: EQ, compressor, reverb, delay, chorus, phaser, filter. */
    juce::Array<EffectType> getBuiltIn();

    /** Scanned plugins that are effects (not instruments), sorted by name. */
    juce::Array<EffectType> getExternal (core::Session&);

    /** The effects of a chain in processing order: the list without the instrument, the volume/pan,
        the level meter, sends and a return track's input. */
    juce::Array<te::Plugin*> get (te::PluginList&);

    /** The chain a plugin is in (a track's or the master's), or nullptr. */
    te::PluginList* getChain (te::Plugin&);

    /** The chain has room for another effect (Tracktion limits plugins per track and on the master). */
    bool canAdd (te::PluginList&);

    /** Adds an effect at a position in the chain (-1: after the others). An external plugin is loaded
        first and not added if that failed. Returns the effect, or nullptr with the reason in `error`. */
    te::Plugin* add (te::PluginList&, const EffectType&, juce::String& error, int position = -1);

    /** Moves an effect to another position in its chain (clamped). Returns false if nothing moved. */
    bool move (te::Plugin&, int newPosition);

    void remove (te::Plugin&);

    /** A bypassed effect stays in the chain but lets the sound through unchanged. */
    void setBypassed (te::Plugin&, bool);
    bool isBypassed (const te::Plugin&);

    /** Short name for a mixer slot ("EQ", "Low-pass"...). */
    juce::String getDisplayName (te::Plugin&);

    /** Opens the effect's window: its own editor or, if it has none, Harmonia's knobs. */
    void showEditor (te::Plugin&);
}
