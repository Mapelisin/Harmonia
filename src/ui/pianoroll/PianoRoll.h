#pragma once

#include "audio/Transport.h"
#include "ui/pianoroll/ControlLane.h"
#include "ui/pianoroll/NoteGrid.h"
#include "ui/pianoroll/PianoKeys.h"
#include "ui/pianoroll/TimeRuler.h"

namespace harmonia::ui::pianoroll
{
    /** The piano roll: toolbar, ruler, keyboard, note grid, lower panel and scrollbars, plus the
        keyboard shortcuts. Edits the model's clip; shows the playhead of `transport`. */
    class PianoRoll final : public juce::Component,
                            private PianoRollModel::Listener,
                            private juce::ScrollBar::Listener
    {
    public:
        PianoRoll (PianoRollModel&, audio::Transport&);
        ~PianoRoll() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

        View& getView() noexcept                    { return view; }
        NoteGrid& getGrid() noexcept                { return grid; }

        /** Scrolls so that the clip's start and the notes (or middle C) are in view. */
        void showClip();

    private:
        PianoRollModel& model;
        audio::Transport& transport;
        View view;

        NoteGrid grid { model, view };
        PianoKeys keys { model, view };
        TimeRuler ruler { model, view };
        ControlLane lane { model, view };

        juce::OwnedArray<juce::TextButton> toolButtons;
        juce::ComboBox snapBox, laneBox;
        juce::ToggleButton ghostsButton { "Ghost notes" };
        juce::Label snapLabel, infoLabel;
        juce::ScrollBar horizontalScroll { false }, verticalScroll { true };

        juce::VBlankAttachment vblank { this, [this] { updatePlayhead(); } };
        std::optional<double> lastPlayheadBeat;
        bool showClipWhenSized = false;

        void pianoRollChanged() override;
        void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

        void viewChanged();
        void updateContentLength();
        void updateScrollBars();
        void updateControls();
        void updatePlayhead();
        void repaintAll();
        void zoomAroundMouse (double factor);
    };
}
