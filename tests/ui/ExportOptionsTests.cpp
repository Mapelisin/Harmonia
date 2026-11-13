#include <doctest.h>

#include "TestSession.h"
#include "audio/Mp3Encoder.h"
#include "core/Project.h"
#include "ui/ExportOptions.h"

using namespace harmonia;
using audio::ExportFormat;
using audio::SongExport;
using ui::ExportOptions;

namespace
{
    /** Picks `format` in the list as a click would (the list tells its change at once). */
    void choose (ExportOptions& options, const ExportFormat& format)
    {
        auto& box = options.getFormatBox();

        for (int i = 0; i < box.getNumItems(); ++i)
        {
            if (box.getItemText (i) == format.describe())
            {
                box.setSelectedId (box.getItemId (i), juce::sendNotificationSync);
                return;
            }
        }

        FAIL ("No " << format.describe() << " in the list");
    }

    /** Picks the item of a list that says `text`, as a click would. */
    void pick (juce::ComboBox& box, const juce::String& text)
    {
        for (int i = 0; i < box.getNumItems(); ++i)
        {
            if (box.getItemText (i) == text)
            {
                box.setSelectedId (box.getItemId (i), juce::sendNotificationSync);
                return;
            }
        }

        FAIL ("No " << text << " in the list");
    }

    std::optional<juce::PopupMenu::Item> findItem (const juce::PopupMenu& menu, const juce::String& text)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
            if (it.getItem().text == text)
                return it.getItem();

        return std::nullopt;
    }

    /** Clicks the item of the second file's menu that says `text`. */
    void clickInMenu (ExportOptions& options, const juce::String& text)
    {
        const auto item = findItem (options.createWithoutMenu(), text);
        REQUIRE (item.has_value());
        REQUIRE (item->isEnabled);
        item->action();
    }
}

TEST_SUITE ("ui::ExportOptions")
{
    TEST_CASE ("the export window lists every format, starts at the one asked for and tells what it is for")
    {
        ExportOptions options (ExportFormat { ExportFormat::Type::flac, 16 });
        CHECK (options.getFormat() == ExportFormat ({ ExportFormat::Type::flac, 16 }));
        CHECK (options.getHint().contains ("half the size"));

        const auto choices = ExportFormat::getChoices();
        CHECK (options.getFormatBox().getNumItems() == choices.size());

        for (const auto& format : choices)
        {
            CAPTURE (format.describe());
            choose (options, format);
            CHECK (options.getFormat() == format);
            CHECK (options.getHint().startsWith (ExportOptions::describeUse (format)));
        }

        // Each kind of file has a line of its own.
        CHECK (ExportOptions::describeUse ({ ExportFormat::Type::wav, 16 }).contains ("CD"));
        CHECK (ExportOptions::describeUse ({ ExportFormat::Type::wav, 32 }).contains ("clipped"));
        CHECK (ExportOptions::describeUse ({ ExportFormat::Type::mp3, 16, 320 }).contains ("phone"));
        CHECK (ExportOptions::describeUse ({}) != ExportOptions::describeUse ({ ExportFormat::Type::wav, 16 }));

        // Without an encoder the window says why there is no MP3.
        CHECK (options.getHint().contains ("no MP3 encoder") == ! audio::mp3::isAvailable (320));
    }

    TEST_CASE ("a format the window doesn't offer opens it at WAV 24-bit")
    {
        ExportOptions options (ExportFormat { ExportFormat::Type::wav, 8 });
        CHECK (options.getFormat() == ExportFormat());
    }

    TEST_CASE ("Export hands on the format chosen, Cancel hands on nothing")
    {
        ExportOptions options ({});
        std::optional<ExportFormat> exported;
        auto cancelled = false;
        options.onExport = [&] (const SongExport& how) { exported = how.format; };
        options.onCancel = [&] { cancelled = true; };

        choose (options, { ExportFormat::Type::flac, 24 });
        options.getExportButton().triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        REQUIRE (exported.has_value());
        CHECK (*exported == ExportFormat ({ ExportFormat::Type::flac, 24 }));
        CHECK (! cancelled);
    }

    TEST_CASE ("for the loop region: its own title, what each format means for a loop, its own format remembered")
    {
        ExportOptions options ({}, audio::ExportUse::loop);
        CHECK (options.getTitle() == "Export the loop region as");
        CHECK (ExportOptions ({}).getTitle() == "Export the song as");
        CHECK (options.getHint().contains ("repeats seamlessly"));

        // An MP3 does not loop cleanly: said before the file is made.
        const ExportFormat mp3 { ExportFormat::Type::mp3, 16, 320 };
        CHECK (ExportOptions::describeUse (mp3, audio::ExportUse::loop).contains ("does not repeat"));
        CHECK_FALSE (ExportOptions::describeUse (mp3).contains ("does not repeat"));
        CHECK (ExportOptions::describeUse ({ ExportFormat::Type::flac, 24 }, audio::ExportUse::loop).contains ("half the size"));

        // The other WAVs are what they are for the song too.
        CHECK (ExportOptions::describeUse ({ ExportFormat::Type::wav, 16 }, audio::ExportUse::loop)
                   == ExportOptions::describeUse ({ ExportFormat::Type::wav, 16 }));

        // The song's format and the loop's are remembered apart.
        auto& session = test::session();
        const auto song = audio::getLastExportFormat (session);
        const auto loop = audio::getLastExportFormat (session, audio::ExportUse::loop);

        audio::setLastExportFormat (session, { ExportFormat::Type::flac, 16 }, audio::ExportUse::loop);
        audio::setLastExportFormat (session, { ExportFormat::Type::wav, 32 });
        CHECK (audio::getLastExportFormat (session, audio::ExportUse::loop) == ExportFormat ({ ExportFormat::Type::flac, 16 }));
        CHECK (audio::getLastExportFormat (session) == ExportFormat ({ ExportFormat::Type::wav, 32 }));

        audio::setLastExportFormat (session, song);
        audio::setLastExportFormat (session, loop, audio::ExportUse::loop);
    }

    TEST_CASE ("for the song: a sample rate, a loudness and a second file without tracks go with the format; a loop has only its format")
    {
        const auto project = core::Project::createEmpty (test::session());
        auto* drums = project->getTracks().getFirst();
        auto* vocals = project->addTrack ("Vocals");
        REQUIRE (drums != nullptr);
        REQUIRE (vocals != nullptr);
        drums->setName ("Drums");
        const juce::Array<te::AudioTrack*> tracks { drums, vocals };

        // At first: the song as it is, at the device's rate, and one file.
        ExportOptions options ({}, audio::ExportUse::song, tracks, 48000.0);
        auto how = options.getOptions();
        CHECK (how.format == ExportFormat());
        CHECK (how.sampleRate == 0.0);
        CHECK_FALSE (how.loudnessLufs.has_value());
        CHECK (how.without.isEmpty());
        CHECK (options.getSampleRateBox().isVisible());
        CHECK (options.getSampleRateBox().getText() == "As the audio device plays (48000 Hz)");
        CHECK (options.getSampleRateBox().getNumItems() == 1 + (int) audio::exportSampleRates.size());
        CHECK (options.getLoudnessBox().getText() == "As the song is");
        CHECK (options.getLoudnessBox().getNumItems() == 1 + (int) audio::loudnessTargets.size());
        CHECK (options.getWithoutButton().getButtonText() == "None");
        CHECK (options.getWithoutButton().isEnabled());
        CHECK (options.getHint().startsWith (ExportOptions::describeUse ({})));
        CHECK_FALSE (options.getHint().contains ("Loudness"));
        CHECK_FALSE (options.getHint().contains ("Second file"));

        // A rate and a loudness from the lists; the hint says what the loudness does.
        pick (options.getSampleRateBox(), "44100 Hz (CD, streaming)");
        pick (options.getLoudnessBox(), "-14 LUFS (Spotify, YouTube, Tidal)");
        how = options.getOptions();
        CHECK (how.sampleRate == 44100.0);
        CHECK (how.loudnessLufs == std::optional<double> (-14.0));
        CHECK (options.getHint().contains ("turned up or down to -14 LUFS"));
        CHECK (options.getHint().contains ("under -1 dB"));
        CHECK (options.getHint().contains ("The song itself stays as it is"));

        // The second file: a tick in its menu for each track to leave out.
        CHECK (findItem (options.createWithoutMenu(), "Drums").has_value());
        CHECK_FALSE (findItem (options.createWithoutMenu(), "Vocals")->isTicked);
        CHECK_FALSE (findItem (options.createWithoutMenu(), "No second file")->isEnabled);

        clickInMenu (options, "Vocals");
        CHECK (options.getWithout() == juce::Array<te::AudioTrack*> ({ vocals }));
        CHECK (findItem (options.createWithoutMenu(), "Vocals")->isTicked);
        CHECK (options.getWithoutButton().getButtonText() == "The song without Vocals");
        CHECK (options.getHint().contains ("its name with \"(no Vocals)\" added, turned up or down as the first is."));

        // Another: top to bottom as the song has them, whatever the order of the clicks.
        clickInMenu (options, "Drums");
        CHECK (options.getWithout() == tracks);
        CHECK (options.getWithoutButton().getButtonText() == "The song without 2 tracks");
        CHECK (options.getHint().contains ("\"(minus)\""));

        // A tick taken back.
        clickInMenu (options, "Vocals");
        CHECK (options.getWithout() == juce::Array<te::AudioTrack*> ({ drums }));

        // Export hands all of it on.
        std::optional<SongExport> exported;
        options.onExport = [&] (const SongExport& chosen) { exported = chosen; };
        choose (options, { ExportFormat::Type::flac, 24 });
        options.getExportButton().triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        REQUIRE (exported.has_value());
        CHECK (exported->format == ExportFormat ({ ExportFormat::Type::flac, 24 }));
        CHECK (exported->sampleRate == 44100.0);
        CHECK (exported->loudnessLufs == std::optional<double> (-14.0));
        CHECK (exported->without == juce::Array<te::AudioTrack*> ({ drums }));

        // It paints. Set HARMONIA_SNAPSHOT_DIR to look at it without opening a window.
        const auto image = options.createComponentSnapshot (options.getLocalBounds(), true, 1.0f);
        CHECK (image.isValid());

        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            const auto file = juce::File (dir).getChildFile ("export-song.png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            REQUIRE (out.openedOk());
            juce::PNGImageFormat().writeImageToStream (image, out);
        }

        // No second file after all.
        clickInMenu (options, "No second file");
        CHECK (options.getWithout().isEmpty());
        CHECK (options.getWithoutButton().getButtonText() == "None");
        CHECK_FALSE (options.getHint().contains ("Second file"));

        // An MP3 can't have 96 kHz: the window says what the file will have.
        if (audio::mp3::isAvailable (320))
        {
            choose (options, { ExportFormat::Type::mp3, 16, 320 });
            pick (options.getSampleRateBox(), "96000 Hz");
            CHECK (options.getOptions().sampleRate == 96000.0);
            CHECK (options.getHint().contains ("An MP3 can't have 96000 Hz: the file will be 48000 Hz."));
            pick (options.getSampleRateBox(), "48000 Hz (video)");
            CHECK_FALSE (options.getHint().contains ("An MP3 can't have"));
        }

        // Opened as the last export was (a track of another song isn't offered).
        const auto other = core::Project::createEmpty (test::session());
        SongExport last (ExportFormat { ExportFormat::Type::flac, 16 });
        last.sampleRate = 96000.0;
        last.loudnessLufs = -16.0;
        last.without = { vocals, other->getTracks().getFirst() };
        ExportOptions again (last, audio::ExportUse::song, tracks, 44100.0);
        how = again.getOptions();
        CHECK (how.format == last.format);
        CHECK (how.sampleRate == 96000.0);
        CHECK (how.loudnessLufs == std::optional<double> (-16.0));
        CHECK (how.without == juce::Array<te::AudioTrack*> ({ vocals }));
        CHECK (again.getSampleRateBox().getText() == "96000 Hz");
        CHECK (again.getLoudnessBox().getText() == "-16 LUFS (Apple Music, podcasts)");

        // A loop region is a sample: its format alone, in the smaller window it always had.
        ExportOptions loop (last, audio::ExportUse::loop, tracks, 44100.0);
        CHECK_FALSE (loop.getSampleRateBox().isVisible());
        CHECK_FALSE (loop.getLoudnessBox().isVisible());
        CHECK_FALSE (loop.getWithoutButton().isVisible());
        CHECK (loop.getHeight() < options.getHeight());
        how = loop.getOptions();
        CHECK (how.format == last.format);
        CHECK (how.sampleRate == 0.0);
        CHECK_FALSE (how.loudnessLufs.has_value());
        CHECK (how.without.isEmpty());
        CHECK_FALSE (loop.getHint().contains ("Loudness"));

        // A song with nothing to leave out: no second file to ask for.
        CHECK_FALSE (ExportOptions ({}).getWithoutButton().isEnabled());
    }

    TEST_CASE ("the format used last is kept in the settings and offered next time")
    {
        auto& session = test::session();
        const auto before = audio::getLastExportFormat (session);

        audio::setLastExportFormat (session, { ExportFormat::Type::wav, 16 });
        CHECK (audio::getLastExportFormat (session) == ExportFormat ({ ExportFormat::Type::wav, 16 }));

        audio::setLastExportFormat (session, { ExportFormat::Type::flac, 24 });
        CHECK (audio::getLastExportFormat (session) == ExportFormat ({ ExportFormat::Type::flac, 24 }));

        audio::setLastExportFormat (session, before);
    }
}
