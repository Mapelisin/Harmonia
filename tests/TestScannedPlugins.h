#pragma once

#include <doctest.h>

#include "TestSession.h"
#include "audio/PluginCatalog.h"

namespace harmonia::test
{
    /** Plugins "found by a scan" while this lives: three instruments (Surge XT, Vital, Analog
        Lab) and three effects (Pro-Q 3, Valhalla Room, Surge XT Effects). They can't be loaded:
        they aren't there. The favourites and the recently used are as on a first run, before and
        after. */
    struct ScannedPlugins
    {
        ScannedPlugins()
        {
            for (const auto& d : all())
                list().addType (d);

            forget();
        }

        ~ScannedPlugins()
        {
            forget();

            for (const auto& d : all())
                list().removeType (d);
        }

        static void forget()
        {
            namespace catalog = audio::plugin_catalog;

            for (const auto kind : { catalog::Kind::instrument, catalog::Kind::effect })
            {
                for (const auto& entry : catalog::getAll (session(), kind))
                    catalog::setFavourite (session(), entry, false);

                catalog::clearRecent (session(), kind);
            }
        }

        static juce::PluginDescription describe (const juce::String& name, const juce::String& maker, bool instrument, int uid)
        {
            juce::PluginDescription d;
            d.name = name;
            d.descriptiveName = name;
            d.manufacturerName = maker;
            d.pluginFormatName = "VST3";
            d.fileOrIdentifier = "C:/Nowhere/" + name + ".vst3";
            d.uniqueId = d.deprecatedUid = uid;
            d.isInstrument = instrument;
            d.numInputChannels = instrument ? 0 : 2;
            d.numOutputChannels = 2;
            return d;
        }

        static std::vector<juce::PluginDescription> all()
        {
            return { describe ("Surge XT", "Surge Synth Team", true, 101),
                     describe ("Vital", "Matt Tytel", true, 102),
                     describe ("Analog Lab", "Arturia", true, 103),
                     describe ("Pro-Q 3", "FabFilter", false, 201),
                     describe ("Valhalla Room", "Valhalla DSP", false, 202),
                     describe ("Surge XT Effects", "Surge Synth Team", false, 203) };
        }

        static juce::KnownPluginList& list()    { return session().getEngine().getPluginManager().knownPluginList; }

        /** The catalog's entry of that name. */
        static audio::plugin_catalog::Entry entry (audio::plugin_catalog::Kind kind, const juce::String& name)
        {
            for (const auto& e : audio::plugin_catalog::getAll (session(), kind))
                if (e.name == name)
                    return e;

            FAIL ("no entry called " << name.toStdString());
            return {};
        }
    };
}
