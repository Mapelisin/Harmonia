#include "audio/PluginCatalog.h"
#include "audio/DrumKit.h"
#include "audio/Instruments.h"
#include "audio/Sampler.h"

namespace harmonia::audio::plugin_catalog
{
    namespace
    {
        const char* const favouritesKey = "pluginFavourites";

        const char* recentKey (Kind kind)
        {
            return kind == Kind::instrument ? "pluginRecentInstruments" : "pluginRecentEffects";
        }

        juce::PropertiesFile& settings (core::Session& session)
        {
            return session.getEngine().getPropertyStorage().getPropertiesFile();
        }

        juce::StringArray readIds (core::Session& session, const char* key)
        {
            auto ids = juce::StringArray::fromLines (settings (session).getValue (key));
            ids.removeEmptyStrings();
            return ids;
        }

        void writeIds (core::Session& session, const char* key, const juce::StringArray& ids)
        {
            auto& file = settings (session);
            file.setValue (key, ids.joinIntoString ("\n"));
            file.saveIfNeeded();
        }

        Entry builtIn (Kind kind, const juce::String& name, const juce::String& type)
        {
            Entry entry;
            entry.kind = kind;
            entry.name = name;
            entry.builtInType = type;
            entry.id = "builtin:" + type;
            return entry;
        }

        Entry scanned (Kind kind, const juce::PluginDescription& description)
        {
            Entry entry;
            entry.kind = kind;
            entry.name = description.name;
            entry.maker = description.manufacturerName;
            entry.description = description;
            entry.id = description.createIdentifierString();
            return entry;
        }

        constexpr int worstMatch = 3;

        /** How well an entry answers a search: 0 the best, worstMatch the worst; -1: not at all. */
        int match (const Entry& entry, const juce::String& text, const juce::StringArray& words)
        {
            const auto name = entry.name.toLowerCase();
            const auto all = name + " " + entry.maker.toLowerCase();
            auto inName = true;

            for (const auto& word : words)
            {
                if (! all.contains (word))
                    return -1;

                inName = inName && name.contains (word);
            }

            if (name.startsWith (text))
                return 0;

            // A word of the name begins with the first word typed ("xt" finds "Surge XT" early).
            for (const auto& part : juce::StringArray::fromTokens (name, " -_.", {}))
                if (part.startsWith (words[0]))
                    return 1;

            // The letters are in the name, or only in the maker's.
            return inName ? 2 : worstMatch;
        }
    }

    juce::String Entry::getLabel() const
    {
        if (isBuiltIn())
            return name + " (built-in)";

        return maker.isNotEmpty() ? name + "  (" + maker + ")" : name;
    }

    juce::Array<Entry> getAll (core::Session& session, Kind kind)
    {
        juce::Array<Entry> result;

        if (kind == Kind::instrument)
        {
            result.add (builtIn (kind, "4OSC", te::FourOscPlugin::xmlTypeName));
            result.add (builtIn (kind, "Drums", drum_kit::Plugin::xmlTypeName));
            result.add (builtIn (kind, "Sampler", sampler::Plugin::xmlTypeName));

            for (const auto& description : instruments::getAvailable (session))
                result.add (scanned (kind, description));
        }
        else
        {
            for (const auto& type : effects::getBuiltIn())
                result.add (builtIn (kind, type.name, type.builtInType));

            for (const auto& type : effects::getExternal (session))
                result.add (scanned (kind, type.description));
        }

        return result;
    }

    std::optional<Entry> find (core::Session& session, te::Plugin& plugin)
    {
        const auto kind = plugin.isSynth() ? Kind::instrument : Kind::effect;
        auto* external = dynamic_cast<te::ExternalPlugin*> (&plugin);

        for (const auto& entry : getAll (session, kind))
        {
            if (external != nullptr ? (! entry.isBuiltIn() && external->desc.isDuplicateOf (entry.description))
                                    : entry.builtInType == plugin.getPluginType())
                return entry;
        }

        return std::nullopt;
    }

    //==============================================================================
    bool isFavourite (core::Session& session, const Entry& entry)
    {
        return readIds (session, favouritesKey).contains (entry.id);
    }

    void setFavourite (core::Session& session, const Entry& entry, bool shouldBe)
    {
        auto ids = readIds (session, favouritesKey);

        if (ids.contains (entry.id) == shouldBe)
            return;

        if (shouldBe)
            ids.add (entry.id);
        else
            ids.removeString (entry.id);

        writeIds (session, favouritesKey, ids);
    }

    juce::Array<Entry> getFavourites (core::Session& session, Kind kind)
    {
        const auto ids = readIds (session, favouritesKey);
        juce::Array<Entry> result;

        for (const auto& entry : getAll (session, kind))
            if (ids.contains (entry.id))
                result.add (entry);

        return result;
    }

    void noteUsed (core::Session& session, const Entry& entry)
    {
        auto ids = readIds (session, recentKey (entry.kind));
        ids.removeString (entry.id);
        ids.insert (0, entry.id);

        while (ids.size() > maxRecent)
            ids.remove (ids.size() - 1);

        writeIds (session, recentKey (entry.kind), ids);
    }

    juce::Array<Entry> getRecent (core::Session& session, Kind kind)
    {
        const auto all = getAll (session, kind);
        juce::Array<Entry> result;

        for (const auto& id : readIds (session, recentKey (kind)))
            for (const auto& entry : all)
                if (entry.id == id)
                    result.add (entry);

        return result;
    }

    void clearRecent (core::Session& session, Kind kind)
    {
        writeIds (session, recentKey (kind), {});
    }

    //==============================================================================
    std::vector<Row> list (core::Session& session, Kind kind, const juce::String& search)
    {
        const auto all = getAll (session, kind);
        const auto favouriteIds = readIds (session, favouritesKey);
        const auto text = search.trim().toLowerCase();
        std::vector<Row> rows;

        const auto add = [&rows, &favouriteIds] (const Entry& entry)
        {
            rows.push_back ({ {}, entry, favouriteIds.contains (entry.id) });
        };

        if (text.isNotEmpty())
        {
            auto words = juce::StringArray::fromTokens (text, " ", {});
            words.removeEmptyStrings();

            // Favourites before the others, then by how well the name answers; as listed within that.
            for (const auto wantFavourite : { true, false })
                for (int score = 0; score <= worstMatch; ++score)
                    for (const auto& entry : all)
                        if (favouriteIds.contains (entry.id) == wantFavourite && match (entry, text, words) == score)
                            add (entry);

            return rows;
        }

        const auto favourites = getFavourites (session, kind);
        juce::Array<Entry> recent;

        for (const auto& entry : getRecent (session, kind))
            if (! favourites.contains (entry))
                recent.add (entry);

        const auto section = [&rows, &add] (const juce::String& heading, const juce::Array<Entry>& entries)
        {
            if (entries.isEmpty())
                return;

            rows.push_back ({ heading, {}, false });

            for (const auto& entry : entries)
                add (entry);
        };

        juce::Array<Entry> others;

        for (const auto& entry : all)
            if (! favourites.contains (entry) && ! recent.contains (entry))
                others.add (entry);

        section ("Favourites", favourites);
        section ("Used lately", recent);
        section (kind == Kind::instrument ? "All instruments" : "All effects", others);
        return rows;
    }

    //==============================================================================
    juce::String useInstrument (core::Session& session, te::AudioTrack& track, const Entry& entry)
    {
        juce::String error;

        if (! entry.isBuiltIn())
            error = instruments::useExternal (track, entry.description);
        else if (entry.builtInType == drum_kit::Plugin::xmlTypeName)
            error = instruments::makeDrumTrack (track);
        else if (entry.builtInType == sampler::Plugin::xmlTypeName)
            error = instruments::useSampler (track);
        else
            error = instruments::useBuiltInSynth (track);

        if (error.isEmpty())
            noteUsed (session, entry);

        return error;
    }

    effects::EffectType toEffectType (const Entry& entry)
    {
        return { entry.name, entry.builtInType, entry.description };
    }
}
