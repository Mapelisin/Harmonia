#pragma once

#include "audio/Transport.h"
#include "ui/stepseq/StepSequencerModel.h"

namespace harmonia::ui::stepseq
{
    /** The step sequencer: the open clip as a grid, drums down and steps across. Click a cell to
        put a hit there (it plays), click a hit to take it away, drag to do the same along the way;
        the right button takes hits away; Alt+wheel makes a hit harder or softer (brighter or
        dimmer). A click on a row's name plays it; its right button clears the row.

        It shows the same notes as the piano roll, whose place it takes (the Piano roll button
        goes back). The column being played is lit. */
    class StepSequencer final : public juce::Component,
                                private pianoroll::PianoRollModel::Listener,
                                private juce::Timer
    {
    public:
        StepSequencer (pianoroll::PianoRollModel&, audio::Transport&);
        ~StepSequencer() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        StepSequencerModel& getModel() noexcept         { return model; }

        /** Re-reads the rows and steps (it follows the clip by itself). */
        void refresh();

        /** The Piano roll button was clicked. */
        std::function<void()> onShowNotes;

        //==============================================================================
        // For tests: where things are, and what the mouse does.

        /** The grid's cell (in the grid's own coordinates: its whole size, not what's on screen). */
        juce::Rectangle<int> getCellBounds (int row, int step) const;
        juce::Point<int> getGridSize() const;

        /** The row and step at a point of the grid; nothing outside the cells. */
        std::optional<std::pair<int, int>> getCellAt (juce::Point<int>) const;

        /** A press, a drag and a release on the grid, at grid coordinates. */
        void gridDown (juce::Point<int>, bool rightButton);
        void gridDrag (juce::Point<int>);
        void gridUp();
        void gridWheel (juce::Point<int>, float deltaY);

        /** The step the song is in now, if it is inside the clip. */
        std::optional<int> getPlayingStep() const noexcept      { return playingStep; }

        juce::TextButton& getNotesButton() noexcept             { return notesButton; }
        juce::ComboBox& getStepBox() noexcept                   { return stepBox; }
        juce::TextButton& getClearButton() noexcept             { return clearButton; }

        /** The menu of a row's name (right button). */
        juce::PopupMenu createRowMenu (int pitch);

        static constexpr int toolbarHeight = 34, namesWidth = 120, rowHeight = 22, headerHeight = 18;

    private:
        class Grid;
        class Names;
        struct View;

        pianoroll::PianoRollModel& notes;
        audio::Transport& transport;
        StepSequencerModel model { notes };

        juce::TextButton notesButton { "Piano roll" }, clearButton { "Clear" };
        juce::Label stepLabel { {}, "Step" }, infoLabel;
        juce::ComboBox stepBox;
        std::unique_ptr<Names> names;
        std::unique_ptr<Grid> grid;
        std::unique_ptr<View> view;

        std::vector<Row> rows;
        int numSteps = 0, cellWidth = 24;
        std::optional<int> playingStep;

        // The drag in progress: what it paints, and the last cell it did.
        bool painting = false, erasing = false;
        std::optional<std::pair<int, int>> lastCell;

        void pianoRollChanged() override;
        void timerCallback() override;
        void layOut();

        JUCE_DECLARE_NON_COPYABLE (StepSequencer)
    };
}
