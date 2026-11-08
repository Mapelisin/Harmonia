#include "core/BassLines.h"

#include "core/Clips.h"
#include "core/Drums.h"

namespace harmonia::core::bass_lines
{
    namespace
    {
        constexpr double tolerance = 1.0e-6;

        /** The note name at the start of `text` ("C", "F#", "Bb"): its pitch class and length. */
        std::optional<std::pair<int, int>> readNoteName (const juce::String& text)
        {
            static const std::array<int, 7> classes { 9, 11, 0, 2, 4, 5, 7 };     // A B C D E F G
            const auto letter = juce::CharacterFunctions::toUpperCase (text[0]);

            if (letter < 'A' || letter > 'G')
                return std::nullopt;

            auto pitchClass = classes[(size_t) (letter - 'A')];
            int length = 1;

            if (text[1] == '#')         { ++pitchClass; ++length; }
            else if (text[1] == 'b')    { --pitchClass; ++length; }

            return std::make_pair ((pitchClass + 12) % 12, length);
        }

        struct Quality
        {
            int third = 4, fifth = 7;
        };

        /** The third and fifth of a chord, from its symbol after the root ("m7" -> minor third). */
        Quality qualityOf (const juce::String& suffix)
        {
            Quality q;

            if (suffix.startsWith ("dim") || (suffix.startsWith ("m") && ! suffix.startsWith ("maj")))
                q.third = 3;
            else if (suffix.contains ("sus4"))
                q.third = 5;
            else if (suffix.startsWith ("sus2"))
                q.third = 2;
            else if (suffix == "5")
                q.third = 7;

            if (suffix.startsWith ("dim") || suffix.contains ("b5"))
                q.fifth = 6;
            else if (suffix.startsWith ("aug"))
                q.fifth = 8;

            return q;
        }
    }

    int rootOf (const juce::String& chordName, bool bass)
    {
        const auto root = readNoteName (chordName);

        if (! root.has_value())
            return -1;

        if (bass && chordName.containsChar ('/'))
            if (const auto under = readNoteName (chordName.fromLastOccurrenceOf ("/", false, false)))
                return under->first;

        return root->first;
    }

    const std::vector<Pattern>& getPatterns()
    {
        static const std::vector<Pattern> all { Pattern::held, Pattern::beats, Pattern::eighths,
                                                Pattern::rootFifth, Pattern::octaves, Pattern::walking };
        return all;
    }

    juce::String getName (Pattern pattern)
    {
        switch (pattern)
        {
            case Pattern::held:         return "Held (one note a chord)";
            case Pattern::beats:        return "On the beats";
            case Pattern::eighths:      return "Eighths (driving)";
            case Pattern::rootFifth:    return "Root and fifth";
            case Pattern::octaves:      return "Octaves (disco)";
            case Pattern::walking:      return "Walking";
        }

        return {};
    }

    std::vector<Note> generate (const std::vector<chords::Span>& spans, const Settings& s)
    {
        std::vector<Note> notes;
        const auto base = 12 * (juce::jlimit (0, 8, s.octave) + 1);
        const auto gate = juce::jlimit (0.05, 1.0, s.gate);
        const auto accent = juce::jlimit (1, 127, s.velocity);
        const auto offBeat = juce::jlimit (1, 127, juce::roundToInt (accent * 0.85));

        for (size_t i = 0; i < spans.size(); ++i)
        {
            const auto& span = spans[i];
            const auto chordRoot = rootOf (span.name);

            if (chordRoot < 0 || span.endBeat - span.startBeat <= tolerance)
                continue;

            const auto rootPitch = base + chordRoot;
            const auto bassPitch = base + (s.slashBass ? rootOf (span.name, true) : chordRoot);
            const auto suffix = span.name.upToFirstOccurrenceOf ("/", false, false)
                                         .substring (readNoteName (span.name)->second);
            const auto quality = qualityOf (suffix);

            const auto add = [&] (int pitch, double start, double step, int velocity)
            {
                Note n;
                n.pitch = juce::jlimit (0, 127, pitch);
                n.startBeat = start;
                n.lengthBeats = std::max (0.01, std::min (span.endBeat, start + step * gate) - start);
                n.velocity = velocity;
                notes.push_back (n);
            };

            // Steps of `step` beats from the chord's start; the first on the chord change is the accent.
            const auto everyStep = [&] (double step, const std::function<int (int)>& pitchOf)
            {
                int k = 0;

                for (auto start = span.startBeat; start < span.endBeat - tolerance; start = span.startBeat + (++k) * step)
                {
                    const auto onBeat = std::abs (start - std::round (start)) < tolerance;
                    add (pitchOf (k), start, step, k == 0 || onBeat ? accent : offBeat);
                }
            };

            switch (s.pattern)
            {
                case Pattern::held:
                    add (bassPitch, span.startBeat, span.endBeat - span.startBeat, accent);
                    break;

                case Pattern::beats:
                    everyStep (1.0, [&] (int) { return bassPitch; });
                    break;

                case Pattern::eighths:
                    everyStep (0.5, [&] (int) { return bassPitch; });
                    break;

                case Pattern::rootFifth:
                    everyStep (1.0, [&] (int k) { return k % 2 == 0 ? bassPitch : rootPitch + quality.fifth; });
                    break;

                case Pattern::octaves:
                    everyStep (0.5, [&] (int k) { return k % 2 == 0 ? bassPitch : bassPitch + 12; });
                    break;

                case Pattern::walking:
                {
                    // Root, third, fifth, octave; the last beat a half step under the next chord's bass.
                    const auto beats = (int) std::ceil (span.endBeat - span.startBeat - tolerance);
                    const auto nextRoot = i + 1 < spans.size() ? rootOf (spans[i + 1].name, s.slashBass) : -1;
                    const auto leadsOn = nextRoot >= 0 && std::abs (spans[i + 1].startBeat - span.endBeat) < tolerance;

                    everyStep (1.0, [&] (int k)
                    {
                        if (k > 0 && k == beats - 1 && leadsOn)
                            return base + nextRoot - 1;

                        const std::array<int, 4> tones { bassPitch, rootPitch + quality.third, rootPitch + quality.fifth, rootPitch + 12 };
                        return tones[(size_t) (k % 4)];
                    });
                    break;
                }
            }
        }

        return notes;
    }

    std::vector<Note> getHarmony (te::Edit& edit, const te::Track* except, double startBeat, double endBeat)
    {
        std::vector<Note> notes;

        for (auto* track : te::getAudioTracks (edit))
        {
            if (track == except || track->isMuted (false) || drums::showsNames (*track))
                continue;

            for (auto* c : track->getClips())
            {
                auto* clip = dynamic_cast<te::MidiClip*> (c);

                if (clip == nullptr || clip->isMuted())
                    continue;

                for (const auto& played : clips::getPlayedNotes (*clip))
                {
                    const auto start = std::max (startBeat, played.startBeat);
                    const auto end = std::min (endBeat, played.endBeat);

                    if (played.note.muted || end - start <= tolerance)
                        continue;

                    auto n = played.note;
                    n.startBeat = start;
                    n.lengthBeats = end - start;
                    notes.push_back (n);
                }
            }
        }

        std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b)
                   { return std::tie (a.startBeat, a.pitch) < std::tie (b.startBeat, b.pitch); });
        return notes;
    }
}
