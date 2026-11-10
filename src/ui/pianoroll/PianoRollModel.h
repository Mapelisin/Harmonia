#pragma once

#include "core/Controllers.h"
#include "core/Markers.h"
#include "core/NoteEditor.h"
#include "core/NoteOps.h"
#include "core/Project.h"
#include "core/Scales.h"
#include "core/Snap.h"

namespace harmonia::ui::pianoroll
{
    enum class Tool
    {
        draw,       // P
        paint,      // B
        erase,      // D
        mute,       // T
        slice,      // C
        select      // E
    };

    const std::vector<Tool>& getAllTools();
    juce::String getName (Tool);
    juce::juce_wchar getShortcutKey (Tool);

    /** Plays notes while they are edited (the app implements it with audio::NotePreview). */
    struct Auditioner
    {
        virtual ~Auditioner() = default;
        virtual void noteOn (int pitch, int velocity) = 0;
        virtual void noteOff (int pitch) = 0;
    };

    /** A note of the edited clip as the piano roll sees it: times in song beats. */
    struct DrawnNote
    {
        te::MidiNote* note = nullptr;
        core::Note values;
        bool selected = false;
    };

    /** A note of another clip, shown dimmed behind the edited notes. */
    struct GhostNote
    {
        double startBeat = 0.0, lengthBeats = 0.0;
        int pitch = 0;
    };

    /** Everything the piano roll edits and remembers, without any drawing: the edited clip,
        selection, tool, snap, clipboard and the editing commands. The components draw it and
        turn mouse and keyboard input into calls here, so all behaviour can be tested headless.

        Times in this API are song beats; notes are stored in the clip relative to its content
        start. Every command is one undo step and works on the selected notes, or on all notes
        when nothing is selected (as in FL Studio). */
    class PianoRollModel final : private juce::ValueTree::Listener,
                                 private juce::AsyncUpdater
    {
    public:
        struct Listener
        {
            virtual ~Listener() = default;

            /** Notes, selection or settings changed (called asynchronously, coalesced). */
            virtual void pianoRollChanged() = 0;
        };

        explicit PianoRollModel (core::Project&);
        ~PianoRollModel() override;

        core::Project& getProject() noexcept            { return project; }
        te::Edit& getEdit() noexcept                    { return project.getEdit(); }

        void addListener (Listener* l)                  { listeners.add (l); }
        void removeListener (Listener* l)               { listeners.remove (l); }

        //==============================================================================
        // The edited clip

        void setClip (te::MidiClip*);
        te::MidiClip* getClip() const noexcept          { return clip.get(); }
        core::NoteEditor* getEditor() noexcept          { return editor.get(); }

        double getContentStartBeat() const;
        /** Song beats of the clip, or of a looping clip's loop (its first time round). */
        double getClipStartBeat() const;
        double getClipEndBeat() const;
        double getBeatsPerBar() const;

        /** The clip repeats its loop (see core::clips::setLooping). */
        bool isLooping() const;

        /** Song beats of the whole clip: for a looping clip, all its times round. */
        juce::Range<double> getWholeClipBeats() const;

        /** A looping clip's loop length (beats); its first time round stays where it is.
            Start a gesture first (a drag is one undo step). */
        void setLoopLength (double beats);

        /** The song's markers (added in the arrangement), in time order. */
        std::vector<core::markers::Marker> getMarkers() const;

        /** Drum names (General MIDI) on the keys and notes, for the open clip's track: on by
            itself for a track called "Drums", "Beat"... Turning them on or off is undoable. */
        bool areDrumNamesShown() const;
        void setDrumNamesShown (bool);

        /** Before a drum pattern goes in: a track with nothing on it yet that plays the built-in
            synth (or nothing) becomes a drum track (audio::instruments::makeDrumTrack), an undo
            step of its own. Returns whether it did. */
        bool makeDrumTrackIfNew();

        /** Takes that step back again, if it is still the last one. */
        void undoMakeDrumTrack();

        /** The drum a key plays when drum names are shown (else, or for other keys, empty): on
            Harmonia's drum machine, the name of the key's sample if it has one. */
        juce::String getDrumName (int pitch) const;

        /** Harmonia's drum machine, if the open clip's track plays it (else nullptr). */
        te::Plugin* getDrumMachine() const;

        /** The key of the drum machine plays this sound file from now on (one undo step), and is
            played once. Returns an empty string, or why it can't (no drum machine, not a sound). */
        juce::String setDrumSample (int pitch, const juce::File&);

        /** The key plays the kit's own sound again (one undo step). */
        void clearDrumSample (int pitch);

        /** Whether the key of the drum machine plays a sample. */
        bool hasDrumSample (int pitch) const;

        //==============================================================================
        // Notes for drawing and hit-testing (rebuilt lazily after changes)

        const std::vector<DrawnNote>& getNotes();

        /** Index range of notes that may overlap [startBeat, endBeat) (callers still check each one). */
        std::pair<size_t, size_t> getNotesInRange (double startBeat, double endBeat);

        /** The note drawn on top at this position, or nullptr. */
        const DrawnNote* findNoteAt (double beat, int pitch);

        const std::vector<GhostNote>& getGhostNotes();
        std::pair<size_t, size_t> getGhostNotesInRange (double startBeat, double endBeat);

        /** Reads a note with its start in song beats. */
        core::Note read (const te::MidiNote&) const;
        std::vector<core::Note> readAll (const juce::Array<te::MidiNote*>&) const;

        //==============================================================================
        // Settings

        Tool getTool() const noexcept                   { return tool; }
        void setTool (Tool);

        core::snap::Mode getSnap() const noexcept       { return snapMode; }
        void setSnap (core::snap::Mode);

        /** The horizontal zoom decides what "Line" and "Cell" snap to. */
        void setPixelsPerBeat (double);
        core::snap::Context getSnapContext() const;
        double getGridBeats() const;

        /** The note value the lower panel shows (unless it shows a controller). Choosing one
            leaves the controllers. */
        core::NoteProperty getLaneProperty() const noexcept  { return laneProperty; }
        void setLaneProperty (core::NoteProperty);

        /** The controller the lower panel shows instead of a note value, if any. */
        std::optional<core::controllers::Controller> getLaneController() const noexcept  { return laneController; }
        void setLaneController (std::optional<core::controllers::Controller>);

        /** F / Shift+F: the next or previous note value, then pitch bend, mod wheel and the pedal. */
        void cycleLaneProperty (int direction);

        bool areGhostsVisible() const noexcept          { return ghostsVisible; }
        void setGhostsVisible (bool);

        /** The chords the clip's notes make over time (core::chords::findChords), in the same beats
            as getNotes. Shown on the ruler while chord names are on (at first they are). */
        const std::vector<core::chords::Span>& getChords();
        bool areChordNamesShown() const noexcept        { return chordNamesShown; }
        void setChordNamesShown (bool);

        /** Length of the next drawn note: the last note clicked or drawn. */
        double getNewNoteLength() const noexcept        { return newNoteLength; }
        void setNewNoteLength (double);

        //==============================================================================
        // Scale and chords

        /** The song's scale (saved with the project): its rows are lit, and notes can snap to it.
            Setting it is an undo step. */
        core::scales::Scale getScale() const;
        void setScale (const core::scales::Scale&);

        /** While on (and a scale is set), drawn and moved notes land on the scale's notes, and
            Shift+Up/Down moves by scale degrees. */
        bool isSnappingToScale() const noexcept         { return snapToScale; }
        void setSnapToScale (bool);

        /** The chord a click in Draw puts down, or none for single notes. */
        std::optional<core::chords::ChordType> getChordStamp() const noexcept   { return chordStamp; }
        void setChordStamp (std::optional<core::chords::ChordType>);

        /** The pitch a new note clicked at `pitch` gets: on the scale if snapping to it. */
        int fitPitch (int pitch) const;

        /** The pitches a click at `pitch` puts down: the stamp's chord, or one (fitted) note. */
        std::vector<int> getStampPitches (int pitch) const;

        /** Moves notes `rows` rows up or down, as a mouse drag does: by scale degrees while
            snapping to the scale (`grabbedPitch` sets how many), otherwise by semitones. */
        std::vector<core::Note> moveVertically (std::vector<core::Note>, int grabbedPitch, int rows) const;

        //==============================================================================
        // Selection

        bool isSelected (const te::MidiNote&) const;
        bool hasSelection();
        int getNumSelected();
        juce::Array<te::MidiNote*> getSelectedNotes();

        /** The selected notes, or every note when nothing is selected. */
        juce::Array<te::MidiNote*> getSelectedOrAll();

        void setSelection (const juce::Array<te::MidiNote*>&);
        void addToSelection (const juce::Array<te::MidiNote*>&);
        void setSelected (te::MidiNote&, bool shouldBeSelected);
        void selectAll();
        void deselectAll();

        /** Time range selected on the ruler; also selects the notes starting inside it. */
        std::optional<core::note_ops::BeatRange> getTimeSelection() const  { return timeSelection; }
        void setTimeSelection (core::note_ops::BeatRange);
        void clearTimeSelection();

        //==============================================================================
        // Commands (one undo step each)

        void deleteSelectedOrAll();
        void transpose (int semitones);
        void nudge (int gridSteps);

        /** Alt+Left/Right: moves by a couple of pixels at the current zoom, off the grid. */
        void nudgeFine (int direction);

        void quantize (bool startsOnly);

        /** Fit to scale: every note's pitch moved to the nearest note of the scale. */
        void fitToScale();

        /** Sets the major or minor scale the notes (selected, or all) most likely are in.
            Returns false (and changes nothing) if there are no notes. */
        bool detectScale();

        /** Quick legato (Ctrl+L): each note lasts until the next one starts. */
        void legato();

        /** Quick chop (Ctrl+U): notes cut into pieces one snap step long. */
        void chop();

        /** Glue (Ctrl+G): touching or overlapping notes of the same pitch become one. */
        void glue();

        /** Discard lengths (Shift+D): every note one snap step long. */
        void discardLengths();

        /** Rotate (Ctrl+Shift+Left/Right): moves by snap steps, wrapping around inside the time
            selection (the notes starting in it), or else inside the whole bars the notes are in. */
        void rotate (int gridSteps);

        /** Alt+M / Alt+Shift+M. */
        void setMuted (bool shouldBeMuted);

        /** Shift+I: selects the notes that weren't and deselects the ones that were. */
        void invertSelection();

        /** Insert space / Delete space (Ctrl+Insert / Ctrl+Delete): puts in, or cuts out, as much
            empty time as the time selection, at it. Every note moves, not just the selection.
            Nothing without a time selection. */
        void insertSpace();
        void deleteSpace();
        void duplicate();
        void copy();
        void cut();
        bool canPaste() const;
        void paste (double atBeat);

        /** Where Paste puts notes when the view starts at `leftVisibleBeat`: the first whole bar on screen. */
        double getPastePosition (double leftVisibleBeat) const;

        void undo();
        void redo();

        //==============================================================================
        // The lower panel (velocity and other per-note values)

        /** Whether the lower panel edits this note: with a selection only selected notes are edited. */
        bool isLaneEditable (const DrawnNote&);

        /** The editable note whose start is nearest to `beat`, if it is within `radiusBeats`
            (the stem the mouse is on in the lower panel). */
        const DrawnNote* findLaneNoteNear (double beat, double radiusBeats);

        /** One step of a drag in the lower panel, from (beatA, valueA) to (beatB, valueB).
            Editable notes starting between the two beats get values interpolated along the way
            (swiping across notes draws a ramp), and the stem nearest to beatB within `grabBeats`
            (with any chord notes on the same start) takes valueB, so a note keeps following the
            mouse when the hand drifts a little sideways. Goes into the current undo transaction. */
        void setLaneValues (double beatA, float valueA, double beatB, float valueB, double grabBeats);

        //==============================================================================
        // Controllers of the clip (core::controllers), in song beats

        /** The clip's events of the controller, at their song beats. */
        std::vector<core::controllers::Point> getControllerPoints (core::controllers::Controller) const;
        float getControllerValueAt (core::controllers::Controller, double songBeat) const;

        /** One step of drawing the lane's controller: a line from (beatA, valueA) to (beatB, valueB),
            the part inside the clip. Goes into the current undo transaction. */
        void drawControllerLine (double beatA, float valueA, double beatB, float valueB);

        /** Removes the lane's controller events between the two beats (inside the clip). */
        void eraseControllers (double beatA, double beatB);

        /** A value played on a MIDI keyboard while recording, at a song beat inside the clip. */
        void addRecordedController (core::controllers::Controller, double songBeat, float value);

        /** Alt+wheel over a note: changes the panel's value of the note (and of the rest of the
            selection if the note is selected) by `delta` on the 0..1 scale. Quick successive
            changes are one undo step. */
        void nudgeProperty (te::MidiNote&, float delta);

        //==============================================================================
        // Editing primitives for mouse gestures. Values are in song beats; changes go into the
        // current undo transaction (start one with beginGesture). The clip grows to fit.

        void beginGesture (const juce::String& name);
        juce::Array<te::MidiNote*> addNotes (const std::vector<core::Note>&);
        te::MidiNote* addNote (const core::Note&);
        void updateNotes (const std::vector<core::NoteEditor::Change>&);
        void removeNotes (const juce::Array<te::MidiNote*>&);

        /** The earliest song beat a note can start at (content beat 0). */
        double getMinBeat() const                       { return getContentStartBeat(); }

        //==============================================================================
        // Live input (see ui::NoteInput)

        /** Notes being recorded whose keys are still down, in song beats: drawn over the notes,
            not in the clip yet. */
        void setLiveNotes (std::vector<core::Note>);
        const std::vector<core::Note>& getLiveNotes() const noexcept    { return liveNotes; }

        /** Pitches being played right now, lit on the keyboard at the left. */
        void setHeldPitches (const std::array<bool, 128>&);
        bool isPitchHeld (int pitch) const noexcept     { return pitch >= 0 && pitch < 128 && heldPitches[(size_t) pitch]; }

        /** Adds recorded notes (song beats) in the current undo transaction. A note that repeats
            one already in the clip (same pitch and start) is skipped, so playing the same part
            again on the next loop pass doesn't stack copies. Returns the notes added. */
        juce::Array<te::MidiNote*> addRecordedNotes (const std::vector<core::Note>&);

        //==============================================================================
        // Hearing notes while editing

        void setAuditioner (Auditioner* a) noexcept     { auditioner = a; }
        void auditionStart (int pitch, int velocity);
        void auditionStart (const std::vector<int>& pitches, int velocity);
        void auditionStop();

    private:
        core::Project& project;

        // Holding a reference keeps the clip object alive if the edit drops it (e.g. an undo
        // removes its track), so the model can notice and let go of it safely.
        juce::ReferenceCountedObjectPtr<te::MidiClip> clip;
        std::unique_ptr<core::NoteEditor> editor;
        juce::ValueTree sequenceState;

        Tool tool = Tool::draw;
        core::snap::Mode snapMode = core::snap::Mode::line;
        double pixelsPerBeat = 80.0;
        core::NoteProperty laneProperty = core::NoteProperty::velocity;
        std::optional<core::controllers::Controller> laneController;
        bool ghostsVisible = true, chordNamesShown = true;
        std::vector<core::chords::Span> chords;
        double newNoteLength = 0.25;

        // Selected notes, keyed by the live MidiNote and checked against its tree, so a stale
        // pointer can never be mistaken for a new note that happens to reuse the address.
        std::unordered_map<const te::MidiNote*, juce::ValueTree> selection;
        std::optional<core::note_ops::BeatRange> timeSelection;

        std::vector<DrawnNote> drawn;
        double longestNote = 0.0;
        int numSelected = 0;
        bool notesDirty = true, selectionDirty = true;
        bool clipRemoved = false;   // the clip left the edit (undo); dropped on the next async update

        std::vector<GhostNote> ghosts;
        double longestGhost = 0.0;
        bool ghostsDirty = true;

        juce::ValueTree lastNudgedNote;
        juce::uint32 lastNudgeTime = 0;

        Auditioner* auditioner = nullptr;
        std::vector<int> auditionPitches;
        bool snapToScale = false;
        std::optional<core::chords::ChordType> chordStamp;

        std::vector<core::Note> liveNotes;
        std::array<bool, 128> heldPitches {};

        juce::ListenerList<Listener> listeners;

        bool canEdit() const noexcept                   { return editor != nullptr && ! clipRemoved; }
        void ensureNotes();
        void ensureSelectionFlags();
        void rebuildGhosts();
        void notifyChanged();
        void selectionChanged();

        core::Note toContent (const core::Note&) const;
        juce::Array<te::MidiNote*> getAll();
        void moveBy (double deltaBeats);
        void applyChanges (const juce::String& gestureName, const juce::Array<te::MidiNote*>&,
                           const std::vector<core::Note>& before, const std::vector<core::Note>& after);
        std::optional<core::note_ops::BeatRange> getSpaceRange() const;
        void growClipToFit (const std::vector<core::Note>& songValues);

        void handleAsyncUpdate() override;
        void treeChanged (const juce::ValueTree&);
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
        void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;

        JUCE_DECLARE_NON_COPYABLE (PianoRollModel)
    };
}
