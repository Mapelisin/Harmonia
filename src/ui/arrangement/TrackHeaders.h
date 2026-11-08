#pragma once

#include "ui/arrangement/ArrangementModel.h"
#include "ui/arrangement/ArrangementView.h"

namespace harmonia::ui::arrangement
{
    /** "Instrument track" / "Audio track". */
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

        /** Number of headers (for tests). */
        int getNumHeaders() const noexcept      { return headers.size(); }

        /** The record arm button of a track's header (for tests); nullptr if it has none. */
        juce::Button* getArmButton (int index) const;

    private:
        class Header;

        ArrangementModel& model;
        ArrangementView& view;
        juce::OwnedArray<Header> headers;
    };
}
