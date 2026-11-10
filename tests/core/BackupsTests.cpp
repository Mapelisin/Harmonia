#include <doctest.h>

#include "TestAudioFiles.h"
#include "TestProject.h"
#include "core/Backups.h"
#include "core/Clips.h"

using namespace harmonia;
namespace backups = core::backups;

namespace
{

    /** A tone in a file, in a folder that may not be there yet. */
    juce::File tone (const juce::File& file, double seconds, float level = 0.5f)
    {
        file.getParentDirectory().createDirectory();
        return test::writeTone (file, seconds, level);
    }
    juce::StringArray trackNames (core::Project& project)
    {
        juce::StringArray names;

        for (auto* track : project.getTracks())
            names.add (track->getName());

        return names;
    }
}

TEST_SUITE ("core: earlier versions of a song")
{
    TEST_CASE ("a save keeps what the file had; the versions are listed, the latest first; nothing new, nothing kept")
    {
        test::TempFolder temp;
        const auto file = temp.getFile ("My song.harmonia");
        auto project = core::Project::createEmpty (test::session());
        juce::String warning;

        // The first save: there was nothing to keep.
        REQUIRE (backups::save (*project, file, warning).isEmpty());
        CHECK (warning.isEmpty());
        CHECK (backups::list (file).empty());
        CHECK_FALSE (backups::getFolder (file).exists());
        CHECK (backups::getFolder (file) == temp.getFile ("My song Backups"));

        // Saved again as it is: still nothing (and no empty folder left behind).
        REQUIRE (backups::save (*project, file, warning).isEmpty());
        CHECK (backups::list (file).empty());
        CHECK_FALSE (backups::getFolder (file).exists());

        // A change, saved: what the file had is the first version.
        project->addTrack ("Bass");
        REQUIRE (backups::save (*project, file, warning).isEmpty());
        CHECK (warning.isEmpty());
        auto versions = backups::list (file);
        REQUIRE (versions.size() == 1);
        CHECK (versions[0].file.getParentDirectory() == backups::getFolder (file));
        CHECK (versions[0].file.getFileName().startsWith ("My song 20"));
        CHECK (versions[0].file.getFileExtension() == ".harmonia");
        CHECK (versions[0].getLabel().isNotEmpty());
        CHECK_FALSE (project->hasUnsavedChanges());

        // Another: two versions, the latest first, each with a time of its own.
        project->addTrack ("Lead");
        REQUIRE (backups::save (*project, file, warning).isEmpty());
        versions = backups::list (file);
        REQUIRE (versions.size() == 2);
        CHECK (versions[0].savedAt > versions[1].savedAt);

        // The versions are the song as it was: one track, then two; the song itself has three.
        juce::String error;
        auto oldest = core::Project::load (test::session(), versions[1].file, error);
        auto newer = core::Project::load (test::session(), versions[0].file, error);
        REQUIRE (oldest != nullptr);
        REQUIRE (newer != nullptr);
        CHECK (oldest->getNumTracks() == 1);
        CHECK (trackNames (*newer).joinIntoString (", ").endsWith ("Bass"));
        CHECK (project->getNumTracks() == 3);

        // Other files in the folder aren't versions.
        backups::getFolder (file).getChildFile ("Notes.txt").replaceWithText ("mine");
        backups::getFolder (file).getChildFile ("My song copy.harmonia").replaceWithText ("mine");
        backups::getFolder (file).getChildFile ("Other 2026-11-10 14-03-22.harmonia").replaceWithText ("mine");
        CHECK (backups::list (file).size() == 2);

        // A song that was never saved has none, and no folder for them.
        CHECK (backups::list ({}).empty());
        CHECK (backups::getFolder ({}) == juce::File());
        CHECK (backups::keep ({}).isEmpty());
    }

    TEST_CASE ("only the last twenty versions are kept")
    {
        test::TempFolder temp;
        const auto file = temp.getFile ("Song.harmonia");
        const auto folder = backups::getFolder (file);
        folder.createDirectory();

        for (int i = 0; i < 25; ++i)
            folder.getChildFile ("Song 2026-01-" + juce::String (i + 1).paddedLeft ('0', 2) + " 10-00-00.harmonia")
                  .replaceWithText ("version " + juce::String (i));

        file.replaceWithText ("the song now");
        CHECK (backups::list (file).size() == 25);
        CHECK (backups::keep (file).isEmpty());

        const auto versions = backups::list (file);
        REQUIRE ((int) versions.size() == backups::maxVersions);
        CHECK (versions.front().file.loadFileAsString() == "the song now");
        CHECK (versions.back().file.loadFileAsString() == "version 6");       // the six oldest went
        CHECK_FALSE (folder.getChildFile ("Song 2026-01-01 10-00-00.harmonia").exists());

        // Kept already: not again.
        CHECK (backups::keep (file).isEmpty());
        CHECK ((int) backups::list (file).size() == backups::maxVersions);
        CHECK (backups::list (file)[1].file.loadFileAsString() == "version 24");

        // Nothing to keep: no file.
        CHECK (backups::keep (temp.getFile ("Never saved.harmonia")).isEmpty());
        CHECK_FALSE (backups::getFolder (temp.getFile ("Never saved.harmonia")).exists());
    }

    TEST_CASE ("an earlier version opens in the song's place, unsaved, with its sounds; saving it keeps the version it replaces")
    {
        test::TempFolder temp;
        const auto file = temp.getFile ("Song.harmonia");
        const auto sound = tone (temp.getFile ("Song Audio").getChildFile ("Take.wav"), 1.0);
        juce::String warning, error;

        {
            auto project = core::Project::createEmpty (test::session());
            auto* track = project->addTrack ("Audio");
            core::tracks::setKind (*track, core::tracks::Kind::audio);
            REQUIRE (project->addAudioClip (*track, sound, 0.0, error) != nullptr);
            REQUIRE (backups::save (*project, file, warning).isEmpty());

            project->addTrack ("Added later");
            REQUIRE (backups::save (*project, file, warning).isEmpty());
        }

        const auto versions = backups::list (file);
        REQUIRE (versions.size() == 1);

        auto restored = backups::open (test::session(), versions[0], file, error);
        REQUIRE_MESSAGE (restored != nullptr, error.toStdString());
        CHECK_FALSE (trackNames (*restored).contains ("Added later"));
        CHECK (restored->hasUnsavedChanges());

        // Its sound, saved by a path from the song's folder, is found from the song's folder
        // (not from the folder of versions).
        te::AudioTrack* audioTrack = nullptr;

        for (auto* track : restored->getTracks())
            if (track->getName() == "Audio")
                audioTrack = track;

        REQUIRE (audioTrack != nullptr);
        const auto clips = core::clips::getClips (*audioTrack);
        REQUIRE (clips.size() == 1);
        CHECK (clips[0]->getSourceFileReference().getFile() == sound);

        // Saved: it is the song now, and what it replaced is a version too.
        REQUIRE (backups::save (*restored, file, warning).isEmpty());
        CHECK (warning.isEmpty());
        CHECK (backups::list (file).size() == 2);
        restored.reset();

        auto again = core::Project::load (test::session(), file, error);
        REQUIRE (again != nullptr);
        CHECK_FALSE (trackNames (*again).contains ("Added later"));

        auto replaced = core::Project::load (test::session(), backups::list (file)[0].file, error);
        REQUIRE (replaced != nullptr);
        CHECK (trackNames (*replaced).contains ("Added later"));
    }
}
