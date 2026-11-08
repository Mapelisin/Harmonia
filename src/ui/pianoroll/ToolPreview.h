#pragma once

#include "core/NoteTools.h"
#include "ui/pianoroll/PianoRollModel.h"

namespace harmonia::ui::pianoroll
{
    /** Shows a tool's result in the clip while its settings are being changed, as one undo step.

        Each new result first takes back the one before (it is still the last undo step: nothing
        else can start a new step while a preview is open) and then goes in its place, so it is
        always worked out from the notes as they were when the preview began. Whatever is shown
        when the preview ends stays; revert() puts the notes back as they were (and any redo
        steps with them). */
    class ToolPreview
    {
    public:
        /** Works on these notes; the selection follows them if they were selected. With no notes
            (a generator) the notes it adds are selected. */
        ToolPreview (PianoRollModel&, const juce::String& name, const juce::Array<te::MidiNote*>& targets);

        /** The notes' values (song beats) when the preview began. */
        const std::vector<core::Note>& getOriginals() const noexcept    { return originals; }

        /** Shows this result in place of the last one. */
        void show (const core::note_tools::Result&);

        /** Shows notes with these values (one per original, same order). */
        void show (const std::vector<core::Note>& values);

        /** Takes back what is shown: the notes are as they were. */
        void revert();

    private:
        PianoRollModel& model;
        te::Edit::UndoTransactionInhibitor inhibitor;
        juce::String name;
        juce::Array<juce::ValueTree> states;        // the notes are found again by these after an undo
        std::vector<core::Note> originals;
        bool wereSelected = false;
        bool showing = false;

        void takeBack();
        juce::Array<te::MidiNote*> findTargets() const;

        JUCE_DECLARE_NON_COPYABLE (ToolPreview)
    };
}
