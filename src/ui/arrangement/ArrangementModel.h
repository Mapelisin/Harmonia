#pragma once

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
        int trackIndex = 0;
        double startBeat = 0.0, endBeat = 0.0;
        bool selected = false, open = false, linked = false, audio = false;
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

        juce::Array<te::AudioTrack*> getTracks() const      { return project.getTracks(); }
        int getNumTracks() const                            { return project.getNumTracks(); }
        te::AudioTrack* getTrack (int index) const          { return getTracks()[index]; }
        int indexOf (const te::Track*) const;

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

        /** Imports audio files at `beat` (snapped down to the grid): the first onto the track at
            `trackIndex` if that is an audio track, the others (or all) onto new audio tracks
            below it, named after the files. Selects the new clips and switches to song mode.
            Returns what went wrong (empty if nothing did). */
        juce::String importAudio (const juce::Array<juce::File>&, int trackIndex, double beat);

        void deleteTrack (te::AudioTrack&);

        /** Clips still named after the track get the new name too. */
        void renameTrack (te::AudioTrack&, const juce::String&);

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

        void renameClip (te::Clip&, const juce::String&);

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

        /** What Play loops: the open clip (or the clicked audio clip) in clip mode; in song mode
            the loop region, or else
            the whole song up to the end of its last bar. Empty when there is nothing to loop. */
        core::note_ops::BeatRange getPlayRange() const;

    private:
        core::Project& project;

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
        bool clipsDirty = true;

        juce::ListenerList<Listener> listeners;

        void setCurrent (te::AudioTrack*, te::MidiClip*);
        te::AudioTrack* createAudioTrack (const juce::String& name, te::Track* after);
        te::Clip* getFocusedAudioClip() const;
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
