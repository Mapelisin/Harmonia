#pragma once

#include "core/Session.h"

namespace harmonia::audio
{
    /** Renders the whole song (from the start to the end of the last clip, plus a moment for
        notes to ring out) to a 24-bit WAV file, as it sounds: muted tracks are left out, and
        while some tracks are soloed only those play. Blocks until done. Changes nothing in the
        project. Returns an error message, empty on success. */
    juce::String exportWav (te::Edit&, const juce::File&);

    /** How long the export lets notes ring out after the last clip ends. */
    inline constexpr double exportTailSeconds = 2.0;

    //==============================================================================
    struct StemsResult
    {
        juce::Array<juce::File> files;
        juce::String error;     // empty if all went well
    };

    /** Stems: each song track that would be heard in exportWav and has clips, rendered to its own
        24-bit WAV in `folder`, named "<prefix> - 01 <track name>.wav" (numbered top to bottom).
        Each is the track with its effects and what it sends to the return tracks (its own reverb,
        delay...), before the master's effects and volume; all are as long as the whole song, so
        put side by side in another program they line up and add up to the mix. Files of the same
        name are replaced. `progress` hears (done, total) before each stem. Blocks until done. */
    StemsResult exportStems (te::Edit&, const juce::File& folder, const juce::String& prefix,
                             const std::function<void (int done, int total)>& progress = {});

    /** The file name exportStems gives the stem of the `number`th (from 1) track called `trackName`. */
    juce::String getStemFileName (const juce::String& prefix, int number, const juce::String& trackName);

    /** Renders `range` of one track as it sounds through its own instrument, effects, volume and
        pan (not its sends, nor the master) to a 24-bit WAV: even while it's muted or another track
        is soloed. Blocks until done. Changes nothing in the project. */
    juce::String renderTrack (te::AudioTrack&, const juce::File&, te::TimeRange);
}
