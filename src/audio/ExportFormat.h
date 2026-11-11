#pragma once

#include <juce_core/juce_core.h>

namespace harmonia::core { class Session; }

namespace harmonia::audio
{
    /** The kind of file a song is exported to: WAV (16 or 24 bit, or 32-bit float), FLAC (16 or
        24 bit, smaller and the same sound) or MP3 (128...320 kbps, to send and to stream). */
    struct ExportFormat
    {
        enum class Type { wav, flac, mp3 };

        Type type = Type::wav;
        int bitDepth = 24;      ///< WAV: 16, 24 or 32 (float); FLAC: 16 or 24
        int kbps = 320;         ///< MP3: 128, 192, 256 or 320

        /** ".wav", ".flac" or ".mp3". */
        juce::String getExtension() const;

        /** For a list: "WAV, 24-bit", "MP3, 320 kbps"... */
        juce::String describe() const;

        /** What the export offers, best first within each type; MP3 only where it can be made. */
        static juce::Array<ExportFormat> getChoices();

        /** For the settings: "wav-24", "mp3-320"... and back (the default if it isn't one). */
        juce::String toString() const;
        static ExportFormat fromString (const juce::String&);

        bool operator== (const ExportFormat&) const noexcept;
        bool operator!= (const ExportFormat& other) const noexcept    { return ! operator== (other); }
    };

    /** What is exported: the song, or a stretch of it to use as a loop. */
    enum class ExportUse { song, loop };

    /** The format the song (or a loop) was exported in last time: each has its own (kept in the
        settings; WAV 24-bit at first). */
    ExportFormat getLastExportFormat (core::Session&, ExportUse = ExportUse::song);
    void setLastExportFormat (core::Session&, const ExportFormat&, ExportUse = ExportUse::song);
}
