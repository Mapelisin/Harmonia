#include <doctest.h>

#include "TestAudioFiles.h"
#include "audio/Export.h"
#include "audio/ExportJob.h"
#include "core/Project.h"
#include "core/Tracks.h"
#include "ui/MainComponent.h"

using namespace harmonia;
using namespace harmonia::ui;
using audio::ExportJob;

namespace
{
    /** A song of one audio track with three seconds of a tone, in a main window. */
    struct Window
    {
        Window()
        {
            auto* track = project->getTracks().getFirst();
            REQUIRE (track != nullptr);
            core::tracks::setKind (*track, core::tracks::Kind::audio);

            juce::String problem;
            REQUIRE (project->addAudioClip (*track, test::writeTone (temp.getFile ("Tone.wav"), 3.0, 0.3f), 0.0, problem) != nullptr);
            project->clearUndoHistory();

            main = std::make_unique<MainComponent> (test::session(), *project);
            main->setBounds (0, 0, 1280, 800);
        }

        ~Window()
        {
            main.reset();
        }

        /** The export in the window, to its end. */
        void finish()
        {
            while (main->getExportProgress().advance (5))
            {}
        }

        test::TempFolder temp;
        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        std::unique_ptr<MainComponent> main;
        int ended = 0;
        juce::String error;
        juce::Array<juce::File> files;

        std::function<void (ExportJob&)> onDone()
        {
            return [this] (ExportJob& job)
            {
                ++ended;
                error = job.getError();
                files = job.getFiles();
            };
        }
    };
}

TEST_SUITE ("ui: export progress")
{
    TEST_CASE ("an export shows over the whole window how far it is, keeps the keys from the song, and ends by itself")
    {
        Window window;
        auto& main = *window.main;
        auto& progress = main.getExportProgress();
        CHECK_FALSE (main.isExporting());
        CHECK_FALSE (progress.isVisible());

        const auto file = window.temp.getFile ("Song.wav");
        main.runExport (ExportJob::song (window.project->getEdit(), file, {}), window.onDone());

        CHECK (main.isExporting());
        CHECK (progress.isRunning());
        CHECK (progress.isVisible());
        CHECK (progress.getBounds() == main.getLocalBounds());
        CHECK (progress.isCurrentlyModal (false));
        CHECK (progress.getStage() == "Rendering the song");
        CHECK (progress.getProgress() == 0.0f);
        CHECK_FALSE (main.getTransport().isPlaying());

        // The keys do nothing to the song meanwhile: no play, no undo, no export on top of it.
        CHECK (main.keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)));
        CHECK_FALSE (main.getTransport().isPlaying());
        CHECK (main.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
        CHECK (main.keyPressed (juce::KeyPress ('a', juce::ModifierKeys::ctrlModifier, 0)));
        CHECK (main.keyPressed (juce::KeyPress (juce::KeyPress::F1Key)));
        CHECK_FALSE (main.areShortcutsShown());
        CHECK (window.project->getTracks().getFirst()->getClips().size() == 1);

        for (int i = 0; i < 200 && progress.getProgress() <= 0.0f; ++i)
            REQUIRE (progress.advance (5));

        CHECK (progress.getProgress() > 0.0f);
        CHECK (progress.getProgress() < 1.0f);
        CHECK (window.ended == 0);

        // It paints. Set HARMONIA_SNAPSHOT_DIR to look at it without opening a window.
        const auto image = main.createComponentSnapshot (main.getLocalBounds(), true, 1.0f);
        CHECK (image.isValid());

        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            const auto png = juce::File (dir).getChildFile ("main-exporting.png");
            png.deleteFile();
            juce::FileOutputStream out (png);
            REQUIRE (out.openedOk());
            juce::PNGImageFormat().writeImageToStream (image, out);
        }

        window.finish();
        CHECK (window.ended == 1);
        CHECK_MESSAGE (window.error.isEmpty(), window.error.toStdString());
        REQUIRE (window.files.size() == 1);
        CHECK (window.files.getFirst() == file);
        CHECK (file.getSize() > 100000);

        CHECK_FALSE (main.isExporting());
        CHECK_FALSE (progress.isVisible());
        CHECK_FALSE (progress.isCurrentlyModal (false));
        CHECK (juce::Component::getCurrentlyModalComponent() == nullptr);
        CHECK_FALSE (window.project->hasUnsavedChanges());

        // The keys are the song's again.
        CHECK (main.keyPressed (juce::KeyPress (juce::KeyPress::F1Key)));
        CHECK (main.areShortcutsShown());
        main.showShortcuts (false);
    }

    TEST_CASE ("it exports by itself, a slice at a time: the window's other work goes on in between")
    {
        struct Ticker final : juce::Timer
        {
            void timerCallback() override   { ++ticks; }
            int ticks = 0;
        };

        Window window;
        auto& main = *window.main;
        const auto file = window.temp.getFile ("Song.wav");

        Ticker ticker;
        ticker.startTimer (5);
        auto messages = 0;
        main.runExport (ExportJob::song (window.project->getEdit(), file, {}), window.onDone());
        REQUIRE (main.isExporting());

        // Nobody steps it here: its own timer does.
        const auto started = juce::Time::getMillisecondCounter();

        while (main.isExporting() && juce::Time::getMillisecondCounter() - started < 30000)
        {
            juce::MessageManager::callAsync ([&messages] { ++messages; });
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        }

        ticker.stopTimer();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);     // the last of those messages
        CHECK_FALSE (main.isExporting());
        CHECK (window.ended == 1);
        CHECK_MESSAGE (window.error.isEmpty(), window.error.toStdString());
        CHECK (file.getSize() > 100000);
        CHECK (ticker.ticks >= 2);
        CHECK (messages >= 2);
    }

    TEST_CASE ("Cancel and Esc stop an export: no file, a word in the status bar, and the next export works")
    {
        Window window;
        auto& main = *window.main;
        auto& progress = main.getExportProgress();
        const auto file = window.temp.getFile ("Song.flac");
        const audio::ExportFormat flac { audio::ExportFormat::Type::flac, 24 };

        main.runExport (ExportJob::song (window.project->getEdit(), file, flac), window.onDone());
        REQUIRE (progress.advance (5));
        REQUIRE (progress.getCancelButton().onClick != nullptr);
        progress.getCancelButton().onClick();

        CHECK_FALSE (main.isExporting());
        CHECK_FALSE (progress.isVisible());
        CHECK (juce::Component::getCurrentlyModalComponent() == nullptr);
        CHECK (window.ended == 0);
        CHECK (main.getStatusBar().getMessage().contains ("Export cancelled"));
        CHECK_FALSE (file.existsAsFile());
        CHECK (window.temp.folder.findChildFiles (juce::File::findFiles, false).size() == 1);     // the tone only
        CHECK_FALSE (progress.advance (5));

        // Esc.
        main.runExport (ExportJob::song (window.project->getEdit(), file, flac), window.onDone());
        REQUIRE (progress.advance (5));
        CHECK (main.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
        CHECK_FALSE (main.isExporting());
        CHECK (window.ended == 0);
        CHECK_FALSE (file.existsAsFile());

        // The window closed while it exports: the same.
        main.runExport (ExportJob::song (window.project->getEdit(), file, flac), window.onDone());
        REQUIRE (progress.advance (5));
        window.main.reset();
        CHECK (juce::Component::getCurrentlyModalComponent() == nullptr);
        CHECK_FALSE (file.existsAsFile());
        CHECK_FALSE (window.project->getEdit().isRendering());
        CHECK_FALSE (window.project->hasUnsavedChanges());

        // And then it exports as ever.
        window.main = std::make_unique<MainComponent> (test::session(), *window.project);
        window.main->setBounds (0, 0, 1280, 800);
        window.main->runExport (ExportJob::song (window.project->getEdit(), file, flac), window.onDone());
        window.finish();
        CHECK (window.ended == 1);
        CHECK_MESSAGE (window.error.isEmpty(), window.error.toStdString());
        CHECK (file.getSize() > 10000);
    }

    TEST_CASE ("what cannot be exported is said at once, without the progress")
    {
        Window window;
        auto& main = *window.main;
        window.project->getTracks().getFirst()->setMute (true);

        main.runExport (ExportJob::song (window.project->getEdit(), window.temp.getFile ("Song.wav"), {}), window.onDone());
        CHECK (window.ended == 1);
        CHECK (window.error == "Nothing to export: every track is muted");
        CHECK_FALSE (main.isExporting());
        CHECK_FALSE (main.getExportProgress().isVisible());
        CHECK (juce::Component::getCurrentlyModalComponent() == nullptr);
    }

    TEST_CASE ("stems in the window: the stem it is at is named")
    {
        Window window;
        auto& main = *window.main;
        window.project->getTracks().getFirst()->setName ("Guitar");

        audio::StemsOptions options;
        options.songName = "Song";
        main.runExport (ExportJob::stems (window.project->getEdit(), window.temp.getFile ("Stems"), options), window.onDone());
        REQUIRE (main.isExporting());
        CHECK (main.getExportProgress().getStage() == "Stem 1 of 1: Guitar");

        window.finish();
        CHECK (window.ended == 1);
        CHECK_MESSAGE (window.error.isEmpty(), window.error.toStdString());
        REQUIRE (window.files.size() == 1);
        CHECK (window.files.getFirst().getFileName() == "Song - 01 Guitar.wav");
        CHECK (window.files.getFirst().existsAsFile());
    }
}
