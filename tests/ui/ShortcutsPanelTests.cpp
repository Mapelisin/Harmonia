#include <doctest.h>

#include "core/TestProject.h"
#include "ui/MainComponent.h"
#include "ui/ShortcutsPanel.h"

using namespace harmonia;
using namespace harmonia::ui;

TEST_SUITE ("shortcuts list")
{
    TEST_CASE ("every section has rows, each with keys and words")
    {
        int total = 0;

        for (const auto& section : ShortcutsPanel::getSections())
        {
            CHECK (section.title.isNotEmpty());
            CHECK_FALSE (section.rows.empty());

            juce::StringArray keys;

            for (const auto& row : section.rows)
            {
                CHECK (row.keys.isNotEmpty());
                CHECK (row.what.isNotEmpty());
                CHECK_MESSAGE (! keys.contains (row.keys), "twice in " << section.title << ": " << row.keys);
                keys.add (row.keys);
                ++total;
            }
        }

        ShortcutsPanel panel;
        CHECK (panel.getNumShownRows() == total);
    }

    TEST_CASE ("F1 shows the list over the window; the search finds rows; Esc and F1 hide it")
    {
        auto project = core::Project::createEmpty (test::session());
        MainComponent main (test::session(), *project);
        main.setBounds (0, 0, 1280, 800);

        CHECK_FALSE (main.areShortcutsShown());
        CHECK (main.keyPressed (juce::KeyPress (juce::KeyPress::F1Key)));
        CHECK (main.areShortcutsShown());

        auto& panel = main.getShortcutsPanel();
        CHECK (panel.getBounds() == main.getLocalBounds());
        const auto all = panel.getNumShownRows();

        panel.setFilter ("ctrl+b");
        CHECK (panel.getNumShownRows() >= 2);       // the arrangement's and the piano roll's
        CHECK (panel.getNumShownRows() < all);

        panel.setFilter ("LOOP");
        CHECK (panel.getNumShownRows() > 0);

        panel.setFilter ("no such thing here");
        CHECK (panel.getNumShownRows() == 0);

        panel.setFilter ("marker");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        const auto image = main.createComponentSnapshot (main.getLocalBounds());
        CHECK (image.isValid());

        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            panel.setFilter ({});
            const auto full = main.createComponentSnapshot (main.getLocalBounds());
            juce::FileOutputStream out (juce::File (dir).getChildFile ("main-shortcuts.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (full, out);
        }

        // Esc hides it; F1 shows it again (with the search cleared) and hides it.
        CHECK (panel.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
        CHECK_FALSE (main.areShortcutsShown());
        CHECK (main.keyPressed (juce::KeyPress (juce::KeyPress::F1Key)));
        CHECK (panel.getFilter().isEmpty());
        CHECK (panel.getNumShownRows() == all);
        CHECK (main.keyPressed (juce::KeyPress (juce::KeyPress::F1Key)));
        CHECK_FALSE (main.areShortcutsShown());
    }
}
