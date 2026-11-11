#pragma once

#include "audio/ExportFormat.h"
#include "core/Session.h"

namespace harmonia::audio
{
    /** Renders the whole song, from its start until what it plays after its last clip has died
        away (reverbs, delays, notes ringing out: audio::export_tail), to a 24-bit WAV file, as it
        sounds: muted tracks are left out, and while some tracks are soloed only those play. With
        `part`, only that stretch of the song, exactly (a loop to use as a sample: nothing added to
        ring out). Blocks until done. Changes nothing in the project. Returns an error message,
        empty on success. */
    juce::String exportWav (te::Edit&, const juce::File&, std::optional<te::TimeRange> part = std::nullopt);

    /** exportWav in another format: WAV of another bit depth, FLAC or MP3. 16-bit files get a
        little noise (dither), so quiet endings fade out smoothly; an MP3 is rendered at 44.1 or
        48 kHz (the audio interface's rate if it is one of them). */
    juce::String exportSong (te::Edit&, const juce::File&, const ExportFormat&, std::optional<te::TimeRange> part = std::nullopt);

    /** How long a track rendered to audio or frozen is let ring out after its last clip ends. (An
        export of the song waits for silence instead: audio::export_tail.) */
    inline constexpr double exportTailSeconds = 2.0;

    //==============================================================================
    struct StemsResult
    {
        juce::Array<juce::File> files;
        juce::String error;     // empty if all went well
    };

    /** What a stem's file is called unless told otherwise: "My song - 01 Drums". */
    inline const char* const defaultStemPattern = "{song} - {n} {track}";

    /** Which stems to make, and how to call and write them. */
    struct StemsOptions
    {
        /** The song tracks and groups to make stems of; empty: every song track that is heard and
            has clips. (getStemChoices: what there is to choose from.) */
        juce::Array<te::AudioTrack*> tracks;

        juce::String songName;                              // for {song}
        juce::String namePattern = defaultStemPattern;      // getStemFileName
        ExportFormat format;                                // WAV 24-bit unless chosen otherwise
    };

    /** What stems can be made of, top to bottom: the song tracks that would be heard in exportWav
        and have clips, then the groups any of them plays through. */
    juce::Array<te::AudioTrack*> getStemChoices (te::Edit&);

    /** Stems: each of the tracks chosen rendered to its own file in `folder` (named by the
        pattern, numbered top to bottom). A song track's stem is the track with its effects, its
        group's (audio::groups) and what it sends to the return tracks (its own reverb, delay...),
        before the master's effects and volume. A group's stem is all the tracks that play through
        it, together. All are as long as each other (as long as the longest of them rings), so
        put side by side in another program they line up; the song tracks' stems add up to the
        mix. Files of the same name are replaced. `progress` hears (done, total) before each stem.
        Blocks until done. */
    StemsResult exportStems (te::Edit&, const juce::File& folder, const StemsOptions&,
                             const std::function<void (int done, int total)>& progress = {});

    /** Every song track that is heard and has clips, as 24-bit WAVs named "<prefix> - 01 <track>.wav". */
    StemsResult exportStems (te::Edit&, const juce::File& folder, const juce::String& prefix,
                             const std::function<void (int done, int total)>& progress = {});

    /** A stem's file name by a pattern: {song}, {n} (its number, two digits), {track}, {bpm} (the
        song's tempo at its start) and {date} (2026-11-10) are filled in, the rest is kept; then
        what a file name can't have is taken out and the format's extension added. An empty
        pattern is the default one. */
    juce::String getStemFileName (const juce::String& pattern, const juce::String& songName, int number,
                                  const juce::String& trackName, double bpm, const ExportFormat& format = {});

    /** The file name exportStems gives the stem of the `number`th (from 1) track called `trackName`. */
    juce::String getStemFileName (const juce::String& prefix, int number, const juce::String& trackName);

    /** The pattern stems were named by last time (kept in the settings; the default at first). */
    juce::String getLastStemPattern (core::Session&);
    void setLastStemPattern (core::Session&, const juce::String&);

    /** Renders `range` of one track as it sounds through its own instrument, effects, volume and
        pan (not its sends, its group, nor the master) to a 24-bit WAV: even while it's muted or
        another track is soloed. Effects listening to other tracks (sidechain) still hear them.
        With `beforeFader`, without its volume and pan (and whatever line moves them).
        Blocks until done. Changes nothing in the project. */
    juce::String renderTrack (te::AudioTrack&, const juce::File&, te::TimeRange, bool beforeFader = false);

    /** Renders `range` of what the clips of an audio track play, as they sound together (each
        with its gain, fades, stretching and pitch, crossfaded where they overlap) and nothing
        more: none of the track's effects, volume or pan. To a 24-bit WAV, for making one clip of
        several (audio::consolidate). Blocks until done. Changes nothing in the project. */
    juce::String renderClips (te::AudioTrack&, const juce::File&, te::TimeRange);
}
