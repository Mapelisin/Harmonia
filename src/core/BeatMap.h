#pragma once

#include <juce_core/juce_core.h>

#include <memory>
#include <optional>
#include <vector>

namespace harmonia::core
{
    /** Where the beats of a sound are in its file. A sound that keeps its tempo has so many beats
        a minute from the start of its file to the end. One that doesn't (somebody playing, not a
        machine) has its tempo change at some of its beats: each tempo holds until the next.

        Beats are counted from the start of the file (beat 0), at the tempo the sound starts
        with; the sound's first beat needn't be at a whole one.

        Cheap to copy: a map is read at every line of a drawing. */
    class BeatMap
    {
    public:
        /** The tempo from a beat on, and the moment of the file that beat is at. */
        struct Change
        {
            double beat = 0.0;
            double bpm = 0.0;
            double seconds = 0.0;
        };

        /** A sound that keeps one tempo. */
        explicit BeatMap (double bpm);

        /** A sound whose beats are at these moments (seconds, going up, two at least): between
            each two the tempo that takes one to the next, before the first as from it to the
            second, after the last as up to it. Nothing if those aren't the beats of anything. */
        static std::optional<BeatMap> fromBeats (const std::vector<double>& beatSeconds);

        /** As kept in a clip, and read by the engine (patch 0004): "0 96.2 4.52 97.1": the beat
            and the tempo of each change. */
        juce::String toString() const;
        static std::optional<BeatMap> fromString (const juce::String&);

        /** One tempo all the way. */
        bool isSteady() const noexcept                              { return changes->size() == 1; }

        double toBeats (double seconds) const noexcept;
        double toSeconds (double beats) const noexcept;

        /** The same sound counted in more beats (2: twice as many, 0.5: half). */
        BeatMap scaled (double factor) const;

        /** The map of the same sound played backwards, its file being `fileSeconds` long. */
        BeatMap reversed (double fileSeconds) const;

        /** The same sound with one of its beats elsewhere in the file: `beat` at `seconds`. The
            beats `span` before and after it stay where they are (before it: the file's start,
            if that is nearer), and so does everything beyond them; from each of them to the beat
            the tempo is what takes one to the other. The beat stays between them, no nearer
            either than a quarter of where an even tempo from one to the other would have it:
            `seconds` is brought within that. Nothing for a beat at or before the file's start. */
        std::optional<BeatMap> withBeatAt (double beat, double seconds, double span = 1.0) const;

        /** The same sound without the change of tempo at `beat` (a beat put elsewhere by hand,
            or one found off the others): one tempo from the change before it to the one after
            it. Nothing if the tempo doesn't change at that beat. */
        std::optional<BeatMap> withoutChangeAt (double beat) const;

        /** The first at beat 0, the others in order. */
        const std::vector<Change>& getChanges() const noexcept      { return *changes; }

    private:
        explicit BeatMap (std::vector<Change>);

        std::shared_ptr<const std::vector<Change>> changes;         // never empty
    };
}
