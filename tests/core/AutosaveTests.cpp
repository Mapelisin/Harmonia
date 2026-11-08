#include <doctest.h>

#include "TestAudioFiles.h"
#include "audio/Instruments.h"
#include "core/Autosave.h"
#include "core/Clips.h"
#include "core/NoteEditor.h"
#include "core/Project.h"

using namespace harmonia;
using core::Autosave;
using core::Project;
namespace clips = core::clips;

namespace
{
    juce::File getSound (Project& project, int clipIndex)
    {
        const auto all = clips::getClips (*project.getTracks().getFirst());
        auto* audioClip = dynamic_cast<te::AudioClipBase*> (all[clipIndex]);
        return audioClip != nullptr ? audioClip->getCurrentSourceFile() : juce::File();
    }

    /** The sound file paths as written in a project file, in clip order. */
    juce::StringArray getWrittenPaths (const juce::File& projectFile)
    {
        juce::StringArray paths;
        const auto xml = juce::parseXML (projectFile);
        REQUIRE (xml != nullptr);

        std::function<void (const juce::XmlElement&)> visit = [&] (const juce::XmlElement& e)
        {
            if (e.hasTagName ("AUDIOCLIP"))
                paths.add (e.getStringAttribute ("source"));

            for (auto* child : e.getChildIterator())
                visit (*child);
        };

        visit (*xml);
        return paths;
    }

    /** A saved song in folder A with a sound from A/Samples and one from elsewhere, both added
        after the first save (the engine then stores sounds near the project relative to it). */
    struct SongWithSounds
    {
        SongWithSounds()
        {
            REQUIRE (folder.getChildFile ("Samples").createDirectory());
            inside = test::writeTone (folder.getChildFile ("Samples").getChildFile ("Kick.wav"), 0.5);
            outside = test::writeTone (temp.getFile ("Outside.wav"), 0.5);

            REQUIRE (project->save (file).isEmpty());

            juce::String error;
            auto& track = *project->getTracks().getFirst();
            REQUIRE (project->addAudioClip (track, inside, 0.0, error) != nullptr);
            REQUIRE (project->addAudioClip (track, outside, 8.0, error) != nullptr);
        }

        test::TempFolder temp;
        juce::File folder = temp.getFile ("A");
        juce::File file = folder.getChildFile ("Song.harmonia");
        juce::File inside, outside;
        std::unique_ptr<Project> project = Project::createEmpty (test::session());
    };

    juce::Time at (int seconds)
    {
        return juce::Time (2026, 10, 8, 23, 0) + juce::RelativeTime::seconds (seconds);
    }
}

TEST_SUITE ("core::Project: saving")
{
    TEST_CASE ("saving changes nothing in the open project: Ctrl+Z after it undoes the last change, redo steps stay")
    {
        test::TempFolder temp;
        auto project = Project::createEmpty (test::session());
        audio::instruments::useBuiltInSynth (*project->getTracks().getFirst());
        project->clearUndoHistory();

        project->beginGesture ("Tempo 100");
        project->setTempoBpm (100.0);
        project->beginGesture ("Tempo 110");
        project->setTempoBpm (110.0);
        project->undo();
        project->beginGesture ({});

        REQUIRE (project->save (temp.getFile ("Song.harmonia")).isEmpty());
        REQUIRE (project->saveCopy (temp.getFile ("Copy.harmonia")).isEmpty());
        CHECK_FALSE (project->hasUnsavedChanges());

        REQUIRE (project->canRedo());
        project->redo();
        CHECK (project->getTempoBpm() == doctest::Approx (110.0));
        project->undo();
        project->undo();
        CHECK (project->getTempoBpm() == doctest::Approx (120.0));

        // The synth's settings are in the file.
        juce::String error;
        auto loaded = Project::load (test::session(), temp.getFile ("Song.harmonia"), error);
        REQUIRE (loaded != nullptr);
        CHECK (loaded->getTracks().getFirst()->pluginList.size() == project->getTracks().getFirst()->pluginList.size());
    }

    TEST_CASE ("sounds in the project's folder are stored relative to it: the folder can be moved whole")
    {
        SongWithSounds s;
        REQUIRE (s.project->save (s.file).isEmpty());

        const auto written = getWrittenPaths (s.file);
        REQUIRE (written.size() == 2);
        CHECK (written[0] == "Samples/Kick.wav");
        CHECK (written[1] == s.outside.getFullPathName());

        const auto moved = s.temp.getFile ("Moved");
        REQUIRE (s.folder.copyDirectoryTo (moved));

        juce::String error;
        auto loaded = Project::load (test::session(), moved.getChildFile ("Song.harmonia"), error);
        REQUIRE (loaded != nullptr);
        CHECK (getSound (*loaded, 0) == moved.getChildFile ("Samples").getChildFile ("Kick.wav"));
        CHECK (getSound (*loaded, 1) == s.outside);
        CHECK_FALSE (loaded->hasUnsavedChanges());
    }

    TEST_CASE ("Save As into another folder: the sounds are still found from there")
    {
        SongWithSounds s;
        const auto elsewhere = s.temp.getFile ("B").getChildFile ("Copy.harmonia");
        REQUIRE (s.project->save (elsewhere).isEmpty());

        // The open project too.
        CHECK (getSound (*s.project, 0) == s.inside);

        juce::String error;
        auto loaded = Project::load (test::session(), elsewhere, error);
        REQUIRE (loaded != nullptr);
        CHECK (getSound (*loaded, 0) == s.inside);
        CHECK (getSound (*loaded, 1) == s.outside);

        // Saved again next to the samples: relative again.
        REQUIRE (loaded->save (s.folder.getChildFile ("Back.harmonia")).isEmpty());
        CHECK (getWrittenPaths (s.folder.getChildFile ("Back.harmonia"))[0] == "Samples/Kick.wav");
    }

    TEST_CASE ("a copy changes nothing: still unsaved, nothing to undo added, recordings still go next to the project")
    {
        SongWithSounds s;
        audio::instruments::useBuiltInSynth (*s.project->addTrack ("Synth"));
        CHECK (s.project->hasUnsavedChanges());
        const auto undoable = s.project->canUndo();
        const auto changes = s.project->getChangeCount();

        const auto copy = s.temp.getFile ("Elsewhere").getChildFile ("Copy.harmonia");
        REQUIRE (s.project->saveCopy (copy).isEmpty());

        CHECK (s.project->hasUnsavedChanges());
        CHECK (s.project->canUndo() == undoable);
        CHECK (s.project->getChangeCount() == changes);
        CHECK (s.project->getEdit().editFileRetriever() == s.file);

        // Full paths in the copy.
        const auto written = getWrittenPaths (copy);
        REQUIRE (written.size() == 2);
        CHECK (written[0] == s.inside.getFullPathName());
    }
}

TEST_SUITE ("core::Autosave")
{
    TEST_CASE ("nothing kept while nothing is unsaved; a change is copied at once, then at most twice a minute")
    {
        test::TempFolder temp;
        Autosave autosave (temp.getFile ("Autosave"));
        auto project = Project::createEmpty (test::session());

        CHECK (autosave.update (*project, {}, at (0)).isEmpty());
        CHECK_FALSE (autosave.findLeftover().has_value());

        project->beginGesture ("Tempo");
        project->setTempoBpm (100.0);
        CHECK (autosave.update (*project, {}, at (1)).isEmpty());

        auto leftover = autosave.findLeftover();
        REQUIRE (leftover.has_value());
        CHECK (leftover->original == juce::File());
        CHECK (leftover->getName() == "Untitled");
        CHECK (leftover->savedAt == at (1));

        juce::String error;
        auto tempoInCopy = [&]
        {
            auto restored = Autosave::restore (test::session(), *autosave.findLeftover(), error);
            REQUIRE (restored != nullptr);
            return restored->getTempoBpm();
        };

        project->beginGesture ("Tempo");
        project->setTempoBpm (110.0);
        CHECK (autosave.update (*project, {}, at (10)).isEmpty());
        CHECK (tempoInCopy() == doctest::Approx (100.0));     // too soon

        CHECK (autosave.update (*project, {}, at (32)).isEmpty());
        CHECK (tempoInCopy() == doctest::Approx (110.0));

        // Nothing new: nothing written.
        const auto before = autosave.getCopyFile().getLastModificationTime();
        CHECK (autosave.update (*project, {}, at (100)).isEmpty());
        CHECK (autosave.findLeftover()->savedAt == at (32));
        CHECK (autosave.getCopyFile().getLastModificationTime() == before);
    }

    TEST_CASE ("restored work is the project it was: unsaved, saved to its own file, its sounds and notes there")
    {
        SongWithSounds s;
        auto* clip = s.project->addMidiClip (*s.project->addTrack ("Keys"), 0.0, 4.0, "Riff");
        core::Note note;
        note.pitch = 64;
        core::NoteEditor (*clip).add (note);
        s.project->setTempoBpm (90.0);

        {
            Autosave autosave (s.temp.getFile ("Autosave"));
            REQUIRE (autosave.update (*s.project, s.file).isEmpty());
            // ...and Harmonia crashes: nothing cleans up.
        }

        s.project.reset();
        Autosave autosave (s.temp.getFile ("Autosave"));
        const auto leftover = autosave.findLeftover();
        REQUIRE (leftover.has_value());
        CHECK (leftover->original == s.file);
        CHECK (leftover->getName() == "Song");

        juce::String error;
        auto restored = Autosave::restore (test::session(), *leftover, error);
        REQUIRE_MESSAGE (restored != nullptr, error.toStdString());
        CHECK (restored->hasUnsavedChanges());
        CHECK (restored->getTempoBpm() == doctest::Approx (90.0));
        CHECK (restored->getEdit().editFileRetriever() == s.file);
        CHECK (getSound (*restored, 0) == s.inside);
        CHECK (getSound (*restored, 1) == s.outside);
        REQUIRE (restored->getNumTracks() == 2);
        const auto midi = clips::getMidiClips (*restored->getTracks()[1]);
        REQUIRE (midi.size() == 1);
        CHECK (midi[0]->getSequence().getNotes().size() == 1);

        // The copy stays until the restored work is copied again or saved.
        autosave.projectReplaced();
        CHECK (autosave.update (*restored, s.file).isEmpty());
        CHECK (autosave.findLeftover().has_value());

        REQUIRE (restored->save (s.file).isEmpty());
        CHECK (autosave.update (*restored, s.file).isEmpty());
        CHECK_FALSE (autosave.findLeftover().has_value());
        CHECK (getWrittenPaths (s.file)[0] == "Samples/Kick.wav");
    }

    TEST_CASE ("work not wanted is kept once more aside; closing normally leaves nothing; a broken copy says so")
    {
        test::TempFolder temp;
        Autosave autosave (temp.getFile ("Autosave"));
        auto project = Project::createEmpty (test::session());
        project->setTempoBpm (140.0);
        REQUIRE (autosave.update (*project, {}).isEmpty());
        REQUIRE (autosave.findLeftover().has_value());

        autosave.discard();
        CHECK_FALSE (autosave.findLeftover().has_value());
        CHECK (autosave.getDiscardedFile().existsAsFile());

        // A new project after "don't save": the old copy goes.
        project->setTempoBpm (150.0);
        autosave.projectReplaced();
        REQUIRE (autosave.update (*project, {}).isEmpty());
        REQUIRE (autosave.findLeftover().has_value());
        autosave.projectReplaced();
        auto fresh = Project::createEmpty (test::session());
        REQUIRE (autosave.update (*fresh, {}).isEmpty());
        CHECK_FALSE (autosave.findLeftover().has_value());

        project->setTempoBpm (160.0);
        autosave.projectReplaced();
        REQUIRE (autosave.update (*project, {}).isEmpty());
        autosave.clear();
        CHECK_FALSE (autosave.findLeftover().has_value());

        REQUIRE (autosave.getCopyFile().replaceWithText ("half a file"));
        const auto broken = autosave.findLeftover();
        REQUIRE (broken.has_value());
        juce::String error;
        CHECK (Autosave::restore (test::session(), *broken, error) == nullptr);
        CHECK (error.isNotEmpty());
    }
}
