#pragma once

#include "ui/pianoroll/PianoRollModel.h"

namespace harmonia::ui::pianoroll
{
    /** Note properties, opened by double-clicking a note: velocity, release, pan, Mod X/Y,
        fine pitch, mute, slide, porta and colour. If the note is part of the selection, a change
        applies to every selected note. All changes made while it is open are one undo step. */
    class NotePropertiesPanel final : public juce::Component
    {
    public:
        NotePropertiesPanel (PianoRollModel&, te::MidiNote&);

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Opens the panel in a call-out box pointing at `area` of `parent`. */
        static void show (PianoRollModel&, te::MidiNote&, juce::Component& parent, juce::Rectangle<int> area);

    private:
        struct Row
        {
            juce::Label label;
            juce::Slider slider;
        };

        PianoRollModel& model;
        juce::Array<te::MidiNote*> targets;
        core::Note shown;

        std::array<Row, 6> rows;
        juce::ToggleButton muteButton { "Mute" }, slideButton { "Slide" }, portaButton { "Porta" };
        juce::ComboBox colourBox;
        juce::Label titleLabel, colourLabel;

        void setUpRow (Row&, const juce::String& name, double min, double max, double interval, double value,
                       std::function<void (core::Note&, double)> apply);
        void apply (const std::function<void (core::Note&)>& change);
    };
}
