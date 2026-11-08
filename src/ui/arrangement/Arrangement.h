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

        /** Delete, Ctrl+B (linked copies), Ctrl+Shift+B, Ctrl+A, Ctrl+D / Escape, Ctrl+C / X / V, Ctrl+E. */
        bool handleShortcut (const juce::KeyPress&);

        /** Ctrl+C / Ctrl+X / Ctrl+V / Ctrl+E, with a word for onMessage. Paste and split go where
            the playhead is (paste snapped to the grid, on the current track). */
        void copySelected();
        void cutSelected();
        void pasteAtPlayhead();
        void splitAtPlayhead();

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

        /** Where files dropped at this point (in the arrangement's coordinates) would go. */
        ClipArea::DropMarker getDropTarget (juce::Point<int>) const;

        static constexpr int headerWidth = 200, rulerHeight = 22, scrollBarSize = 10;

    private:
        ArrangementModel& model;
        audio::Transport& transport;
        ArrangementView view;

        ClipArea clipArea { model, view };
        ArrangementRuler ruler { model, view };
        TrackHeaders headers { model, view };
        juce::TextButton addTrackButton { "+ Track" };
        juce::ComboBox snapBox;
        juce::ScrollBar horizontalScroll { false }, verticalScroll { true };

        juce::VBlankAttachment vblank { this, [this] { updatePlayhead(); } };

        void arrangementChanged() override;
        void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

        void viewChanged();
        void updateContentSize();
        void updateScrollBars();
        void updatePlayhead();
        void showClipMenu (te::Clip&, juce::Point<float>);
        void askToRename (te::Clip&);
        double getPlayheadBeat() const;
        void tell (const juce::String&, bool isProblem);
    };
}
