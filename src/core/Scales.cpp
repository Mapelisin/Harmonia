#include "core/Scales.h"

namespace harmonia::core
{
    namespace scales
    {
        namespace
        {
            const juce::Identifier rootId ("hScaleRoot"), typeId ("hScaleType");

            int pitchClass (int pitch)
            {
                return ((pitch % 12) + 12) % 12;
            }
        }

        const std::vector<ScaleType>& getAllTypes()
        {
            static const std::vector<ScaleType> all {
                ScaleType::chromatic, ScaleType::major, ScaleType::minor, ScaleType::harmonicMinor,
                ScaleType::melodicMinor, ScaleType::dorian, ScaleType::phrygian, ScaleType::lydian,
                ScaleType::mixolydian, ScaleType::majorPentatonic, ScaleType::minorPentatonic, ScaleType::blues
            };

            return all;
        }

        juce::String getName (ScaleType type)
        {
            switch (type)
            {
                case ScaleType::chromatic:       return "No scale";
                case ScaleType::major:           return "Major";
                case ScaleType::minor:           return "Minor";
                case ScaleType::harmonicMinor:   return "Harmonic minor";
                case ScaleType::melodicMinor:    return "Melodic minor";
                case ScaleType::dorian:          return "Dorian";
                case ScaleType::phrygian:        return "Phrygian";
                case ScaleType::lydian:          return "Lydian";
                case ScaleType::mixolydian:      return "Mixolydian";
                case ScaleType::majorPentatonic: return "Major pentatonic";
                case ScaleType::minorPentatonic: return "Minor pentatonic";
                case ScaleType::blues:           return "Blues";
            }

            return {};
        }

        const std::vector<int>& getIntervals (ScaleType type)
        {
            static const std::vector<int> chromatic { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },
                                          major { 0, 2, 4, 5, 7, 9, 11 },
                                          minor { 0, 2, 3, 5, 7, 8, 10 },
                                          harmonicMinor { 0, 2, 3, 5, 7, 8, 11 },
                                          melodicMinor { 0, 2, 3, 5, 7, 9, 11 },
                                          dorian { 0, 2, 3, 5, 7, 9, 10 },
                                          phrygian { 0, 1, 3, 5, 7, 8, 10 },
                                          lydian { 0, 2, 4, 6, 7, 9, 11 },
                                          mixolydian { 0, 2, 4, 5, 7, 9, 10 },
                                          majorPentatonic { 0, 2, 4, 7, 9 },
                                          minorPentatonic { 0, 3, 5, 7, 10 },
                                          blues { 0, 3, 5, 6, 7, 10 };

            switch (type)
            {
                case ScaleType::chromatic:       return chromatic;
                case ScaleType::major:           return major;
                case ScaleType::minor:           return minor;
                case ScaleType::harmonicMinor:   return harmonicMinor;
                case ScaleType::melodicMinor:    return melodicMinor;
                case ScaleType::dorian:          return dorian;
                case ScaleType::phrygian:        return phrygian;
                case ScaleType::lydian:          return lydian;
                case ScaleType::mixolydian:      return mixolydian;
                case ScaleType::majorPentatonic: return majorPentatonic;
                case ScaleType::minorPentatonic: return minorPentatonic;
                case ScaleType::blues:           return blues;
            }

            return chromatic;
        }

        juce::String getRootName (int root)
        {
            static const char* const names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            return names[pitchClass (root)];
        }

        bool Scale::contains (int pitch) const
        {
            const auto& intervals = getIntervals (type);
            return std::find (intervals.begin(), intervals.end(), pitchClass (pitch - root)) != intervals.end();
        }

        bool Scale::isRoot (int pitch) const
        {
            return pitchClass (pitch - root) == 0;
        }

        int Scale::snap (int pitch) const
        {
            pitch = juce::jlimit (0, 127, pitch);

            for (int distance = 0; distance < 12; ++distance)
            {
                if (pitch - distance >= 0 && contains (pitch - distance))
                    return pitch - distance;

                if (pitch + distance <= 127 && contains (pitch + distance))
                    return pitch + distance;
            }

            return pitch;
        }

        int Scale::step (int pitch, int steps) const
        {
            auto result = snap (pitch);
            const auto direction = steps > 0 ? 1 : -1;

            for (int i = 0; i < std::abs (steps); ++i)
            {
                auto next = result + direction;

                while (next >= 0 && next <= 127 && ! contains (next))
                    next += direction;

                if (next < 0 || next > 127)
                    break;

                result = next;
            }

            return result;
        }

        int Scale::countSteps (int from, int to) const
        {
            from = snap (from);
            to = snap (to);
            int count = 0;

            for (auto pitch = std::min (from, to) + 1; pitch <= std::max (from, to); ++pitch)
                if (contains (pitch))
                    ++count;

            return to >= from ? count : -count;
        }

        juce::String Scale::getName() const
        {
            if (isChromatic())
                return scales::getName (type);

            return getRootName (root) + " " + scales::getName (type).toLowerCase();
        }

        Scale read (const te::Edit& edit)
        {
            Scale scale;
            scale.root = pitchClass ((int) edit.state.getProperty (rootId, 0));

            const auto type = (int) edit.state.getProperty (typeId, 0);
            const auto& all = getAllTypes();

            if (type >= 0 && type < (int) all.size())
                scale.type = all[(size_t) type];

            return scale;
        }

        void write (te::Edit& edit, const Scale& scale, juce::UndoManager* um)
        {
            const auto& all = getAllTypes();
            const auto type = (int) (std::find (all.begin(), all.end(), scale.type) - all.begin());

            if (scale.isChromatic())
            {
                edit.state.removeProperty (rootId, um);
                edit.state.removeProperty (typeId, um);
                return;
            }

            edit.state.setProperty (rootId, pitchClass (scale.root), um);
            edit.state.setProperty (typeId, type, um);
        }
    }

    namespace chords
    {
        const std::vector<ChordType>& getAllTypes()
        {
            static const std::vector<ChordType> all {
                ChordType::scaleTriad, ChordType::scaleSeventh,
                ChordType::major, ChordType::minor, ChordType::diminished, ChordType::augmented,
                ChordType::sus2, ChordType::sus4, ChordType::major7, ChordType::minor7,
                ChordType::dominant7, ChordType::add9, ChordType::power
            };

            return all;
        }

        juce::String getName (ChordType type)
        {
            switch (type)
            {
                case ChordType::major:        return "Major";
                case ChordType::minor:        return "Minor";
                case ChordType::diminished:   return "Diminished";
                case ChordType::augmented:    return "Augmented";
                case ChordType::sus2:         return "Sus2";
                case ChordType::sus4:         return "Sus4";
                case ChordType::major7:       return "Major 7";
                case ChordType::minor7:       return "Minor 7";
                case ChordType::dominant7:    return "7";
                case ChordType::add9:         return "Add9";
                case ChordType::power:        return "Power (5)";
                case ChordType::scaleTriad:   return "Scale chord";
                case ChordType::scaleSeventh: return "Scale chord 7";
            }

            return {};
        }

        std::vector<int> build (int root, ChordType type, const scales::Scale& scale)
        {
            std::vector<int> intervals;

            switch (type)
            {
                case ChordType::major:        intervals = { 0, 4, 7 }; break;
                case ChordType::minor:        intervals = { 0, 3, 7 }; break;
                case ChordType::diminished:   intervals = { 0, 3, 6 }; break;
                case ChordType::augmented:    intervals = { 0, 4, 8 }; break;
                case ChordType::sus2:         intervals = { 0, 2, 7 }; break;
                case ChordType::sus4:         intervals = { 0, 5, 7 }; break;
                case ChordType::major7:       intervals = { 0, 4, 7, 11 }; break;
                case ChordType::minor7:       intervals = { 0, 3, 7, 10 }; break;
                case ChordType::dominant7:    intervals = { 0, 4, 7, 10 }; break;
                case ChordType::add9:         intervals = { 0, 4, 7, 14 }; break;
                case ChordType::power:        intervals = { 0, 7, 12 }; break;

                case ChordType::scaleTriad:
                case ChordType::scaleSeventh:
                {
                    const auto count = type == ChordType::scaleTriad ? 3 : 4;

                    if (scale.isChromatic())
                        return build (root, type == ChordType::scaleTriad ? ChordType::major : ChordType::dominant7, scale);

                    std::vector<int> pitches;
                    const auto bottom = scale.snap (root);

                    for (int i = 0; i < count; ++i)
                        if (const auto pitch = scale.step (bottom, 2 * i); i == 0 || pitch > pitches.back())
                            pitches.push_back (pitch);

                    return pitches;
                }
            }

            std::vector<int> pitches;

            for (auto interval : intervals)
                if (root + interval >= 0 && root + interval <= 127)
                    pitches.push_back (root + interval);

            return pitches;
        }

        namespace
        {
            constexpr int maskOf (std::initializer_list<int> intervals)
            {
                int mask = 0;

                for (auto i : intervals)
                    mask |= 1 << (i % 12);

                return mask;
            }

            struct Shape
            {
                const char* suffix;
                int mask;
            };

            // In the order a musician would rather read them when the notes fit more than one.
            constexpr Shape shapes[] {
                { "",      maskOf ({ 0, 4, 7 }) },
                { "m",     maskOf ({ 0, 3, 7 }) },
                { "7",     maskOf ({ 0, 4, 7, 10 }) },
                { "m7",    maskOf ({ 0, 3, 7, 10 }) },
                { "maj7",  maskOf ({ 0, 4, 7, 11 }) },
                { "dim",   maskOf ({ 0, 3, 6 }) },
                { "aug",   maskOf ({ 0, 4, 8 }) },
                { "sus4",  maskOf ({ 0, 5, 7 }) },
                { "sus2",  maskOf ({ 0, 2, 7 }) },
                { "5",     maskOf ({ 0, 7 }) },
                { "6",     maskOf ({ 0, 4, 7, 9 }) },
                { "m6",    maskOf ({ 0, 3, 7, 9 }) },
                { "m7b5",  maskOf ({ 0, 3, 6, 10 }) },
                { "dim7",  maskOf ({ 0, 3, 6, 9 }) },
                { "mMaj7", maskOf ({ 0, 3, 7, 11 }) },
                { "7sus4", maskOf ({ 0, 5, 7, 10 }) },
                { "add9",  maskOf ({ 0, 2, 4, 7 }) },
                { "madd9", maskOf ({ 0, 2, 3, 7 }) },
                { "9",     maskOf ({ 0, 2, 4, 7, 10 }) },
                { "m9",    maskOf ({ 0, 2, 3, 7, 10 }) },
                { "maj9",  maskOf ({ 0, 2, 4, 7, 11 }) },
                { "7",     maskOf ({ 0, 4, 10 }) },         // sevenths are often played without the fifth
                { "m7",    maskOf ({ 0, 3, 10 }) },
                { "maj7",  maskOf ({ 0, 4, 11 }) },
            };

            /** The pitch classes `mask` holds, counted from `root`. */
            int rotate (int mask, int root)
            {
                return ((mask >> root) | (mask << (12 - root))) & 0xfff;
            }
        }

        juce::String nameOf (const std::vector<int>& pitches)
        {
            int mask = 0, bass = 128;

            for (auto p : pitches)
            {
                if (p < 0 || p > 127)
                    continue;

                mask |= 1 << (p % 12);
                bass = std::min (bass, p);
            }

            if (bass > 127 || juce::countNumberOfBits ((juce::uint32) mask) < 2)
                return {};

            const auto bassClass = bass % 12;
            std::optional<std::pair<int, const Shape*>> found;

            // The first shape that fits with the bass note as its root; else the first that fits at all.
            for (const auto& shape : shapes)
            {
                const auto shapeMask = shape.mask;

                if (rotate (mask, bassClass) == shapeMask)
                {
                    found = std::make_pair (bassClass, &shape);
                    break;
                }

                if (! found.has_value())
                    for (int i = 1; i < 12; ++i)
                        if (const auto root = (bassClass + i) % 12; ((mask >> root) & 1) != 0 && rotate (mask, root) == shapeMask)
                        {
                            found = std::make_pair (root, &shape);
                            break;
                        }
            }

            if (! found.has_value())
                return {};

            const auto [root, shape] = *found;
            auto name = scales::getRootName (root) + shape->suffix;

            if (root != bassClass)
                name << "/" << scales::getRootName (bassClass);

            return name;
        }

        std::vector<Span> findChords (const std::vector<Note>& notes, double shortest)
        {
            constexpr double tolerance = 1.0e-6;

            struct Change
            {
                double beat;
                int pitch;
                bool starts;
            };

            std::vector<Change> changes;

            for (const auto& n : notes)
            {
                if (n.muted || n.lengthBeats <= tolerance || n.pitch < 0 || n.pitch > 127)
                    continue;

                changes.push_back ({ n.startBeat, n.pitch, true });
                changes.push_back ({ n.getEndBeat(), n.pitch, false });
            }

            // In time order; at the same moment, notes stop before others start.
            std::sort (changes.begin(), changes.end(), [] (const Change& a, const Change& b)
            {
                return a.beat != b.beat ? a.beat < b.beat : (! a.starts && b.starts);
            });

            std::array<int, 128> sounding {};
            std::vector<Span> pieces;

            for (size_t i = 0; i < changes.size();)
            {
                const auto beat = changes[i].beat;

                for (; i < changes.size() && changes[i].beat - beat <= tolerance; ++i)
                    sounding[(size_t) changes[i].pitch] += changes[i].starts ? 1 : -1;

                if (i == changes.size())
                    break;

                std::vector<int> pitches;

                for (int p = 0; p < 128; ++p)
                    if (sounding[(size_t) p] > 0)
                        pitches.push_back (p);

                auto name = nameOf (pitches);

                if (name.isEmpty() && pitches.size() > 3)
                {
                    pitches.pop_back();     // the melody on top
                    name = nameOf (pitches);
                }

                if (name.isEmpty())
                    continue;

                const auto end = changes[i].beat;

                if (! pieces.empty() && pieces.back().name == name && std::abs (pieces.back().endBeat - beat) <= tolerance)
                    pieces.back().endBeat = end;
                else
                    pieces.push_back ({ beat, end, name });
            }

            std::vector<Span> chords;

            for (const auto& piece : pieces)
            {
                if (piece.endBeat - piece.startBeat < shortest - tolerance)
                    continue;

                if (! chords.empty() && chords.back().name == piece.name && piece.startBeat - chords.back().endBeat <= shortest + tolerance)
                    chords.back().endBeat = piece.endBeat;
                else
                    chords.push_back (piece);
            }

            return chords;
        }
    }
}
