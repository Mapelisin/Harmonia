#include "core/TempoChanges.h"
#include "core/Tempo.h"

namespace harmonia::core::tempo_changes
{
    namespace
    {
        // Tracktion's curve of a tempo is how the song gets from it to the next one: 1 holds it
        // until the next (a jump there), 0 goes there in a straight line.
        constexpr float jump = 1.0f, glide = 0.0f;

        bool glidesOn (const te::TempoSetting& tempo)
        {
            return std::abs (tempo.getCurve()) < 0.999f;
        }

        te::TempoSetting* get (te::Edit& edit, int index)
        {
            return edit.tempoSequence.getTempo (index);
        }

        double beatOf (const te::TempoSetting& tempo)
        {
            return tempo.getStartBeat().inBeats();
        }
    }

    std::vector<Change> getAll (const te::Edit& edit)
    {
        auto& sequence = edit.tempoSequence;
        std::vector<Change> changes;

        for (int i = 0; i < sequence.getNumTempos(); ++i)
        {
            const auto& tempo = *sequence.getTempo (i);
            changes.push_back ({ beatOf (tempo), tempo.getBpm(), i > 0 && glidesOn (*sequence.getTempo (i - 1)) });
        }

        return changes;
    }

    int getIndexAt (const te::Edit& edit, double beat)
    {
        auto& sequence = edit.tempoSequence;
        int index = 0;

        for (int i = 1; i < sequence.getNumTempos(); ++i)
            if (beatOf (*sequence.getTempo (i)) <= beat + 1.0e-9)
                index = i;

        return index;
    }

    double getBpmAt (const te::Edit& edit, double beat)
    {
        auto& sequence = edit.tempoSequence;
        return sequence.getBpmAt (sequence.toTime (te::BeatPosition::fromBeats (std::max (0.0, beat))));
    }

    int add (te::Edit& edit, double beat, double bpm)
    {
        beat = std::max (0.0, beat);
        bpm = tempo::clampBpm (bpm);
        const auto changes = getAll (edit);

        for (int i = 0; i < (int) changes.size(); ++i)
        {
            if (std::abs (changes[(size_t) i].beat - beat) < minGapBeats)
            {
                setBpm (edit, i, bpm);
                return i;
            }
        }

        te::EditTimecodeRemapperSnapshot snapshot;
        snapshot.savePreChangeState (edit);

        // What glided to the next change glides to this one now, and on from it.
        const auto before = getIndexAt (edit, beat);
        const auto curve = glidesOn (*get (edit, before)) ? glide : jump;
        edit.tempoSequence.insertTempo (te::BeatPosition::fromBeats (beat), bpm, curve);

        snapshot.remapEdit (edit);
        return getIndexAt (edit, beat);
    }

    void setBpm (te::Edit& edit, int index, double bpm)
    {
        if (auto* tempo = get (edit, index))
            tempo->setBpm (tempo::clampBpm (bpm));
    }

    void move (te::Edit& edit, int index, double beat)
    {
        auto* tempo = get (edit, index);

        if (index <= 0 || tempo == nullptr)
            return;

        const auto changes = getAll (edit);
        const auto earliest = changes[(size_t) index - 1].beat + minGapBeats;
        const auto latest = index + 1 < (int) changes.size() ? changes[(size_t) index + 1].beat - minGapBeats
                                                             : std::numeric_limits<double>::max();

        if (latest < earliest)
            return;

        tempo->set (te::BeatPosition::fromBeats (juce::jlimit (earliest, latest, beat)), tempo->getBpm(), tempo->getCurve(), true);
    }

    void setGradual (te::Edit& edit, int index, bool shouldGlide)
    {
        auto* before = get (edit, index - 1);

        if (index <= 0 || before == nullptr || get (edit, index) == nullptr)
            return;

        before->set (before->getStartBeat(), before->getBpm(), shouldGlide ? glide : jump, true);
    }

    void remove (te::Edit& edit, int index)
    {
        if (index <= 0 || get (edit, index) == nullptr)
            return;

        // The glide to it would become one to the change after.
        const auto glided = glidesOn (*get (edit, index - 1));
        const auto glidesFurther = glidesOn (*get (edit, index));
        edit.tempoSequence.removeTempo (index, true);

        if (glided != glidesFurther)
            if (auto* before = get (edit, index - 1))
                before->set (before->getStartBeat(), before->getBpm(), glidesFurther ? glide : jump, true);
    }

    juce::String describe (const te::Edit& edit, const Change& change)
    {
        const auto beatsPerBar = std::max (1, edit.tempoSequence.getTimeSig (0)->numerator.get());
        const auto bar = std::floor (change.beat / beatsPerBar + 1.0e-9);
        const auto beatInBar = change.beat - bar * beatsPerBar;
        auto where = "bar " + juce::String ((int) bar + 1);

        if (beatInBar > 1.0e-6)
            where << "." << juce::String (beatInBar + 1.0, 2);

        return where + ": " + (change.gradual ? "glide to " : "") + tempo::toText (change.bpm);
    }
}
