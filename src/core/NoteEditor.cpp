#include "core/NoteEditor.h"
#include "core/ClipLinks.h"

namespace harmonia::core
{
    NoteEditor::NoteEditor (te::MidiClip& c)
        : clip (c)
    {
    }

    const juce::Array<te::MidiNote*>& NoteEditor::getNotes() const
    {
        return clip.getSequence().getNotes();
    }

    te::MidiNote* NoteEditor::add (const Note& values)
    {
        return add (std::vector<Note> { values }).getFirst();
    }

    juce::Array<te::MidiNote*> NoteEditor::add (const std::vector<Note>& values)
    {
        auto& sequence = clip.getSequence();
        juce::Array<te::MidiNote*> added;

        for (const auto& v : values)
        {
            auto state = note::createState (v);
            sequence.state.addChild (state, -1, undoManager());
            added.add (sequence.getNoteFor (state));
        }

        notesChanged();
        return added;
    }

    void NoteEditor::update (te::MidiNote& target, const Note& values)
    {
        update (std::vector<Change> { { &target, values } });
    }

    void NoteEditor::update (const std::vector<Change>& changes)
    {
        for (const auto& change : changes)
        {
            jassert (change.note != nullptr);

            if (change.note != nullptr)
                note::write (*change.note, change.values, undoManager());
        }

        notesChanged();
    }

    void NoteEditor::remove (te::MidiNote& target)
    {
        remove (juce::Array<te::MidiNote*> { &target });
    }

    void NoteEditor::remove (const juce::Array<te::MidiNote*>& targets)
    {
        auto& sequence = clip.getSequence();

        // Removing a note deletes its MidiNote object, so collect the trees first.
        juce::Array<juce::ValueTree> states;

        for (auto* n : targets)
            if (n != nullptr)
                states.add (n->state);

        for (auto& state : states)
            sequence.state.removeChild (state, undoManager());

        notesChanged();
    }

    juce::UndoManager* NoteEditor::undoManager() const
    {
        return &clip.edit.getUndoManager();
    }

    void NoteEditor::notesChanged()
    {
        clip_links::syncFrom (clip);
    }
}
