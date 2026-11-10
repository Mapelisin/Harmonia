#pragma once

#include "audio/Transport.h"
#include "ui/arrangement/ArrangementRuler.h"
#include "ui/arrangement/ClipArea.h"
#include "ui/arrangement/TrackHeaders.h"

namespace harmonia::ui::arrangement
{
    /** The arrangement (FL's playlist): track headers, ruler, clips, scrollbars, "+ Track" and
        Snap, plus its keyboard shortcuts (they work while it has keyboard focus). Audio files
        dragged in from Explorer become audio clips where they are dropped. */
    class Arrangement final : public juce::Component,
                              public juce::FileDragAndDropTarget,
                              public juce::DragAndDropTarget,
                              private ArrangementModel::Listener,
                              private juce::ScrollBar::Listener
    {
    public:
        Arrangement (ArrangementModel&, audio::Transport&);
        ~Arrangement() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

        ArrangementView& getView() noexcept         { return view; }
        ClipArea& getClipArea() noexcept            { return clipArea; }
        TrackHeaders& getHeaders() noexcept         { return headers; }

        /** Delete, Ctrl+B (linked copies), Ctrl+Shift+B, Ctrl+A, Ctrl+D / Escape, Ctrl+C / X / V, Ctrl+E;
            Ctrl+Insert / Ctrl+Delete: bars on every track over the loop region. With a piece of
            time selected: Ctrl+B repeats it, Ctrl+L makes it the loop region. */
        bool handleShortcut (const juce::KeyPress&);

        /** Ctrl+C / Ctrl+X / Ctrl+V / Ctrl+E, with a word for onMessage. Paste and split go where
            the playhead is (paste snapped to the grid, on the current track). While a piece of
            time is selected (ArrangementModel::TimeSelection) they act on it: copy and cut what
            it has, paste at its start, split at its two edges; Del deletes what it has. */
        void deleteSelection();
        void copySelected();
        void cutSelected();
        void pasteAtPlayhead();
        void splitAtPlayhead();

        /** Ctrl+J: one clip of the selected time (or of the selected clips) on each row; says what
            was done, or why nothing was. */
        void consolidate();

        /** The clip's menu: its hits onto the keys of a new drum track; says what was made. */
        void sliceToDrums (te::AudioClipBase&);

        /** The view scrolled or zoomed (what lines up with it repaints). */
        std::function<void()> onViewChanged;

        /** A position was clicked on the ruler. */
        std::function<void (double beat)> onPositionChosen;

        /** Something to tell the user (e.g. a dropped file couldn't be read). */
        std::function<void (const juce::String& text, bool isProblem)> onMessage;

        bool isInterestedInFileDrag (const juce::StringArray&) override;
        void fileDragEnter (const juce::StringArray&, int x, int y) override;
        void fileDragMove (const juce::StringArray&, int x, int y) override;
        void fileDragExit (const juce::StringArray&) override;
        void filesDropped (const juce::StringArray&, int x, int y) override;

        /** Sounds dragged from the sound browser: dropped like files from the file explorer. */
        bool isInterestedInDragSource (const SourceDetails&) override;
        void itemDragEnter (const SourceDetails&) override;
        void itemDragMove (const SourceDetails&) override;
        void itemDragExit (const SourceDetails&) override;
        void itemDropped (const SourceDetails&) override;

        /** Where files dropped at this point (in the arrangement's coordinates) would go: MIDI
            files onto instrument tracks, audio files onto audio tracks (else onto new ones). */
        ClipArea::DropMarker getDropTarget (juce::Point<int>, bool midi = false) const;

        /** Zooms so the selected clips, or else the whole song, fill the width (Z). */
        void zoomToFit();

        /** Off while the computer keyboard plays notes: Z is a note then, not a shortcut. */
        void setLetterShortcutsEnabled (bool shouldBeEnabled) noexcept   { letterShortcutsEnabled = shouldBeEnabled; }

        /** Asks where to save the clip's notes as a MIDI file. */
        void exportClipAsMidi (te::MidiClip&);

        static constexpr int headerWidth = 200, rulerHeight = 40, scrollBarSize = 10;

        ArrangementRuler& getRuler() noexcept       { return ruler; }

        /** Adds a marker where the playhead is (the "+ Marker" button). */
        void addMarkerAtPlayhead();

        /** Asks for the marker's new name. */
        void askToRenameMarker (const core::markers::Marker&);
        void askToRenameFolder (const core::folders::Folder&);

        /** Asks for the tempo of a change at `beat`: a new one, or that of `index`. */
        void askForTempo (double beat, std::optional<int> index);

        /** What was typed as a tempo ("92,5", "90 bpm"), if it is one. */
        static std::optional<double> parseTempo (const juce::String&);

    private:
        ArrangementModel& model;
        audio::Transport& transport;
        ArrangementView view;

        ClipArea clipArea { model, view };
        ArrangementRuler ruler { model, view };
        TrackHeaders headers { model, view };
        juce::TextButton addTrackButton { "+ Track" }, addMarkerButton { "+ Marker" };
        juce::ComboBox snapBox;
        juce::ScrollBar horizontalScroll { false }, verticalScroll { true };

        juce::VBlankAttachment vblank { this, [this] { updatePlayhead(); } };
        bool letterShortcutsEnabled = true;

        void arrangementChanged() override;
        void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

        void viewChanged();
        void updateContentSize();
        void updateScrollBars();
        void updatePlayhead();
        void showClipMenu (te::Clip&, juce::Point<float>);
    public:
        /** The menu a right-click on the clip's mark shows. */
        juce::PopupMenu createClipMenu (te::Clip&);
    private:
        /** A sound clip's tempo and pitch: following the song's tempo, the loop's tempo, higher / lower. */
        void addTempoAndPitchItems (juce::PopupMenu&, te::AudioClipBase&);
        void askForLoopBpm (te::WaveAudioClip&);
        void askToRename (te::Clip&);
        double getPlayheadBeat() const;
        void tell (const juce::String&, bool isProblem);
        static bool areAllMidi (const juce::StringArray&);
        std::unique_ptr<juce::FileChooser> midiChooser;
    };
}
