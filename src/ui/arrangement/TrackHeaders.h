#pragma once

#include "ui/arrangement/ArrangementModel.h"
#include "ui/arrangement/ArrangementView.h"

namespace harmonia::ui::arrangement
{
    /** "Instrument track" / "Drum track" / "Audio track". */
    juce::PopupMenu makeAddTrackMenu (ArrangementModel&);

    /** makeAddTrackMenu, shown under `target`. */
    void showAddTrackMenu (ArrangementModel&, juce::Component& target);

    /** The column left of the clips: one header per track with its name (double-click to
        rename), mute, solo and volume; audio tracks also have record arm, their input and its
        level while armed. Click a header to make the track current; right-click for its menu. */
    class TrackHeaders final : public juce::Component
    {
    public:
        TrackHeaders (ArrangementModel&, ArrangementView&);
        ~TrackHeaders() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        /** Re-reads the tracks (rebuilding headers if tracks came or went) and their values. */
        void refresh();

        /** Scrolled with the wheel. */
        std::function<void()> onViewChanged;

        /** Something to tell the user (a problem if `isProblem`). */
        std::function<void (const juce::String&, bool isProblem)> onMessage;

        /** Dragging a header up or down moves its track: while dragged (y in this component),
            a line shows the gap it would go into. */
        void dragHeader (int trackIndex, float y);
        void endHeaderDrag (int trackIndex, float y);

        /** The gap (0 = above the first track) a dragged header would go into, if one is dragged. */
        std::optional<int> getDropGap() const noexcept     { return dropGap; }

        /** The track menu's commands (also for tests). */
        void renderToAudio (te::AudioTrack&);

        /** The track's menu: right-click on its header, or Track in the menu bar. */
        juce::PopupMenu createTrackMenu (te::AudioTrack&);

        /** Opens the editor of the track's name in its header. */
        void startRenaming (te::AudioTrack&);

        void paintOverChildren (juce::Graphics&) override;

        /** Number of headers (for tests). */
        int getNumHeaders() const noexcept      { return headers.size(); }

        /** The record arm button of a track's header (for tests); nullptr if it has none. */
        juce::Button* getArmButton (int index) const;

    private:
        class Header;

        ArrangementModel& model;
        ArrangementView& view;
        juce::OwnedArray<Header> headers;
        std::optional<int> dropGap;

        int gapAt (float y) const;
    };
}
