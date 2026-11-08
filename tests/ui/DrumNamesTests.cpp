#include <doctest.h>

#include "core/Drums.h"
#include "core/TestProject.h"
#include "ui/MainComponent.h"

using namespace harmonia;
using namespace harmonia::ui;
namespace drums = core::drums;

namespace
{
    core::Note makeNote (int pitch, double start, double length = 0.25)
    {
        core::Note n;
        n.pitch = pitch;
        n.startBeat = start;
        n.lengthBeats = length;
        return n;
    }

    bool runItem (const juce::PopupMenu& menu, const juce::String& text, bool* ticked = nullptr)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
        {
            const auto& item = it.getItem();

            if (item.text == text && item.isEnabled && item.action != nullptr)
            {
                if (ticked != nullptr)
                    *ticked = item.isTicked;

                item.action();
                return true;
            }
        }

        return false;
    }
}

TEST_SUITE ("drum names")
{
    TEST_CASE ("General MIDI names; a track called Drums shows them unless told not to (with undo)")
    {
        CHECK (drums::getName (36) == "Kick");
        CHECK (drums::getName (38) == "Snare");
        CHECK (drums::getName (39) == "Clap");
        CHECK (drums::getName (42) == "Closed hat");
        CHECK (drums::getName (46) == "Open hat");
        CHECK (drums::getName (49) == "Crash");
        CHECK (drums::getName (35) == "Kick 2");
        CHECK (drums::getName (81) == "Open triangle");
        CHECK (drums::getName (34).isEmpty());
        CHECK (drums::getName (82).isEmpty());
        CHECK (drums::getName (-1).isEmpty());

        CHECK (drums::looksLikeDrums ("Drums"));
        CHECK (drums::looksLikeDrums ("my BEAT"));
        CHECK (drums::looksLikeDrums ("Percussion"));
        CHECK_FALSE (drums::looksLikeDrums ("Piano"));

        test::ProjectWithClip p;
        CHECK_FALSE (drums::showsNames (*p.track));
        p.track->setName ("Drums");
        CHECK (drums::showsNames (*p.track));

        p.project->beginGesture ("Hide drum names");
        drums::setShowsNames (*p.track, false);
        CHECK_FALSE (drums::showsNames (*p.track));
        p.project->undo();
        CHECK (drums::showsNames (*p.track));
    }

    TEST_CASE ("the piano roll: the keyboard's menu and the Scale menu turn them on; one undo step")
    {
        test::ProjectWithClip p;
        pianoroll::PianoRollModel model (*p.project);
        model.setClip (p.clip);
        audio::Transport transport (p.project->getEdit());
        pianoroll::PianoRoll pianoRoll (model, transport);
        pianoRoll.setBounds (0, 0, 1280, 600);
        pianoroll::PianoKeys& keys = pianoRoll.getKeys();

        CHECK_FALSE (model.areDrumNamesShown());
        CHECK (model.getDrumName (38).isEmpty());

        bool ticked = true;
        CHECK (runItem (keys.createMenu(), "Drum names (General MIDI)", &ticked));
        CHECK_FALSE (ticked);
        CHECK (model.areDrumNamesShown());
        CHECK (model.getDrumName (38) == "Snare");
        CHECK (model.getDrumName (20).isEmpty());

        CHECK (runItem (pianoRoll.createScaleMenu(), "Drum names on the keys (General MIDI)", &ticked));
        CHECK (ticked);
        CHECK_FALSE (model.areDrumNamesShown());

        p.project->undo();
        CHECK (model.areDrumNamesShown());
        p.project->undo();
        CHECK_FALSE (model.areDrumNamesShown());

        // Without a clip: nothing to name.
        model.setClip (nullptr);
        CHECK_FALSE (model.areDrumNamesShown());
        model.setDrumNamesShown (true);
        CHECK_FALSE (model.areDrumNamesShown());
    }

    TEST_CASE ("the main window paints a drum pattern with the drums' names")
    {
        auto project = core::Project::createEmpty (test::session());
        MainComponent main (test::session(), *project);
        main.setBounds (0, 0, 1280, 800);
        auto& model = main.getArrangementModel();
        auto& track = *project->getTracks().getFirst();
        track.setName ("Drums");

        auto* beat = project->addMidiClip (track, 0.0, 8.0, "Beat");
        std::vector<core::Note> notes;

        for (int i = 0; i < 8; ++i)
        {
            notes.push_back (makeNote (42, i * 1.0, 0.25));            // closed hat on every beat
            notes.push_back (makeNote (42, i * 1.0 + 0.5, 0.25));      // and between
        }

        for (auto b : { 0.0, 2.5, 4.0, 6.5 })
            notes.push_back (makeNote (36, b, 0.5));                    // kick

        for (auto b : { 1.0, 3.0, 5.0, 7.0 })
        {
            notes.push_back (makeNote (38, b, 0.5));                    // snare
            notes.push_back (makeNote (39, b, 0.5));                    // and clap
        }

        notes.push_back (makeNote (46, 7.5, 0.5));                      // open hat
        notes.push_back (makeNote (49, 0.0, 2.0));                      // crash
        core::NoteEditor (*beat).add (notes);

        model.openClip (beat);
        auto& pianoRoll = main.getPianoRoll();
        pianoRoll.zoomToFit();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        CHECK (main.getPianoRollModel().areDrumNamesShown());

        const auto image = main.createComponentSnapshot (main.getLocalBounds());
        CHECK (image.isValid());

        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("main-drums.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
    }
}
