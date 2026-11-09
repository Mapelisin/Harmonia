#pragma once

#include "core/Markers.h"
#include "core/TempoChanges.h"
#include "core/TrackFolders.h"
#include "core/SongForms.h"
#include "core/NoteOps.h"
#include "core/Project.h"
#include "core/Snap.h"

namespace harmonia::ui::arrangement
{
    /** What Play plays, like FL Studio's PAT / SONG switch. */
    enum class PlayMode
    {
        clip,   // loops the clip open in the piano roll
        song    // plays the arrangement: loops the loop region, or the whole song
    };

    /** A clip as the arrangement draws it. Valid until the next change of the edit. */
    struct DrawnClip
    {
        te::Clip* clip = nullptr;
        int trackIndex = 0;             // the row it is drawn in
        double startBeat = 0.0, endBeat = 0.0;
        bool selected = false, open = false, linked = false, audio = false;
        bool folded = false;            // in a collapsed folder's row: shown, not edited there
    };

    /** Everything the arrangement edits and remembers, without drawing: tracks, the current
        track and the clip open in the piano roll, clip selection, snap, play mode, loop region
        and the commands. The components draw it and turn input into calls here, so the
        behaviour can be tested headless.

        Positions are song beats. Every command is one undo step. */
    class ArrangementModel final : private juce::ValueTree::Listener,
                                   private juce::AsyncUpdater
    {
    public:
        struct Listener
        {
            virtual ~Listener() = default;

            /** Tracks, clips, selection or settings changed (asynchronously, coalesced). */
            virtual void arrangementChanged() = 0;
        };

        explicit ArrangementModel (core::Project&);
        ~ArrangementModel() override;

        core::Project& getProject() noexcept                { return project; }
        te::Edit& getEdit() noexcept                        { return project.getEdit(); }

        void addListener (Listener* l)                      { listeners.add (l); }
        void removeListener (Listener* l)                   { listeners.remove (l); }

        //==============================================================================
        // Tracks

        /** The tracks the arrangement has a row for, top to bottom: every song track, but of a
            collapsed folder only the first (its row stands for the folder). */
        juce::Array<te::AudioTrack*> getTracks() const;
        int getNumTracks() const                            { return getTracks().size(); }
        te::AudioTrack* getTrack (int index) const          { return getTracks()[index]; }

        /** The row of a track; -1 for one hidden in a collapsed folder. */
        int indexOf (const te::Track*) const;

        /** Every song track, those hidden in collapsed folders too. */
        juce::Array<te::AudioTrack*> getAllTracks() const   { return project.getTracks(); }

        //==============================================================================
        // Folders of tracks (core::folders): neighbouring tracks under a name, collapsed to one
        // row, muted and soloed together. Each change is one undo step (collapsing isn't one).

        std::optional<core::folders::Folder> getFolderOf (const te::Track&) const;

        /** The track's row stands for its whole collapsed folder. */
        bool isFoldedRow (const te::Track*) const;

        /** A new folder of the track (and the tracks of the selected clips), named by the next
            free name. */
        std::optional<core::folders::Folder> newFolder (te::AudioTrack&);

        /** Into a folder (after its last track), or with an empty ID out of its folder. */
        void setTrackFolder (te::AudioTrack&, const juce::String& folderId);
        void renameFolder (const juce::String& folderId, const juce::String& name);

        /** The folder is forgotten; its tracks stay. */
        void removeFolder (const juce::String& folderId);
        void setFolderCollapsed (const juce::String& folderId, bool);
        void setFolderMuted (const juce::String& folderId, bool);
        void setFolderSoloed (const juce::String& folderId, bool);

        //==============================================================================
        // The current track (its instrument is shown and played by the keyboards) and the clip
        // open in the piano roll, which is always on the current track (or none).

        te::AudioTrack* getCurrentTrack() const;
        te::MidiClip* getOpenClip() const;

        /** Makes the track current and opens its first clip (or none). */
        void setCurrentTrack (te::AudioTrack*);

        /** Opens the clip in the piano roll; its track becomes current. */
        void openClip (te::MidiClip*);

        /** A clip was clicked: a MIDI clip opens in the piano roll; an audio clip makes its track
            current (nothing open) and is what clip mode plays. */
        void focusClip (te::Clip&);

        /** The audio clip shown in the audio clip editor, played in clip mode and drawn as open:
            the one last clicked on the current track, else the current track's first audio clip
            (none on an instrument track). */
        te::AudioClipBase* getFocusedAudioClip() const;

        /** The current track or the open clip changed (called right away, before returning). */
        std::function<void()> onCurrentChanged;

        //==============================================================================
        // Clips for drawing and hit-testing

        /** Every clip (MIDI and audio), track by track, each track's in time order. */
        const std::vector<DrawnClip>& getClips();

        /** The clip drawn on top at this beat of the track's row, or nullptr. */
        const DrawnClip* findClipAt (int trackIndex, double beat);

        /** Where the last clip ends (0 for an empty song). */
        double getSongEndBeat() const;

        //==============================================================================
        // Selection

        bool isSelected (const te::Clip&) const;
        juce::Array<te::Clip*> getSelectedClips() const;
        void setSelection (const juce::Array<te::Clip*>&);
        void setSelected (te::Clip&, bool shouldBeSelected);
        void selectAll();
        void deselectAll();

        //==============================================================================
        // Snap (FL's playlist snap; "Line" follows the zoom)

        core::snap::Mode getSnap() const noexcept           { return snapMode; }
        void setSnap (core::snap::Mode);
        void setPixelsPerBeat (double);
        core::snap::Context getSnapContext() const;
        double getBeatsPerBar() const;

        /** Snap modes offered in the arrangement, in menu order. */
        static const std::vector<core::snap::Mode>& getSnapModes();

        //==============================================================================
        // Commands (one undo step each)

        /** A new track with the built-in synth and an empty clip where the open clip is (so
            loops line up), or of four bars at the start. The new track becomes current. */
        te::AudioTrack* addTrack();

        /** A new empty audio track at the bottom; it becomes current. */
        te::AudioTrack* addAudioTrack();

        /** As addTrack, but a drum track (audio::instruments::makeDrumTrack) called "Drums". */
        te::AudioTrack* addDrumTrack();

        /** Imports audio files at `beat` (snapped down to the grid): the first onto the track at
            `trackIndex` if that is an audio track, the others (or all) onto new audio tracks
            below it, named after the files. Selects the new clips and switches to song mode.
            Returns what went wrong (empty if nothing did). */
        juce::String importAudio (const juce::Array<juce::File>&, int trackIndex, double beat);

        /** Imports MIDI files at `beat` (snapped down to the grid): a file with one part onto the
            track at `trackIndex` if that is an instrument track; other parts and files onto new
            instrument tracks (the built-in synth) below it, named after the file or its parts.
            Into a song without notes or audio yet, the first file's tempo and time signature come
            along. Selects the new clips and opens the first. One undo step. Returns what went
            wrong (empty if nothing did). */
        juce::String importMidi (const juce::Array<juce::File>&, int trackIndex, double beat);

        /** Audio and MIDI files together: each kind as above. */
        juce::String importFiles (const juce::Array<juce::File>&, int trackIndex, double beat);

        /** No notes and no audio clips anywhere (empty clips don't count). */
        bool isSongEmpty() const;

        void deleteTrack (te::AudioTrack&);

        /** A copy of the track right below it: instrument, effects with their settings and
            automation, sends, volume and pan, and clips (copies of their own, not linked to the
            originals). Named "<name> 2" (or the next number not taken). It becomes current.
            One undo step. */
        te::AudioTrack* duplicateTrack (te::AudioTrack&);

        /** Moves the track to row `newIndex`; the row of a collapsed folder moves the whole folder.
            A track moved away from its folder leaves it, one moved into a folder joins it.
            One undo step. */
        void moveTrack (te::AudioTrack&, int newIndex);

        /** "Render to audio": the track as it sounds through its instrument and effects (with its
            volume and pan), from where its first clip starts to where its last ends plus a moment
            to ring out, into a sound file next to the project (as recordings go); onto a new audio
            track below it that sends to the return tracks what the track did. The track is muted,
            so the song sounds as before. One undo step (the file stays). Returns what went wrong. */
        juce::String renderToAudio (te::AudioTrack&);

        /** The name for a copy of a track called `name`: "Bass" -> "Bass 2", "Bass 2" -> "Bass 3",
            skipping names the song's tracks already have. */
        juce::String makeCopyName (const juce::String& name) const;

        /** Clips still named after the track get the new name too. */
        void renameTrack (te::AudioTrack&, const juce::String&);

        /** A colour for the track (none: by its place in the song). One undo step. */
        void setTrackColour (te::AudioTrack&, std::optional<juce::Colour>);

        /** A colour for these clips (none: their track's). One undo step. */
        void setClipColour (const juce::Array<te::Clip*>&, std::optional<juce::Colour>);

        /** An empty one-bar clip in the grid cell at `beat`, opened in the piano roll.
            Nothing on an audio track. */
        te::MidiClip* createClip (te::AudioTrack&, double beat);

        void deleteSelected();

        /** Deletes clips in the current undo transaction (for gestures; see beginGesture). */
        void removeClips (const juce::Array<te::Clip*>&);

        /** Copies the selected clips right after the selection (by whole bars) and selects the
            copies. Linked copies share their notes with the originals (audio clips are always
            copied independently). */
        void duplicateSelected (bool linked);

        /** Splits clips in two at `beat`: the selected clips there or, if none of them is there,
            the clips of the current track there. One undo step; the right halves become the
            selection. Returns how many clips were split. */
        int splitAt (double beat);

        /** Ctrl+C: remembers the selected clips (for every project opened in this run). Returns how many. */
        int copySelected();

        /** Ctrl+X: copySelected, then deletes them (one undo step). */
        int cutSelected();

        /** Ctrl+V: the copied clips as new independent clips, the earliest at `beat` and the topmost
            on the current track; the others keep their distance in time and in tracks. A clip lands
            on the nearest track below its place that takes its kind (MIDI on instrument tracks, audio
            on audio tracks), or is left out. One undo step; the pasted clips become the selection.
            Returns how many were pasted. */
        int paste (double beat);

        /** Something was copied. */
        static bool canPaste();

        /** How many clips are on the clipboard (for messages). */
        static int getNumCopiedClips();

        /** Makes the clip independent of its linked copies. */
        void makeUnique (te::MidiClip&);

        /** Loop the clip, or stop (core::clips::setLooping). One undo step. */
        void setLooping (te::MidiClip&, bool);

        /** Its repeats written as notes (core::clips::bakeLoop). One undo step. */
        void bakeLoop (te::MidiClip&);

        void renameClip (te::Clip&, const juce::String&);

        /** The audio clip follows the song's tempo (core::audio_tempo), or plays at its own speed
            again. Its loop tempo: what its file says, or what the length of a file called a loop
            suggests, or else the song's tempo (it sounds as before, and follows from then on).
            One undo step. Dropped loops follow by themselves (importFiles). */
        void setFollowsTempo (te::WaveAudioClip&, bool);

        /** The tempo the file of a clip following the song is at (it is played faster or slower
            to fit). One undo step. */
        void setLoopBpm (te::WaveAudioClip&, double);

        /** The audio clip higher or lower, in semitones from its file's own (its speed stays).
            One undo step. */
        void setPitch (te::AudioClipBase&, float semitones);

        //==============================================================================
        // Bars on every track (core::sections): one undo step each. Beats are song beats.

        /** `lengthBeats` of empty space at `startBeat` on every track; what comes after (clips,
            automation, tempo, markers) moves later. A loop region after it moves along (one
            starting there stays, over the new bars). */
        void insertBars (double startBeat, double lengthBeats);

        /** Takes the stretch out of every track and closes the gap. A loop region over it goes;
            one after it moves along. */
        void deleteBars (double startBeat, double endBeat);

        /** Plays the stretch twice: a copy right after it on every track (MIDI clips linked to
            their originals, as Ctrl+B makes them), the rest later. The copies become the
            selection; a loop region over the stretch moves onto the copy (so doing it again
            repeats it once more). Returns how many clips were copied. */
        int duplicateBars (double startBeat, double endBeat);

        /** A song from the loop region (core::song_forms::build): its clips laid out section by
            section from where it starts, markers at the sections. One undo step; the loop region
            goes and Song mode comes on. Returns how many beats the song is (0: nothing in the
            loop region, or none). */
        double makeSong (const core::song_forms::Form&);

        /** "bars 5-8", "bar 3", or "beats 2.50-5" when it isn't whole bars. */
        static juce::String describeBars (core::note_ops::BeatRange, double beatsPerBar);

        void undo();
        void redo();

        //==============================================================================
        // Playback

        PlayMode getPlayMode() const noexcept               { return playMode; }
        void setPlayMode (PlayMode);

        /** The range song mode loops instead of the whole song (set on the ruler). */
        std::optional<core::note_ops::BeatRange> getLoopRegion() const   { return loopRegion; }
        void setLoopRegion (core::note_ops::BeatRange);
        void clearLoopRegion();

        //==============================================================================
        // Markers: named places in the song (core::markers); each change is one undo step.

        std::vector<core::markers::Marker> getMarkers() const;
        core::markers::Marker addMarker (double beat, const juce::String& name);
        void renameMarker (const core::markers::Marker&, const juce::String& name);
        void removeMarker (const core::markers::Marker&);

        //==============================================================================
        // Tempo changes along the song (core::tempo_changes); each call is one undo step, except
        // moveTempoChange (a drag: the caller starts the gesture).

        std::vector<core::tempo_changes::Change> getTempoChanges() const;

        /** A change at `beat` (or the tempo of the one there). Returns its index. */
        int addTempoChange (double beat, double bpm);
        void setTempoChangeBpm (int index, double bpm);
        void moveTempoChange (int index, double beat);
        void setTempoChangeGradual (int index, bool);
        void removeTempoChange (int index);

        /** A name for a new marker: the first suggestion not used yet ("Intro", "Verse"...), else
            "Marker N". */
        juce::String getNewMarkerName() const;

        /** Song mode, looping from the marker to the next one (or the song's end). */
        void loopSection (const core::markers::Marker&);

        /** What Play loops: the open clip (or the clicked audio clip) in clip mode; in song mode
            the loop region, or else
            the whole song up to the end of its last bar. Empty when there is nothing to loop. */
        core::note_ops::BeatRange getPlayRange() const;

    private:
        core::Project& project;

        te::AudioTrack* addInstrumentTrack (bool drums);

        juce::ReferenceCountedObjectPtr<te::AudioTrack> currentTrack;
        juce::ReferenceCountedObjectPtr<te::MidiClip> openedClip;
        te::Clip::Ptr focusedAudioClip;

        // Selected clips by ID, so a clip deleted by undo can't leave a dangling pointer.
        std::vector<te::EditItemID> selection;

        core::snap::Mode snapMode = core::snap::Mode::line;
        double pixelsPerBeat = 16.0;
        PlayMode playMode = PlayMode::clip;
        std::optional<core::note_ops::BeatRange> loopRegion;

        std::vector<DrawnClip> drawn;

        /** The track a new track goes after when asked for below this row's track (the last
            song track for none; the last of a collapsed folder for its row). */
        te::Track* getTrackToAddAfter (te::AudioTrack* rowTrack) const;
        bool clipsDirty = true;

        juce::ListenerList<Listener> listeners;

        void setCurrent (te::AudioTrack*, te::MidiClip*);
        te::AudioTrack* createAudioTrack (const juce::String& name, te::Track* after);
        void changed();
        void validateCurrent();

        void handleAsyncUpdate() override;
        void treeChanged (const juce::ValueTree&);
        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
        void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;

        JUCE_DECLARE_NON_COPYABLE (ArrangementModel)
    };
}
