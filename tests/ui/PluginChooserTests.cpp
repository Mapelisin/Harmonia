#include <doctest.h>

#include "TestScannedPlugins.h"
#include "audio/Effects.h"
#include "audio/Instruments.h"
#include "core/Project.h"
#include "ui/InstrumentPanel.h"
#include "ui/MainComponent.h"
#include "ui/PluginChooser.h"

using namespace harmonia;
namespace catalog = audio::plugin_catalog;
using catalog::Kind;
using Scanned = test::ScannedPlugins;

namespace
{
    void dispatch (int ms = 60)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    /** The chooser's rows as text: headings in brackets, a star before favourites, `>` before
        the row Enter would choose. */
    juce::String show (const ui::PluginChooser& chooser)
    {
        juce::StringArray result;
        const auto& rows = chooser.getRows();

        for (int i = 0; i < (int) rows.size(); ++i)
        {
            const auto& row = rows[(size_t) i];
            result.add (row.isHeading() ? "[" + row.heading + "]"
                                        : juce::String (i == chooser.getSelected() ? ">" : "") + (row.favourite ? "*" : "") + row.entry.name);
        }

        return result.joinIntoString (", ");
    }

    int rowOf (const ui::PluginChooser& chooser, const juce::String& name)
    {
        const auto& rows = chooser.getRows();

        for (int i = 0; i < (int) rows.size(); ++i)
            if (! rows[(size_t) i].isHeading() && rows[(size_t) i].entry.name == name)
                return i;

        return -1;
    }

    std::optional<juce::PopupMenu::Item> find (const juce::PopupMenu& menu, const juce::String& text)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
            if (it.getItem().text.startsWith (text))
                return it.getItem();

        return std::nullopt;
    }

    /** The texts of a menu's own items (not of its submenus), headings in brackets. */
    juce::String topItems (const juce::PopupMenu& menu)
    {
        juce::StringArray result;

        for (juce::PopupMenu::MenuItemIterator it (menu, false); it.next();)
        {
            const auto& item = it.getItem();

            if (! item.isSeparator)
                result.add (item.isSectionHeader ? "[" + item.text + "]" : item.text);
        }

        return result.joinIntoString (", ");
    }
}

TEST_SUITE ("ui: the plugin chooser")
{
    TEST_CASE ("two letters and Enter: typing narrows the list, the best match is chosen")
    {
        Scanned scanned;
        std::vector<juce::String> chosen;
        ui::PluginChooser chooser (test::session(), Kind::instrument, [&chosen] (const catalog::Entry& entry) { chosen.push_back (entry.name); });
        chooser.setSize (380, 440);

        // Everything at first, the first entry ready for Enter.
        CHECK (show (chooser) == "[All instruments], >4OSC, Drums, Sampler, Analog Lab, Surge XT, Vital");
        CHECK (chooser.getEmptyText().isEmpty());

        chooser.setSearch ("vi");
        CHECK (show (chooser) == ">Vital");
        chooser.choose (chooser.getSelected());
        REQUIRE (chosen.size() == 1);
        CHECK (chosen[0] == "Vital");

        // Up and Down go over the entries, not the headings, and stop at the ends.
        chooser.setSearch ({});
        chooser.moveSelection (1);
        chooser.moveSelection (1);
        CHECK (show (chooser) == "[All instruments], 4OSC, Drums, >Sampler, Analog Lab, Surge XT, Vital");
        chooser.moveSelection (-1);
        chooser.moveSelection (-1);
        chooser.moveSelection (-1);
        CHECK (show (chooser) == "[All instruments], >4OSC, Drums, Sampler, Analog Lab, Surge XT, Vital");

        for (int i = 0; i < 10; ++i)
            chooser.moveSelection (1);

        CHECK (chooser.getSelected() == rowOf (chooser, "Vital"));

        // A heading isn't a choice; nor is nothing.
        chooser.choose (0);
        chooser.choose (99);
        CHECK (chosen.size() == 1);

        // Nothing found: said so, and Enter does nothing.
        chooser.setSearch ("qqq");
        CHECK (chooser.getRows().empty());
        CHECK (chooser.getSelected() == -1);
        CHECK (chooser.getEmptyText().contains ("qqq"));
        chooser.choose (chooser.getSelected());
        CHECK (chosen.size() == 1);

        // It paints (set HARMONIA_SNAPSHOT_DIR to look at it).
        chooser.setSearch ("s");
        const auto image = chooser.createComponentSnapshot (chooser.getLocalBounds());
        CHECK (image.isValid());

        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            chooser.setSearch ({});
            chooser.toggleFavourite (rowOf (chooser, "Vital"));
            const auto file = juce::File (dir).getChildFile ("plugin-chooser.png");
            file.deleteFile();
            juce::FileOutputStream stream (file);
            juce::PNGImageFormat().writeImageToStream (chooser.createComponentSnapshot (chooser.getLocalBounds()), stream);
        }
    }

    TEST_CASE ("the star makes a favourite: on top from then on, also the next time the chooser opens")
    {
        Scanned scanned;
        auto& session = test::session();

        {
            ui::PluginChooser chooser (session, Kind::effect, nullptr);
            chooser.toggleFavourite (rowOf (chooser, "Valhalla Room"));
            CHECK (show (chooser).startsWith ("[Favourites], >*Valhalla Room, [All effects], EQ, Compressor"));
            CHECK (catalog::isFavourite (session, Scanned::entry (Kind::effect, "Valhalla Room")));

            chooser.toggleFavourite (rowOf (chooser, "Reverb"));
            CHECK (show (chooser).startsWith ("[Favourites], >*Reverb, *Valhalla Room, [All effects], EQ, Compressor, Limiter, Delay"));

            // In a search too: the star shows, and a favourite comes first.
            chooser.setSearch ("r");
            CHECK (show (chooser).startsWith (">*Reverb, *Valhalla Room, Compressor"));
        }

        {
            // Used lately: under the favourites.
            catalog::noteUsed (session, Scanned::entry (Kind::effect, "Pro-Q 3"));
            ui::PluginChooser chooser (session, Kind::effect, nullptr);
            CHECK (show (chooser).startsWith ("[Favourites], >*Reverb, *Valhalla Room, [Used lately], Pro-Q 3, [All effects], EQ"));

            // The star again: not a favourite any more.
            chooser.toggleFavourite (rowOf (chooser, "Reverb"));
            CHECK_FALSE (catalog::isFavourite (session, Scanned::entry (Kind::effect, "Reverb")));
            CHECK (show (chooser).startsWith ("[Favourites], *Valhalla Room, [Used lately], Pro-Q 3, [All effects], EQ, Compressor, Limiter, >Reverb"));
        }

        // The instruments' chooser has its own list; the favourites of effects aren't in it.
        ui::PluginChooser instruments (session, Kind::instrument, nullptr);
        CHECK (show (instruments) == "[All instruments], >4OSC, Drums, Sampler, Analog Lab, Surge XT, Vital");
    }

    TEST_CASE ("the instrument's button says what the track plays; a choice changes it in one undo step and is offered first after")
    {
        Scanned scanned;
        auto& session = test::session();
        auto project = core::Project::createEmpty (session);
        auto& track = *project->getTracks().getFirst();
        REQUIRE (audio::instruments::useBuiltInSynth (track).isEmpty());
        dispatch();
        project->clearUndoHistory();

        ui::InstrumentPanel panel (session, track);
        CHECK (panel.getInstrumentText() == "4OSC (built-in)");

        panel.choose (Scanned::entry (Kind::instrument, "Sampler"));
        CHECK (audio::instruments::playsSampler (track));
        CHECK (panel.getInstrumentText() == "Sampler (built-in)");
        CHECK (project->getUndoDescription() == "Change instrument");
        CHECK (catalog::getRecent (session, Kind::instrument).getFirst().name == "Sampler");

        // A plugin that can't be loaded (it isn't there): said, the track keeps its instrument.
        panel.choose (Scanned::entry (Kind::instrument, "Surge XT"));
        CHECK (audio::instruments::playsSampler (track));
        CHECK (panel.getInstrumentText() == "Sampler (built-in)");
        CHECK (panel.getMessage().isNotEmpty());
        CHECK (catalog::getRecent (session, Kind::instrument).size() == 1);

        // The try left no undo step: one Ctrl+Z takes the sampler back.
        project->undo();
        dispatch();
        CHECK (panel.getInstrumentText() == "4OSC (built-in)");
    }

    TEST_CASE ("the mixer's + menu: the search, the favourites and the effects used lately a click away")
    {
        Scanned scanned;
        auto& session = test::session();
        auto project = core::Project::createEmpty (session);
        auto main = std::make_unique<ui::MainComponent> (session, *project);
        main->setBounds (0, 0, 1280, 800);
        auto* strip = main->getMixer().getTrackStrip (0);
        REQUIRE (strip != nullptr);
        auto& chain = project->getTracks().getFirst()->pluginList;

        auto menu = strip->makeAddMenu();
        CHECK (topItems (menu) == "Find an effect... (type its name), [Built-in], EQ, Compressor, Limiter, Reverb, Delay, Chorus, Phaser, Filter, Pump, Plugins, Chains");
        CHECK (find (menu, "Pro-Q 3  (FabFilter)").has_value());       // in Plugins

        // An effect added counts as used: offered at the top the next time.
        REQUIRE (find (menu, "Delay").has_value());
        find (menu, "Delay")->action();
        REQUIRE (audio::effects::get (chain).size() == 1);
        CHECK (audio::effects::get (chain)[0]->getName().containsIgnoreCase ("Delay"));

        catalog::setFavourite (session, Scanned::entry (Kind::effect, "Compressor"), true);
        menu = strip->makeAddMenu();
        CHECK (topItems (menu).startsWith ("Find an effect... (type its name), [Favourites], Compressor (built-in), [Used lately], Delay (built-in), [Built-in], EQ"));

        // A favourite from the menu; as the chooser adds one.
        find (menu, "Compressor (built-in)")->action();
        strip->addEffect (Scanned::entry (Kind::effect, "Reverb"));
        CHECK (audio::effects::get (chain).size() == 3);
        CHECK (catalog::getRecent (session, Kind::effect).getFirst().name == "Reverb");

        // A favourite isn't listed again under "Used lately".
        CHECK (topItems (strip->makeAddMenu()).startsWith ("Find an effect... (type its name), [Favourites], Compressor (built-in), [Used lately], Reverb (built-in), Delay (built-in), [Built-in]"));

        // A plugin that can't be loaded: said, not added, not counted as used.
        strip->addEffect (Scanned::entry (Kind::effect, "Pro-Q 3"));
        CHECK (audio::effects::get (chain).size() == 3);
        CHECK (catalog::getRecent (session, Kind::effect).getFirst().name == "Reverb");

        main.reset();
    }
}
