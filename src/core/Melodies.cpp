#include "core/Melodies.h"

#include "core/BassLines.h"

namespace harmonia::core::melodies
{
    namespace
    {
        constexpr double tolerance = 1.0e-6;

        int pitchClassOf (int pitch)    { return ((pitch % 12) + 12) % 12; }

        //==============================================================================
        // Rhythm: each beat of a bar gets a cell, picked at random with weights by how busy.

        struct Slot
        {
            double start = 0.0, length = 0.0;       // beats from the bar's start
        };

        enum class Cell { quarter, hold, rest, eighths, offbeat, dotted, sixteenthsEighth, eighthSixteenths, sixteenths };

        /** The notes of a cell within its beat: start and length. */
        std::vector<Slot> notesOf (Cell cell)
        {
            switch (cell)
            {
                case Cell::quarter:             return { { 0.0, 1.0 } };
                case Cell::eighths:             return { { 0.0, 0.5 }, { 0.5, 0.5 } };
                case Cell::offbeat:             return { { 0.5, 0.5 } };
                case Cell::dotted:              return { { 0.0, 0.75 }, { 0.75, 0.25 } };
                case Cell::sixteenthsEighth:    return { { 0.0, 0.25 }, { 0.25, 0.25 }, { 0.5, 0.5 } };
                case Cell::eighthSixteenths:    return { { 0.0, 0.5 }, { 0.5, 0.25 }, { 0.75, 0.25 } };
                case Cell::sixteenths:          return { { 0.0, 0.25 }, { 0.25, 0.25 }, { 0.5, 0.25 }, { 0.75, 0.25 } };
                case Cell::hold:
                case Cell::rest:                return {};
            }

            return {};
        }

        const std::vector<std::pair<Cell, float>>& cellsFor (Rhythm rhythm)
        {
            static const std::vector<std::pair<Cell, float>> calm { { Cell::quarter, 4.0f }, { Cell::hold, 4.0f },
                                                                    { Cell::rest, 1.0f }, { Cell::eighths, 1.0f } };
            static const std::vector<std::pair<Cell, float>> medium { { Cell::quarter, 4.0f }, { Cell::eighths, 4.0f },
                                                                      { Cell::hold, 1.5f }, { Cell::rest, 0.7f },
                                                                      { Cell::offbeat, 1.0f }, { Cell::dotted, 1.0f } };
            static const std::vector<std::pair<Cell, float>> busy { { Cell::eighths, 4.0f }, { Cell::sixteenthsEighth, 2.0f },
                                                                    { Cell::eighthSixteenths, 2.0f }, { Cell::sixteenths, 1.0f },
                                                                    { Cell::quarter, 1.0f }, { Cell::dotted, 1.0f },
                                                                    { Cell::offbeat, 0.5f } };

            switch (rhythm)
            {
                case Rhythm::calm:      return calm;
                case Rhythm::medium:    return medium;
                case Rhythm::busy:      return busy;
            }

            return medium;
        }

        Cell pickCell (juce::Random& random, Rhythm rhythm, bool onDownbeat)
        {
            // The bar's first beat always starts a note: the melody keeps its footing.
            std::vector<std::pair<Cell, float>> cells;

            for (const auto& c : cellsFor (rhythm))
                if (! onDownbeat || (c.first != Cell::hold && c.first != Cell::rest && c.first != Cell::offbeat))
                    cells.push_back (c);

            auto total = 0.0f;

            for (const auto& c : cells)
                total += c.second;

            auto r = random.nextFloat() * total;

            for (const auto& c : cells)
            {
                if (r < c.second)
                    return c.first;

                r -= c.second;
            }

            return cells.back().first;
        }

        /** A bar's rhythm, `length` beats long. An ending bar closes on a long note from its middle
            (from its start, when calm). */
        std::vector<Slot> makeRhythm (juce::Random& random, Rhythm rhythm, double length, bool ending)
        {
            std::vector<Slot> slots;
            const auto beats = (int) std::ceil (length - tolerance);
            const auto endingBeat = ending ? (rhythm == Rhythm::calm ? 0 : beats / 2) : beats;

            for (int beat = 0; beat < endingBeat; ++beat)
            {
                const auto cell = pickCell (random, rhythm, beat == 0);

                if (cell == Cell::hold && ! slots.empty() && std::abs (slots.back().start + slots.back().length - beat) < tolerance)
                {
                    slots.back().length += 1.0;
                    continue;
                }

                auto notes = notesOf (cell == Cell::hold ? Cell::quarter : cell);

                for (auto& n : notes)
                    slots.push_back ({ beat + n.start, n.length });
            }

            if (ending)
                slots.push_back ({ (double) endingBeat, length - endingBeat });

            // Nothing past the bar's end (a short last bar).
            std::erase_if (slots, [length] (const Slot& s) { return s.start >= length - tolerance; });

            for (auto& s : slots)
                s.length = std::min (s.length, length - s.start);

            return slots;
        }

        //==============================================================================
        Chord chordAt (const std::vector<Chord>& chords, const scales::Scale& scale, double beat)
        {
            const Chord* found = nullptr;

            for (const auto& c : chords)
                if (c.startBeat <= beat + tolerance)
                    found = &c;

            if (found == nullptr && ! chords.empty())
                found = &chords.front();

            if (found != nullptr)
                return *found;

            // No chords: the key's own triad.
            Chord tonic;
            tonic.root = scale.root;

            for (auto p : chords::build (60 + scale.root, chords::ChordType::scaleTriad, scale))
                tonic.pitchClasses.push_back (pitchClassOf (p));

            std::sort (tonic.pitchClasses.begin(), tonic.pitchClasses.end());
            return tonic;
        }

        bool holds (const Chord& chord, int pitch)
        {
            return std::find (chord.pitchClasses.begin(), chord.pitchClasses.end(), pitchClassOf (pitch)) != chord.pitchClasses.end();
        }

        /** The writer: the pitches so far, the range, the phrase's shape. */
        struct Writer
        {
            const std::vector<Chord>& chords;
            const scales::Scale& scale;
            juce::Random& random;
            int low = 0, high = 127, base = 72;

            int previous = -1, beforePrevious = -1, lastInterval = 0, repeats = 0;

            /** Where the phrase wants to be: an arch up to its peak and back down. */
            double phraseStart = 0.0, phraseLength = 16.0, peak = 7.0, peakAt = 0.5;

            double targetAt (double beat) const
            {
                const auto x = juce::jlimit (0.0, 1.0, (beat - phraseStart) / std::max (1.0, phraseLength));
                const auto rise = x < peakAt ? x / peakAt : (1.0 - x) / (1.0 - peakAt);
                return base + 1.0 + (peak - 1.0) * std::sin (rise * juce::MathConstants<double>::halfPi);
            }

            std::vector<int> candidates (double beat, bool strong) const
            {
                std::vector<int> result;
                const auto chord = chordAt (chords, scale, beat);

                for (int p = low; p <= high; ++p)
                {
                    if (strong ? holds (chord, p) : (scale.contains (p) || (scale.isChromatic() && holds (chord, p))))
                    {
                        // Between the beats: no more than a fourth from the last note.
                        if (! strong && previous >= 0 && std::abs (p - previous) > 5)
                            continue;

                        result.push_back (p);
                    }
                }

                if (result.empty())
                    for (int p = low; p <= high; ++p)
                        if (holds (chord, p))
                            result.push_back (p);

                return result;
            }

            int choose (double beat, bool strong, std::optional<int> wanted = {})
            {
                const auto target = wanted.has_value() ? (double) *wanted : targetAt (beat);
                auto best = -1;
                auto bestScore = std::numeric_limits<double>::max();

                // On the beats the phrase's shape leads; between them, small steps.
                const auto towardsTarget = strong ? 1.0 : 0.8;
                const auto smallSteps = strong ? 0.6 : 1.0;

                for (auto p : candidates (beat, strong))
                {
                    auto score = towardsTarget * std::abs (p - target) + random.nextDouble() * 3.0;

                    if (previous >= 0)
                    {
                        const auto interval = p - previous;
                        score += smallSteps * std::abs (interval);

                        if (interval == 0)
                            score += repeats >= 1 ? 12.0 : 4.0;

                        // Not back and forth between two notes.
                        if (! strong && p == beforePrevious && interval != 0)
                            score += 3.0;

                        if (std::abs (interval) > 7)
                            score += 6.0;

                        // After a leap, turn back by step.
                        if (std::abs (lastInterval) > 4 && interval * lastInterval > 0)
                            score += 4.0;
                    }

                    if (score < bestScore)
                    {
                        bestScore = score;
                        best = p;
                    }
                }

                return best < 0 ? juce::jlimit (low, high, (int) std::round (target)) : best;
            }

            void played (int pitch)
            {
                if (previous >= 0)
                {
                    lastInterval = pitch - previous;
                    repeats = lastInterval == 0 ? repeats + 1 : 0;
                }

                beforePrevious = previous;
                previous = pitch;
            }

            /** The chord tone of the chord at `beat` nearest `pitch`, in range. */
            int nearestChordTone (double beat, int pitch) const
            {
                const auto chord = chordAt (chords, scale, beat);
                auto best = pitch;

                for (int d = 0; d <= 12; ++d)
                {
                    for (auto p : { pitch - d, pitch + d })
                    {
                        if (p >= low && p <= high && holds (chord, p))
                            return p;
                    }
                }

                return best;
            }

            /** The root of the chord at `beat` nearest `target`, in range. */
            int nearestRoot (double beat, double target) const
            {
                const auto root = chordAt (chords, scale, beat).root;
                auto best = -1;

                for (int p = low; p <= high; ++p)
                    if (pitchClassOf (p) == root && (best < 0 || std::abs (p - target) < std::abs (best - target)))
                        best = p;

                return best < 0 ? juce::jlimit (low, high, (int) std::round (target)) : best;
            }
        };

        struct Motif
        {
            std::vector<Slot> rhythm;
            std::vector<int> pitches;
        };

        /** Where an answer to the motif starts: a tone of the chord there near where the motif
            started (and near the last note), from which the motif's moves stay in range. */
        int startAnswer (const Writer& writer, const scales::Scale& scale, const Motif& motif, double beat)
        {
            const auto chord = chordAt (writer.chords, scale, beat);
            auto best = -1;
            auto bestScore = std::numeric_limits<double>::max();

            for (int p = writer.low; p <= writer.high; ++p)
            {
                if (! holds (chord, p))
                    continue;

                // The motif's moves from here, by steps of the key.
                auto pitch = p, lowest = p, highest = p;

                for (size_t i = 1; i < motif.pitches.size(); ++i)
                {
                    pitch = scale.step (pitch, scale.countSteps (motif.pitches[i - 1], motif.pitches[i]));
                    lowest = std::min (lowest, pitch);
                    highest = std::max (highest, pitch);
                }

                const auto fits = lowest >= writer.low && highest <= writer.high;
                const auto score = std::abs (p - motif.pitches[0]) + 0.5 * (writer.previous >= 0 ? std::abs (p - writer.previous) : 0)
                                 + (fits ? 0.0 : 100.0);

                if (score < bestScore)
                {
                    bestScore = score;
                    best = p;
                }
            }

            return best < 0 ? motif.pitches[0] : best;
        }
    }

    //==============================================================================
    std::vector<Chord> getChords (const std::vector<chords::Span>& spans, const std::vector<Note>& harmony)
    {
        std::vector<Chord> result;

        for (const auto& span : spans)
        {
            const auto root = bass_lines::rootOf (span.name);
            const auto length = span.endBeat - span.startBeat;

            if (root < 0 || length <= tolerance)
                continue;

            std::array<double, 12> sounding {};

            for (const auto& n : harmony)
            {
                const auto overlap = std::min (span.endBeat, n.startBeat + n.lengthBeats) - std::max (span.startBeat, n.startBeat);

                if (! n.muted && overlap > 0.0)
                    sounding[(size_t) pitchClassOf (n.pitch)] += overlap;
            }

            Chord chord { span.startBeat, span.endBeat, root, {} };

            for (int pc = 0; pc < 12; ++pc)
                if (pc == root || sounding[(size_t) pc] >= length * 0.5 - tolerance)
                    chord.pitchClasses.push_back (pc);

            result.push_back (std::move (chord));
        }

        return result;
    }

    scales::Scale fitScale (const std::vector<Chord>& chords, const scales::Scale& songScale)
    {
        if (! songScale.isChromatic() || chords.empty())
            return songScale.isChromatic() ? scales::Scale { songScale.root, scales::ScaleType::major } : songScale;

        // The first chord says which of the keys with the same notes it is (C major or A minor).
        const auto& first = chords.front();
        const auto firstIsMinor = std::find (first.pitchClasses.begin(), first.pitchClasses.end(),
                                             (first.root + 3) % 12) != first.pitchClasses.end();

        scales::Scale best;
        auto bestScore = std::numeric_limits<double>::lowest();

        for (auto type : { scales::ScaleType::major, scales::ScaleType::minor })
        {
            for (int root = 0; root < 12; ++root)
            {
                const scales::Scale candidate { root, type };
                auto score = 0.0;

                for (const auto& c : chords)
                {
                    const auto length = c.endBeat - c.startBeat;

                    for (auto pc : c.pitchClasses)
                        score += candidate.contains (pc) ? length : -2.0 * length;
                }

                const auto isMinor = type == scales::ScaleType::minor;

                if (root == first.root && isMinor == firstIsMinor)
                    score += 0.5 * (first.endBeat - first.startBeat);

                if (score > bestScore + tolerance)
                {
                    bestScore = score;
                    best = candidate;
                }
            }
        }

        return best;
    }

    const std::vector<Rhythm>& getRhythms()
    {
        static const std::vector<Rhythm> all { Rhythm::calm, Rhythm::medium, Rhythm::busy };
        return all;
    }

    juce::String getName (Rhythm rhythm)
    {
        switch (rhythm)
        {
            case Rhythm::calm:      return "Calm (long notes)";
            case Rhythm::medium:    return "Medium (quarters, eighths)";
            case Rhythm::busy:      return "Busy (eighths, sixteenths)";
        }

        return {};
    }

    std::vector<Note> generate (const std::vector<Chord>& chords, const scales::Scale& scale, const Settings& s)
    {
        std::vector<Note> notes;
        const auto beatsPerBar = std::max (1.0, s.beatsPerBar);
        const auto total = s.endBeat - s.startBeat;

        if (total <= tolerance)
            return notes;

        juce::Random random (0x5eed + 7919 * (juce::int64) s.variation);
        Writer writer { chords, scale, random };
        writer.base = 12 * (juce::jlimit (1, 8, s.octave) + 1);
        writer.low = std::max (0, writer.base - 5);
        writer.high = std::min (127, writer.base + 12);

        const auto bars = std::max (1, (int) std::ceil (total / beatsPerBar - tolerance));
        const auto accent = juce::jlimit (1, 127, s.velocity);
        std::optional<Motif> motif;

        for (int bar = 0; bar < bars; ++bar)
        {
            const auto barStart = s.startBeat + bar * beatsPerBar;
            const auto barLength = std::min (beatsPerBar, s.endBeat - barStart);
            const auto inPhrase = bar % 4;
            const auto isLast = bar == bars - 1;
            const auto ending = inPhrase == 3 || isLast;

            if (inPhrase == 0)
            {
                writer.phraseStart = barStart;
                writer.phraseLength = std::min (4.0 * beatsPerBar, s.endBeat - barStart);
                writer.peak = 5.0 + random.nextDouble() * 5.0;
                writer.peakAt = 0.4 + random.nextDouble() * 0.3;
            }

            // The answer: the motif's rhythm again, its shape moved to fit the chord here.
            const auto answering = s.answer && motif.has_value() && ! ending && (inPhrase == 2 || inPhrase == 0)
                                && barLength >= beatsPerBar - tolerance;
            const auto rhythm = answering ? motif->rhythm : makeRhythm (random, s.rhythm, barLength, ending);
            std::vector<int> pitches;
            const auto answerStart = answering ? startAnswer (writer, scale, *motif, barStart) : 0;

            for (size_t i = 0; i < rhythm.size(); ++i)
            {
                const auto start = barStart + rhythm[i].start;
                const auto strong = std::abs (rhythm[i].start - std::round (rhythm[i].start)) < tolerance;
                const auto isFinal = isLast && i == rhythm.size() - 1;
                const auto isPhraseEnd = ending && i == rhythm.size() - 1;
                int pitch;

                if (isFinal)
                {
                    pitch = writer.nearestRoot (start, writer.previous >= 0 ? (double) writer.previous : writer.targetAt (start));
                }
                else if (answering)
                {
                    if (i == 0)
                        pitch = answerStart;
                    else
                        pitch = scale.step (pitches.back(), scale.countSteps (motif->pitches[i - 1], motif->pitches[i]));

                    pitch = juce::jlimit (writer.low, writer.high, pitch);

                    if (strong && ! holds (chordAt (chords, scale, start), pitch))
                        pitch = writer.nearestChordTone (start, pitch);
                }
                else
                {
                    pitch = writer.choose (start, strong || isPhraseEnd);
                }

                pitches.push_back (pitch);
                writer.played (pitch);

                const auto slotEnd = std::min (s.endBeat, start + rhythm[i].length);
                Note n;
                n.pitch = pitch;
                n.startBeat = start;
                n.lengthBeats = std::max (0.05, (isFinal ? slotEnd - start : (slotEnd - start) * 0.92));

                const auto onDownbeat = std::abs (rhythm[i].start) < tolerance;
                const auto shade = onDownbeat ? 0 : strong ? 8 : 14;
                n.velocity = juce::jlimit (1, 127, accent - shade + random.nextInt (7) - 3);
                notes.push_back (n);
            }

            if (s.answer && ! motif.has_value() && ! ending && barLength >= beatsPerBar - tolerance && ! rhythm.empty())
                motif = Motif { rhythm, pitches };
        }

        return notes;
    }
}
