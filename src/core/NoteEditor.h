#pragma once

#include "core/Note.h"

namespace harmonia::core
{
    /** The only way Harmonia changes the notes of a MIDI clip.

        Every change is recorded in the Edit's UndoManager (group a mouse gesture with
        Project::beginGesture) and is mirrored to the clip's linked copies in the same
        transaction. The batch overloads mirror once per call, so use them for selections. */
    class NoteEditor
    {
    public:
        explicit NoteEditor (te::MidiClip&);

        te::MidiClip& getClip() noexcept                  { return clip; }

        /** Live notes of the clip in storage order. Pointers stay valid until that note is removed
            or an undo/redo touches it. */
        const juce::Array<te::MidiNote*>& getNotes() const;

        te::MidiNote* add (const Note&);
        juce::Array<te::MidiNote*> add (const std::vector<Note>&);

        void update (te::MidiNote&, const Note&);

        struct Change
        {
            te::MidiNote* note = nullptr;
            Note values;
        };

        void update (const std::vector<Change>&);

        void remove (te::MidiNote&);
        void remove (const juce::Array<te::MidiNote*>&);

    private:
        juce::UndoManager* undoManager() const;
        void notesChanged();

        te::MidiClip& clip;

        JUCE_DECLARE_NON_COPYABLE (NoteEditor)
    };
}
