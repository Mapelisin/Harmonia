#include <doctest.h>

#include "TestScannedPlugins.h"
#include "audio/DrumKit.h"
#include "audio/Instruments.h"
#include "audio/PluginCatalog.h"
#include "audio/Sampler.h"
#include "core/Project.h"

using namespace harmonia;
namespace catalog = audio::plugin_catalog;
using catalog::Kind;

namespace
{
    using Scanned = test::ScannedPlugins;

    juce::StringArray names (const juce::Array<catalog::Entry>& entries)
    {
        juce::StringArray result;

        for (const auto& entry : entries)
            result.add (entry.name);

        return result;
    }

    /** The rows as text: headings in brackets, a star before favourites. */
    juce::String show (const std::vector<catalog::Row>& rows)
    {
        juce::StringArray result;

        for (const auto& row : rows)
            result.add (row.isHeading() ? "[" + row.heading + "]" : (row.favourite ? "*" : "") + row.entry.name);

        return result.joinIntoString (", ");
    }

    catalog::Entry entryCalled (Kind kind, const juce::String& name)
    {
        return Scanned::entry (kind, name);
    }
}

TEST_SUITE ("audio: the plugin catalog")
{
    TEST_CASE ("Harmonia's own instruments and effects come first, then the scanned plugins by name")
    {
        Scanned scanned;
        auto& session = test::session();

        CHECK (names (catalog::getAll (session, Kind::instrument)).joinIntoString (", ")
                 == "4OSC, Drums, Sampler, Analog Lab, Surge XT, Vital");
        CHECK (names (catalog::getAll (session, Kind::effect)).joinIntoString (", ")
                 == "EQ, Compressor, Limiter, Reverb, Delay, Chorus, Phaser, Filter, Pump, Pro-Q 3, Surge XT Effects, Valhalla Room");

        const auto sampler = entryCalled (Kind::instrument, "Sampler");
        CHECK (sampler.isBuiltIn());
        CHECK (sampler.getLabel() == "Sampler (built-in)");
        CHECK (sampler.id == "builtin:" + juce::String (audio::sampler::Plugin::xmlTypeName));

        const auto surge = entryCalled (Kind::instrument, "Surge XT");
        CHECK_FALSE (surge.isBuiltIn());
        CHECK (surge.getLabel() == "Surge XT  (Surge Synth Team)");
        CHECK (surge.id.isNotEmpty());
        CHECK (surge.id != entryCalled (Kind::effect, "Surge XT Effects").id);

        const auto eq = catalog::toEffectType (entryCalled (Kind::effect, "EQ"));
        CHECK (eq.isBuiltIn());
        CHECK (eq.name == "EQ");
        CHECK_FALSE (catalog::toEffectType (entryCalled (Kind::effect, "Pro-Q 3")).isBuiltIn());
    }

    TEST_CASE ("the search finds by name and maker, any case, every word; the best match first")
    {
        Scanned scanned;
        auto& session = test::session();

        // Without a search: everything, under one heading.
        CHECK (show (catalog::list (session, Kind::instrument, {}))
                 == "[All instruments], 4OSC, Drums, Sampler, Analog Lab, Surge XT, Vital");

        CHECK (show (catalog::list (session, Kind::instrument, "su")) == "Surge XT");
        CHECK (show (catalog::list (session, Kind::instrument, "  VIT ")) == "Vital");
        CHECK (show (catalog::list (session, Kind::instrument, "arturia")) == "Analog Lab");       // by its maker
        CHECK (show (catalog::list (session, Kind::instrument, "surge team")) == "Surge XT");      // every word, anywhere
        CHECK (show (catalog::list (session, Kind::instrument, "surge piano")).isEmpty());
        CHECK (show (catalog::list (session, Kind::instrument, "zzz")).isEmpty());

        // A name that begins with the text, then a word of a name that does, then the rest.
        CHECK (show (catalog::list (session, Kind::instrument, "a")) == "Analog Lab, Sampler, Vital, Surge XT");     // the last by its maker's name
        CHECK (show (catalog::list (session, Kind::effect, "r")) == "Reverb, Valhalla Room, Compressor, Limiter, Chorus, Phaser, Filter, Pro-Q 3, Surge XT Effects");
        CHECK (show (catalog::list (session, Kind::effect, "xt")) == "Surge XT Effects");
        CHECK (show (catalog::list (session, Kind::effect, "comp")) == "Compressor");
    }

    TEST_CASE ("favourites are kept on top and remembered; the recently used follow")
    {
        Scanned scanned;
        auto& session = test::session();
        const auto vital = entryCalled (Kind::instrument, "Vital");
        const auto drums = entryCalled (Kind::instrument, "Drums");
        const auto surge = entryCalled (Kind::instrument, "Surge XT");

        CHECK_FALSE (catalog::isFavourite (session, vital));
        catalog::setFavourite (session, vital, true);
        catalog::setFavourite (session, drums, true);
        catalog::setFavourite (session, drums, true);       // twice: still once
        CHECK (catalog::isFavourite (session, vital));
        CHECK (names (catalog::getFavourites (session, Kind::instrument)).joinIntoString (", ") == "Drums, Vital");
        CHECK (catalog::getFavourites (session, Kind::effect).isEmpty());

        CHECK (show (catalog::list (session, Kind::instrument, {}))
                 == "[Favourites], *Drums, *Vital, [All instruments], 4OSC, Sampler, Analog Lab, Surge XT");

        // In a search a favourite comes before a better match that isn't one.
        CHECK (show (catalog::list (session, Kind::instrument, "a")) == "*Vital, Analog Lab, Sampler, Surge XT");

        // Used lately: the latest first, each once; a favourite stays under Favourites.
        catalog::noteUsed (session, surge);
        catalog::noteUsed (session, entryCalled (Kind::instrument, "Sampler"));
        catalog::noteUsed (session, surge);
        catalog::noteUsed (session, vital);
        CHECK (names (catalog::getRecent (session, Kind::instrument)).joinIntoString (", ") == "Vital, Surge XT, Sampler");
        CHECK (catalog::getRecent (session, Kind::effect).isEmpty());
        CHECK (show (catalog::list (session, Kind::instrument, {}))
                 == "[Favourites], *Drums, *Vital, [Used lately], Surge XT, Sampler, [All instruments], 4OSC, Analog Lab");

        // No more than a few are remembered.
        for (const auto& entry : catalog::getAll (session, Kind::effect))
            catalog::noteUsed (session, entry);

        CHECK (catalog::getRecent (session, Kind::effect).size() == catalog::maxRecent);
        CHECK (catalog::getRecent (session, Kind::effect).getFirst().name == "Valhalla Room");

        // A plugin that is no longer found by the scan: gone from both, back when it is found again.
        Scanned::list().removeType (vital.description);
        CHECK (names (catalog::getFavourites (session, Kind::instrument)).joinIntoString (", ") == "Drums");
        CHECK (names (catalog::getRecent (session, Kind::instrument)).joinIntoString (", ") == "Surge XT, Sampler");
        Scanned::list().addType (vital.description);
        CHECK (catalog::isFavourite (session, vital));

        catalog::setFavourite (session, vital, false);
        catalog::clearRecent (session, Kind::instrument);
        CHECK (show (catalog::list (session, Kind::instrument, {}))
                 == "[Favourites], *Drums, [All instruments], 4OSC, Sampler, Analog Lab, Surge XT, Vital");
    }

    TEST_CASE ("an entry gives a track its instrument and counts as used; a plugin that can't be loaded leaves the track as it is")
    {
        Scanned scanned;
        auto& session = test::session();
        auto project = core::Project::createEmpty (session);
        auto& track = *project->getTracks().getFirst();
        REQUIRE (audio::instruments::useBuiltInSynth (track).isEmpty());

        project->beginGesture ("Change instrument");
        CHECK (catalog::useInstrument (session, track, entryCalled (Kind::instrument, "Sampler")).isEmpty());
        CHECK (audio::instruments::playsSampler (track));
        CHECK (catalog::find (session, *audio::instruments::get (track))->name == "Sampler");

        project->beginGesture ("Change instrument");
        CHECK (catalog::useInstrument (session, track, entryCalled (Kind::instrument, "Drums")).isEmpty());
        CHECK (audio::instruments::playsDrumKit (track));

        project->beginGesture ("Change instrument");
        CHECK (catalog::useInstrument (session, track, entryCalled (Kind::instrument, "4OSC")).isEmpty());
        CHECK (dynamic_cast<te::FourOscPlugin*> (audio::instruments::get (track)) != nullptr);
        CHECK (names (catalog::getRecent (session, Kind::instrument)).joinIntoString (", ") == "4OSC, Drums, Sampler");

        // It isn't really there: said, the track keeps its synth, and it doesn't count as used.
        project->beginGesture ("Change instrument");
        CHECK (catalog::useInstrument (session, track, entryCalled (Kind::instrument, "Vital")).isNotEmpty());
        CHECK (dynamic_cast<te::FourOscPlugin*> (audio::instruments::get (track)) != nullptr);
        CHECK (catalog::getRecent (session, Kind::instrument).getFirst().name == "4OSC");
    }
}
