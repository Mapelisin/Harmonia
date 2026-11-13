#include <doctest.h>

#include "TestSession.h"
#include "audio/Groups.h"
#include "audio/Instruments.h"
#include "core/Project.h"
#include "ui/StemsOptions.h"

using namespace harmonia;
using audio::ExportFormat;

namespace
{
    /** A song with "Drums" and "Bass" playing through a group "Rhythm", and "Keys" on its own:
        each a track with a clip. */
    struct Song
    {
        Song()
        {
            drums = project->getTracks().getFirst();
            drums->setName ("Drums");
            bass = project->addTrack ("Bass");
            keys = project->addTrack ("Keys");

            for (auto* track : { drums, bass, keys })
            {
                REQUIRE (audio::instruments::useBuiltInSynth (*track).isEmpty());
                REQUIRE (project->addMidiClip (*track, 0.0, 4.0, track->getName()) != nullptr);
            }

            rhythm = audio::groups::addGroup (*project, { drums, bass }, "Rhythm");
            REQUIRE (rhythm != nullptr);
            project->clearUndoHistory();
        }

        std::unique_ptr<ui::StemsOptions> makeWindow (const juce::String& pattern = audio::defaultStemPattern, const ExportFormat& format = {})
        {
            return std::make_unique<ui::StemsOptions> (audio::getStemChoices (project->getEdit()), "My song", 96.0, pattern, format);
        }

        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        te::AudioTrack* drums = nullptr;
        te::AudioTrack* bass = nullptr;
        te::AudioTrack* keys = nullptr;
        te::AudioTrack* rhythm = nullptr;
    };
}

TEST_SUITE ("ui::StemsOptions")
{
    TEST_CASE ("the stems window lists the tracks and groups, the tracks ticked; Export hands on what is ticked and written")
    {
        Song song;
        auto window = song.makeWindow();
        REQUIRE (window->getNumChoices() == 4);
        CHECK (window->getTick (0).getButtonText() == "Drums");
        CHECK (window->getTick (2).getButtonText() == "Keys");
        CHECK (window->getTick (3).getButtonText() == "Rhythm  (group)");
        CHECK (window->getTick (0).getToggleState());
        CHECK (window->getTick (2).getToggleState());
        CHECK_FALSE (window->getTick (3).getToggleState());        // a group is a stem when asked for
        CHECK (window->getExample() == "For example: My song - 01 Drums.wav");
        CHECK (window->getExportButton().isEnabled());

        std::optional<audio::StemsOptions> exported;
        auto cancelled = false;
        window->onExport = [&exported] (const audio::StemsOptions& options) { exported = options; };
        window->onCancel = [&cancelled] { cancelled = true; };

        // As it opens: the three tracks, WAV 24-bit, the usual names.
        window->getExportButton().onClick();
        REQUIRE (exported.has_value());
        CHECK (exported->tracks == juce::Array<te::AudioTrack*> { song.drums, song.bass, song.keys });
        CHECK (exported->songName == "My song");
        CHECK (exported->namePattern == audio::defaultStemPattern);
        CHECK (exported->format == ExportFormat());
        CHECK_FALSE (cancelled);

        // The group instead of its two tracks, named another way, as FLAC.
        window->getTick (0).setToggleState (false, juce::sendNotificationSync);
        window->getTick (1).setToggleState (false, juce::sendNotificationSync);
        window->getTick (3).setToggleState (true, juce::sendNotificationSync);
        window->getPatternEditor().setText ("{n} {track} {bpm}", true);
        auto& formats = window->getFormatBox();

        for (int i = 0; i < formats.getNumItems(); ++i)
            if (formats.getItemText (i) == ExportFormat { ExportFormat::Type::flac, 24 }.describe())
                formats.setSelectedId (formats.getItemId (i), juce::sendNotificationSync);

        CHECK (window->getExample() == "For example: 03 Keys 96.flac");
        window->getExportButton().onClick();
        CHECK (exported->tracks == juce::Array<te::AudioTrack*> { song.keys, song.rhythm });
        CHECK (exported->namePattern == "{n} {track} {bpm}");
        CHECK (exported->format == ExportFormat { ExportFormat::Type::flac, 24 });
    }

    TEST_CASE ("the stems window asks the sample rate too: the device's at first, the one chosen goes with the rest")
    {
        Song song;
        const auto choices = audio::getStemChoices (song.project->getEdit());
        auto window = std::make_unique<ui::StemsOptions> (choices, "My song", 96.0, audio::defaultStemPattern, ExportFormat(), 0.0, 48000.0);
        auto& rates = window->getSampleRateBox();
        CHECK (rates.getRate() == 0.0);
        CHECK (rates.getText() == "As the audio device plays (48000 Hz)");

        std::optional<audio::StemsOptions> exported;
        window->onExport = [&exported] (const audio::StemsOptions& options) { exported = options; };
        window->getExportButton().onClick();
        REQUIRE (exported.has_value());
        CHECK (exported->sampleRate == 0.0);

        rates.setRate (96000.0);
        CHECK (rates.getText() == "96000 Hz");
        window->getExportButton().onClick();
        CHECK (exported->sampleRate == 96000.0);

        // Opened again: as the last export was.
        window = std::make_unique<ui::StemsOptions> (choices, "My song", 96.0, audio::defaultStemPattern, ExportFormat(), 44100.0, 48000.0);
        CHECK (window->getSampleRateBox().getRate() == 44100.0);
        CHECK (window->getSampleRateBox().getText() == "44100 Hz (CD, streaming)");
    }

    TEST_CASE ("All and None tick every row or none; with nothing ticked there is nothing to export")
    {
        Song song;
        auto window = song.makeWindow ("{track}", { ExportFormat::Type::wav, 16 });
        CHECK (window->getPatternEditor().getText() == "{track}");
        CHECK (window->getFormatBox().getText() == ExportFormat { ExportFormat::Type::wav, 16 }.describe());

        window->getNoneButton().onClick();

        for (int i = 0; i < window->getNumChoices(); ++i)
            CHECK_FALSE (window->getTick (i).getToggleState());

        CHECK_FALSE (window->getExportButton().isEnabled());
        CHECK (window->getExample() == "Tick at least one track");

        window->getAllButton().onClick();

        for (int i = 0; i < window->getNumChoices(); ++i)
            CHECK (window->getTick (i).getToggleState());

        CHECK (window->getExportButton().isEnabled());
        CHECK (window->getOptions().tracks.size() == 4);

        // An empty pattern is the usual one. (The editor tells of its change a moment later.)
        window->getPatternEditor().setText ("", true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        CHECK (window->getExample() == "For example: My song - 01 Drums.wav");

        // It lays itself out and paints.
        window->setBounds (0, 0, window->getWidth(), window->getHeight());
        const auto picture = window->createComponentSnapshot (window->getLocalBounds());
        CHECK (picture.getWidth() == window->getWidth());

        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            const auto file = juce::File (dir).getChildFile ("stems-options.png");
            file.deleteFile();
            juce::FileOutputStream stream (file);
            juce::PNGImageFormat().writeImageToStream (picture, stream);
        }
    }
}
