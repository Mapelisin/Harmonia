#pragma once

#include "audio/ExportFormat.h"
#include "core/Session.h"

/** Where a rendered song stops ringing, and the file cut there. An export goes on past the song's
    last clip until its reverbs and delays have died away, rather than for a fixed moment: the
    song is rendered well past its end, the end of its sound is found in what was rendered, and
    the file is written up to there. */
namespace harmonia::audio::export_tail
{
    /** How long an export waits for silence after the song's last clip, at the most. */
    inline constexpr double longestSeconds = 30.0;

    /** The sound has died away when it is this far under the loudest moment of the song (a quiet
        song rings out as long as a loud one), though never taken for silence over `loudestSilenceDb`
        nor looked for under `quietestSilenceDb` (the noise a 16-bit file is given: dither). */
    inline constexpr float silenceBelowPeakDb = 60.0f, loudestSilenceDb = -60.0f, quietestSilenceDb = -84.0f;

    /** What is kept after the last sound (or after the song's last clip, if nothing rings after
        it), fading to nothing. */
    inline constexpr double keptSeconds = 0.5;

    /** Where the last sound above silence is in a sound file, in seconds: `fromSeconds` if there
        is none after that. Nothing if the file can't be read. */
    std::optional<double> findLastSound (te::Engine&, const juce::File&, double fromSeconds);

    /** How long the export of a song is to be, its last clip ending at `songEndSeconds` and its
        last sound being at `lastSoundSeconds`. */
    double getLength (double songEndSeconds, double lastSoundSeconds);

    /** Writes the first `seconds` of a WAV (all of it, if it is shorter) to `target` in `format`,
        the last `keptSeconds` fading to nothing. The WAV is to be of the bit depth of `format` (a
        32-bit float one for an MP3): its samples are written as they are. Returns an error
        message, empty on success. */
    juce::String writeCut (te::Engine&, const juce::File& wav, const juce::File& target, const ExportFormat& format, double seconds);
}
