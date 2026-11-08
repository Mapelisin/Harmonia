#pragma once

#include "core/Session.h"

namespace harmonia::audio::instruments
{
    /** The track's instrument (the first synth in its plugin chain), or nullptr. */
    te::Plugin* get (te::AudioTrack&);

    juce::String getName (te::AudioTrack&);

    /** Replaces the track's instrument with Tracktion's built-in 4OSC synth.
        Returns an empty string on success, otherwise the reason. */
    juce::String useBuiltInSynth (te::AudioTrack&);

    /** Replaces the track's instrument with Harmonia's drum machine (audio::drum_kit). */
    juce::String useDrumKit (te::AudioTrack&);

    /** Whether the track plays Harmonia's drum machine. */
    bool playsDrumKit (te::AudioTrack&);

    /** An instrument track with no notes yet that plays the built-in synth (or nothing), as a new
        track does: free to become what the first part put on it needs. */
    bool isUntouched (te::AudioTrack&);

    /** Makes the track a drum track: the drum machine plays it, its keys show drum names, and a
        track still called "Track 2" is called "Drums" (and so are its clips of that name). Into
        the current undo step. Returns an empty string on success, otherwise the reason. */
    juce::String makeDrumTrack (te::AudioTrack&);

    /** Replaces the track's instrument with an external plugin (VST3/AU).
        The plugin is loaded first; the old instrument is only removed if loading worked,
        so a broken plugin never leaves the track silent. Safe while playing. */
    juce::String useExternal (te::AudioTrack&, const juce::PluginDescription&);

    /** Instruments in the scanned plugin list, sorted by name. */
    juce::Array<juce::PluginDescription> getAvailable (core::Session&);

    /** Opens the instrument's own editor window (if it has one). */
    void showEditor (te::AudioTrack&);
}
