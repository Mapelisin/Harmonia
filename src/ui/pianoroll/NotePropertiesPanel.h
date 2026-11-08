#pragma once

#include "ui/pianoroll/ToolPreview.h"

namespace harmonia::ui::pianoroll
{
    /** Note properties, opened by double-clicking a note: velocity, release, pan, Mod X/Y,
        fine pitch, mute, slide, porta and colour. If the note is part of the selection, a change
        applies to every selected note. All changes made while it is open are one undo step
        (see ToolPreview), and other keys than Space edit nothing meanwhile. */
    class NotePropertiesPanel final : public juce::Component
    {
    public:
        NotePropertiesPanel (PianoRollModel&, te::MidiNote&);

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

        /** Opens the panel in a call-out box pointing at `area` of `parent`. */
        static void show (PianoRollModel&, te::MidiNote&, juce::Component& parent, juce::Rectangle<int> area);

    private:
        struct Row
        {
            juce::Label label;
            juce::Slider slider;
        };

        PianoRollModel& model;
        ToolPreview preview;
        std::map<int, std::function<void (core::Note&)>> edits;     // by control: what it sets
        core::Note shown;

        std::array<Row, 6> rows;
        juce::ToggleButton muteButton { "Mute" }, slideButton { "Slide" }, portaButton { "Porta" };
        juce::ComboBox colourBox;
        juce::Label titleLabel, colourLabel;

    public:
        /** As if the control was used (for tests): row 0..5 (velocity, release, pan, Mod X,
            Mod Y, fine pitch) to `value` in its own units. */
        void setRowValue (int row, double value);

    private:
        void setUpRow (Row&, const juce::String& name, double min, double max, double interval, double value,
                       std::function<void (core::Note&, double)> apply);
        void apply (int control, std::function<void (core::Note&)> change);
    };
}
