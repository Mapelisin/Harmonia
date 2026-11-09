#include "ui/MainMenu.h"
#include "ui/MainComponent.h"

namespace harmonia::ui
{
    namespace
    {
        using Safe = juce::Component::SafePointer<MainComponent>;

        /** An action on the main component, done only if it still exists when the item is chosen. */
        template <typename Function>
        std::function<void()> on (MainComponent& main, Function&& function)
        {
            return [safe = Safe (&main), function = std::forward<Function> (function)]
            {
                if (safe != nullptr)
                    function (*safe);
            };
        }
    }

    MainMenu::MainMenu (MainComponent& m)
        : main (m)
    {
    }

    juce::StringArray MainMenu::getMenuBarNames()
    {
        return { "File", "Edit", "View", "Track", "Transport", "Help" };
    }

    juce::PopupMenu MainMenu::getMenuForIndex (int index, const juce::String&)
    {
        switch (index)
        {
            case 0:  return main.createFileMenu();
            case 1:  return createEditMenu();
            case 2:  return createViewMenu();
            case 3:  return createTrackMenu();
            case 4:  return createTransportMenu();
            case 5:  return createHelpMenu();
            default: return {};
        }
    }

    juce::PopupMenu::Item MainMenu::makeItem (const juce::String& text, const juce::String& keys, bool enabled,
                                              std::function<void()> action, bool ticked)
    {
        juce::PopupMenu::Item item (text);
        item.shortcutKeyDescription = keys;
        item.isEnabled = enabled;
        item.isTicked = ticked;
        item.action = std::move (action);
        return item;
    }

    //==============================================================================
    juce::PopupMenu MainMenu::createEditMenu()
    {
        auto& arrangementModel = main.getArrangementModel();
        auto& project = arrangementModel.getProject();
        auto& notes = main.getPianoRollModel();

        const auto stepName = [] (const juce::String& command, const juce::String& step)
        {
            return step.isEmpty() ? command : command + " \"" + step + "\"";
        };

        juce::PopupMenu menu;
        menu.addItem (makeItem (stepName ("Undo", project.getUndoDescription()), "Ctrl+Z", project.canUndo(),
                                on (main, [] (MainComponent& m) { m.undo(); })));
        menu.addItem (makeItem (stepName ("Redo", project.getRedoDescription()), "Ctrl+Y", project.canRedo(),
                                on (main, [] (MainComponent& m) { m.redo(); })));
        menu.addSeparator();

        // The same commands as the keys: on the clips while the arrangement has the keyboard.
        if (main.isArrangementTheEditTarget())
        {
            const auto selected = ! arrangementModel.getSelectedClips().isEmpty();
            menu.addSectionHeader ("Clips in the arrangement");
            menu.addItem (makeItem ("Cut", "Ctrl+X", selected, on (main, [] (MainComponent& m) { m.getArrangement().cutSelected(); })));
            menu.addItem (makeItem ("Copy", "Ctrl+C", selected, on (main, [] (MainComponent& m) { m.getArrangement().copySelected(); })));
            menu.addItem (makeItem ("Paste at the playhead", "Ctrl+V", arrangement::ArrangementModel::canPaste(),
                                    on (main, [] (MainComponent& m) { m.getArrangement().pasteAtPlayhead(); })));
            menu.addItem (makeItem ("Duplicate as linked copies", "Ctrl+B", selected,
                                    on (main, [] (MainComponent& m) { m.getArrangementModel().duplicateSelected (true); })));
            menu.addItem (makeItem ("Duplicate as independent copies", "Ctrl+Shift+B", selected,
                                    on (main, [] (MainComponent& m) { m.getArrangementModel().duplicateSelected (false); })));
            menu.addItem (makeItem ("Split at the playhead", "Ctrl+E", true,
                                    on (main, [] (MainComponent& m) { m.getArrangement().splitAtPlayhead(); })));
            menu.addItem (makeItem ("Delete", "Del", selected, on (main, [] (MainComponent& m) { m.getArrangementModel().deleteSelected(); })));
            menu.addSeparator();
            menu.addItem (makeItem ("Select all", "Ctrl+A", true, on (main, [] (MainComponent& m) { m.getArrangementModel().selectAll(); })));
            menu.addItem (makeItem ("Deselect", "Ctrl+D", selected, on (main, [] (MainComponent& m) { m.getArrangementModel().deselectAll(); })));
        }
        else
        {
            const auto hasClip = notes.getClip() != nullptr;
            const auto selected = hasClip && notes.hasSelection();
            menu.addSectionHeader ("Notes in the piano roll");
            menu.addItem (makeItem ("Cut", "Ctrl+X", selected, on (main, [] (MainComponent& m) { m.getPianoRollModel().cut(); })));
            menu.addItem (makeItem ("Copy", "Ctrl+C", selected, on (main, [] (MainComponent& m) { m.getPianoRollModel().copy(); })));
            menu.addItem (makeItem ("Paste", "Ctrl+V", hasClip && notes.canPaste(), on (main, [] (MainComponent& m)
            {
                auto& model = m.getPianoRollModel();
                model.paste (model.getPastePosition (m.getPianoRoll().getView().leftBeat));
            })));
            menu.addItem (makeItem ("Duplicate", "Ctrl+B", selected, on (main, [] (MainComponent& m) { m.getPianoRollModel().duplicate(); })));
            menu.addItem (makeItem ("Delete", "Del", selected, on (main, [] (MainComponent& m) { m.getPianoRollModel().deleteSelectedOrAll(); })));
            menu.addItem (makeItem ("Quantize", "Ctrl+Q", hasClip, on (main, [] (MainComponent& m) { m.getPianoRollModel().quantize (false); })));
            menu.addSeparator();
            menu.addItem (makeItem ("Select all", "Ctrl+A", hasClip, on (main, [] (MainComponent& m) { m.getPianoRollModel().selectAll(); })));
            menu.addItem (makeItem ("Deselect", "Ctrl+D", selected, on (main, [] (MainComponent& m) { m.getPianoRollModel().deselectAll(); })));
        }

        return menu;
    }

    //==============================================================================
    juce::PopupMenu MainMenu::createViewMenu()
    {
        using Panel = MainComponent::LowerPanel;
        const auto shown = main.getLowerPanel();
        juce::PopupMenu menu;

        for (const auto& [panel, name, key] : { std::tuple<Panel, const char*, const char*> { Panel::sounds, "Sounds (samples and loops)", "F6" },
                                                { Panel::pianoRoll, "Piano roll", "F7" },
                                                { Panel::automation, "Automation", "F8" },
                                                { Panel::mixer, "Mixer", "F9" } })
        {
            // The editor is the piano roll, or the audio clip editor on an audio track.
            const auto title = panel == Panel::pianoRoll ? main.getEditorName() : juce::String (name);
            menu.addItem (makeItem (title, key, true, on (main, [p = panel] (MainComponent& m) { m.showLowerPanel (p); }), panel == shown));
        }

        menu.addSeparator();
        menu.addItem (makeItem ("Show the whole song", "Z", true, on (main, [] (MainComponent& m) { m.getArrangement().zoomToFit(); })));
        menu.addItem (makeItem ("Show the whole clip in the piano roll", "Z", main.getPianoRollModel().getClip() != nullptr,
                                on (main, [] (MainComponent& m) { m.getPianoRoll().zoomToFit(); })));
        menu.addItem (makeItem ("Ghost notes of the other clips", "Alt+V", true, on (main, [] (MainComponent& m)
        {
            auto& model = m.getPianoRollModel();
            model.setGhostsVisible (! model.areGhostsVisible());
        }), main.getPianoRollModel().areGhostsVisible()));
        menu.addSeparator();
        menu.addItem (makeItem ("Keyboard shortcuts", "F1", true, on (main, [] (MainComponent& m) { m.showShortcuts (true); })));
        return menu;
    }

    //==============================================================================
    juce::PopupMenu MainMenu::createTrackMenu()
    {
        auto& model = main.getArrangementModel();
        juce::PopupMenu menu;
        menu.addSectionHeader ("Add");

        const auto addMenu = arrangement::makeAddTrackMenu (model);

        for (juce::PopupMenu::MenuItemIterator it (addMenu); it.next();)
            menu.addItem (it.getItem());

        if (auto* track = model.getCurrentTrack())
        {
            menu.addSectionHeader ("\"" + track->getName() + "\"");

            // The same menu as a right-click on the track's header, without its own "Add".
            const auto trackMenu = main.getArrangement().getHeaders().createTrackMenu (*track);

            for (juce::PopupMenu::MenuItemIterator it (trackMenu); it.next();)
                if (it.getItem().text != "Add track")
                    menu.addItem (it.getItem());
        }

        return menu;
    }

    //==============================================================================
    juce::PopupMenu MainMenu::createTransportMenu()
    {
        auto& transport = main.getTransport();
        auto& noteInput = main.getNoteInput();
        const auto playing = transport.isPlayRequested() || transport.isPlaying();
        const auto song = main.getArrangementModel().getPlayMode() == arrangement::PlayMode::song;

        juce::PopupMenu menu;
        menu.addItem (makeItem (playing ? "Stop" : "Play", "Space", true, on (main, [] (MainComponent& m) { m.getTransport().togglePlayStop(); })));
        menu.addItem (makeItem ("Back to the start", "Home", true, on (main, [] (MainComponent& m) { m.goToStart(); })));
        menu.addItem (makeItem ("Record", "R", true, on (main, [] (MainComponent& m)
        {
            auto& input = m.getNoteInput();
            input.setRecordArmed (! input.isRecordArmed());
        }), noteInput.isRecordArmed()));
        menu.addSeparator();
        menu.addItem (makeItem ("Play the song", "L", true, on (main, [] (MainComponent& m) { m.setPlayMode (arrangement::PlayMode::song); }), song));
        menu.addItem (makeItem ("Play the open clip in a loop", "L", true,
                                on (main, [] (MainComponent& m) { m.setPlayMode (arrangement::PlayMode::clip); }), ! song));
        menu.addItem (makeItem ("Previous marker", "Ctrl+Left", true, on (main, [] (MainComponent& m) { m.jumpToMarker (false); })));
        menu.addItem (makeItem ("Next marker", "Ctrl+Right", true, on (main, [] (MainComponent& m) { m.jumpToMarker (true); })));
        menu.addSeparator();
        menu.addItem (makeItem ("Metronome", "Ctrl+M", true, on (main, [] (MainComponent& m) { m.toggleMetronome(); }),
                                main.getArrangementModel().getEdit().clickTrackEnabled.get()));

        using CountIn = audio::Transport::CountIn;
        juce::PopupMenu countInMenu;

        for (const auto& [countIn, name] : { std::pair<CountIn, const char*> { CountIn::off, "Off" },
                                             { CountIn::oneBar, "1 bar" },
                                             { CountIn::twoBars, "2 bars" } })
        {
            countInMenu.addItem (makeItem (name, {}, true, on (main, [c = countIn] (MainComponent& m)
            {
                m.getTransport().setCountIn (c);
                m.getTransportBar().refresh();
            }), transport.getCountIn() == countIn));
        }

        menu.addSubMenu ("Count-in before recording audio", countInMenu);
        menu.addItem (makeItem ("Play notes on the computer keyboard", "Ctrl+T", true, on (main, [] (MainComponent& m)
        {
            auto& keyboard = m.getNoteInput().getComputerKeyboard();
            keyboard.setEnabled (! keyboard.isEnabled());
        }), noteInput.getComputerKeyboard().isEnabled()));
        return menu;
    }

    //==============================================================================
    juce::PopupMenu MainMenu::createHelpMenu()
    {
        juce::PopupMenu menu;
        menu.addItem (makeItem ("Keyboard shortcuts and mouse tricks", "F1", true, on (main, [] (MainComponent& m) { m.showShortcuts (true); })));
        menu.addItem (makeItem ("User guide (on the web)", {}, true, []
        {
            juce::URL ("https://github.com/Mapelisin/Harmonia/tree/main/docs/user").launchInDefaultBrowser();
        }));
        menu.addSeparator();
        menu.addItem (makeItem ("About Harmonia", {}, true, on (main, [] (MainComponent& m) { m.showAbout(); })));
        return menu;
    }
}
