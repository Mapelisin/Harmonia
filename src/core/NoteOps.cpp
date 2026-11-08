#include "core/NoteOps.h"
#include "core/Snap.h"

namespace harmonia::core
{
    namespace note_property
    {
        const std::vector<NoteProperty>& getAll()
        {
            static const std::vector<NoteProperty> all {
                NoteProperty::velocity, NoteProperty::release, NoteProperty::pan,
                NoteProperty::modX, NoteProperty::modY, NoteProperty::finePitch
            };

            return all;
        }

        juce::String getName (NoteProperty p)
        {
            switch (p)
            {
                case NoteProperty::velocity:  return "Velocity";
                case NoteProperty::release:   return "Release";
                case NoteProperty::pan:       return "Pan";
                case NoteProperty::modX:      return "Mod X";
                case NoteProperty::modY:      return "Mod Y";
                case NoteProperty::finePitch: return "Fine pitch";
            }

            return {};
        }

        bool isBipolar (NoteProperty p)
        {
            return p == NoteProperty::pan || p == NoteProperty::finePitch;
        }

        float get (const Note& n, NoteProperty p)
        {
            switch (p)
            {
                case NoteProperty::velocity:  return (float) n.velocity / 127.0f;
                case NoteProperty::release:   return (float) n.releaseVelocity / 127.0f;
                case NoteProperty::pan:       return (n.pan + 1.0f) * 0.5f;
                case NoteProperty::modX:      return n.modX;
                case NoteProperty::modY:      return n.modY;
                case NoteProperty::finePitch: return (n.finePitchCents / note::maxFinePitchCents + 1.0f) * 0.5f;
            }

            return 0.0f;
        }

        void set (Note& n, NoteProperty p, float normalised)
        {
            const auto v = juce::jlimit (0.0f, 1.0f, normalised);

            switch (p)
            {
                case NoteProperty::velocity:  n.velocity = juce::roundToInt (v * 127.0f); break;
                case NoteProperty::release:   n.releaseVelocity = juce::roundToInt (v * 127.0f); break;
                case NoteProperty::pan:       n.pan = v * 2.0f - 1.0f; break;
                case NoteProperty::modX:      n.modX = v; break;
                case NoteProperty::modY:      n.modY = v; break;
                case NoteProperty::finePitch: n.finePitchCents = std::round ((v * 2.0f - 1.0f) * note::maxFinePitchCents); break;
            }
        }

        float getDefault (NoteProperty p)
        {
            return get (Note {}, p);
        }

        juce::String describe (const Note& n, NoteProperty p)
        {
            const auto name = getName (p) + " ";

            switch (p)
            {
                case NoteProperty::velocity:  return name + juce::String (n.velocity);
                case NoteProperty::release:   return name + juce::String (n.releaseVelocity);
                case NoteProperty::modX:      return name + juce::String (juce::roundToInt (n.modX * 100.0f)) + "%";
                case NoteProperty::modY:      return name + juce::String (juce::roundToInt (n.modY * 100.0f)) + "%";

                case NoteProperty::pan:
                {
                    const auto percent = juce::roundToInt (std::abs (n.pan) * 100.0f);
                    return percent == 0 ? name + "centre"
                                        : name + juce::String (percent) + (n.pan < 0.0f ? "% L" : "% R");
                }

                case NoteProperty::finePitch:
                {
                    const auto cents = juce::roundToInt (n.finePitchCents);
                    return name + (cents > 0 ? "+" : "") + juce::String (cents) + " cents";
                }
            }

            return {};
        }
    }

    namespace note_ops
    {
        BeatRange getSpan (const std::vector<Note>& notes)
        {
            if (notes.empty())
                return {};

            BeatRange span { notes.front().startBeat, notes.front().getEndBeat() };

            for (const auto& n : notes)
            {
                span.start = std::min (span.start, n.startBeat);
                span.end = std::max (span.end, n.getEndBeat());
            }

            return span;
        }

        Note quantized (const Note& source, double gridBeats, bool startsOnly)
        {
            if (gridBeats <= 0.0)
                return source;

            auto n = source;
            n.startBeat = snap::roundToTicks (std::max (0.0, std::round (source.startBeat / gridBeats) * gridBeats));

            if (startsOnly)
                return n;

            const auto end = std::round (source.getEndBeat() / gridBeats) * gridBeats;
            n.lengthBeats = snap::roundToTicks (std::max (gridBeats, end - n.startBeat));
            return n;
        }

        std::pair<Note, Note> split (const Note& source, double beat)
        {
            jassert (beat > source.startBeat && beat < source.getEndBeat());

            auto left = source;
            auto right = source;
            left.lengthBeats = beat - source.startBeat;
            right.startBeat = beat;
            right.lengthBeats = source.getEndBeat() - beat;
            return { left, right };
        }

        int limitTransposition (const std::vector<Note>& notes, int semitones)
        {
            if (notes.empty())
                return 0;

            int lowest = 127, highest = 0;

            for (const auto& n : notes)
            {
                lowest = std::min (lowest, n.pitch);
                highest = std::max (highest, n.pitch);
            }

            return juce::jlimit (-lowest, 127 - highest, semitones);
        }

        double limitMove (const std::vector<Note>& notes, double deltaBeats, double minStart)
        {
            if (notes.empty())
                return 0.0;

            auto earliest = notes.front().startBeat;

            for (const auto& n : notes)
                earliest = std::min (earliest, n.startBeat);

            return std::max (deltaBeats, minStart - earliest);
        }

        double getDuplicateOffset (const std::vector<Note>& notes)
        {
            if (notes.empty())
                return 0.0;

            const auto span = getSpan (notes);
            const auto from = std::floor (span.start + 1.0e-9);
            return std::max (1.0, std::ceil (span.end - from - 1.0e-9));
        }
    }
}
