#include <doctest.h>

#include "audio/Export.h"
#include "audio/Instruments.h"
#include "core/TestProject.h"
#include "ui/MainComponent.h"

using namespace harmonia;
using namespace harmonia::ui;

namespace
{
    struct FakeFileCommands final : FileCommands
    {
        void newProject() override          {}
        void openProject() override         {}
        void saveProject() override         {}
        void saveProjectAs() override       {}
        void exportSong() override          {}
        void exportStems() override         {}
        void exportLoop() override          { ++loopExports; }
        juce::String getProjectName() const override    { return "Song"; }

        int loopExports = 0;
    };

    struct MenuItem
    {
        juce::String text;
        bool enabled = false;
        std::function<void()> action;
    };

    std::optional<MenuItem> findItem (const juce::PopupMenu& menu, const juce::String& start)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            if (const auto& item = it.getItem(); item.text.startsWith (start))
                return MenuItem { item.text, item.isEnabled, item.action };

        return std::nullopt;
    }

    /** A song of two bars at 120 BPM (4 seconds) with a chord all through. */
    std::unique_ptr<core::Project> makeSong()
    {
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();
        REQUIRE (audio::instruments::useBuiltInSynth (track).isEmpty());

        auto* clip = project->addMidiClip (track, 0.0, 8.0, "Chord");
        REQUIRE (clip != nullptr);

        for (int pitch : { 60, 64, 67 })
            clip->getSequence().addNote (pitch, te::BeatPosition::fromBeats (0.0), te::BeatDuration::fromBeats (8.0), 110, 0, nullptr);

        return project;
    }
}

TEST_SUITE ("exporting the loop region")
{
    TEST_CASE ("a part of the song is exported exactly, with nothing added to ring out")
    {
        auto project = makeSong();
        juce::TemporaryFile file (".wav");
        const te::TimeRange part (te::TimePosition::fromSeconds (1.0), te::TimePosition::fromSeconds (3.0));
        const auto error = audio::exportWav (project->getEdit(), file.getFile(), part);
        REQUIRE_MESSAGE (error.isEmpty(), error.toStdString());

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file.getFile()));
        REQUIRE (reader != nullptr);
        CHECK ((double) reader->lengthInSamples / reader->sampleRate == doctest::Approx (2.0).epsilon (0.002));

        juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, true);
        CHECK (buffer.getMagnitude (0, 1000) > 0.01f);     // the chord already sounds at the start
        CHECK (buffer.getMagnitude (buffer.getNumSamples() - 1000, 1000) > 0.01f);

        // An empty part, or one before the song's start: nothing.
        juce::TemporaryFile other (".wav");
        CHECK (audio::exportWav (project->getEdit(), other.getFile(),
                                 te::TimeRange (te::TimePosition::fromSeconds (1.0), te::TimePosition::fromSeconds (1.0)))
                   .contains ("Nothing to export"));
        CHECK_FALSE (other.getFile().existsAsFile());
    }

    TEST_CASE ("names for a stretch of the song: bars, or beats when it isn't whole bars")
    {
        CHECK (MainComponent::describeBars ({ 16.0, 32.0 }, 4.0) == "bars 5-8");
        CHECK (MainComponent::describeBars ({ 8.0, 12.0 }, 4.0) == "bar 3");
        CHECK (MainComponent::describeBars ({ 0.0, 4.0 }, 4.0) == "bar 1");
        CHECK (MainComponent::describeBars ({ 0.0, 6.0 }, 3.0) == "bars 1-2");
        CHECK (MainComponent::describeBars ({ 1.5, 4.0 }, 4.0) == "beats 2.50-5");
    }

    TEST_CASE ("the File menu exports the loop region once there is one")
    {
        auto project = makeSong();
        MainComponent main (test::session(), *project);
        main.setBounds (0, 0, 1280, 800);
        FakeFileCommands commands;
        main.setFileCommands (&commands);

        auto item = findItem (main.createFileMenu(), "Export the loop region");
        REQUIRE (item.has_value());
        CHECK_FALSE (item->enabled);
        CHECK (item->text.contains ("Ctrl+drag"));

        main.getArrangementModel().setLoopRegion ({ 4.0, 8.0 });
        item = findItem (main.createFileMenu(), "Export the loop region");
        REQUIRE (item.has_value());
        CHECK (item->enabled);
        CHECK (item->text == "Export the loop region (bar 2)...");
        REQUIRE (item->action != nullptr);
        item->action();
        CHECK (commands.loopExports == 1);

        main.setFileCommands (nullptr);
    }
}
