#include <doctest.h>

#include "TestSession.h"
#include "audio/Mp3Encoder.h"
#include "ui/ExportOptions.h"

using namespace harmonia;
using audio::ExportFormat;
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
}

TEST_SUITE ("ui::ExportOptions")
{
    TEST_CASE ("the export window lists every format, starts at the one asked for and tells what it is for")
    {
        ExportOptions options ({ ExportFormat::Type::flac, 16 });
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
        ExportOptions options ({ ExportFormat::Type::wav, 8 });
        CHECK (options.getFormat() == ExportFormat());
    }

    TEST_CASE ("Export hands on the format chosen, Cancel hands on nothing")
    {
        ExportOptions options ({});
        std::optional<ExportFormat> exported;
        auto cancelled = false;
        options.onExport = [&] (const ExportFormat& f) { exported = f; };
        options.onCancel = [&] { cancelled = true; };

        choose (options, { ExportFormat::Type::flac, 24 });
        options.getExportButton().triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        REQUIRE (exported.has_value());
        CHECK (*exported == ExportFormat ({ ExportFormat::Type::flac, 24 }));
        CHECK (! cancelled);
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
