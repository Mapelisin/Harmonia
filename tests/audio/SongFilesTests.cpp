#include <doctest.h>

#include "TestAudioFiles.h"
#include "TestSession.h"
#include "audio/DrumKit.h"
#include "audio/Instruments.h"
#include "audio/Sampler.h"
#include "core/Clips.h"
#include "core/Project.h"
#include "core/SongFiles.h"
#include "core/Takes.h"

using namespace harmonia;
namespace song_files = core::song_files;
namespace drum_kit = audio::drum_kit;
namespace sampler = audio::sampler;
namespace instruments = audio::instruments;
using Kind = song_files::Reference::Kind;

namespace
{
    constexpr int kick = 36;

    void dispatch (int ms = 60)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    /** A tone in a file, in a folder that may not be there yet. */
    juce::File tone (const juce::File& file, double seconds, float level = 0.5f)
    {
        file.getParentDirectory().createDirectory();
        return test::writeTone (file, seconds, level);
    }

    /** A song saved as "Song.harmonia" in a folder of its own, using sounds from another folder:
        an audio clip (with two takes), a drum machine with a sample on the kick, a sampler. */
    struct SongWithSounds
    {
        SongWithSounds()
        {
            juce::String error;
            audioTrack = project->getTracks().getFirst();
            audioTrack->setName ("Vocals");
            core::tracks::setKind (*audioTrack, core::tracks::Kind::audio);
            clip = project->addAudioClip (*audioTrack, vocal, 0.0, error);
            REQUIRE_MESSAGE (clip != nullptr, error.toStdString());
            clip->setName ("Verse");

            // Two takes of it, as a recording in a loop leaves them: the clip plays the first.
            juce::ValueTree takes ("HTAKES");
            takes.appendChild (juce::ValueTree (te::IDs::TAKE, { { te::IDs::source, vocal.getFullPathName() } }), nullptr);
            takes.appendChild (juce::ValueTree (te::IDs::TAKE, { { te::IDs::source, take2.getFullPathName() } }), nullptr);
            clip->state.appendChild (takes, nullptr);
            REQUIRE (core::takes::getCount (*clip) == 2);

            drumTrack = project->addTrack ("Drums");
            REQUIRE (instruments::useDrumKit (*drumTrack).isEmpty());
            REQUIRE (drum_kit::setSample (*instruments::get (*drumTrack), kick, myKick).isEmpty());

            samplerTrack = project->addTrack ("Lead");
            REQUIRE (instruments::useSampler (*samplerTrack, pluck).isEmpty());

            dispatch();
            REQUIRE (project->save (file).isEmpty());
            project->clearUndoHistory();
        }

        juce::File fileOf (te::Plugin* plugin) const    { return sampler::getFile (*plugin); }

        test::TempFolder temp;
        const juce::File elsewhere = temp.getFile ("Elsewhere");
        const juce::File vocal = tone (elsewhere.getChildFile ("Vocal.wav"), 1.0, 0.5f);
        const juce::File take2 = tone (elsewhere.getChildFile ("Vocal take 2.wav"), 1.0, 0.4f);
        const juce::File myKick = tone (elsewhere.getChildFile ("Kicks").getChildFile ("My kick.wav"), 0.2, 0.5f);
        const juce::File pluck = tone (elsewhere.getChildFile ("Pluck.wav"), 0.5, 0.3f);
        const juce::File file = temp.getFile ("My songs").getChildFile ("Song.harmonia");

        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        te::AudioTrack* audioTrack = nullptr;
        te::AudioTrack* drumTrack = nullptr;
        te::AudioTrack* samplerTrack = nullptr;
        te::WaveAudioClip* clip = nullptr;
    };

    juce::StringArray describe (const std::vector<song_files::Reference>& references)
    {
        juce::StringArray result;

        for (const auto& r : references)
            result.add (r.file.getFileName() + " <- " + r.usedBy);

        return result;
    }
}

TEST_SUITE ("core: the song's sound files")
{
    TEST_CASE ("every sound the song uses is listed, with what uses it; the missing ones are named once each")
    {
        SongWithSounds song;
        auto& edit = song.project->getEdit();

        const auto all = song_files::getAll (edit);
        CHECK (describe (all).joinIntoString ("; ")
                 == "Vocal.wav <- Verse on Vocals; Vocal.wav <- a take of Verse on Vocals; Vocal take 2.wav <- a take of Verse on Vocals; "
                    "My kick.wav <- a drum of Drums; Pluck.wav <- the sampler of Lead");
        REQUIRE (all.size() == 5);
        CHECK (all[0].kind == Kind::clip);
        CHECK (all[1].kind == Kind::take);
        CHECK (all[3].kind == Kind::drumSample);
        CHECK (all[4].kind == Kind::samplerSound);
        CHECK (all[0].file == song.vocal);
        CHECK (song_files::getMissing (edit).empty());
        CHECK (song_files::getFolder (song.file) == song.file.getSiblingFile ("Song Audio"));

        // Two of them gone: named once each, with everything that uses them.
        song.vocal.deleteFile();
        song.myKick.deleteFile();
        const auto missing = song_files::getMissing (edit);
        REQUIRE (missing.size() == 2);
        CHECK (missing[0].file == song.myKick);
        CHECK (missing[0].usedBy.joinIntoString (", ") == "a drum of Drums");
        CHECK (missing[1].file == song.vocal);
        CHECK (missing[1].usedBy.joinIntoString (", ") == "Verse on Vocals, a take of Verse on Vocals");

        // A song with no sounds has none.
        auto empty = core::Project::createEmpty (test::session());
        CHECK (song_files::getAll (empty->getEdit()).empty());
    }

    TEST_CASE ("collecting copies the sounds from elsewhere into the song's own folder, once each, and the song uses the copies")
    {
        SongWithSounds song;
        auto& edit = song.project->getEdit();
        const auto folder = song_files::getFolder (song.file);
        juce::String error;

        // One more clip of the same sound, a sound of the same name from another folder, and a
        // recording that is in the song's folder already.
        const auto otherVocal = tone (song.temp.getFile ("Other").getChildFile ("Vocal.wav"), 2.0, 0.2f);
        const auto recording = tone (folder.getChildFile ("Recorded.wav"), 1.0, 0.3f);
        REQUIRE (song.project->addAudioClip (*song.audioTrack, song.vocal, 8.0, error) != nullptr);
        REQUIRE (song.project->addAudioClip (*song.audioTrack, otherVocal, 16.0, error) != nullptr);
        REQUIRE (song.project->addAudioClip (*song.audioTrack, recording, 24.0, error) != nullptr);
        dispatch();
        song.project->clearUndoHistory();

        const auto result = song_files::collect (*song.project, song.file);
        CHECK (result.folder == folder);
        CHECK (result.copied == 5);         // Vocal, its second take, the other Vocal, the kick, the pluck
        CHECK (result.already == 1);        // the recording
        CHECK (result.missing == 0);
        CHECK (result.problems.isEmpty());
        CHECK (result.bytes > 100000);
        CHECK (result.describe().startsWith ("Copied 5 files ("));
        CHECK (result.describe().endsWith ("; 1 was there already"));
        CHECK (song.project->getUndoDescription() == "Collect files");

        // The copies are there; the same name with another sound got a number; the originals stay.
        CHECK (folder.getChildFile ("Vocal.wav").hasIdenticalContentTo (song.vocal));
        CHECK (folder.getChildFile ("Vocal (2).wav").hasIdenticalContentTo (otherVocal));
        CHECK (folder.getChildFile ("Vocal take 2.wav").existsAsFile());
        CHECK (folder.getChildFile ("My kick.wav").existsAsFile());
        CHECK (folder.getChildFile ("Pluck.wav").existsAsFile());
        CHECK (folder.getNumberOfChildFiles (juce::File::findFiles) == 6);
        CHECK (song.vocal.existsAsFile());
        CHECK (song.myKick.existsAsFile());

        // The song uses the copies, every place of it.
        for (const auto& reference : song_files::getAll (edit))
            CHECK_MESSAGE (reference.file.isAChildOf (folder), reference.file.getFullPathName().toStdString());

        CHECK (song.clip->getSourceFileReference().getFile() == folder.getChildFile ("Vocal.wav"));
        CHECK (core::takes::getCount (*song.clip) == 2);
        CHECK (core::takes::getCurrent (*song.clip) == 0);
        CHECK (drum_kit::getSample (*instruments::get (*song.drumTrack), kick)->file == folder.getChildFile ("My kick.wav"));
        CHECK (sampler::getFile (*instruments::get (*song.samplerTrack)) == folder.getChildFile ("Pluck.wav"));

        // Again: nothing to do, and nothing to undo.
        const auto again = song_files::collect (*song.project, song.file);
        CHECK (again.copied == 0);
        CHECK (again.already == 6);
        CHECK (again.describe().startsWith ("All the song's sound files are in "));
        CHECK (song.project->getUndoDescription() == "Collect files");

        // One Ctrl+Z: the song uses the sounds where they were (the copies stay on the disk).
        song.project->undo();
        dispatch();
        CHECK (song.clip->getSourceFileReference().getFile() == song.vocal);
        CHECK (sampler::getFile (*instruments::get (*song.samplerTrack)) == song.pluck);
        CHECK (drum_kit::getSample (*instruments::get (*song.drumTrack), kick)->file == song.myKick);
        CHECK (folder.getChildFile ("Pluck.wav").existsAsFile());
        CHECK_FALSE (song.project->canUndo());

        // Collected again, the copies made before are used: nothing is copied twice.
        const auto third = song_files::collect (*song.project, song.file);
        CHECK (third.copied == 0);
        CHECK (third.already == 6);
        CHECK (folder.getNumberOfChildFiles (juce::File::findFiles) == 6);
        CHECK (sampler::getFile (*instruments::get (*song.samplerTrack)) == folder.getChildFile ("Pluck.wav"));

        // A sound that isn't anywhere is counted and left as it is.
        song.project->undo();
        dispatch();
        song.pluck.deleteFile();
        const auto withMissing = song_files::collect (*song.project, song.file);
        CHECK (withMissing.missing == 1);
        CHECK (withMissing.describe().contains ("1 is missing"));
        CHECK (sampler::getFile (*instruments::get (*song.samplerTrack)) == song.pluck);
    }

    TEST_CASE ("collected and saved, the song and its folder can be moved: every sound is found, the instruments' too")
    {
        SongWithSounds song;
        juce::String error;
        song_files::collect (*song.project, song.file);
        REQUIRE (song.project->save (song.file).isEmpty());

        // In the file the sounds are named from the song's folder, not by where the folder is.
        const auto text = song.file.loadFileAsString();
        CHECK (text.contains ("Song Audio/Vocal.wav"));
        CHECK (text.contains ("Song Audio/My kick.wav"));
        CHECK (text.contains ("Song Audio/Pluck.wav"));
        CHECK_FALSE (text.contains (song.temp.folder.getFileName()));

        // The open song still finds them (it knows them by their full paths).
        CHECK (song_files::getMissing (song.project->getEdit()).empty());
        song.project.reset();
        test::session().getEngine().getAudioFileManager().releaseAllFiles();

        // The song's folder goes to another place; the sounds it was made from are deleted.
        const auto moved = song.temp.getFile ("Another disk").getChildFile ("Music");
        REQUIRE (song.file.getParentDirectory().copyDirectoryTo (moved));
        REQUIRE (song.file.getParentDirectory().deleteRecursively());
        REQUIRE (song.elsewhere.deleteRecursively());

        auto opened = core::Project::load (test::session(), moved.getChildFile ("Song.harmonia"), error);
        REQUIRE_MESSAGE (opened != nullptr, error.toStdString());
        dispatch();
        const auto folder = moved.getChildFile ("Song Audio");
        CHECK (song_files::getMissing (opened->getEdit()).empty());

        for (const auto& reference : song_files::getAll (opened->getEdit()))
            CHECK_MESSAGE (reference.file.isAChildOf (folder), reference.file.getFullPathName().toStdString());

        te::AudioTrack* drums = nullptr;
        te::AudioTrack* lead = nullptr;

        for (auto* track : opened->getTracks())
        {
            if (track->getName() == "Drums")    drums = track;
            if (track->getName() == "Lead")     lead = track;
        }

        REQUIRE (drums != nullptr);
        REQUIRE (lead != nullptr);
        const auto kickSample = drum_kit::getSample (*instruments::get (*drums), kick);
        REQUIRE (kickSample.has_value());
        CHECK (kickSample->file == folder.getChildFile ("My kick.wav"));
        CHECK_FALSE (kickSample->missing);

        auto* leadSampler = dynamic_cast<sampler::Plugin*> (instruments::get (*lead));
        REQUIRE (leadSampler != nullptr);
        CHECK (sampler::getFile (*leadSampler) == folder.getChildFile ("Pluck.wav"));
        CHECK_FALSE (sampler::isFileMissing (*leadSampler));
        CHECK (leadSampler->getSample() != nullptr);        // read as the song opened, from where it is now

        // Saved again from there, and saved as another song elsewhere: still found.
        REQUIRE (opened->save (moved.getChildFile ("Song.harmonia")).isEmpty());
        const auto copy = song.temp.getFile ("Copies").getChildFile ("Song copy.harmonia");
        REQUIRE (opened->save (copy).isEmpty());
        opened.reset();

        auto fromCopy = core::Project::load (test::session(), copy, error);
        REQUIRE (fromCopy != nullptr);
        CHECK (song_files::getMissing (fromCopy->getEdit()).empty());
        CHECK (song_files::getAll (fromCopy->getEdit()).size() == 5);
    }

    TEST_CASE ("missing files are found again by name in a folder and the folders inside it; one undo step")
    {
        SongWithSounds song;
        auto& edit = song.project->getEdit();

        // The sounds were moved (and one is gone for good).
        const auto newPlace = song.temp.getFile ("New place");
        REQUIRE (newPlace.getChildFile ("Deep").getChildFile ("Deeper").createDirectory());
        REQUIRE (song.vocal.moveFileTo (newPlace.getChildFile ("Vocal.wav")));
        REQUIRE (song.myKick.moveFileTo (newPlace.getChildFile ("Deep").getChildFile ("Deeper").getChildFile ("my KICK.wav")));
        REQUIRE (song.pluck.deleteFile());
        CHECK (song_files::getMissing (edit).size() == 3);

        // A folder where none of them is: nothing changes.
        auto found = song_files::findIn (*song.project, song.temp.getFile ("My songs"));
        CHECK (found.found == 0);
        CHECK (found.stillMissing == 3);
        CHECK_FALSE (song.project->canUndo());

        found = song_files::findIn (*song.project, newPlace);
        CHECK (found.found == 2);
        CHECK (found.stillMissing == 1);
        CHECK (song.project->getUndoDescription() == "Find missing files");
        CHECK (song.clip->getSourceFileReference().getFile() == newPlace.getChildFile ("Vocal.wav"));
        CHECK (core::takes::getFile (*song.clip, 0) == newPlace.getChildFile ("Vocal.wav"));
        CHECK (core::takes::getCurrent (*song.clip) == 0);
        CHECK (drum_kit::getSample (*instruments::get (*song.drumTrack), kick)->file.getFileName().equalsIgnoreCase ("my kick.wav"));
        CHECK_FALSE (drum_kit::getSample (*instruments::get (*song.drumTrack), kick)->missing);

        const auto missing = song_files::getMissing (edit);
        REQUIRE (missing.size() == 1);
        CHECK (missing[0].file == song.pluck);

        // One Ctrl+Z: as it was.
        song.project->undo();
        dispatch();
        CHECK (song_files::getMissing (edit).size() == 3);
        song.project->redo();
        dispatch();

        // The last one, chosen by hand: every place that named the old file names the new one.
        const auto another = tone (song.temp.getFile ("Another pluck.wav"), 0.5, 0.3f);
        song.project->beginGesture ("Replace a file");
        CHECK (song_files::repoint (*song.project, song.pluck, another) == 1);
        CHECK (sampler::getFile (*instruments::get (*song.samplerTrack)) == another);
        CHECK (song_files::getMissing (edit).empty());
        CHECK (song_files::findIn (*song.project, newPlace).stillMissing == 0);
    }
}
