#pragma once

#include "core/Session.h"

namespace harmonia::core
{
    /** A Harmonia project: one Tracktion Edit plus Harmonia's own metadata
        (stored as extra properties on the Edit's ValueTree). */
    class Project : private juce::ChangeListener
    {
    public:
        /** Bumped whenever the on-disk format changes in a way older versions can't read. */
        static constexpr int currentFormatVersion = 1;

        static std::unique_ptr<Project> createEmpty (Session&);

        /** Loads a project file. Returns nullptr and fills `error` on failure. */
        static std::unique_ptr<Project> load (Session&, const juce::File&, juce::String& error);

        ~Project() override;

        /** Writes the project to `file`. Returns an error message, empty on success.
            On success the project counts as saved (no unsaved changes). */
        juce::String save (const juce::File& file);

        /** Something changed since the project was created, opened or saved. */
        bool hasUnsavedChanges() const;

        te::Edit& getEdit() noexcept                { return *edit; }
        const te::Edit& getEdit() const noexcept    { return *edit; }

        int getFormatVersion() const;

        //==============================================================================
        // Tracks and clips. All changes are undoable.

        juce::Array<te::AudioTrack*> getTracks() const;
        int getNumTracks() const;

        /** Appends a track at the bottom. */
        te::AudioTrack* addTrack (const juce::String& name);

        /** Inserts an empty MIDI clip; positions are in beats from the start of the song. */
        te::MidiClip* addMidiClip (te::AudioTrack&, double startBeat, double lengthBeats,
                                   const juce::String& name = {});

        double getTempoBpm() const;
        void setTempoBpm (double bpm);

        //==============================================================================
        // Undo. One user gesture (a click, a drag, a menu command) = one transaction.

        /** Starts a new undo transaction; everything changed until the next call is undone together. */
        void beginGesture (const juce::String& name);

        /** Forgets all undo steps, e.g. after setting up a new project's defaults. The project
            then counts as unchanged. */
        void clearUndoHistory();

        bool canUndo() const;
        bool canRedo() const;
        void undo();
        void redo();

    private:
        explicit Project (std::unique_ptr<te::Edit>);

        std::unique_ptr<te::Edit> edit;

        // Every undoable change counts (Tracktion's own flag only starts listening a moment after
        // the edit is created).
        bool changedSinceSave = false;

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void markAsSaved();

        JUCE_DECLARE_NON_COPYABLE (Project)
    };
}
