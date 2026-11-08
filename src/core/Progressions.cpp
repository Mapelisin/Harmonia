#include "core/Progressions.h"
#include "core/Snap.h"

namespace harmonia::core
{
    namespace progressions
    {
        namespace
        {
            /** Sum of how far each voice moves (both chords from the bottom up). */
            int getMovement (const std::vector<int>& from, const std::vector<int>& to)
            {
                int total = 0;

                for (size_t i = 0; i < std::min (from.size(), to.size()); ++i)
                    total += std::abs (from[i] - to[i]);

                return total;
            }

            double getMiddle (const std::vector<int>& chord)
            {
                return chord.empty() ? 0.0 : (chord.front() + chord.back()) * 0.5;
            }

            /** The inversion of `chord` (moved by octaves too) that moves the voices least from
                `previous`, without wandering far from `centre`. */
            std::vector<int> voiceNear (const std::vector<int>& chord, const std::vector<int>& previous, double centre)
            {
                std::vector<int> best = chord;
                double bestCost = std::numeric_limits<double>::max();

                for (size_t inversion = 0; inversion < chord.size(); ++inversion)
                {
                    for (int shift : { -12, 0, 12 })
                    {
                        std::vector<int> candidate;

                        for (size_t i = 0; i < chord.size(); ++i)
                            candidate.push_back (chord[i] + shift + (i < inversion ? 12 : 0));

                        std::sort (candidate.begin(), candidate.end());

                        if (candidate.front() < 0 || candidate.back() > 127)
                            continue;

                        const auto cost = getMovement (previous, candidate) + 0.5 * std::abs (getMiddle (candidate) - centre);

                        if (cost < bestCost)
                        {
                            bestCost = cost;
                            best = candidate;
                        }
                    }
                }

                return best;
            }
        }

        const std::vector<Progression>& getAll()
        {
            static const std::vector<Progression> all {
                { "I - V - vi - IV  (pop)", { 0, 4, 5, 3 } },
                { "vi - IV - I - V  (pop, sad)", { 5, 3, 0, 4 } },
                { "I - vi - IV - V  (50s)", { 0, 5, 3, 4 } },
                { "I - IV - V - IV  (rock)", { 0, 3, 4, 3 } },
                { "ii - V - I - I  (jazz)", { 1, 4, 0, 0 } },
                { "I - V - vi - iii - IV - I - IV - V  (canon)", { 0, 4, 5, 2, 3, 0, 3, 4 } },
                { "i - VI - III - VII  (minor pop)", { 0, 5, 2, 6 } },
                { "i - iv - v - i  (minor)", { 0, 3, 4, 0 } },
                { "i - VII - VI - VII  (epic)", { 0, 6, 5, 6 } },
                { "i - VI - iv - v  (dark)", { 0, 5, 3, 4 } },
                { "I - IV  (two chords)", { 0, 3 } },
                { "i - v  (two chords)", { 0, 4 } }
            };

            return all;
        }

        const std::vector<Rhythm>& getRhythms()
        {
            static const std::vector<Rhythm> all { Rhythm::hold, Rhythm::quarters, Rhythm::eighths, Rhythm::offbeats };
            return all;
        }

        juce::String getName (Rhythm rhythm)
        {
            switch (rhythm)
            {
                case Rhythm::hold:     return "Hold";
                case Rhythm::quarters: return "Quarter notes";
                case Rhythm::eighths:  return "Eighth notes";
                case Rhythm::offbeats: return "Offbeats";
            }

            return {};
        }

        double getLength (const Settings& settings)
        {
            const auto& all = getAll();
            const auto& progression = all[(size_t) juce::jlimit (0, (int) all.size() - 1, settings.progression)];
            return (double) progression.degrees.size() * settings.chordBeats * std::max (1, settings.repeats);
        }

        std::vector<Note> generate (const Settings& settings)
        {
            const auto& all = getAll();
            const auto& progression = all[(size_t) juce::jlimit (0, (int) all.size() - 1, settings.progression)];

            auto key = settings.key;

            if (key.isChromatic())
                key.type = scales::ScaleType::major;

            // The tonic chord sits around C of the octave: from G up it starts in the octave below.
            const auto tonic = juce::jlimit (0, 127, 12 * (settings.octave + 1) + key.root - (key.root >= 7 ? 12 : 0));
            const auto chordType = settings.sevenths ? chords::ChordType::scaleSeventh : chords::ChordType::scaleTriad;
            const auto centre = getMiddle (chords::build (tonic, chordType, key));

            // When each chord's notes sound within its length.
            std::vector<std::pair<double, double>> hits;

            switch (settings.rhythm)
            {
                case Rhythm::hold:
                    hits.emplace_back (0.0, settings.chordBeats);
                    break;

                case Rhythm::quarters:
                case Rhythm::eighths:
                case Rhythm::offbeats:
                {
                    const auto step = settings.rhythm == Rhythm::quarters ? 1.0 : (settings.rhythm == Rhythm::eighths ? 0.5 : 1.0);
                    const auto first = settings.rhythm == Rhythm::offbeats ? 0.5 : 0.0;
                    const auto length = settings.rhythm == Rhythm::quarters ? 0.9 : 0.45;

                    for (auto at = first; at < settings.chordBeats - 1.0e-6; at += step)
                        hits.emplace_back (at, std::min (length, settings.chordBeats - at));

                    break;
                }
            }

            std::vector<Note> notes;
            std::vector<int> previous;
            auto start = settings.startBeat;

            for (int pass = 0; pass < std::max (1, settings.repeats); ++pass)
            {
                for (const auto degree : progression.degrees)
                {
                    const auto root = key.step (tonic, degree);
                    auto chord = chords::build (root, chordType, key);

                    if (settings.smooth && ! previous.empty())
                        chord = voiceNear (chord, previous, centre);

                    previous = chord;

                    const auto addNote = [&] (int pitch, double at, double length)
                    {
                        Note n;
                        n.pitch = pitch;
                        n.startBeat = snap::roundToTicks (start + at);
                        n.lengthBeats = snap::roundToTicks (length);
                        n.velocity = settings.velocity;
                        notes.push_back (n);
                    };

                    if (settings.bass && root - 12 >= 0)
                        addNote (root - 12, 0.0, settings.chordBeats);

                    for (const auto& [at, length] : hits)
                        for (auto pitch : chord)
                            addNote (pitch, at, length);

                    start += settings.chordBeats;
                }
            }

            return notes;
        }
    }

    namespace scales
    {
        Scale detect (const std::vector<Note>& notes)
        {
            if (notes.empty())
                return {};

            // How long each pitch class sounds.
            std::array<double, 12> weights {};

            for (const auto& n : notes)
                weights[(size_t) (n.pitch % 12)] += std::max (0.0625, n.lengthBeats);

            static constexpr std::array<double, 12> majorProfile { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
            static constexpr std::array<double, 12> minorProfile { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

            const auto correlate = [&weights] (const std::array<double, 12>& profile, int root)
            {
                double meanW = 0.0, meanP = 0.0;

                for (int i = 0; i < 12; ++i)
                {
                    meanW += weights[(size_t) i] / 12.0;
                    meanP += profile[(size_t) i] / 12.0;
                }

                double sum = 0.0, sumW = 0.0, sumP = 0.0;

                for (int i = 0; i < 12; ++i)
                {
                    const auto w = weights[(size_t) ((i + root) % 12)] - meanW;
                    const auto p = profile[(size_t) i] - meanP;
                    sum += w * p;
                    sumW += w * w;
                    sumP += p * p;
                }

                return sumW > 0.0 ? sum / std::sqrt (sumW * sumP) : 0.0;
            };

            Scale best { 0, ScaleType::major };
            auto bestScore = -2.0;

            for (int root = 0; root < 12; ++root)
            {
                for (const auto type : { ScaleType::major, ScaleType::minor })
                {
                    const auto score = correlate (type == ScaleType::major ? majorProfile : minorProfile, root);

                    if (score > bestScore + 1.0e-9)
                    {
                        bestScore = score;
                        best = { root, type };
                    }
                }
            }

            return best;
        }
    }
}
