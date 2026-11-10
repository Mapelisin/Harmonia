#pragma once

#include "core/Session.h"
#include "core/Tracks.h"

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

        /** Opens a copy of the project (one written by saveCopy(), or an earlier version of the
            project file) as the project at `original` (its own file; none if it was never saved):
            Ctrl+S saves there, recordings go next to it, and the sounds the copy names by a path
            from the project file are found from there. It counts as having unsaved changes. For
            restoring work after a crash (core::Autosave) and earlier versions (core::backups). */
        static std::unique_ptr<Project> loadRecovered (Session&, const juce::File& copy, const juce::File& original,
                                                       juce::String& error);

        ~Project() override;

        /** Writes the project to `file`. Returns an error message, empty on success.
            On success the project counts as saved (no unsaved changes), and `file` is where it
            lives from then on. Sound files in the project file's folder (or below) are stored
            relative to it, so the folder can be moved or copied whole; others by their full path
            (audio clips, takes, the drum machine's samples and the sampler's sound alike).
            A half-written file never replaces the old one. */
        juce::String save (const juce::File& file);

        /** Writes a copy to `file` (sound files by their full paths) and changes nothing else: the
            project keeps its unsaved changes and its own file. For autosaving. */
        juce::String saveCopy (const juce::File& file);

        /** Something changed since the project was created, opened or saved. */
        bool hasUnsavedChanges() const;

        /** The engine is still finishing the last change to a track (it tidies a track's nodes
            a moment after a clip is added or moved, which is an undoable change of its own): until
            it has, no new undo step starts by itself, so that the change stays one Ctrl+Z. Never
            for longer than a second or two. */
        bool isSettling() const;

        /** Goes up with every change (an undo or redo counts too): for noticing new changes. */
        juce::uint64 getChangeCount() const;

        te::Edit& getEdit() noexcept                { return *edit; }
        const te::Edit& getEdit() const noexcept    { return *edit; }

        int getFormatVersion() const;

        //==============================================================================
        // Tracks and clips. All changes are undoable.

        /** The song's tracks (instrument and audio tracks), top to bottom. Return and group tracks
            are not among them. */
        juce::Array<te::AudioTrack*> getTracks() const;
        int getNumTracks() const;

        /** The return tracks (shared effects, see audio::sends), in mixer order. */
        juce::Array<te::AudioTrack*> getReturnTracks() const;

        /** The group tracks (one fader for several tracks, see audio::groups), in mixer order. */
        juce::Array<te::AudioTrack*> getGroupTracks() const;

        /** Adds a track after `after`, or below the other song tracks. */
        te::AudioTrack* addTrack (const juce::String& name, te::Track* after = nullptr);

        /** Adds an empty return track after all the others (audio::sends::addReturnTrack sets it up). */
        te::AudioTrack* addReturnTrack (const juce::String& name);

        /** Adds an empty group track after all the others (audio::groups::addGroup sets it up). */
        te::AudioTrack* addGroupTrack (const juce::String& name);

        /** Inserts an empty MIDI clip; positions are in beats from the start of the song. */
        te::MidiClip* addMidiClip (te::AudioTrack&, double startBeat, double lengthBeats,
                                   const juce::String& name = {});

        /** Inserts a clip playing a whole audio file (WAV, AIFF, FLAC, OGG, MP3...) from `startBeat`.
            The clip refers to the file where it is. Returns nullptr with the reason in `error`
            if the file can't be read. */
        te::WaveAudioClip* addAudioClip (te::AudioTrack&, const juce::File&, double startBeat, juce::String& error);

        /** Whether a file looks like audio Harmonia can read (by its extension). */
        bool canImport (const juce::File&) const;

        double getTempoBpm() const;

        /** Clips, notes and automation stay on their beats (audio clips keep their speed: they
            start on the same beat). Within core::tempo's limits. Undoable. */
        void setTempoBpm (double bpm);

        /** The time signature is beats-per-bar over 4 (beats are quarter notes). Changing it
            moves the bar lines, nothing else. Undoable. */
        int getBeatsPerBar() const;
        void setBeatsPerBar (int);

        //==============================================================================
        // Undo. One user gesture (a click, a drag, a menu command) = one transaction.

        /** Starts a new undo transaction; everything changed until the next call is undone together. */
        void beginGesture (const juce::String& name);

        /** Forgets all undo steps, e.g. after setting up a new project's defaults. The project
            then counts as unchanged. */
        void clearUndoHistory();

        bool canUndo() const;
        bool canRedo() const;

        /** The name of the step undo / redo would take back or do again ("Change tempo"),
            empty if there is none: for the Edit menu. */
        juce::String getUndoDescription() const;
        juce::String getRedoDescription() const;
        void undo();
        void redo();

    private:
        explicit Project (std::unique_ptr<te::Edit>);

        te::AudioTrack* addBusTrack (const juce::String& name, tracks::Kind);

        std::unique_ptr<te::Edit> edit;

        class StepKeeper;
        std::unique_ptr<StepKeeper> stepKeeper;

        // Every undoable change counts (Tracktion's own flag only starts listening a moment after
        // the edit is created). Tracktion's flag is taken in too: it also catches settings changed
        // in a plugin's own window, which don't go through undo.
        mutable bool changedSinceSave = false;
        mutable juce::uint64 changeCount = 0;

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void markAsSaved();
        void takeInChanges() const;
        void syncPluginParameters();
        void makeSoundPathsAbsolute();
        juce::String write (const juce::File&, const juce::File& relativeTo);

        /** Loads `file` as the project at `asFile`: sounds it names by a path from the project
            file are looked for from there. */
        static std::unique_ptr<Project> open (Session&, const juce::File& file, const juce::File& asFile, juce::String& error);

        JUCE_DECLARE_NON_COPYABLE (Project)
    };
}
