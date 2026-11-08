#pragma once

#include "audio/Transport.h"
#include "ui/pianoroll/ControlLane.h"
#include "ui/pianoroll/NoteGrid.h"
#include "ui/pianoroll/NoteToolWindow.h"
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

        /** Zooms so the selected notes, or else the clip (its loop) and all its notes, fill the view (Z). */
        void zoomToFit();

        TimeRuler& getRuler() noexcept              { return ruler; }
        PianoKeys& getKeys() noexcept               { return keys; }

        /** The line right of the tools: the clip's size, or the chord the selected notes make. */
        juce::String getInfoText() const            { return infoLabel.getText(); }

        /** The Tools button's menu: the note commands with their shortcuts. */
        juce::PopupMenu createToolsMenu();

        /** The Scale button's menu: root, scale, snapping to it. */
        juce::PopupMenu createScaleMenu();

        /** The Chord button's menu: what a click in Draw puts down. */
        juce::PopupMenu createChordMenu();

        /** Opens a tool's window on the selected notes (or all); nothing if there are no notes. */
        void openNoteTool (NoteTool);

        /** Where a tool's window goes; by default a call-out box at the Tools button. (Tests
            take it here: a call-out box needs the piano roll on the screen.) */
        std::function<void (std::unique_ptr<NoteToolWindow>)> onShowNoteTool;

        /** Something to tell the user (why a tool didn't open...). */
        std::function<void (const juce::String&)> onMessage;

        /** Off while the computer keyboard plays notes: letters, digits and punctuation are left
            to it (keyPressed returns false for them). */
        void setLetterShortcutsEnabled (bool shouldBeEnabled) noexcept   { letterShortcutsEnabled = shouldBeEnabled; }

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
        static constexpr int firstControllerId = 101;     // laneBox: the controllers after the note values
        juce::TextButton toolsButton { "Tools" }, scaleButton, chordButton;
        juce::ToggleButton ghostsButton { "Ghost notes" };
        juce::Label snapLabel, infoLabel;
        juce::ScrollBar horizontalScroll { false }, verticalScroll { true };

        juce::VBlankAttachment vblank { this, [this] { updatePlayhead(); } };
        std::optional<double> lastPlayheadBeat;
        bool showClipWhenSized = false;
        bool letterShortcutsEnabled = true;

        void pianoRollChanged() override;
        void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

        void viewChanged();
        void updateContentLength();
        void updateScrollBars();
        void updateControls();
        void updatePlayhead();
        void repaintAll();
        void zoomAroundMouse (double factor);
        void showToolsMenu();
    };
}
