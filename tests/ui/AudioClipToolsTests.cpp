#include <doctest.h>

#include "TestAudioFiles.h"
#include "core/Clips.h"
#include "core/TestProject.h"
#include "core/Tracks.h"
#include "ui/MainComponent.h"

using namespace harmonia;
namespace clips = core::clips;

namespace
{
    /** An audio track holding one clip of a test sound. */
    struct SoundClip
    {
        explicit SoundClip (float level = 0.5f, bool beats = false, double seconds = 2.0)
        {
            file = test::writeTone (temp.getFile ("Sound.wav"), seconds, level, 1, beats);
            track = project->addTrack ("Audio");
            core::tracks::setKind (*track, core::tracks::Kind::audio);
            juce::String error;
            clip = project->addAudioClip (*track, file, 0.0, error);
            REQUIRE (clip != nullptr);
            project->clearUndoHistory();
        }

        ~SoundClip()
        {
            project.reset();
        }

        test::TempFolder temp;
        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        juce::File file;
        te::AudioTrack* track = nullptr;
        te::WaveAudioClip* clip = nullptr;
    };

    bool runItem (const juce::PopupMenu& menu, const juce::String& text)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
        {
            const auto& item = it.getItem();

            if (item.text == text && item.isEnabled && item.action != nullptr)
            {
                item.action();
                return true;
            }
        }

        return false;
    }

    std::optional<bool> isTicked (const juce::PopupMenu& menu, const juce::String& text)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
            if (it.getItem().text == text)
                return it.getItem().isTicked;

        return std::nullopt;
    }

    /** Lets the engine write a reversed copy (in the background); false if it takes too long. */
    bool waitWhileReversing (const te::AudioClipBase& clip)
    {
        const auto end = juce::Time::getMillisecondCounterHiRes() + 15000.0;

        while (clips::isReversing (clip))
        {
            if (juce::Time::getMillisecondCounterHiRes() > end)
                return false;

            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        }

        return true;
    }

    std::vector<float> readSamples (const juce::File& file)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));

        if (reader == nullptr)
            return {};

        juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
        return { buffer.getReadPointer (0), buffer.getReadPointer (0) + buffer.getNumSamples() };
    }
}

TEST_SUITE ("audio clip tools")
{
    TEST_CASE ("the peak is measured over the part the clip plays; normalize brings it to 0 dB in one undo step")
    {
        SoundClip f (0.5f, true);     // hits every half second: the first of four at 0.5, the others at 0.275

        const auto whole = clips::getPeakDb (*f.clip);
        REQUIRE (whole.has_value());
        CHECK (*whole == doctest::Approx (-6.02).epsilon (0.02));

        // Only from 0.6 s to 1.4 s: the quieter hits at 1.0 s.
        f.clip->setPosition (te::ClipPosition { te::TimeRange (te::TimePosition::fromSeconds (0.6), te::TimePosition::fromSeconds (1.4)),
                                                te::TimeDuration::fromSeconds (0.6) });
        const auto part = clips::getPeakDb (*f.clip);
        REQUIRE (part.has_value());
        CHECK (*part == doctest::Approx (-11.25).epsilon (0.02));

        f.project->clearUndoHistory();
        f.project->beginGesture ("Normalize");
        CHECK (clips::normalize (*f.clip));
        CHECK (clips::getGainDb (*f.clip) == doctest::Approx (-0.1f - *part).epsilon (0.001));
        CHECK (clips::getGainDb (*f.clip) > clips::maxGainDb);     // past what the handle reaches

        f.project->undo();
        CHECK (clips::getGainDb (*f.clip) == doctest::Approx (0.0f));
    }

    TEST_CASE ("silence can't be normalized; a missing file has no peak")
    {
        SoundClip silent (0.0f);
        CHECK (clips::getPeakDb (*silent.clip) == doctest::Approx (clips::silentGainDb));
        CHECK_FALSE (clips::normalize (*silent.clip));
        CHECK (clips::getGainDb (*silent.clip) == doctest::Approx (0.0f));

        SoundClip gone;
        gone.project->getEdit().engine.getAudioFileManager().releaseAllFiles();
        gone.file.deleteFile();
        CHECK_FALSE (clips::getPeakDb (*gone.clip).has_value());
        CHECK_FALSE (clips::normalize (*gone.clip));
    }

    TEST_CASE ("reverse: the engine writes the sound backwards; undo plays it forwards again")
    {
        SoundClip f (0.5f, true);
        const auto original = readSamples (f.file);
        REQUIRE (original.size() > 1000);

        f.project->beginGesture ("Reverse");
        clips::setReversed (*f.clip, true);
        CHECK (clips::isReversed (*f.clip));
        REQUIRE (waitWhileReversing (*f.clip));

        const auto reversedFile = f.clip->getCurrentSourceFile();
        CHECK (reversedFile != f.file);
        const auto reversed = readSamples (reversedFile);
        REQUIRE (reversed.size() == original.size());

        for (size_t i : { (size_t) 0, (size_t) 100, (size_t) 22050, original.size() - 1 })
            CHECK (reversed[i] == doctest::Approx (original[original.size() - 1 - i]).epsilon (0.001));

        // The same part is measured either way.
        CHECK (clips::getPeakDb (*f.clip).value_or (0.0f) == doctest::Approx (-6.02).epsilon (0.02));

        f.project->undo();
        CHECK_FALSE (clips::isReversed (*f.clip));
        CHECK (f.clip->getCurrentSourceFile() == f.file);
        f.project->redo();
        CHECK (clips::isReversed (*f.clip));
        REQUIRE (waitWhileReversing (*f.clip));
    }

    TEST_CASE ("the clip menu: normalize and reverse; the main window paints a reversed clip")
    {
        SoundClip f (0.3f, true, 4.0);
        auto main = std::make_unique<ui::MainComponent> (test::session(), *f.project);
        main->setBounds (0, 0, 1280, 800);
        auto& arrangement = main->getArrangement();

        CHECK (isTicked (arrangement.createClipMenu (*f.clip), "Reverse (play backwards)") == false);
        CHECK (runItem (arrangement.createClipMenu (*f.clip), "Normalize (loudest point to 0 dB)"));
        CHECK (clips::getGainDb (*f.clip) == doctest::Approx (-0.1f + 10.46f).epsilon (0.02));

        CHECK (runItem (arrangement.createClipMenu (*f.clip), "Reverse (play backwards)"));
        CHECK (clips::isReversed (*f.clip));
        CHECK (isTicked (arrangement.createClipMenu (*f.clip), "Reverse (play backwards)") == true);

        // While the copy is written the clip says so; then its waveform shows.
        CHECK (main->createComponentSnapshot (main->getLocalBounds()).isValid());
        REQUIRE (waitWhileReversing (*f.clip));

        auto& clipArea = arrangement.getClipArea();
        clipArea.repaint();

        for (int i = 0; i < 200 && ! clipArea.areWaveformsLoaded(); ++i)
        {
            (void) main->createComponentSnapshot (main->getLocalBounds());
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        }

        CHECK (clipArea.areWaveformsLoaded());
        const auto image = main->createComponentSnapshot (main->getLocalBounds());
        CHECK (image.isValid());

        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("main-reverse.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }

        // Two undo steps: forwards again, then the gain back.
        f.project->undo();
        CHECK_FALSE (clips::isReversed (*f.clip));
        f.project->undo();
        CHECK (clips::getGainDb (*f.clip) == doctest::Approx (0.0f));

        main.reset();
    }
}
