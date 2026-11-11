#include <doctest.h>

#include "TestAudioFiles.h"
#include "TestProject.h"
#include "core/Clips.h"
#include "core/SongFiles.h"
#include "core/Templates.h"
#include "core/Tracks.h"

using namespace harmonia;
namespace templates = core::templates;

namespace
{
    juce::StringArray trackNames (core::Project& project)
    {
        juce::StringArray names;

        for (auto* track : project.getTracks())
            names.add (track->getName());

        return names;
    }

    /** A song worth starting from: "Keys" with a clip of three notes, "Guitar" with a sound, 97 BPM. */
    struct Song
    {
        Song()
        {
            auto* keys = project->getTracks().getFirst();
            REQUIRE (keys != nullptr);
            keys->setName ("Keys");
            auto* clip = project->addMidiClip (*keys, 0.0, 8.0, "Chords");
            REQUIRE (clip != nullptr);

            for (int pitch : { 60, 64, 67 })
                clip->getSequence().addNote (pitch, te::BeatPosition(), te::BeatDuration::fromBeats (4.0), 100, 0, nullptr);

            auto* guitar = project->addTrack ("Guitar");
            REQUIRE (guitar != nullptr);
            core::tracks::setKind (*guitar, core::tracks::Kind::audio);
            juce::String error;
            sound = test::writeTone (sounds.getFile ("Riff.wav"), 1.0);
            REQUIRE (project->addAudioClip (*guitar, sound, 0.0, error) != nullptr);
            project->setTempoBpm (97.0);
        }

        test::TempFolder sounds, folder;
        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        juce::File sound;
    };

    /** The sound files a song's clips play, as the song finds them. */
    juce::Array<juce::File> soundsOf (core::Project& project)
    {
        juce::Array<juce::File> files;

        for (auto* track : project.getTracks())
            for (auto* clip : core::clips::getClips (*track))
                if (auto* audio = dynamic_cast<te::WaveAudioClip*> (clip))
                    files.add (audio->getOriginalFile());

        return files;
    }
}

TEST_SUITE ("core: song templates")
{
    TEST_CASE ("a song saved as a template is listed by its name; the song itself stays as it was")
    {
        Song song;
        const auto folder = song.folder.getFile ("Templates");      // made when the first is saved
        CHECK (templates::list (folder).isEmpty());
        REQUIRE (song.project->hasUnsavedChanges());
        const auto couldUndo = song.project->canUndo();

        REQUIRE (templates::save (*song.project, "  My: band? ", folder).isEmpty());
        auto listed = templates::list (folder);
        REQUIRE (listed.size() == 1);
        CHECK (templates::getName (listed[0]) == "My band");
        CHECK (listed[0] == templates::getFileFor ("My: band?", folder));
        CHECK (listed[0].getFileExtension() == ".harmonia");

        // The song: still unsaved, still without a file, the same steps to undo.
        CHECK (song.project->hasUnsavedChanges());
        CHECK (song.project->canUndo() == couldUndo);
        CHECK (song.project->getEdit().editFileRetriever() == juce::File());

        // A name with nothing in it; nothing is written.
        CHECK (templates::save (*song.project, "  ", folder) == "Type a name for the template");
        CHECK (templates::save (*song.project, "??", folder) == "Type a name for the template");
        CHECK (templates::getFileFor (" ", folder) == juce::File());
        CHECK (templates::list (folder).size() == 1);

        // The same name again: in its place.
        song.project->addTrack ("Bass");
        REQUIRE (templates::save (*song.project, "My band", folder).isEmpty());
        CHECK (templates::list (folder).size() == 1);

        // By name, as a person reads them; what isn't a song isn't a template.
        REQUIRE (templates::save (*song.project, "Beat 10", folder).isEmpty());
        REQUIRE (templates::save (*song.project, "Beat 2", folder).isEmpty());
        REQUIRE (templates::save (*song.project, "acoustic", folder).isEmpty());
        folder.getChildFile ("notes.txt").replaceWithText ("not a song");
        listed = templates::list (folder);
        REQUIRE (listed.size() == 4);
        CHECK (templates::getName (listed[0]) == "acoustic");
        CHECK (templates::getName (listed[1]) == "Beat 2");
        CHECK (templates::getName (listed[2]) == "Beat 10");
        CHECK (templates::getName (listed[3]) == "My band");
    }

    TEST_CASE ("a new song from a template has what the template had; untitled, nothing unsaved, nothing to undo")
    {
        Song song;
        const auto folder = song.folder.getFile ("Templates");
        REQUIRE (templates::save (*song.project, "Band", folder).isEmpty());
        const auto file = templates::getFileFor ("Band", folder);
        const auto written = file.loadFileAsString();

        juce::String error;
        auto fresh = templates::createSong (test::session(), file, error);
        REQUIRE_MESSAGE (fresh != nullptr, error.toStdString());
        CHECK (error.isEmpty());

        CHECK (trackNames (*fresh) == juce::StringArray ({ "Keys", "Guitar" }));
        CHECK (fresh->getTempoBpm() == doctest::Approx (97.0));
        const auto clips = core::clips::getMidiClips (*fresh->getTracks().getFirst());
        REQUIRE (clips.size() == 1);
        CHECK (clips.getFirst()->getSequence().getNumNotes() == 3);

        // Its sounds are the ones the song had, where they were.
        const auto sounds = soundsOf (*fresh);
        REQUIRE (sounds.size() == 1);
        CHECK (sounds.getFirst() == song.sound);
        CHECK (core::song_files::getMissing (fresh->getEdit()).empty());

        // A song of its own, not yet saved anywhere.
        CHECK_FALSE (fresh->hasUnsavedChanges());
        CHECK_FALSE (fresh->canUndo());
        CHECK (fresh->getEdit().editFileRetriever() == juce::File());

        // Worked on and saved, it is a song in its own file: the template is as it was.
        fresh->addTrack ("Vocals");
        CHECK (fresh->hasUnsavedChanges());
        const auto saved = song.folder.getFile ("Songs").getChildFile ("First.harmonia");
        REQUIRE (fresh->save (saved).isEmpty());
        CHECK (fresh->getEdit().editFileRetriever() == saved);
        CHECK (file.loadFileAsString() == written);

        auto another = templates::createSong (test::session(), file, error);
        REQUIRE (another != nullptr);
        CHECK (trackNames (*another) == juce::StringArray ({ "Keys", "Guitar" }));

        // The two are apart.
        another->getTracks().getFirst()->setName ("Piano");
        CHECK (fresh->getTracks().getFirst()->getName() == "Keys");

        // And the saved one opens as any song.
        fresh.reset();
        auto opened = core::Project::load (test::session(), saved, error);
        REQUIRE (opened != nullptr);
        CHECK (trackNames (*opened) == juce::StringArray ({ "Keys", "Guitar", "Vocals" }));
    }

    TEST_CASE ("a template that is gone, or isn't a song, says so")
    {
        test::TempFolder temp;
        juce::String error;
        CHECK (templates::createSong (test::session(), temp.getFile ("Gone.harmonia"), error) == nullptr);
        CHECK (error.contains ("not found"));

        const auto broken = temp.getFile ("Broken.harmonia");
        broken.replaceWithText ("this is not a song");
        CHECK (templates::createSong (test::session(), broken, error) == nullptr);
        CHECK (error.contains ("Not a Harmonia project"));
    }

    TEST_CASE ("the template new songs start from is kept in the settings; one that is gone counts as none")
    {
        auto& session = test::session();
        const auto before = templates::getStartTemplate (session);

        Song song;
        const auto folder = song.folder.getFile ("Templates");
        REQUIRE (templates::save (*song.project, "Band", folder).isEmpty());
        const auto file = templates::getFileFor ("Band", folder);
        juce::String problem;

        // None: the caller makes the empty song, and there is nothing to say.
        templates::setStartTemplate (session, {});
        CHECK (templates::getStartTemplate (session) == juce::File());
        CHECK (templates::createStartSong (session, problem) == nullptr);
        CHECK (problem.isEmpty());

        templates::setStartTemplate (session, file);
        CHECK (templates::getStartTemplate (session) == file);
        auto started = templates::createStartSong (session, problem);
        REQUIRE (started != nullptr);
        CHECK (problem.isEmpty());
        CHECK (trackNames (*started) == juce::StringArray ({ "Keys", "Guitar" }));
        CHECK_FALSE (started->hasUnsavedChanges());
        started.reset();

        // Broken: said, and the caller makes the empty song.
        file.replaceWithText ("not a song any more");
        CHECK (templates::createStartSong (session, problem) == nullptr);
        CHECK (problem.contains ("Band"));
        CHECK (problem.contains ("couldn't be opened"));

        // Gone (deleted in the file manager): as if none was chosen.
        REQUIRE (file.deleteFile());
        CHECK (templates::getStartTemplate (session) == juce::File());
        CHECK (templates::createStartSong (session, problem) == nullptr);
        CHECK (problem.isEmpty());

        templates::setStartTemplate (session, before);
    }
}
