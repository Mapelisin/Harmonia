#include <doctest.h>

#include "TestSession.h"
#include "core/Project.h"
#include "ui/MainComponent.h"

using namespace harmonia;
using namespace harmonia::ui;

TEST_SUITE ("mastering helpers in the File menu")
{
    TEST_CASE ("the File menu checks the loudness and the mix")
    {
        auto project = core::Project::createEmpty (test::session());
        MainComponent main (test::session(), *project);
        main.setBounds (0, 0, 1280, 800);

        std::vector<juce::String> texts;
        const auto menu = main.createFileMenu();

        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            if (const auto& item = it.getItem(); item.text.startsWith ("Check "))
            {
                texts.push_back (item.text);
                CHECK (item.isEnabled);
                CHECK (item.action != nullptr);
            }

        CHECK (texts == std::vector<juce::String> { "Check loudness for streaming...", "Check the mix (Mix Doctor)..." });
    }
}
