#pragma once

#include "core/Tracktion.h"

/** How much of the sound's time each plugin of a song takes: which one is the heavy one. */
namespace harmonia::audio::plugin_load
{
    struct Entry
    {
        te::Plugin::Ptr plugin;
        juce::String name;      ///< "Surge XT", "EQ"
        juce::String owner;     ///< its track's name, or "Master"
        double load = 0.0;      ///< its share of the time between two audio buffers, 0..1
    };

    /** A plugin's load, 0..1. */
    using Reader = std::function<double (te::Plugin&)>;

    /** The instruments and effects of the song's tracks, return tracks, groups and of the master,
        the heaviest first (those with the same load stay in the song's order). Without `read`
        the engine's own measurement is taken: smoothed over a moment, zero for a plugin that is
        switched off or frozen. A plugin in a process of its own counts with the wait for it. */
    std::vector<Entry> measure (te::Edit&, const Reader& read = {});

    /** All of them together (0..1, more if the sound can't keep up). */
    double getTotal (const std::vector<Entry>&);

    /** "12%", "0.4%", "0%". */
    juce::String describe (double load);

    /** "Lead: Surge XT", "Master: EQ". */
    juce::String describe (const Entry&);
}
