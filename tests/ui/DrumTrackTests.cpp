#include <doctest.h>

#include "TestSession.h"
#include "audio/Instruments.h"
#include "core/Clips.h"
#include "core/Drums.h"
#include "core/NoteEditor.h"
#include "core/Project.h"
#include "core/Tracks.h"
#include "ui/InstrumentPanel.h"
#include "ui/arrangement/TrackHeaders.h"

using namespace harmonia;
using namespace harmonia::ui;
namespace instruments = audio::instruments;

namespace
{
    bool runItem (const juce::PopupMenu& menu, const juce::String& text)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
        {
            const auto& item = it.getItem();

            if (item.text == text && item.isEnabled && item.action != nullptr)
            {
                item.action();
                return true;
            }
        }

        return false;
    }

    bool playsSynth (te::AudioTrack& track)
    {
        return dynamic_cast<te::FourOscPlugin*> (instruments::get (track)) != nullptr;
    }

    /** The chooser's entry for one of Harmonia's own instruments. */
    audio::plugin_catalog::Entry instrumentCalled (const juce::String& name)
    {
        for (const auto& entry : audio::plugin_catalog::getAll (test::session(), audio::plugin_catalog::Kind::instrument))
            if (entry.name == name)
                return entry;

        FAIL ("no instrument called " << name.toStdString());
        return {};
    }

    void dispatch()
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    }
}

TEST_SUITE ("drum tracks")
{
    TEST_CASE ("what may become a drum track: no notes yet, the built-in synth or nothing")
    {
        auto project = core::Project::createEmpty (test::session());
        auto* track = project->getTracks().getFirst();
        CHECK (instruments::isUntouched (*track));                     // no instrument at all

        REQUIRE (instruments::useBuiltInSynth (*track).isEmpty());
        auto* clip = project->addMidiClip (*track, 0.0, 16.0, "Clip");
        REQUIRE (clip != nullptr);
        CHECK (instruments::isUntouched (*track));                     // an empty clip doesn't count

        core::Note note;
        core::NoteEditor (*clip).add (note);
        CHECK_FALSE (instruments::isUntouched (*track));               // a note does

        auto* audioTrack = project->addTrack ("Vocals");
        core::tracks::setKind (*audioTrack, core::tracks::Kind::audio);
        CHECK_FALSE (instruments::isUntouched (*audioTrack));

        auto* drums = project->addTrack ("Beat");
        REQUIRE (instruments::useDrumKit (*drums).isEmpty());
        CHECK_FALSE (instruments::isUntouched (*drums));               // already chosen
    }

    TEST_CASE ("making a drum track: the drum kit, drum names, and \"Drums\" for a \"Track 2\" and its clips")
    {
        auto project = core::Project::createEmpty (test::session());
        auto* track = project->addTrack ("Track 2");
        REQUIRE (project->addMidiClip (*track, 0.0, 16.0, "Track 2") != nullptr);
        REQUIRE (project->addMidiClip (*track, 16.0, 16.0, "Fill") != nullptr);
        project->clearUndoHistory();

        project->beginGesture ("Make a drum track");
        REQUIRE (instruments::makeDrumTrack (*track).isEmpty());
        CHECK (instruments::playsDrumKit (*track));
        CHECK (core::drums::showsNames (*track));
        CHECK (track->getName() == "Drums");
        CHECK (track->getClips()[0]->getName() == "Drums");
        CHECK (track->getClips()[1]->getName() == "Fill");

        project->undo();
        CHECK (instruments::get (*track) == nullptr);
        CHECK (track->getName() == "Track 2");
        CHECK (track->getClips()[0]->getName() == "Track 2");
        CHECK_FALSE (core::drums::showsNames (*track));

        // A name of its own stays.
        track->setName ("Percussion");
        REQUIRE (instruments::makeDrumTrack (*track).isEmpty());
        CHECK (track->getName() == "Percussion");
    }

    TEST_CASE ("+ Track > Drum track: the built-in drums and an empty clip, one undo step")
    {
        auto project = core::Project::createEmpty (test::session());
        ui::arrangement::ArrangementModel model (*project);
        project->clearUndoHistory();
        const auto before = model.getNumTracks();

        CHECK (runItem (ui::arrangement::makeAddTrackMenu (model), "Drum track (built-in drums)"));
        REQUIRE (model.getNumTracks() == before + 1);

        auto* track = model.getTrack (before);
        REQUIRE (track != nullptr);
        CHECK (track->getName() == "Drums");
        CHECK (instruments::playsDrumKit (*track));
        CHECK (core::drums::showsNames (*track));
        CHECK (model.getCurrentTrack() == track);

        const auto onTrack = core::clips::getClips (*track);
        REQUIRE (onTrack.size() == 1);
        CHECK (onTrack[0]->getName() == "Drums");
        CHECK (model.getOpenClip() == onTrack[0]);

        project->undo();
        dispatch();
        CHECK (model.getNumTracks() == before);
    }

    TEST_CASE ("the instrument panel offers the drums, and shows what the track plays even after an undo")
    {
        auto project = core::Project::createEmpty (test::session());
        auto* track = project->getTracks().getFirst();
        REQUIRE (instruments::useBuiltInSynth (*track).isEmpty());
        project->clearUndoHistory();

        InstrumentPanel panel (test::session(), *track);
        CHECK (panel.getInstrumentText() == "4OSC (built-in)");

        // As a choice in the chooser does.
        panel.choose (instrumentCalled ("Drums"));
        CHECK (instruments::playsDrumKit (*track));
        CHECK (core::drums::showsNames (*track));
        CHECK (panel.getInstrumentText() == "Drums (built-in)");
        CHECK (panel.getMessage() == "Instrument: Drums (built-in)");

        project->undo();
        dispatch();
        CHECK (playsSynth (*track));
        CHECK (panel.getInstrumentText() == "4OSC (built-in)");

        project->redo();
        dispatch();
        CHECK (panel.getInstrumentText() == "Drums (built-in)");
    }
}
