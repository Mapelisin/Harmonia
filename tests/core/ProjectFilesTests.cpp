#include <doctest.h>

#include "core/ProjectFiles.h"

using namespace harmonia;
namespace project_files = core::project_files;

TEST_SUITE ("core::project_files")
{
    TEST_CASE ("the project a command line names: quoted, with spaces, or not at all")
    {
        const juce::TemporaryFile folder;
        REQUIRE (folder.getFile().getChildFile ("My Songs").createDirectory());
        const auto song = folder.getFile().getChildFile ("My Songs").getChildFile ("First song.harmonia");
        REQUIRE (song.replaceWithText ("<EDIT/>"));
        const auto path = song.getFullPathName();

        // As Explorer passes it after a double click.
        CHECK (project_files::fromCommandLine (path.quoted()) == song);

        // Without quotes, spaces and all; and with another argument before it.
        CHECK (project_files::fromCommandLine (path) == song);
        CHECK (project_files::fromCommandLine ("--something " + path.quoted()) == song);

        // Upper case extension: still a project.
        const auto upper = song.getSiblingFile ("LOUD.HARMONIA");
        REQUIRE (upper.replaceWithText ("<EDIT/>"));
        CHECK (project_files::fromCommandLine (upper.getFullPathName().quoted()) == upper);

        // Nothing to open.
        CHECK (project_files::fromCommandLine ({}) == juce::File());
        CHECK (project_files::fromCommandLine ("--selftest --quiet") == juce::File());
        CHECK (project_files::fromCommandLine ("First song.harmonia") == juce::File());
        CHECK (project_files::fromCommandLine (song.getSiblingFile ("Missing.harmonia").getFullPathName().quoted()) == juce::File());

        const auto notAProject = song.getSiblingFile ("Notes.txt");
        REQUIRE (notAProject.replaceWithText ("hello"));
        CHECK (project_files::fromCommandLine (notAProject.getFullPathName().quoted()) == juce::File());

        folder.getFile().deleteRecursively();
    }
}
