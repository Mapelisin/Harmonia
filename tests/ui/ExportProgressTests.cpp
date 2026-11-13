#include <doctest.h>

#include "TestAudioFiles.h"
#include "audio/Effects.h"
#include "audio/Export.h"
#include "audio/ExportJob.h"
#include "audio/Freeze.h"
#include "audio/Limiter.h"
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

    audio::limiter::Plugin* findLimiter (te::Edit& edit)
    {
        for (auto* plugin : audio::effects::get (edit.getMasterPluginList()))
            if (auto* found = dynamic_cast<audio::limiter::Plugin*> (plugin))
                return found;

        return nullptr;
    }
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

    TEST_CASE ("freezing a track from its header goes on under the progress: Esc leaves the track as it was; left alone, it freezes")
    {
        Window window;
        auto& main = *window.main;
        auto& progress = main.getExportProgress();
        auto& track = *window.project->getTracks().getFirst();
        track.setName ("Guitar");
        window.project->clearUndoHistory();
        auto& headers = main.getArrangement().getHeaders();

        headers.toggleFrozen (track);
        REQUIRE (main.isExporting());
        CHECK (progress.isVisible());
        CHECK (progress.getStage() == "Freezing Guitar");
        REQUIRE (progress.advance (0));
        CHECK_FALSE (audio::freeze::isFrozen (track));

        CHECK (main.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
        CHECK_FALSE (main.isExporting());
        CHECK_FALSE (progress.isVisible());
        CHECK (juce::Component::getCurrentlyModalComponent() == nullptr);
        CHECK_FALSE (audio::freeze::isFrozen (track));
        CHECK (main.getStatusBar().getMessage() == "Freezing Guitar was cancelled: the track is as it was");
        CHECK_FALSE (window.project->canUndo());

        headers.toggleFrozen (track);
        REQUIRE (main.isExporting());
        window.finish();
        CHECK_FALSE (main.isExporting());
        CHECK (audio::freeze::isFrozen (track));
        CHECK (main.getArrangementModel().isFrozen (&track));
        CHECK (main.getStatusBar().getMessage() == "Guitar frozen: it plays as audio, its instrument and effects are off");
        CHECK (window.project->getUndoDescription() == "Freeze track");

        // Unfreezing renders nothing: done at once, no progress.
        headers.toggleFrozen (track);
        CHECK_FALSE (main.isExporting());
        CHECK_FALSE (audio::freeze::isFrozen (track));
        CHECK (main.getStatusBar().getMessage() == "Guitar unfrozen");
    }

    TEST_CASE ("a track rendered to audio and one clip made of several: under the progress when sound is rendered, at once when it is notes")
    {
        Window window;
        auto& main = *window.main;
        auto& progress = main.getExportProgress();
        auto& model = main.getArrangementModel();
        auto& track = *window.project->getTracks().getFirst();
        track.setName ("Guitar");
        auto& headers = main.getArrangement().getHeaders();

        headers.renderToAudio (track);
        REQUIRE (main.isExporting());
        CHECK (progress.getStage() == "Rendering Guitar");
        REQUIRE (progress.advance (0));
        progress.cancel();
        CHECK_FALSE (main.isExporting());
        CHECK (window.project->getTracks().size() == 1);
        CHECK (main.getStatusBar().getMessage() == "Rendering Guitar to audio was cancelled: nothing was added");
        CHECK_FALSE (track.isMuted (false));

        headers.renderToAudio (track);
        window.finish();
        REQUIRE (window.project->getTracks().size() == 2);
        CHECK (main.getStatusBar().getMessage() == "Rendered Guitar to audio (the track is muted)");
        CHECK (track.isMuted (false));
        CHECK (window.project->getUndoDescription() == "Render to audio");

        // Ctrl+J on the new audio clip (it is the selected one): its sound is rendered first.
        REQUIRE (model.getSelectedClips().size() == 1);
        auto* rendered = model.getSelectedClips().getFirst();
        main.getArrangement().consolidate();
        REQUIRE (main.isExporting());
        CHECK (progress.getStage() == "Consolidating Guitar (audio)");
        REQUIRE (progress.advance (0));
        progress.cancel();
        CHECK (main.getStatusBar().getMessage() == "Consolidating was cancelled: the clips are as they were");
        REQUIRE (model.getSelectedClips().size() == 1);
        CHECK (model.getSelectedClips().getFirst() == rendered);
        CHECK (window.project->getUndoDescription() == "Render to audio");

        main.getArrangement().consolidate();
        REQUIRE (main.isExporting());
        window.finish();
        CHECK (main.getStatusBar().getMessage().startsWith ("One clip made of 1 clip."));
        CHECK (window.project->getUndoDescription() == "Consolidate");

        // Notes: joined at once, no progress.
        auto* notes = window.project->addTrack ("Notes");
        REQUIRE (notes != nullptr);
        auto* first = window.project->addMidiClip (*notes, 0.0, 4.0, "A");
        auto* second = window.project->addMidiClip (*notes, 4.0, 4.0, "B");
        REQUIRE (first != nullptr);
        REQUIRE (second != nullptr);
        model.setSelection ({ first, second });
        main.getArrangement().consolidate();
        CHECK_FALSE (main.isExporting());
        CHECK (main.getStatusBar().getMessage().startsWith ("One clip made of 2 clips."));
    }

    TEST_CASE ("Check loudness, Mix Doctor and the limiter from the window: under the progress; cancelled, the song is as it was")
    {
        Window window;
        auto& main = *window.main;
        auto& progress = main.getExportProgress();
        auto& edit = window.project->getEdit();
        const auto faderBefore = edit.getMasterVolumePlugin()->getVolumeDb();

        main.checkLoudness();
        REQUIRE (main.isExporting());
        CHECK (progress.getStage() == "Measuring the song");
        REQUIRE (progress.advance (0));
        progress.cancel();
        CHECK_FALSE (main.isExporting());
        CHECK (main.getStatusBar().getMessage() == "Loudness check cancelled");

        main.checkMix();
        REQUIRE (main.isExporting());
        CHECK (progress.getStage() == "Mix Doctor: listening to the mix");

        while (progress.getStage() == "Mix Doctor: listening to the mix")
            REQUIRE (progress.advance (5));

        CHECK (progress.getStage() == "Mix Doctor: listening to track 1 of 1");
        CHECK (progress.getProgress() >= 0.5f);
        progress.cancel();
        CHECK (main.getStatusBar().getMessage() == "Mix Doctor cancelled");
        CHECK_FALSE (window.project->canUndo());
        CHECK_FALSE (edit.isRendering());

        // The limiter: stopped once it is on the master, it is taken off again.
        main.raiseWithLimiter();
        REQUIRE (main.isExporting());

        while (findLimiter (edit) == nullptr)
            REQUIRE (progress.advance (5));

        REQUIRE (main.isExporting());
        progress.cancel();
        CHECK (findLimiter (edit) == nullptr);
        CHECK (edit.getMasterVolumePlugin()->getVolumeDb() == doctest::Approx (faderBefore));
        CHECK (main.getStatusBar().getMessage() == "Limiter cancelled: the song is as it was");
        CHECK_FALSE (window.project->canUndo());

        // Left alone, it is one step to undo.
        main.raiseWithLimiter();
        window.finish();
        CHECK_FALSE (main.isExporting());
        CHECK (findLimiter (edit) != nullptr);
        CHECK (main.getStatusBar().getMessage().startsWith ("Limiter on the master at +"));
        CHECK (window.project->getUndoDescription() == "Limiter for streaming");
        window.project->undo();
        CHECK (findLimiter (edit) == nullptr);
        CHECK (edit.getMasterVolumePlugin()->getVolumeDb() == doctest::Approx (faderBefore));
    }
}
