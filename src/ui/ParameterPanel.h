#pragma once

#include "audio/PluginControls.h"

namespace harmonia::ui
{
    /** Harmonia's own window content for a plugin without an editor: a bar with its presets,
        then one row per setting (name, slider or choice, value). A slider drag is one undo step;
        double-click resets; right-click: MIDI Learn. Follows changes made elsewhere (undo,
        automation, a preset, a MIDI control). */
    class ParameterPanel final : public juce::Component,
                                 private juce::Timer
    {
    public:
        explicit ParameterPanel (te::Plugin&);
        ~ParameterPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Height that shows the bar and every row (the window caps it and scrolls). */
        int getIdealHeight() const;

        int getNumRows() const noexcept         { return rows.size(); }

        /** The row's "CC 21" / "Learn..." mark (MIDI Learn), as last refreshed. */
        juce::String getBadge (int row) const;

        /** Shows each row's current value and mark again (the window does it 10 times a second). */
        void refresh();

        juce::TextButton& getPresetsButton() noexcept   { return presetsButton; }
        const juce::Label& getMessage() const noexcept  { return message; }

        /** Says something in the bar, next to the Presets button (a problem in the warning colour). */
        void tell (const juce::String& text, bool isProblem);

        static constexpr int rowHeight = 26, barHeight = 34;

    private:
        class Row;

        te::Plugin& plugin;
        juce::TextButton presetsButton { "Presets" };
        juce::Label message;

        juce::Viewport viewport;
        juce::Component content;
        juce::OwnedArray<Row> rows;

        void timerCallback() override;
    };
}
