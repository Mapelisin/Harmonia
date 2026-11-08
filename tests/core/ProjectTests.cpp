#include <doctest.h>

#include "TestSession.h"
#include "core/Project.h"

using harmonia::core::Project;

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
}
