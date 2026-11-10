#pragma once

#include "audio/Effects.h"

/** What a track can be given: the instruments and effects that come with Harmonia and the scanned
    plugins, as the plugin chooser offers them, with a search by name and maker, favourites, and
    the ones used lately.

    Favourites and the recently used are the user's, not a song's: they are kept in the program's
    settings. */
namespace harmonia::audio::plugin_catalog
{
    enum class Kind { instrument, effect };

    /** An instrument or an effect to choose. */
    struct Entry
    {
        Kind kind = Kind::instrument;
        juce::String name;                      // "Surge XT", "Sampler"
        juce::String maker;                     // "Surge Synth Team"; one of Harmonia's own: empty
        juce::String id;                        // the same from run to run: what favourites keep
        juce::String builtInType;               // the engine's type name; a scanned plugin: empty
        juce::PluginDescription description;    // a scanned plugin

        bool isBuiltIn() const noexcept         { return builtInType.isNotEmpty(); }

        /** As a list says it: "Sampler (built-in)", "Surge XT  (Surge Synth Team)". */
        juce::String getLabel() const;

        bool operator== (const Entry& other) const noexcept     { return id == other.id; }
    };

    /** Harmonia's own first (the synth, the drums, the sampler; the nine effects), then the
        scanned plugins by name. */
    juce::Array<Entry> getAll (core::Session&, Kind);

    /** The entry of the plugin, if the catalog has it (a plugin since unscanned: nullopt). */
    std::optional<Entry> find (core::Session&, te::Plugin&);

    //==============================================================================
    bool isFavourite (core::Session&, const Entry&);
    void setFavourite (core::Session&, const Entry&, bool shouldBe);

    /** The favourites the catalog has, in its order. */
    juce::Array<Entry> getFavourites (core::Session&, Kind);

    /** Puts the entry at the front of the recently used of its kind. */
    void noteUsed (core::Session&, const Entry&);

    /** The recently used the catalog still has, the latest first (at most maxRecent). */
    juce::Array<Entry> getRecent (core::Session&, Kind);
    void clearRecent (core::Session&, Kind);

    inline constexpr int maxRecent = 8;

    //==============================================================================
    /** A row of the chooser: a heading ("Favourites"), or an entry under it. */
    struct Row
    {
        juce::String heading;       // not empty: a heading, and `entry` means nothing
        Entry entry;
        bool favourite = false;

        bool isHeading() const noexcept         { return heading.isNotEmpty(); }
    };

    /** What the chooser shows. Without a search text: the favourites, the recently used that
        aren't favourites, then all the rest, each under its heading. With one: the entries whose
        name or maker has every word of it (any case), best first: favourites, names that begin
        with the text, names with a word that begins with it, names that have it, then those
        found by their maker; no headings. */
    std::vector<Row> list (core::Session&, Kind, const juce::String& search);

    //==============================================================================
    /** Gives the track this instrument (as audio::instruments does: a plugin is loaded first, and
        the track keeps what it has if that failed). Into the current undo step. Notes the use. */
    juce::String useInstrument (core::Session&, te::AudioTrack&, const Entry&);

    /** The entry as audio::effects adds it. */
    effects::EffectType toEffectType (const Entry&);
}
