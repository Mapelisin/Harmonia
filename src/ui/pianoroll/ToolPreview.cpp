#include "ui/pianoroll/ToolPreview.h"

namespace harmonia::ui::pianoroll
{
    ToolPreview::ToolPreview (PianoRollModel& m, const juce::String& n, const juce::Array<te::MidiNote*>& targets)
        : model (m), inhibitor (m.getEdit()), name (n)
    {
        for (auto* note : targets)
        {
            states.add (note->state);
            originals.push_back (model.read (*note));
            wereSelected = wereSelected || model.isSelected (*note);
        }
    }

    juce::Array<te::MidiNote*> ToolPreview::findTargets() const
    {
        juce::Array<te::MidiNote*> found;
        found.insertMultiple (0, nullptr, states.size());

        if (auto* editor = model.getEditor())
        {
            for (auto* note : editor->getNotes())
            {
                const auto index = states.indexOf (note->state);

                if (index >= 0)
                    found.set (index, note);
            }
        }

        return found;
    }

    void ToolPreview::takeBack()
    {
        if (! showing)
            return;

        showing = false;
        auto& undoManager = model.getEdit().getUndoManager();

        // Only if the last step is still this preview's own.
        if (undoManager.getNumActionsInCurrentTransaction() > 0 && undoManager.getCurrentTransactionName() == name)
            undoManager.undoCurrentTransactionOnly();
    }

    void ToolPreview::show (const std::vector<core::Note>& values)
    {
        show (core::note_tools::Result { values, {}, {} });
    }

    void ToolPreview::show (const core::note_tools::Result& result)
    {
        jassert (result.notes.size() == originals.size());

        takeBack();

        const auto targets = findTargets();
        std::vector<core::NoteEditor::Change> changes;
        juce::Array<te::MidiNote*> removed, kept;

        for (int i = 0; i < targets.size(); ++i)
        {
            auto* note = targets[i];

            if (note == nullptr)
                continue;

            if (std::find (result.removed.begin(), result.removed.end(), (size_t) i) != result.removed.end())
            {
                removed.add (note);
                continue;
            }

            kept.add (note);

            if (! (result.notes[(size_t) i] == originals[(size_t) i]))
                changes.push_back ({ note, result.notes[(size_t) i] });
        }

        model.beginGesture (name);
        model.updateNotes (changes);
        model.removeNotes (removed);
        const auto added = model.addNotes (result.added);
        showing = model.getEdit().getUndoManager().getNumActionsInCurrentTransaction() > 0;

        if (wereSelected || states.isEmpty())
        {
            kept.addArray (added);
            model.setSelection (kept);
        }
    }

    void ToolPreview::revert()
    {
        takeBack();

        if (wereSelected)
        {
            juce::Array<te::MidiNote*> back;

            for (auto* note : findTargets())
                if (note != nullptr)
                    back.add (note);

            model.setSelection (back);
        }
    }
}
