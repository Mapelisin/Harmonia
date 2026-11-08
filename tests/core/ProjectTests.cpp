#include <doctest.h>

#include "TestProject.h"
#include "core/ClipLinks.h"

using harmonia::core::Project;
namespace te = harmonia::te;

TEST_SUITE ("core::Project")
{
    TEST_CASE ("a new project has one track, 120 BPM and the current format version")
    {
        auto project = Project::createEmpty (harmonia::test::session());

        REQUIRE (project != nullptr);
        CHECK (project->getNumTracks() == 1);
        CHECK (project->getTempoBpm() == doctest::Approx (120.0));
        CHECK (project->getFormatVersion() == Project::currentFormatVersion);
    }

    TEST_CASE ("save and load keep tracks and tempo")
    {
        auto& session = harmonia::test::session();
        juce::TemporaryFile file (".harmonia");

        {
            auto project = Project::createEmpty (session);
            project->getEdit().ensureNumberOfAudioTracks (3);
            project->getEdit().tempoSequence.getTempo (0)->setBpm (128.0);

            REQUIRE (project->save (file.getFile()).isEmpty());
        }

        juce::String error;
        auto loaded = Project::load (session, file.getFile(), error);

        REQUIRE_MESSAGE (loaded != nullptr, error.toStdString());
        CHECK (error.isEmpty());
        CHECK (loaded->getNumTracks() == 3);
        CHECK (loaded->getTempoBpm() == doctest::Approx (128.0));
        CHECK (loaded->getFormatVersion() == Project::currentFormatVersion);
    }

    TEST_CASE ("tracks and clips are added where asked and can be undone")
    {
        auto project = Project::createEmpty (harmonia::test::session());

        project->beginGesture ("Add track");
        auto* track = project->addTrack ("Bass");

        REQUIRE (track != nullptr);
        CHECK (project->getNumTracks() == 2);
        CHECK (project->getTracks().getLast() == track);
        CHECK (track->getName() == "Bass");

        project->beginGesture ("Add clip");
        auto* clip = project->addMidiClip (*track, 4.0, 8.0, "Riff");

        REQUIRE (clip != nullptr);
        const auto& tempo = project->getEdit().tempoSequence;
        CHECK (tempo.toBeats (clip->getPosition().getStart()).inBeats() == doctest::Approx (4.0));
        CHECK (tempo.toBeats (clip->getPosition().getEnd()).inBeats() == doctest::Approx (12.0));
        CHECK (clip->getName() == "Riff");

        const auto stateAfterEdits = project->getEdit().state.toXmlString();

        project->undo();
        CHECK (track->getClips().isEmpty());

        project->undo();
        CHECK (project->getNumTracks() == 1);

        // Redo must rebuild exactly the same project (no duplicated or missing nodes).
        project->redo();
        project->redo();
        CHECK (project->getEdit().state.toXmlString() == stateAfterEdits);
    }

    TEST_CASE ("a new project starts with an empty undo history")
    {
        auto project = Project::createEmpty (harmonia::test::session());

        CHECK_FALSE (project->canUndo());
        CHECK_FALSE (project->canRedo());
        CHECK (project->getUndoDescription().isEmpty());
        CHECK (project->getRedoDescription().isEmpty());
    }

    TEST_CASE ("undo and redo name the step they take back or do again (for the Edit menu)")
    {
        auto project = Project::createEmpty (harmonia::test::session());

        project->beginGesture ("Change tempo");
        project->setTempoBpm (140.0);
        project->beginGesture ("Time signature");
        project->setBeatsPerBar (3);

        CHECK (project->getUndoDescription() == "Time signature");
        CHECK (project->getRedoDescription().isEmpty());

        project->undo();
        CHECK (project->getUndoDescription() == "Change tempo");
        CHECK (project->getRedoDescription() == "Time signature");

        project->undo();
        CHECK (project->getUndoDescription().isEmpty());
        CHECK (project->getRedoDescription() == "Change tempo");
    }

    TEST_CASE ("save and load keep every note property and linked copies")
    {
        auto& session = harmonia::test::session();
        juce::TemporaryFile file (".harmonia");
        std::vector<harmonia::core::Note> written;

        {
            harmonia::test::ProjectWithClip p;
            harmonia::core::NoteEditor editor (*p.clip);
            editor.add (harmonia::test::makeRichNote (48, 0.0));
            editor.add (harmonia::core::Note {});
            editor.add (harmonia::test::makeRichNote (72, 3.25));
            written = harmonia::test::readNotes (*p.clip);

            REQUIRE (harmonia::core::clip_links::duplicate (*p.clip, 16.0, true) != nullptr);
            REQUIRE (p.project->save (file.getFile()).isEmpty());
        }

        juce::String error;
        auto loaded = Project::load (session, file.getFile(), error);
        REQUIRE_MESSAGE (loaded != nullptr, error.toStdString());

        auto clips = loaded->getTracks().getFirst()->getClips();
        REQUIRE (clips.size() == 2);

        auto* first = dynamic_cast<te::MidiClip*> (clips[0]);
        auto* second = dynamic_cast<te::MidiClip*> (clips[1]);
        REQUIRE (first != nullptr);
        REQUIRE (second != nullptr);

        CHECK (harmonia::test::readNotes (*first) == written);
        CHECK (harmonia::test::readNotes (*second) == written);

        // The link survives: editing one copy still changes the other.
        harmonia::core::NoteEditor (*second).add (harmonia::core::Note {});
        CHECK (harmonia::test::readNotes (*first).size() == 4);
    }

    TEST_CASE ("loading a missing file fails with a message")
    {
        juce::String error;
        auto project = Project::load (harmonia::test::session(),
                                      juce::File::getSpecialLocation (juce::File::tempDirectory)
                                          .getChildFile ("harmonia-does-not-exist.harmonia"),
                                      error);

        CHECK (project == nullptr);
        CHECK (error.contains ("not found"));
    }

    TEST_CASE ("loading a file that is not a project fails")
    {
        juce::TemporaryFile file (".harmonia");
        REQUIRE (file.getFile().replaceWithText ("<SOMETHING_ELSE/>"));

        juce::String error;
        auto project = Project::load (harmonia::test::session(), file.getFile(), error);

        CHECK (project == nullptr);
        CHECK (error.contains ("Not a Harmonia project"));
    }

    TEST_CASE ("a project from a newer format version is refused")
    {
        auto& session = harmonia::test::session();
        juce::TemporaryFile file (".harmonia");

        {
            auto project = Project::createEmpty (session);
            REQUIRE (project->save (file.getFile()).isEmpty());
        }

        auto xml = juce::parseXML (file.getFile());
        REQUIRE (xml != nullptr);
        xml->setAttribute ("hFormatVersion", Project::currentFormatVersion + 1);
        REQUIRE (xml->writeTo (file.getFile()));

        juce::String error;
        auto project = Project::load (session, file.getFile(), error);

        CHECK (project == nullptr);
        CHECK (error.contains ("newer version"));
    }

    TEST_CASE ("unsaved changes: none in a new or just saved project, any edit counts")
    {
        auto& session = harmonia::test::session();
        juce::TemporaryFile file (".harmonia");
        auto project = Project::createEmpty (session);

        CHECK_FALSE (project->hasUnsavedChanges());

        project->beginGesture ("Add track");
        project->addTrack ("Bass");
        CHECK (project->hasUnsavedChanges());

        REQUIRE (project->save (file.getFile()).isEmpty());
        CHECK_FALSE (project->hasUnsavedChanges());

        project->beginGesture ("Rename");
        project->getTracks().getLast()->setName ("Lead");
        CHECK (project->hasUnsavedChanges());

        // Setting up defaults and forgetting the undo history counts as a fresh start.
        project->clearUndoHistory();
        CHECK_FALSE (project->hasUnsavedChanges());
    }

    TEST_CASE ("a whole arrangement survives saving: tracks, names, clip positions, mute and volume")
    {
        auto& session = harmonia::test::session();
        juce::TemporaryFile file (".harmonia");

        {
            harmonia::test::ProjectWithClip p;
            p.track->setName ("Drums");
            p.clip->setName ("Beat");
            auto* bass = p.project->addTrack ("Bass");
            REQUIRE (bass != nullptr);
            p.project->addMidiClip (*bass, 16.0, 8.0, "Line");
            bass->setMute (true);
            bass->getVolumePlugin()->setVolumeDb (-6.0f);
            REQUIRE (p.project->save (file.getFile()).isEmpty());
        }

        juce::String error;
        auto loaded = Project::load (session, file.getFile(), error);
        REQUIRE_MESSAGE (loaded != nullptr, error.toStdString());
        CHECK_FALSE (loaded->hasUnsavedChanges());

        const auto tracks = loaded->getTracks();
        REQUIRE (tracks.size() == 2);
        CHECK (tracks[0]->getName() == "Drums");
        CHECK (tracks[1]->getName() == "Bass");
        CHECK (tracks[1]->isMuted (false));
        CHECK (tracks[1]->getVolumePlugin()->getVolumeDb() == doctest::Approx (-6.0f).epsilon (0.01));

        auto* line = dynamic_cast<te::MidiClip*> (tracks[1]->getClips().getFirst());
        REQUIRE (line != nullptr);
        CHECK (line->getName() == "Line");
        CHECK (line->getStartBeat().inBeats() == doctest::Approx (16.0));
        CHECK (line->getEndBeat().inBeats() == doctest::Approx (24.0));
    }
}
