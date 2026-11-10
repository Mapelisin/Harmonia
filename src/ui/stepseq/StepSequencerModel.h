#pragma once

#include "ui/pianoroll/PianoRollModel.h"

namespace harmonia::ui::stepseq
{
    /** A row of the step sequencer: a key and what it is called ("Kick", or the note's name). */
    struct Row
    {
        int pitch = 0;
        juce::String name;

        bool operator== (const Row&) const = default;
    };

    /** The step sequencer's behaviour, without drawing: the open clip (the piano roll's) seen as
        a grid of drums down and steps across, where a click puts a hit or takes it away.

        It is another view of the clip's notes, nothing of its own is kept: a hit is a note that
        starts in the step, on the row's key. Notes made in the piano roll show here (also those
        off the grid: in the step they start in), and the hits made here are notes there.
        A click is one undo step, a drag across cells too. */
    class StepSequencerModel final
    {
    public:
        explicit StepSequencerModel (pianoroll::PianoRollModel&);

        pianoroll::PianoRollModel& getNotes() noexcept      { return notes; }
        bool hasClip() const                                { return notes.getClip() != nullptr; }

        //==============================================================================
        /** Steps to a beat: 2 (eighths), 3 (eighth triplets), 4 (sixteenths, as it starts), 6, 8. */
        static const std::vector<int>& getStepChoices();
        static juce::String getStepName (int stepsPerBeat);     // "1/16"

        int getStepsPerBeat() const noexcept                { return stepsPerBeat; }
        void setStepsPerBeat (int);
        double getStepBeats() const noexcept                { return 1.0 / (double) stepsPerBeat; }

        /** The steps of the clip, from its start to its end (a looping clip: of its loop). */
        int getNumSteps() const;

        /** The song beat a step starts at. */
        double getStepBeat (int step) const;

        /** The step a song beat is in (may be outside 0..getNumSteps()). */
        int getStepAt (double beat) const;

        /** Steps to a bar and to a beat: where the grid draws its stronger lines. */
        int getStepsPerBar() const;

        //==============================================================================
        /** The rows, top to bottom (the highest key first, as in the piano roll): the kit's main
            drums, every other key the clip has notes on, and the keys of the drum machine that
            have samples. Named as the piano roll's keys are (drum names, where they are shown). */
        std::vector<Row> getRows();

        /** How hard the hit in this cell is (the loudest note starting in the step): 0 for none. */
        int getVelocity (int pitch, int step);
        bool hasHit (int pitch, int step)                   { return getVelocity (pitch, step) > 0; }

        //==============================================================================
        /** A click on a cell: a hit there (a step long, played once), or the hit that was there
            taken away. One undo step. Returns whether the cell has a hit now. */
        bool toggle (int pitch, int step);

        /** A drag across cells, after the click that started it: begin, then set every cell
            passed to what the click made of the first. One undo step for the whole drag. */
        void beginPaint (bool hits);
        void paint (int pitch, int step);

        /** How hard the cell's hit is, 1..127 (nothing for an empty cell): into the current undo
            step (the caller starts it). */
        void setVelocity (int pitch, int step, int velocity);

        /** Alt+wheel over a hit: harder or softer by `delta`, an undo step of its own. */
        void nudgeVelocity (int pitch, int step, int delta);

        /** Takes every hit of the row away; of every row. One undo step each. */
        void clearRow (int pitch);
        void clearAll();

        /** The velocity a new hit gets. */
        static constexpr int defaultVelocity = 100;

    private:
        pianoroll::PianoRollModel& notes;
        int stepsPerBeat = 4;
        bool paintsHits = true;

        juce::Array<te::MidiNote*> getNotesIn (int pitch, int step);
        void setCell (int pitch, int step, bool hit);

        JUCE_DECLARE_NON_COPYABLE (StepSequencerModel)
    };
}
