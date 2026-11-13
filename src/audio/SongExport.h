#pragma once

#include "audio/ExportFormat.h"
#include "core/Session.h"

#include <array>

namespace harmonia::audio
{
    /** How the song is exported: the kind of file, and what is done to the sound on its way
        there. A format alone is the song as it is, at the audio device's sample rate. */
    struct SongExport
    {
        SongExport() = default;
        SongExport (const ExportFormat& f) : format (f) {}

        ExportFormat format;

        /** Samples a second of the file. 0: as many as the audio device plays. (An MP3 gets the
            nearest rate it can have: getFileSampleRate.) */
        double sampleRate = 0.0;

        /** The loudness (LUFS) the file is brought to: all of it is turned up or down by as much
            as that takes, though never so far that its peaks go over loudness::peakCeilingDb.
            The song itself stays as it is. Nothing: as loud as the song is. Only for the whole
            song, not for a part of it. */
        std::optional<double> loudnessLufs;

        /** A second file next to the first (getMinusFile): the song without these tracks (a
            group: without all that plays through it), turned up or down as the first was. Only
            for the whole song. */
        juce::Array<te::AudioTrack*> without;
    };

    /** The sample rates an export offers besides the audio device's own. */
    inline constexpr std::array<double, 4> exportSampleRates { 44100.0, 48000.0, 88200.0, 96000.0 };

    /** The sample rate the file of an export gets: the one asked for (0: `deviceRate`); an MP3
        can't have more than 48 kHz, so sound at twice that or more is written at 48 or 44.1. */
    double getFileSampleRate (const ExportFormat&, double wanted, double deviceRate);

    /** A loudness to export at, and who plays songs at it. */
    struct LoudnessTarget
    {
        double lufs;
        const char* usedBy;
    };

    inline constexpr std::array<LoudnessTarget, 3> loudnessTargets { { { -14.0, "Spotify, YouTube, Tidal" },
                                                                       { -16.0, "Apple Music, podcasts" },
                                                                       { -23.0, "TV and radio" } } };

    /** What the name of an export's second file adds to the first's: " (no Vocals)" for one
        track left out, " (minus)" for several. */
    juce::String getMinusSuffix (const juce::Array<te::AudioTrack*>& without);

    /** The second file of an export: "Song (no Vocals).wav" next to "Song.wav". */
    juce::File getMinusFile (const juce::File& songFile, const juce::Array<te::AudioTrack*>& without);

    /** The sample rate of the last export, of the song or of stems (kept in the settings; 0, the
        audio device's, at first and for anything the export doesn't offer). */
    double getLastExportSampleRate (core::Session&);
    void setLastExportSampleRate (core::Session&, double);

    /** The format, sample rate and loudness the song was exported with last time (kept in the
        settings; at first a 24-bit WAV of the song as it is). The tracks left out of a second
        file are not kept: they are another song's next time. */
    SongExport getLastSongExport (core::Session&);
    void setLastSongExport (core::Session&, const SongExport&);
}
