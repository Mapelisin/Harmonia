#include "ui/stepseq/StepSequencerModel.h"
#include "audio/DrumKit.h"
#include "core/Drums.h"

namespace harmonia::ui::stepseq
{
    namespace
    {
        // A note this close before a step's line belongs to that step (played a hair early).
        constexpr double early = 1.0e-6;

        // Kick, rim, snare, clap, closed hat, low tom, open hat, mid tom, crash, high tom, ride.
        constexpr int mainDrums[] = { 36, 37, 38, 39, 42, 45, 46, 47, 49, 50, 51 };
    }

    StepSequencerModel::StepSequencerModel (pianoroll::PianoRollModel& n)
        : notes (n)
    {
    }

    const std::vector<int>& StepSequencerModel::getStepChoices()
    {
        static const std::vector<int> choices { 2, 3, 4, 6, 8 };
        return choices;
    }

    juce::String StepSequencerModel::getStepName (int steps)
    {
        switch (steps)
        {
            case 2:  return "1/8";
            case 3:  return "1/8 triplets";
            case 4:  return "1/16";
            case 6:  return "1/16 triplets";
            case 8:  return "1/32";
            default: return juce::String (steps) + " to a beat";
        }
    }

    void StepSequencerModel::setStepsPerBeat (int steps)
    {
        const auto& choices = getStepChoices();

        if (std::find (choices.begin(), choices.end(), steps) != choices.end())
            stepsPerBeat = steps;
    }

    int StepSequencerModel::getNumSteps() const
    {
        if (! hasClip())
            return 0;

        const auto beats = notes.getClipEndBeat() - notes.getClipStartBeat();
        return std::max (0, (int) std::ceil (beats * stepsPerBeat - 1.0e-6));
    }

    double StepSequencerModel::getStepBeat (int step) const
    {
        return notes.getClipStartBeat() + (double) step / (double) stepsPerBeat;
    }

    int StepSequencerModel::getStepAt (double beat) const
    {
        return (int) std::floor ((beat - notes.getClipStartBeat()) * stepsPerBeat + early);
    }

    int StepSequencerModel::getStepsPerBar() const
    {
        return juce::roundToInt (notes.getBeatsPerBar() * stepsPerBeat);
    }

    //==============================================================================
    std::vector<Row> StepSequencerModel::getRows()
    {
        if (! hasClip())
            return {};

        std::array<bool, 128> shown {};

        for (const auto pitch : mainDrums)
            shown[(size_t) pitch] = true;

        for (const auto& note : notes.getNotes())
            shown[(size_t) juce::jlimit (0, 127, note.values.pitch)] = true;

        if (auto* drums = notes.getDrumMachine())
            for (const auto pitch : audio::drum_kit::getSampledKeys (*drums))
                shown[(size_t) pitch] = true;

        std::vector<Row> rows;

        for (int pitch = 127; pitch >= 0; --pitch)
        {
            if (! shown[(size_t) pitch])
                continue;

            // As the piano roll's keys say: the drum (or its sample), else the note.
            auto name = notes.getDrumName (pitch);

            if (name.isEmpty())
                name = juce::MidiMessage::getMidiNoteName (pitch, true, true, 4);

            rows.push_back ({ pitch, name });
        }

        return rows;
    }

    juce::Array<te::MidiNote*> StepSequencerModel::getNotesIn (int pitch, int step)
    {
        juce::Array<te::MidiNote*> found;

        if (! hasClip() || step < 0 || step >= getNumSteps())
            return found;

        const auto from = getStepBeat (step) - early, to = getStepBeat (step + 1) - early;
        const auto [first, last] = notes.getNotesInRange (from, to);
        const auto& all = notes.getNotes();

        for (auto i = first; i < last; ++i)
            if (all[i].values.pitch == pitch && all[i].values.startBeat >= from && all[i].values.startBeat < to)
                found.add (all[i].note);

        return found;
    }

    int StepSequencerModel::getVelocity (int pitch, int step)
    {
        auto loudest = 0;

        for (auto* note : getNotesIn (pitch, step))
            if (const auto values = notes.read (*note); ! values.muted)
                loudest = std::max (loudest, std::max (1, values.velocity));

        return loudest;
    }

    //==============================================================================
    void StepSequencerModel::setCell (int pitch, int step, bool hit)
    {
        if (! hasClip() || step < 0 || step >= getNumSteps() || pitch < 0 || pitch > 127)
            return;

        const auto there = getNotesIn (pitch, step);

        if (! hit)
        {
            if (! there.isEmpty())
                notes.removeNotes (there);

            return;
        }

        if (hasHit (pitch, step))
            return;

        // Muted notes there would hide under the new hit: it takes their place.
        if (! there.isEmpty())
            notes.removeNotes (there);

        core::Note note;
        note.pitch = pitch;
        note.startBeat = getStepBeat (step);
        note.lengthBeats = getStepBeats();
        note.velocity = defaultVelocity;
        notes.addNote (note);
    }

    bool StepSequencerModel::toggle (int pitch, int step)
    {
        if (! hasClip() || step < 0 || step >= getNumSteps())
            return false;

        const auto hit = ! hasHit (pitch, step);
        notes.beginGesture (hit ? "Add hit" : "Remove hit");
        setCell (pitch, step, hit);

        // Heard as it is put down.
        if (hit)
        {
            notes.auditionStart (pitch, defaultVelocity);
            notes.auditionStop();
        }

        return hit;
    }

    void StepSequencerModel::beginPaint (bool hits)
    {
        paintsHits = hits;
    }

    void StepSequencerModel::paint (int pitch, int step)
    {
        setCell (pitch, step, paintsHits);
    }

    void StepSequencerModel::setVelocity (int pitch, int step, int velocity)
    {
        std::vector<core::NoteEditor::Change> changes;

        for (auto* note : getNotesIn (pitch, step))
        {
            auto values = notes.read (*note);
            values.velocity = juce::jlimit (1, 127, velocity);
            changes.push_back ({ note, values });
        }

        if (! changes.empty())
            notes.updateNotes (changes);
    }

    void StepSequencerModel::nudgeVelocity (int pitch, int step, int delta)
    {
        const auto current = getVelocity (pitch, step);

        if (current == 0 || delta == 0)
            return;

        notes.beginGesture ("Hit velocity");
        setVelocity (pitch, step, current + delta);
    }

    void StepSequencerModel::clearRow (int pitch)
    {
        juce::Array<te::MidiNote*> row;

        for (const auto& note : notes.getNotes())
            if (note.values.pitch == pitch)
                row.add (note.note);

        if (row.isEmpty())
            return;

        notes.beginGesture ("Clear row");
        notes.removeNotes (row);
    }

    void StepSequencerModel::clearAll()
    {
        juce::Array<te::MidiNote*> all;

        for (const auto& note : notes.getNotes())
            all.add (note.note);

        if (all.isEmpty())
            return;

        notes.beginGesture ("Clear all hits");
        notes.removeNotes (all);
    }
}
