#pragma once

#include "core/Session.h"

/** Effect chains (inserts) of tracks and of the master.

    A track's plugin list is [instrument, effects..., (track delay,) volume/pan, level meter]: effects
    sit after the instrument and before the fader, so the track's meter shows what the effects made
    of the sound.
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

    /** The effects that come with Harmonia: EQ, compressor, limiter, reverb, delay, chorus, phaser, filter, pump. */
    juce::Array<EffectType> getBuiltIn();

    /** Scanned plugins that are effects (not instruments), sorted by name. */
    juce::Array<EffectType> getExternal (core::Session&);

    /** The effects of a chain in processing order: the list without the instrument, the volume/pan,
        the level meter, sends, a track's delay and a return track's input. */
    juce::Array<te::Plugin*> get (te::PluginList&);

    /** Where the fader end of a list starts (the track's delay, its volume and pan, sends, the
        meter): what makes and shapes the sound goes before it. -1 if the list has none (the
        master's). */
    int getFaderIndex (te::PluginList&);

    /** The chain a plugin is in (a track's or the master's), or nullptr. */
    te::PluginList* getChain (te::Plugin&);

    /** The chain has room for another effect (Tracktion limits plugins per track and on the master). */
    bool canAdd (te::PluginList&);

    /** Adds an effect at a position in the chain (-1: after the others). An external plugin is loaded
        first and not added if that failed. Returns the effect, or nullptr with the reason in `error`. */
    te::Plugin* add (te::PluginList&, const EffectType&, juce::String& error, int position = -1);

    /** Moves an effect to another position in its chain (clamped). Returns false if nothing moved. */
    bool move (te::Plugin&, int newPosition);

    /** Moves an effect to a position (-1: after the others) in a chain: its own, or another track's
        or the master's (with its settings, its window closed). One undo step. Returns false, with
        the reason, if it can't go there (no room, the master and an effect that listens to a track). */
    bool moveTo (te::Plugin&, te::PluginList& target, int position, juce::String& error);

    /** Puts a copy of an effect, with its settings as they are now, at a position in a chain (-1:
        after the others). Lines of automation aren't copied. One undo step. */
    te::Plugin* copyTo (te::Plugin&, te::PluginList& target, int position, juce::String& error);

    /** An effect's node with its settings as they are now, as a copy of it starts from: no ID of
        its own, no automation. */
    juce::ValueTree makeCopyState (te::Plugin&);

    /** Adds an effect made from such a node, into the current undo step. */
    te::Plugin* addFromState (te::PluginList&, const juce::ValueTree& state, juce::String& error, int position = -1);

    void remove (te::Plugin&);

    /** A bypassed effect stays in the chain but lets the sound through unchanged. */
    void setBypassed (te::Plugin&, bool);
    bool isBypassed (const te::Plugin&);

    /** Short name for a mixer slot ("EQ", "Low-pass"...). */
    juce::String getDisplayName (te::Plugin&);

    /** Whose chain a plugin is in, for the user: its track's name, or "Master". */
    juce::String getOwnerName (te::Plugin&);

    /** Every instrument and effect of the song in its order: track by track (return tracks and
        groups among them), then the master's effects. */
    juce::Array<te::Plugin*> getAllOfSong (te::Edit&);

    /** Opens the effect's window: its own editor or, if it has none, Harmonia's knobs. */
    void showEditor (te::Plugin&);
}
