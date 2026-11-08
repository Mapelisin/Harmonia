#pragma once

#include "audio/PluginControls.h"

namespace harmonia::ui
{
    /** Harmonia's own window content for a plugin without an editor: one row per setting
        (name, slider or choice, value). A slider drag is one undo step; double-click resets.
        Follows changes made elsewhere (undo, automation). */
    class ParameterPanel final : public juce::Component,
                                 private juce::Timer
    {
    public:
        explicit ParameterPanel (te::Plugin&);
        ~ParameterPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Height that shows every row (the window caps it and scrolls). */
        int getIdealHeight() const;

        int getNumRows() const noexcept         { return rows.size(); }

        static constexpr int rowHeight = 26;

    private:
        class Row;

        juce::Viewport viewport;
        juce::Component content;
        juce::OwnedArray<Row> rows;

        void timerCallback() override;
    };
}
