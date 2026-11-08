#pragma once

#include "audio/Transport.h"
#include "ui/arrangement/ArrangementRuler.h"
#include "ui/arrangement/ClipArea.h"
#include "ui/arrangement/TrackHeaders.h"

namespace harmonia::ui::arrangement
{
    /** The arrangement (FL's playlist): track headers, ruler, clips, scrollbars, "+ Track" and
        Snap, plus its keyboard shortcuts (they work while it has keyboard focus). */
    class Arrangement final : public juce::Component,
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

        /** Delete, Ctrl+B (linked copies), Ctrl+Shift+B, Ctrl+A, Ctrl+D / Escape. */
        bool handleShortcut (const juce::KeyPress&);

        /** A position was clicked on the ruler. */
        std::function<void (double beat)> onPositionChosen;

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
        void showClipMenu (te::MidiClip&, juce::Point<float>);
        void askToRename (te::MidiClip&);
    };
}
