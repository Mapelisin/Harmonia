#pragma once

#include "ui/pianoroll/ToolPreview.h"

namespace harmonia::ui::pianoroll
{
    /** The piano roll's tools with settings. */
    enum class NoteTool
    {
        strum,          // Alt+S
        arpeggiate,     // Alt+A
        humanize,       // Alt+R
        scaleLevels,    // Alt+X
        chordProgression, // Alt+P: writes chords, needs no notes
        drumPattern,    // Alt+D: writes a groove, needs no notes
        bassLine        // Alt+B: writes a bass under the other tracks' chords
    };

    const std::vector<NoteTool>& getAllNoteTools();

    /** Whether a tool writes new notes (needing none in the clip) rather than changing them. */
    bool writesNewNotes (NoteTool);
    juce::String getName (NoteTool);
    juce::String getShortcutDescription (NoteTool);

    /** A tool's window: its settings as sliders and lists, the notes changing live as they move,
        so the result can be heard (Space plays and stops as everywhere; other keys edit nothing
        while it is open). Accept (or Enter, or clicking away) keeps it as one undo step, Cancel
        (or Escape) puts the notes back, Reset returns the settings to their defaults. Works on
        the selected notes, or on all of them. */
    class NoteToolWindow final : public juce::Component
    {
    public:
        struct Setting
        {
            juce::String name;
            double min = 0.0, max = 1.0, interval = 0.0, defaultValue = 0.0;
            juce::String suffix;
            juce::StringArray choices;      // a list instead of a slider when given (the value is the index)
        };

        /** Turns the notes as they were (song beats) and the settings' values into the result. */
        using Apply = std::function<core::note_tools::Result (const std::vector<core::Note>& originals,
                                                              const std::vector<double>& values)>;

        /** Works on `targets` (none for a tool that only adds notes). */
        NoteToolWindow (PianoRollModel&, const juce::String& title, std::vector<Setting>, Apply,
                        const juce::Array<te::MidiNote*>& targets);

        const juce::String& getToolName() const noexcept    { return toolName; }

        /** The window for this tool on the selected notes (or all), already showing its result
            with the default settings; nullptr if there are no notes to work on. */
        static std::unique_ptr<NoteToolWindow> create (PianoRollModel&, NoteTool);

        /** Why create() gave nothing, for the user ("The clip has no notes..."). */
        static juce::String getUnavailableReason (PianoRollModel&, NoteTool);

        /** Opens the window in a call-out box pointing at `area` of `parent`. */
        static void show (std::unique_ptr<NoteToolWindow>, juce::Component& parent, juce::Rectangle<int> area);

        int getNumSettings() const noexcept             { return (int) settings.size(); }
        const Setting& getSetting (int index) const     { return settings[(size_t) index]; }
        double getValue (int index) const;

        /** As if the control was moved: the notes change. */
        void setValue (int index, double value);

        void reset();
        void accept();
        void cancel();

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

    private:
        struct Control
        {
            juce::Label label;
            juce::Slider slider;
            juce::ComboBox list;
        };

        PianoRollModel& model;
        juce::String toolName;
        ToolPreview preview;
        std::vector<Setting> settings;
        Apply apply;

        juce::Label titleLabel, hintLabel;
        juce::OwnedArray<Control> controls;
        juce::TextButton resetButton { "Reset" }, cancelButton { "Cancel" }, acceptButton { "Accept" };

        static std::unique_ptr<NoteToolWindow> createChordProgression (PianoRollModel&);
        static std::unique_ptr<NoteToolWindow> createDrumPattern (PianoRollModel&);
        static std::unique_ptr<NoteToolWindow> createBassLine (PianoRollModel&);
        std::vector<double> getValues() const;
        void update();
        void close();
    };
}
