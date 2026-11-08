#pragma once

#include "ui/arrangement/ArrangementModel.h"
#include "ui/arrangement/ArrangementView.h"

namespace harmonia::ui::arrangement
{
    /** The column left of the clips: one header per track with its name (double-click to
        rename), mute, solo and volume. Click a header to make the track current; right-click
        for its menu. */
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

    private:
        class Header;

        ArrangementModel& model;
        ArrangementView& view;
        juce::OwnedArray<Header> headers;
    };
}
