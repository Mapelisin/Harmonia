#include "ui/arrangement/ArrangementModel.h"
#include "audio/Export.h"
#include "audio/Groups.h"
#include "audio/Instruments.h"
#include "audio/SampleData.h"
#include "audio/Sends.h"
#include "audio/Sidechain.h"
#include "audio/Freeze.h"
#include "core/AudioTempo.h"
#include "core/ClipLinks.h"
#include "core/Clips.h"
#include "core/MidiFiles.h"
#include "core/NoteEditor.h"
#include "core/Sections.h"
#include "core/TempoChanges.h"
#include "core/Tracks.h"

namespace harmonia::ui::arrangement
{
    namespace clips = core::clips;
    namespace snap = core::snap;

    namespace
    {
        constexpr double newTrackBars = 4.0;

        // Grid lines closer than this (in pixels) are skipped when Snap is "Line".
        constexpr double minLinePixels = 20.0;

        bool isTrackInEdit (const te::Track& track)
        {
            return track.state.isAChildOf (track.edit.state);
        }

        /** Only tracks, clips, notes and tempo matter here; the transport and the rest of the
            edit change all the time while playing. */
        bool affectsArrangement (juce::ValueTree tree)
        {
            for (; tree.isValid(); tree = tree.getParent())
                if (tree.hasType (te::IDs::TRACK) || tree.hasType (te::IDs::MIDICLIP) || tree.hasType (te::IDs::AUDIOCLIP)
                    || tree.hasType (te::IDs::TEMPOSEQUENCE) || core::markers::isMarkerTree (tree))
                    return true;

            return false;
        }

        /** A part all on MIDI channel 10: General MIDI drums. */
        bool isDrumPart (const core::midi_files::Part& part)
        {
            return ! part.notes.empty()
                && std::all_of (part.notes.begin(), part.notes.end(), [] (const core::Note& n) { return n.colour == 9; });
        }
    }

    ArrangementModel::ArrangementModel (core::Project& p)
        : project (p)
    {
        getEdit().state.addListener (this);
        setCurrentTrack (getTracks().getFirst());
    }

    ArrangementModel::~ArrangementModel()
    {
        cancelPendingUpdate();
        getEdit().state.removeListener (this);
    }

    juce::Array<te::AudioTrack*> ArrangementModel::getTracks() const
    {
        const auto all = project.getTracks();
        juce::StringArray collapsed;

        for (const auto& folder : core::folders::getAll (project.getEdit()))
            if (folder.collapsed)
                collapsed.add (folder.id);

        if (collapsed.isEmpty())
            return all;

        // Of a collapsed folder, only its first track has a row.
        juce::Array<te::AudioTrack*> rows;
        juce::StringArray shown;

        for (auto* track : all)
        {
            const auto folder = core::folders::getFolderOf (*track);

            if (folder.has_value() && collapsed.contains (folder->id))
            {
                if (shown.contains (folder->id))
                    continue;

                shown.add (folder->id);
            }

            rows.add (track);
        }

        return rows;
    }

    int ArrangementModel::indexOf (const te::Track* track) const
    {
        return getTracks().indexOf (dynamic_cast<te::AudioTrack*> (const_cast<te::Track*> (track)));
    }

    //==============================================================================
    std::optional<core::folders::Folder> ArrangementModel::getFolderOf (const te::Track& track) const
    {
        return core::folders::getFolderOf (track);
    }

    bool ArrangementModel::isFoldedRow (const te::Track* track) const
    {
        if (track == nullptr)
            return false;

        const auto folder = core::folders::getFolderOf (*track);
        return folder.has_value() && folder->collapsed;
    }

    bool ArrangementModel::isFrozen (const te::Track* track) const
    {
        auto* audioTrack = dynamic_cast<const te::AudioTrack*> (track);
        return audioTrack != nullptr && audio::freeze::isFrozen (*audioTrack);
    }

    bool ArrangementModel::isLockedRow (const te::Track* track) const
    {
        return isFoldedRow (track) || isFrozen (track);
    }

    juce::String ArrangementModel::freezeTrack (te::AudioTrack& track)
    {
        if (const auto error = audio::freeze::freeze (project, track); error.isNotEmpty())
            return error;

        // Its notes aren't heard while it's frozen: nothing of it stays open or selected.
        if (auto* open = getOpenClip(); open != nullptr && open->getClipTrack() == &track)
            openClip (nullptr);

        auto stillSelected = getSelectedClips();
        stillSelected.removeIf ([&] (te::Clip* clip) { return clip->getClipTrack() == &track; });
        setSelection (stillSelected);
        clipsDirty = true;
        changed();
        return {};
    }

    void ArrangementModel::unfreezeTrack (te::AudioTrack& track)
    {
        audio::freeze::unfreeze (project, track);
        clipsDirty = true;
        changed();
    }

    std::optional<core::folders::Folder> ArrangementModel::newFolder (te::AudioTrack& track)
    {
        auto tracks = getTracksFor (track);

        for (auto* clip : getSelectedClips())
            if (auto* clipTrack = dynamic_cast<te::AudioTrack*> (clip->getClipTrack()))
                tracks.addIfNotAlreadyThere (clipTrack);

        project.beginGesture ("New folder");
        const auto folder = core::folders::add (getEdit(), tracks, {});
        clipsDirty = true;
        changed();
        return folder;
    }

    void ArrangementModel::setTrackFolder (te::AudioTrack& track, const juce::String& folderId)
    {
        project.beginGesture (folderId.isEmpty() ? "Take out of the folder" : "Move into a folder");
        core::folders::setFolder (track, folderId);
        clipsDirty = true;
        changed();
    }

    void ArrangementModel::renameFolder (const juce::String& folderId, const juce::String& name)
    {
        project.beginGesture ("Rename folder");
        core::folders::rename (getEdit(), folderId, name);
        changed();
    }

    void ArrangementModel::removeFolder (const juce::String& folderId)
    {
        project.beginGesture ("Remove folder");
        core::folders::remove (getEdit(), folderId);
        clipsDirty = true;
        changed();
    }

    void ArrangementModel::setFolderCollapsed (const juce::String& folderId, bool collapsed)
    {
        const auto members = core::folders::getMembers (getEdit(), folderId);
        core::folders::setCollapsed (getEdit(), folderId, collapsed);

        // The current track hidden: the folder's row is current.
        if (collapsed && members.contains (currentTrack.get()) && currentTrack.get() != members.getFirst())
            setCurrentTrack (members.getFirst());

        // What's hidden isn't selected.
        if (collapsed)
        {
            auto stillSelected = getSelectedClips();
            stillSelected.removeIf ([&] (te::Clip* clip) { return members.contains (dynamic_cast<te::AudioTrack*> (clip->getClipTrack())); });
            setSelection (stillSelected);
        }

        clipsDirty = true;
        changed();
    }

    void ArrangementModel::setFolderMuted (const juce::String& folderId, bool muted)
    {
        core::folders::setMuted (getEdit(), folderId, muted);
        changed();
    }

    void ArrangementModel::setFolderSoloed (const juce::String& folderId, bool soloed)
    {
        core::folders::setSoloed (getEdit(), folderId, soloed);
        changed();
    }

    //==============================================================================
    te::AudioTrack* ArrangementModel::getCurrentTrack() const
    {
        return currentTrack != nullptr && isTrackInEdit (*currentTrack) ? currentTrack.get() : nullptr;
    }

    te::MidiClip* ArrangementModel::getOpenClip() const
    {
        return openedClip != nullptr && clips::isInEdit (*openedClip) ? openedClip.get() : nullptr;
    }

    void ArrangementModel::setCurrentTrack (te::AudioTrack* track)
    {
        if (track != nullptr && track == getCurrentTrack())
            return;

        te::MidiClip* clip = nullptr;

        if (track != nullptr)
            clip = clips::getMainClip (*track);

        setCurrent (track, clip);
    }

    void ArrangementModel::openClip (te::MidiClip* clip)
    {
        auto* track = clip != nullptr ? dynamic_cast<te::AudioTrack*> (clip->getClipTrack()) : getCurrentTrack();
        setCurrent (track, clip);
    }

    void ArrangementModel::focusClip (te::Clip& clip)
    {
        if (auto* midi = dynamic_cast<te::MidiClip*> (&clip))
        {
            openClip (midi);
            return;
        }

        if (focusedAudioClip.get() != &clip)
        {
            focusedAudioClip = &clip;
            clipsDirty = true;
            changed();
        }

        setCurrent (dynamic_cast<te::AudioTrack*> (clip.getClipTrack()), nullptr);
    }

    te::AudioClipBase* ArrangementModel::getFocusedAudioClip() const
    {
        auto* track = getCurrentTrack();

        if (track == nullptr)
            return nullptr;

        if (auto* clip = dynamic_cast<te::AudioClipBase*> (focusedAudioClip.get());
            clip != nullptr && clips::isInEdit (*clip) && clip->getClipTrack() == track)
            return clip;

        for (auto* clip : clips::getClips (*track))
            if (auto* audio = dynamic_cast<te::AudioClipBase*> (clip))
                return audio;

        return nullptr;
    }

    //==============================================================================
    juce::Array<te::AudioTrack*> ArrangementModel::getSelectedTracks() const
    {
        juce::Array<te::AudioTrack*> selected;
        const auto* current = getCurrentTrack();

        for (auto* track : project.getTracks())
            if (track == current || std::find (selectedTracks.begin(), selectedTracks.end(), track->itemID) != selectedTracks.end())
                selected.add (track);

        return selected;
    }

    bool ArrangementModel::isTrackSelected (const te::Track* track) const
    {
        if (track == nullptr || ! isTrackInEdit (*track))
            return false;

        return track == getCurrentTrack()
            || std::find (selectedTracks.begin(), selectedTracks.end(), track->itemID) != selectedTracks.end();
    }

    void ArrangementModel::clickTrack (te::AudioTrack& track, bool toggle, bool range)
    {
        auto* current = getCurrentTrack();

        if (current == nullptr || ! (toggle || range))
        {
            selectedTracks.clear();

            if (current == &track)
                changed();
            else
                setCurrentTrack (&track);

            return;
        }

        if (range)
        {
            const auto all = project.getTracks();
            const auto from = all.indexOf (current), to = all.indexOf (&track);
            selectedTracks.clear();

            for (int i = std::min (from, to); from >= 0 && to >= 0 && i <= std::max (from, to); ++i)
                if (all[i] != current)
                    selectedTracks.push_back (all[i]->itemID);
        }
        else if (&track == current)
        {
            // Out of the selection: the first of the others is current now.
            auto others = getSelectedTracks();
            others.removeFirstMatchingValue (current);

            if (others.isEmpty())
                return;

            const auto kept = selectedTracks;
            setCurrentTrack (others.getFirst());
            selectedTracks = kept;
            std::erase (selectedTracks, others.getFirst()->itemID);
        }
        else if (const auto it = std::find (selectedTracks.begin(), selectedTracks.end(), track.itemID); it != selectedTracks.end())
        {
            selectedTracks.erase (it);
        }
        else
        {
            selectedTracks.push_back (track.itemID);
        }

        changed();
    }

    juce::Array<te::AudioTrack*> ArrangementModel::getTracksFor (te::AudioTrack& track) const
    {
        const auto selected = getSelectedTracks();
        return selected.size() > 1 && selected.contains (&track) ? selected : juce::Array<te::AudioTrack*> { &track };
    }

    void ArrangementModel::setTracksMuted (te::AudioTrack& track, bool muted)
    {
        const auto tracks = getTracksFor (track);
        project.beginGesture (tracks.size() > 1 ? "Mute tracks" : "Mute track");

        for (auto* t : tracks)
            t->setMute (muted);

        changed();
    }

    void ArrangementModel::setTracksSoloed (te::AudioTrack& track, bool soloed)
    {
        const auto tracks = getTracksFor (track);
        project.beginGesture (tracks.size() > 1 ? "Solo tracks" : "Solo track");

        for (auto* t : tracks)
            t->setSolo (soloed);

        changed();
    }

    void ArrangementModel::setTracksColour (te::AudioTrack& track, std::optional<juce::Colour> colour)
    {
        const auto tracks = getTracksFor (track);

        if (std::all_of (tracks.begin(), tracks.end(), [&] (te::AudioTrack* t) { return core::tracks::getColour (*t) == colour; }))
            return;

        project.beginGesture ("Track colour");

        for (auto* t : tracks)
            core::tracks::setColour (*t, colour);
    }

    void ArrangementModel::setTracksFolder (te::AudioTrack& track, const juce::String& folderId)
    {
        project.beginGesture (folderId.isEmpty() ? "Take out of the folder" : "Move into a folder");

        for (auto* t : getTracksFor (track))
            core::folders::setFolder (*t, folderId);

        clipsDirty = true;
        changed();
    }

    void ArrangementModel::setTracksGroup (te::AudioTrack& track, te::AudioTrack* group)
    {
        if (group != nullptr && ! isTrackInEdit (*group))
            return;

        project.beginGesture (group != nullptr ? "Play through " + group->getName() : juce::String ("Play to the master"));

        for (auto* t : getTracksFor (track))
            audio::groups::setGroup (*t, group);

        changed();
    }

    te::AudioTrack* ArrangementModel::groupTracks (te::AudioTrack& track)
    {
        auto* group = audio::groups::addGroup (project, getTracksFor (track));
        changed();
        return group;
    }

    void ArrangementModel::deleteTracks (te::AudioTrack& track)
    {
        const auto tracks = getTracksFor (track);
        project.beginGesture (tracks.size() > 1 ? "Delete tracks" : "Delete track");
        removeTracks (tracks);
    }

    void ArrangementModel::setCurrent (te::AudioTrack* track, te::MidiClip* clip)
    {
        if (track == currentTrack.get() && clip == openedClip.get())
            return;

        // Another track made current: it is the only one selected (clickTrack keeps the rest itself).
        if (track != currentTrack.get())
            selectedTracks.clear();

        currentTrack = track;
        openedClip = clip;
        clipsDirty = true;

        if (onCurrentChanged != nullptr)
            onCurrentChanged();

        changed();
    }

    void ArrangementModel::validateCurrent()
    {
        // Undo, delete or a move to another track can pull things away from under us.
        auto* track = getCurrentTrack();

        if (auto* clip = getOpenClip())
        {
            // The open clip was dragged to another track: that track is current now.
            setCurrent (dynamic_cast<te::AudioTrack*> (clip->getClipTrack()), clip);
            return;
        }

        if (track == nullptr)
        {
            auto* first = getTracks().getFirst();
            setCurrent (first, first != nullptr ? clips::getMainClip (*first) : nullptr);
            return;
        }

        if (openedClip != nullptr)
            setCurrent (track, nullptr);
    }

    //==============================================================================
    const std::vector<DrawnClip>& ArrangementModel::getClips()
    {
        if (clipsDirty)
        {
            drawn.clear();
            const auto tracks = getTracks();
            te::Clip* open = getOpenClip();

            if (open == nullptr)
                open = getFocusedAudioClip();

            // Every track's clips: those of a collapsed folder in the folder's row.
            for (auto* track : project.getTracks())
            {
                const auto folder = core::folders::getFolderOf (*track);
                const auto collapsed = folder.has_value() && folder->collapsed;
                const auto folded = collapsed || audio::freeze::isFrozen (*track);
                const auto row = collapsed ? tracks.indexOf (core::folders::getMembers (getEdit(), folder->id).getFirst())
                                        : tracks.indexOf (track);

                if (row < 0)
                    continue;

                for (auto* clip : clips::getClips (*track))
                {
                    DrawnClip d;
                    d.clip = clip;
                    d.trackIndex = row;
                    d.folded = folded;
                    d.startBeat = clips::getStartBeat (*clip);
                    d.endBeat = clips::getEndBeat (*clip);
                    d.selected = isSelected (*clip);
                    d.open = clip == open;
                    d.audio = clips::isAudio (*clip);

                    if (auto* midi = dynamic_cast<te::MidiClip*> (clip))
                        d.linked = core::clip_links::isLinked (*midi);

                    drawn.push_back (d);
                }
            }

            clipsDirty = false;
        }

        return drawn;
    }

    const DrawnClip* ArrangementModel::findClipAt (int trackIndex, double beat)
    {
        const auto& all = getClips();

        // Later clips are drawn over earlier ones.
        for (auto it = all.rbegin(); it != all.rend(); ++it)
            if (it->trackIndex == trackIndex && ! it->folded && beat >= it->startBeat && beat < it->endBeat)
                return &*it;

        return nullptr;
    }

    double ArrangementModel::getSongEndBeat() const
    {
        return clips::getSongEndBeat (project.getEdit());
    }

    //==============================================================================
    bool ArrangementModel::isSelected (const te::Clip& clip) const
    {
        return std::find (selection.begin(), selection.end(), clip.itemID) != selection.end();
    }

    juce::Array<te::Clip*> ArrangementModel::getSelectedClips() const
    {
        juce::Array<te::Clip*> result;

        for (auto id : selection)
            if (auto* clip = te::findClipForID (project.getEdit(), id))
                if (clips::isInEdit (*clip))
                    result.add (clip);

        return result;
    }

    void ArrangementModel::setSelection (const juce::Array<te::Clip*>& clipsToSelect)
    {
        std::vector<te::EditItemID> ids;

        for (auto* clip : clipsToSelect)
            if (clip != nullptr && std::find (ids.begin(), ids.end(), clip->itemID) == ids.end())
                ids.push_back (clip->itemID);

        // Clips or a piece of time: one kind of selection at a time.
        const auto hadTime = timeSelection.has_value() && ! ids.empty();

        if (hadTime)
            timeSelection.reset();

        if (ids != selection || hadTime)
        {
            selection = std::move (ids);
            clipsDirty = true;
            changed();
        }
    }

    void ArrangementModel::setSelected (te::Clip& clip, bool shouldBeSelected)
    {
        auto current = getSelectedClips();

        if (shouldBeSelected)
            current.addIfNotAlreadyThere (&clip);
        else
            current.removeFirstMatchingValue (&clip);

        setSelection (current);
    }

    void ArrangementModel::selectAll()
    {
        juce::Array<te::Clip*> all;

        // Not what's hidden in collapsed folders.
        for (const auto& d : getClips())
            if (! d.folded)
                all.add (d.clip);

        setSelection (all);
    }

    void ArrangementModel::deselectAll()
    {
        clearTimeSelection();
        setSelection ({});
    }

    //==============================================================================
    std::optional<ArrangementModel::TimeSelection> ArrangementModel::getTimeSelection() const
    {
        if (! timeSelection.has_value())
            return std::nullopt;

        // Rows may have gone since (a track deleted, a folder collapsed).
        const auto numRows = getNumTracks();
        auto piece = *timeSelection;

        if (piece.firstRow >= numRows)
            return std::nullopt;

        piece.lastRow = std::min (piece.lastRow, numRows - 1);
        return piece;
    }

    void ArrangementModel::setTimeSelection (double beatA, double beatB, int rowA, int rowB)
    {
        const auto numRows = getNumTracks();
        const auto start = std::max (0.0, std::min (beatA, beatB)), end = std::max (0.0, std::max (beatA, beatB));

        if (numRows == 0 || end - start < 1.0e-6)
        {
            clearTimeSelection();
            return;
        }

        TimeSelection piece;
        piece.startBeat = start;
        piece.endBeat = end;
        piece.firstRow = juce::jlimit (0, numRows - 1, std::min (rowA, rowB));
        piece.lastRow = juce::jlimit (0, numRows - 1, std::max (rowA, rowB));

        const auto same = timeSelection.has_value()
                            && std::abs (timeSelection->startBeat - piece.startBeat) < 1.0e-9
                            && std::abs (timeSelection->endBeat - piece.endBeat) < 1.0e-9
                            && timeSelection->firstRow == piece.firstRow && timeSelection->lastRow == piece.lastRow;

        if (same && selection.empty())
            return;

        timeSelection = piece;
        selection.clear();
        clipsDirty = true;
        changed();
    }

    void ArrangementModel::clearTimeSelection()
    {
        if (timeSelection.has_value())
        {
            timeSelection.reset();
            changed();
        }
    }

    //==============================================================================
    void ArrangementModel::setSnap (snap::Mode mode)
    {
        if (snapMode != mode)
        {
            snapMode = mode;
            changed();
        }
    }

    void ArrangementModel::setPixelsPerBeat (double ppb)
    {
        pixelsPerBeat = ppb;
    }

    snap::Context ArrangementModel::getSnapContext() const
    {
        snap::Context context;
        context.beatsPerBar = getBeatsPerBar();
        context.lineBeats = snap::getLineBeats (pixelsPerBeat, context.beatsPerBar, minLinePixels);
        return context;
    }

    double ArrangementModel::getBeatsPerBar() const
    {
        return clips::getBeatsPerBar (project.getEdit());
    }

    const std::vector<snap::Mode>& ArrangementModel::getSnapModes()
    {
        static const std::vector<snap::Mode> modes { snap::Mode::line, snap::Mode::bar, snap::Mode::beat,
                                                     snap::Mode::halfBeat, snap::Mode::quarterBeat, snap::Mode::none };
        return modes;
    }

    //==============================================================================
    te::AudioTrack* ArrangementModel::addTrack()
    {
        return addInstrumentTrack ("Add track", "Track " + juce::String (project.getNumTracks() + 1),
                                   [] (te::AudioTrack& track) { audio::instruments::useBuiltInSynth (track); });
    }

    te::AudioTrack* ArrangementModel::addDrumTrack()
    {
        return addInstrumentTrack ("Add drum track", "Drums", [] (te::AudioTrack& track) { audio::instruments::makeDrumTrack (track); });
    }

    te::AudioTrack* ArrangementModel::addSamplerTrack (const juce::File& sound, juce::String& error)
    {
        // Before there is a track: can the sound be used at all?
        error = sound != juce::File() ? audio::SampleData::check (project.getEdit().engine, sound) : juce::String();

        if (error.isNotEmpty())
            return nullptr;

        return addInstrumentTrack ("Add sampler track", sound != juce::File() ? sound.getFileNameWithoutExtension() : juce::String ("Sampler"),
                                   [&sound] (te::AudioTrack& track) { audio::instruments::useSampler (track, sound); });
    }

    te::AudioTrack* ArrangementModel::addInstrumentTrack (const juce::String& undoName, const juce::String& name,
                                                          const std::function<void (te::AudioTrack&)>& giveInstrument)
    {
        auto start = 0.0, length = newTrackBars * getBeatsPerBar();

        if (auto* open = getOpenClip())
        {
            start = clips::getStartBeat (*open);
            length = clips::getEndBeat (*open) - start;
        }

        project.beginGesture (undoName);
        auto* track = project.addTrack (name);

        if (track == nullptr)
            return nullptr;

        giveInstrument (*track);
        auto* clip = project.addMidiClip (*track, start, length, track->getName());
        setSelection ({});
        setCurrent (track, clip);
        return track;
    }

    te::AudioTrack* ArrangementModel::createAudioTrack (const juce::String& name, te::Track* after)
    {
        auto* track = project.addTrack (name, after);

        if (track != nullptr)
            core::tracks::setKind (*track, core::tracks::Kind::audio);

        return track;
    }

    te::AudioTrack* ArrangementModel::addAudioTrack()
    {
        project.beginGesture ("Add audio track");
        auto* track = createAudioTrack ("Audio " + juce::String (project.getNumTracks() + 1), nullptr);

        if (track != nullptr)
        {
            setSelection ({});
            setCurrent (track, nullptr);
        }

        return track;
    }

    juce::String ArrangementModel::importAudio (const juce::Array<juce::File>& files, int trackIndex, double beat)
    {
        if (files.isEmpty())
            return {};

        const auto start = std::max (0.0, snap::snapFloor (beat, snapMode, getSnapContext()));
        auto* target = getTrack (trackIndex);
        te::Track* previous = getTrackToAddAfter (target);
        auto useTarget = target != nullptr && ! isLockedRow (target) && core::tracks::isAudio (*target);

        project.beginGesture (files.size() == 1 ? "Import audio" : "Import audio files");
        juce::StringArray problems;
        juce::Array<te::Clip*> added;

        for (const auto& file : files)
        {
            if (! project.canImport (file))
            {
                problems.add ("Can't read " + file.getFileName() + " as audio");
                continue;
            }

            auto* track = useTarget ? target : createAudioTrack (file.getFileNameWithoutExtension(), previous);
            useTarget = false;

            if (track == nullptr)
                continue;

            juce::String error;

            if (auto* clip = project.addAudioClip (*track, file, start, error))
            {
                // A loop plays in time with the song (at its tempo to the hundredth, measured);
                // anything else at its own speed.
                if (const auto bpm = core::audio_tempo::findLoopBpm (getEdit().engine, file, project.getTempoBpm()))
                    follow (*clip, bpm);

                added.add (clip);
            }
            else
            {
                problems.add (error);
            }

            previous = track;
        }

        if (! added.isEmpty())
        {
            setSelection (added);
            focusClip (*added.getFirst());
            setPlayMode (PlayMode::song);
        }

        clipsDirty = true;
        changed();
        return problems.joinIntoString ("; ");
    }

    te::Track* ArrangementModel::getTrackToAddAfter (te::AudioTrack* rowTrack) const
    {
        if (rowTrack == nullptr)
            return project.getTracks().getLast();

        // Below a collapsed folder, not into it.
        if (isFoldedRow (rowTrack))
            return core::folders::getMembers (project.getEdit(), core::folders::getFolderOf (*rowTrack)->id).getLast();

        return rowTrack;
    }

    bool ArrangementModel::isSongEmpty() const
    {
        for (auto* track : project.getTracks())
            for (auto* clip : clips::getClips (*track))
                if (clips::isAudio (*clip) || ! dynamic_cast<te::MidiClip*> (clip)->getSequence().getNotes().isEmpty())
                    return false;

        return true;
    }

    juce::String ArrangementModel::importMidi (const juce::Array<juce::File>& files, int trackIndex, double beat)
    {
        if (files.isEmpty())
            return {};

        const auto start = std::max (0.0, snap::snapFloor (beat, snapMode, getSnapContext()));
        auto* target = getTrack (trackIndex);
        te::Track* previous = getTrackToAddAfter (target);
        auto useTarget = target != nullptr && ! isLockedRow (target) && ! core::tracks::isAudio (*target);
        auto adoptTempo = isSongEmpty();

        project.beginGesture (files.size() == 1 ? "Import MIDI" : "Import MIDI files");
        juce::StringArray problems;
        juce::Array<te::Clip*> added;

        for (const auto& file : files)
        {
            juce::String error;
            const auto contents = core::midi_files::read (file, error);

            if (! contents.has_value())
            {
                problems.add (error);
                continue;
            }

            // A song started from a MIDI file plays at the file's tempo.
            if (std::exchange (adoptTempo, false))
            {
                if (contents->bpm.has_value())
                    project.setTempoBpm (*contents->bpm);

                if (contents->beatsPerBar.has_value())
                    project.setBeatsPerBar (*contents->beatsPerBar);
            }

            const auto& parts = contents->parts;
            const auto single = parts.size() == 1;
            const auto beatsPerBar = getBeatsPerBar();
            const auto baseName = file.getFileNameWithoutExtension();

            for (size_t i = 0; i < parts.size(); ++i)
            {
                const auto& part = parts[i];
                const auto name = single ? baseName
                                         : part.name.isNotEmpty() ? part.name : baseName + " " + juce::String ((int) i + 1);
                te::AudioTrack* track = nullptr;

                const auto drums = isDrumPart (part);

                if (useTarget && single)
                {
                    track = target;

                    // Drums dropped on a new, empty track: it becomes the drum track.
                    if (drums && audio::instruments::isUntouched (*track))
                        audio::instruments::makeDrumTrack (*track);
                }
                else if ((track = project.addTrack (name, previous)) != nullptr)
                {
                    if (drums)
                        audio::instruments::makeDrumTrack (*track);
                    else
                        audio::instruments::useBuiltInSynth (*track);
                }

                useTarget = false;

                if (track == nullptr)
                    continue;

                const auto bars = std::max (1.0, std::ceil (part.endBeat / beatsPerBar - 1.0e-9));

                if (auto* clip = project.addMidiClip (*track, start, bars * beatsPerBar, name))
                {
                    core::NoteEditor (*clip).add (part.notes);

                    for (const auto& [controller, points] : part.controllerEvents)
                        core::controllers::set (*clip, controller, points);

                    added.add (clip);
                }

                previous = track;
            }

            useTarget = false;
        }

        if (! added.isEmpty())
        {
            setSelection (added);

            if (auto* first = dynamic_cast<te::MidiClip*> (added.getFirst()))
                openClip (first);

            if (added.size() > 1)
                setPlayMode (PlayMode::song);
        }

        clipsDirty = true;
        changed();
        return problems.joinIntoString ("; ");
    }

    juce::String ArrangementModel::importFiles (const juce::Array<juce::File>& files, int trackIndex, double beat)
    {
        juce::Array<juce::File> audioFiles, midiFiles;

        for (const auto& file : files)
            (core::midi_files::isMidiFile (file) ? midiFiles : audioFiles).add (file);

        juce::StringArray problems;
        problems.add (importAudio (audioFiles, trackIndex, beat));
        problems.add (importMidi (midiFiles, trackIndex, beat));
        problems.removeEmptyStrings();
        return problems.joinIntoString ("; ");
    }

    void ArrangementModel::deleteTrack (te::AudioTrack& track)
    {
        if (getTracks().indexOf (&track) < 0)
            return;

        project.beginGesture ("Delete track");
        removeTracks ({ &track });
    }

    void ArrangementModel::removeTracks (const juce::Array<te::AudioTrack*>& tracksToRemove)
    {
        // The row the current track was on: the track that comes there is current afterwards.
        const auto index = getTracks().indexOf (getCurrentTrack());
        auto currentRemoved = false;

        for (auto* track : tracksToRemove)
        {
            if (track == nullptr || ! isTrackInEdit (*track))
                continue;

            const te::Track::Ptr keepAlive (track);
            currentRemoved = currentRemoved || currentTrack.get() == track;

            // Linked copies on other tracks stay linked only if more than one of them is left.
            clips::remove (clips::getClips (*track));
            audio::sidechain::forgetSource (*track);
            getEdit().deleteTrack (track);
        }

        core::folders::tidy (getEdit());
        const auto remaining = getTracks();

        if (currentRemoved)
            setCurrentTrack (remaining.isEmpty() ? nullptr : remaining[juce::jlimit (0, remaining.size() - 1, index)]);

        clipsDirty = true;
        changed();
    }

    juce::String ArrangementModel::makeCopyName (const juce::String& name) const
    {
        auto base = name.trim();
        auto number = 2;

        if (const auto last = base.fromLastOccurrenceOf (" ", false, false);
                base.containsChar (' ') && last.isNotEmpty() && last.containsOnly ("0123456789"))
        {
            number = last.getIntValue() + 1;
            base = base.upToLastOccurrenceOf (" ", false, false).trimEnd();
        }

        if (base.isEmpty())
            base = "Track";

        const auto taken = [this] (const juce::String& candidate)
        {
            for (auto* track : project.getTracks())
                if (track->getName() == candidate)
                    return true;

            return false;
        };

        while (taken (base + " " + juce::String (number)))
            ++number;

        return base + " " + juce::String (number);
    }

    te::AudioTrack* ArrangementModel::duplicateTrack (te::AudioTrack& track)
    {
        if (! project.getTracks().contains (&track))
            return nullptr;

        const auto oldName = track.getName();
        const auto name = makeCopyName (oldName);

        project.beginGesture ("Duplicate track");
        const auto copied = getEdit().copyTrack (&track, te::TrackInsertPoint (nullptr, &track));
        auto* copy = dynamic_cast<te::AudioTrack*> (copied.get());

        if (copy == nullptr)
            return nullptr;

        copy->setName (name);

        for (auto* clip : clips::getClips (*copy))
        {
            if (clip->getName() == oldName)
                clip->setName (name);

            // The copies are the new track's own: editing them leaves the originals alone.
            if (auto* midi = dynamic_cast<te::MidiClip*> (clip); midi != nullptr && core::clip_links::isLinked (*midi))
                core::clip_links::unlink (*midi);
        }

        core::folders::tidy (getEdit());
        setSelection ({});
        setCurrentTrack (copy);
        clipsDirty = true;
        changed();
        return copy;
    }

    void ArrangementModel::moveTrack (te::AudioTrack& track, int newIndex)
    {
        auto others = getTracks();
        const auto oldIndex = others.indexOf (&track);
        newIndex = juce::jlimit (0, others.size() - 1, newIndex);

        if (oldIndex < 0 || newIndex == oldIndex)
            return;

        // The row of a collapsed folder: all its tracks go.
        juce::Array<te::AudioTrack*> moved { &track };

        if (isFoldedRow (&track))
            moved = core::folders::getMembers (getEdit(), core::folders::getFolderOf (track)->id);

        others.remove (oldIndex);
        te::Track* preceding = nullptr;

        if (newIndex > 0)
        {
            // After the row above, the whole of it if that is a collapsed folder.
            preceding = getTrackToAddAfter (others[newIndex - 1]);
        }
        else
        {
            // Before the first song track: after whatever the edit has before it (its own tracks).
            auto all = te::getAllTracks (getEdit());

            for (auto* m : moved)
                all.removeFirstMatchingValue (m);

            const auto first = all.indexOf (others.getFirst());
            preceding = first > 0 ? all[first - 1] : nullptr;
        }

        project.beginGesture (moved.size() > 1 ? "Move folder" : "Move track");

        for (auto* m : moved)
        {
            getEdit().moveTrack (m, te::TrackInsertPoint (nullptr, preceding));
            preceding = m;
        }

        if (moved.size() == 1)
            core::folders::trackMoved (track);
        else
            core::folders::tidy (getEdit());
        clipsDirty = true;
        changed();
    }

    juce::String ArrangementModel::renderToAudio (te::AudioTrack& track)
    {
        const auto onTrack = clips::getClips (track);

        if (onTrack.isEmpty())
            return "Nothing to render: " + track.getName() + " has no clips";

        auto start = std::numeric_limits<double>::max(), end = 0.0;

        for (auto* clip : onTrack)
        {
            start = std::min (start, clips::getStartBeat (*clip));
            end = std::max (end, clips::getEndBeat (*clip));
        }

        auto& edit = getEdit();
        const te::TimeRange range (edit.tempoSequence.toTime (te::BeatPosition::fromBeats (start)),
                                   edit.tempoSequence.toTime (te::BeatPosition::fromBeats (end))
                                       + te::TimeDuration::fromSeconds (audio::exportTailSeconds));
        const auto file = edit.engine.getEngineBehaviour().getFileForNewAudioRecording (track, ".wav");

        if (const auto error = audio::renderTrack (track, file, range); error.isNotEmpty())
            return error;

        project.beginGesture ("Render to audio");
        const auto name = track.getName() + " (audio)";
        auto* audioTrack = createAudioTrack (name, &track);

        if (audioTrack != nullptr)
            core::tracks::setColour (*audioTrack, core::tracks::getColour (track));

        if (audioTrack == nullptr)
            return "Couldn't add a track for the audio";

        juce::String error;
        auto* clip = project.addAudioClip (*audioTrack, file, start, error);

        if (clip == nullptr)
            return error;

        clip->setName (name);

        // Its reverb and delay as before: the muted track sends nothing any more.
        for (auto* returnTrack : project.getReturnTracks())
            if (audio::sends::getSend (track, *returnTrack) != nullptr)
                audio::sends::setLevel (*audioTrack, *returnTrack, audio::sends::getLevel (track, *returnTrack));

        // Through its group as before: the render has only the track's own sound.
        audio::groups::setGroup (*audioTrack, audio::groups::getGroup (track));

        // Tracktion's mute isn't undoable: set it so that undo unmutes the track again.
        track.state.setProperty (te::IDs::mute, true, &edit.getUndoManager());
        setSelection ({ clip });
        setCurrentTrack (audioTrack);
        clipsDirty = true;
        changed();
        return {};
    }

    void ArrangementModel::renameTrack (te::AudioTrack& track, const juce::String& name)
    {
        const auto trimmed = name.trim();

        const auto oldName = track.getName();

        if (trimmed.isNotEmpty() && trimmed != oldName)
        {
            project.beginGesture ("Rename track");
            track.setName (trimmed);

            // Clips named after the track (as new clips are) keep following it.
            for (auto* clip : clips::getClips (track))
                if (clip->getName() == oldName)
                    clip->setName (trimmed);
        }
    }

    te::MidiClip* ArrangementModel::createClip (te::AudioTrack& track, double beat)
    {
        if (core::tracks::isAudio (track))
            return nullptr;

        const auto start = std::max (0.0, snap::snapFloor (beat, snapMode, getSnapContext()));

        project.beginGesture ("Add clip");
        auto* clip = project.addMidiClip (track, start, getBeatsPerBar(), track.getName());

        if (clip != nullptr)
        {
            setSelection ({ clip });
            openClip (clip);
        }

        return clip;
    }

    void ArrangementModel::deleteSelected()
    {
        const auto toDelete = getSelectedClips();

        if (toDelete.isEmpty())
            return;

        project.beginGesture ("Delete clips");
        removeClips (toDelete);
    }

    void ArrangementModel::removeClips (const juce::Array<te::Clip*>& toDelete)
    {
        if (toDelete.isEmpty())
            return;

        for (auto* clip : toDelete)
            std::erase (selection, clip->itemID);

        clips::remove (toDelete);
        clipsDirty = true;
        validateCurrent();
        changed();
    }

    void ArrangementModel::duplicateSelected (bool linked)
    {
        const auto originals = getSelectedClips();

        if (originals.isEmpty())
            return;

        auto start = std::numeric_limits<double>::max(), end = 0.0;

        for (auto* clip : originals)
        {
            start = std::min (start, clips::getStartBeat (*clip));
            end = std::max (end, clips::getEndBeat (*clip));
        }

        const auto beatsPerBar = getBeatsPerBar();
        const auto offset = std::max (1.0, std::ceil ((end - start) / beatsPerBar - 1.0e-9)) * beatsPerBar;

        project.beginGesture (linked ? "Duplicate clips (linked)" : "Duplicate clips");
        juce::Array<te::Clip*> copies;

        for (auto* clip : originals)
            if (auto* copy = clips::duplicate (*clip, clips::getStartBeat (*clip) + offset, linked))
                copies.add (copy);

        setSelection (copies);
        clipsDirty = true;
        changed();
    }

    void ArrangementModel::makeUnique (te::MidiClip& clip)
    {
        if (! core::clip_links::isLinked (clip))
            return;

        project.beginGesture ("Make unique");
        core::clip_links::unlink (clip);
        clipsDirty = true;
        changed();
    }

    void ArrangementModel::setTrackColour (te::AudioTrack& track, std::optional<juce::Colour> colour)
    {
        if (core::tracks::getColour (track) == colour)
            return;

        project.beginGesture ("Track colour");
        core::tracks::setColour (track, colour);
    }

    void ArrangementModel::setClipColour (const juce::Array<te::Clip*>& clips, std::optional<juce::Colour> colour)
    {
        auto began = false;

        for (auto* clip : clips)
        {
            if (clip == nullptr || core::clips::getColour (*clip) == colour)
                continue;

            if (! began)
                project.beginGesture ("Clip colour");

            began = true;
            core::clips::setColour (*clip, colour);
        }
    }

    std::vector<core::markers::Marker> ArrangementModel::getMarkers() const
    {
        return core::markers::getAll (project.getEdit());
    }

    std::vector<core::tempo_changes::Change> ArrangementModel::getTempoChanges() const
    {
        return core::tempo_changes::getAll (project.getEdit());
    }

    int ArrangementModel::addTempoChange (double beat, double bpm)
    {
        project.beginGesture ("Tempo change");
        const auto index = core::tempo_changes::add (getEdit(), beat, bpm);
        changed();
        return index;
    }

    void ArrangementModel::setTempoChangeBpm (int index, double bpm)
    {
        project.beginGesture ("Change tempo");
        core::tempo_changes::setBpm (getEdit(), index, bpm);
        changed();
    }

    void ArrangementModel::moveTempoChange (int index, double beat)
    {
        core::tempo_changes::move (getEdit(), index, beat);
        changed();
    }

    void ArrangementModel::setTempoChangeGradual (int index, bool gradual)
    {
        project.beginGesture (gradual ? "Glide to the tempo" : "Jump to the tempo");
        core::tempo_changes::setGradual (getEdit(), index, gradual);
        changed();
    }

    void ArrangementModel::removeTempoChange (int index)
    {
        project.beginGesture ("Remove tempo change");
        core::tempo_changes::remove (getEdit(), index);
        changed();
    }

    core::markers::Marker ArrangementModel::addMarker (double beat, const juce::String& name)
    {
        project.beginGesture ("Add marker");
        return core::markers::add (getEdit(), beat, name.trim().isNotEmpty() ? name.trim() : getNewMarkerName());
    }

    void ArrangementModel::renameMarker (const core::markers::Marker& marker, const juce::String& name)
    {
        if (name.trim().isEmpty() || name.trim() == marker.name)
            return;

        project.beginGesture ("Rename marker");
        core::markers::rename (getEdit(), marker, name);
    }

    void ArrangementModel::removeMarker (const core::markers::Marker& marker)
    {
        project.beginGesture ("Delete marker");
        core::markers::remove (getEdit(), marker);
    }

    juce::String ArrangementModel::getNewMarkerName() const
    {
        juce::StringArray used;

        for (const auto& m : getMarkers())
            used.add (m.name);

        for (const auto& name : core::markers::getSuggestedNames())
            if (! used.contains (name))
                return name;

        return "Marker " + juce::String (used.size() + 1);
    }

    void ArrangementModel::loopSection (const core::markers::Marker& marker)
    {
        const auto [start, end] = core::markers::getSection (project.getEdit(), marker, getSongEndBeat());

        if (end <= start)
            return;

        setPlayMode (PlayMode::song);
        setLoopRegion ({ start, end });
    }

    void ArrangementModel::setLooping (te::MidiClip& clip, bool shouldLoop)
    {
        if (clips::isLooping (clip) == shouldLoop)
            return;

        project.beginGesture (shouldLoop ? "Loop clip" : "Stop looping");
        clips::setLooping (clip, shouldLoop);
    }

    ArrangementModel::TempoFound ArrangementModel::setFollowsTempo (te::WaveAudioClip& clip, bool shouldFollow)
    {
        namespace audio_tempo = core::audio_tempo;

        if (audio_tempo::followsTempo (clip) == shouldFollow)
            return {};

        project.beginGesture (shouldFollow ? "Follow the song tempo" : "Play at its own speed");

        if (! shouldFollow)
        {
            audio_tempo::setFollowsTempo (clip, std::nullopt);
            return {};
        }

        const auto songBpm = core::tempo_changes::getBpmAt (getEdit(), clips::getStartBeat (clip));
        return follow (clip, audio_tempo::findLoopBpm (getEdit().engine, clip.getOriginalFile(), songBpm));
    }

    ArrangementModel::TempoFound ArrangementModel::follow (te::WaveAudioClip& clip, std::optional<double> named)
    {
        namespace audio_tempo = core::audio_tempo;

        // The tempo of the song where the clip is; what the file says about itself is the better
        // guide to which of the tempi that fit (twice, half as fast) is meant.
        const auto songBpm = core::tempo_changes::getBpmAt (getEdit(), clips::getStartBeat (clip));
        auto measured = audio_tempo::measure (getEdit().engine, clip.getAudioFile().getFile(), named.value_or (songBpm));

        // The file knows its tempo: a measurement far from it heard something else in the sound.
        if (measured.has_value() && named.has_value() && std::abs (measured->bpm / *named - 1.0) > 0.02)
            measured.reset();

        if (measured.has_value())
        {
            audio_tempo::setFollowsTempo (clip, *measured);
            const auto hidden = audio_tempo::startOnBeat (clip, *measured);

            // A sound that keeps its tempo; or one that doesn't, each beat put on a beat of the song.
            auto note = measured->drifts() ? "Its tempo isn't steady (around " + audio_tempo::formatBpm (measured->bpm) + " BPM): its "
                                                 + juce::String ((int) measured->beatSeconds.size()) + " beats were put on the grid one by one"
                                           : "Its beat was measured: " + juce::String (measured->bpm, 2) + " BPM";

            if (hidden > 0.0005)
                note << ", first beat " << juce::String (hidden, 2) << " s in (the clip starts there now)";

            note << ". Not on the grid? Loop, " << juce::String::fromUTF8 ("\xc3\xb7" "2 / " "\xc3\x97" "2") << " and the arrows put it right";
            return { note, false };
        }

        if (named.has_value())
        {
            audio_tempo::setFollowsTempo (clip, *named);
            return { "No clear beat to hear in the sound: its tempo is as its file says, " + audio_tempo::formatBpm (*named) + " BPM", false };
        }

        audio_tempo::setFollowsTempo (clip, songBpm);
        return { "No steady beat could be heard in this sound: it plays as before until you set its tempo (Loop)", true };
    }

    bool ArrangementModel::slideSound (te::WaveAudioClip& clip, double beats)
    {
        if (! core::audio_tempo::followsTempo (clip))
            return false;

        project.beginGesture ("Slide the sound");
        return core::audio_tempo::slide (clip, beats);
    }

    void ArrangementModel::setLoopBpm (te::WaveAudioClip& clip, double bpm)
    {
        const auto current = core::audio_tempo::getLoopBpm (clip);

        if (! current.has_value() || std::abs (*current - bpm) < 0.001)
            return;

        project.beginGesture ("Loop tempo");
        core::audio_tempo::setFollowsTempo (clip, bpm);
    }

    void ArrangementModel::setPitch (te::AudioClipBase& clip, float semitones)
    {
        if (std::abs (core::audio_tempo::getPitch (clip) - semitones) < 0.001f)
            return;

        project.beginGesture ("Pitch");
        core::audio_tempo::setPitch (clip, semitones);
    }

    void ArrangementModel::bakeLoop (te::MidiClip& clip)
    {
        if (! clips::isLooping (clip))
            return;

        project.beginGesture ("Make the repeats notes");
        clips::bakeLoop (clip);
    }

    void ArrangementModel::renameClip (te::Clip& clip, const juce::String& name)
    {
        const auto trimmed = name.trim();

        if (trimmed.isNotEmpty() && trimmed != clip.getName())
        {
            project.beginGesture ("Rename clip");
            clip.setName (trimmed);
        }
    }

    //==============================================================================
    void ArrangementModel::insertBars (double startBeat, double lengthBeats)
    {
        startBeat = std::max (0.0, startBeat);

        if (lengthBeats <= 0.0)
            return;

        project.beginGesture ("Insert bars");
        core::sections::insertSpace (getEdit(), startBeat, lengthBeats);

        if (loopRegion.has_value())
        {
            if (loopRegion->start > startBeat + 1.0e-6)
                loopRegion = core::note_ops::BeatRange { loopRegion->start + lengthBeats, loopRegion->end + lengthBeats };
            else if (loopRegion->start < startBeat - 1.0e-6 && loopRegion->end > startBeat + 1.0e-6)
                loopRegion->end += lengthBeats;
        }

        clipsDirty = true;
        changed();
    }

    void ArrangementModel::deleteBars (double startBeat, double endBeat)
    {
        startBeat = std::max (0.0, startBeat);

        if (endBeat <= startBeat)
            return;

        project.beginGesture ("Delete bars");
        core::sections::deleteRange (getEdit(), startBeat, endBeat);

        if (loopRegion.has_value())
        {
            const auto length = endBeat - startBeat;

            if (loopRegion->start >= endBeat - 1.0e-6)
                loopRegion = core::note_ops::BeatRange { loopRegion->start - length, loopRegion->end - length };
            else if (loopRegion->end > startBeat + 1.0e-6)
                loopRegion.reset();
        }

        clipsDirty = true;
        changed();
    }

    int ArrangementModel::duplicateBars (double startBeat, double endBeat)
    {
        startBeat = std::max (0.0, startBeat);

        if (endBeat <= startBeat)
            return 0;

        project.beginGesture ("Duplicate bars");
        const auto copies = core::sections::duplicateRange (getEdit(), startBeat, endBeat);
        const auto length = endBeat - startBeat;

        if (loopRegion.has_value())
        {
            if (std::abs (loopRegion->start - startBeat) < 1.0e-6 && std::abs (loopRegion->end - endBeat) < 1.0e-6)
                loopRegion = core::note_ops::BeatRange { endBeat, endBeat + length };
            else if (loopRegion->start >= endBeat - 1.0e-6)
                loopRegion = core::note_ops::BeatRange { loopRegion->start + length, loopRegion->end + length };
            else if (loopRegion->end > endBeat + 1.0e-6)
                loopRegion->end += length;
        }

        setSelection (copies);
        clipsDirty = true;
        changed();
        return copies.size();
    }

    double ArrangementModel::makeSong (const core::song_forms::Form& form)
    {
        if (! loopRegion.has_value())
            return 0.0;

        project.beginGesture ("Make a song");
        const auto length = core::song_forms::build (getEdit(), loopRegion->start, loopRegion->end, form, getBeatsPerBar());

        if (length > 0.0)
        {
            loopRegion.reset();
            playMode = PlayMode::song;
            deselectAll();
        }

        clipsDirty = true;
        changed();
        return length;
    }

    juce::String ArrangementModel::describeBars (core::note_ops::BeatRange range, double beatsPerBar)
    {
        const auto isWhole = [] (double x) { return std::abs (x - std::round (x)) < 1.0e-6; };

        if (beatsPerBar > 0.0 && isWhole (range.start / beatsPerBar) && isWhole (range.end / beatsPerBar))
        {
            const auto first = juce::roundToInt (range.start / beatsPerBar) + 1;
            const auto last = juce::roundToInt (range.end / beatsPerBar);
            return first >= last ? "bar " + juce::String (first) : "bars " + juce::String (first) + "-" + juce::String (last);
        }

        const auto beat = [isWhole] (double b) { return juce::String (b + 1.0, isWhole (b) ? 0 : 2); };
        return "beats " + beat (range.start) + "-" + beat (range.end);
    }

    void ArrangementModel::undo()
    {
        project.undo();
        clipsDirty = true;
        validateCurrent();
        changed();
    }

    void ArrangementModel::redo()
    {
        project.redo();
        clipsDirty = true;
        validateCurrent();
        changed();
    }

    //==============================================================================
    void ArrangementModel::setPlayMode (PlayMode mode)
    {
        if (playMode != mode)
        {
            playMode = mode;
            changed();
        }
    }

    void ArrangementModel::setLoopRegion (core::note_ops::BeatRange range)
    {
        range = { std::max (0.0, std::min (range.start, range.end)), std::max (0.0, std::max (range.start, range.end)) };

        if (range.getLength() <= 0.0)
        {
            clearLoopRegion();
            return;
        }

        loopRegion = range;
        changed();
    }

    void ArrangementModel::clearLoopRegion()
    {
        if (loopRegion.has_value())
        {
            loopRegion.reset();
            changed();
        }
    }

    core::note_ops::BeatRange ArrangementModel::getPlayRange() const
    {
        if (playMode == PlayMode::clip)
        {
            // A looping clip: its loop, as the piano roll shows it.
            if (auto* clip = getOpenClip(); clip != nullptr && clips::isLooping (*clip))
            {
                const auto loop = clips::getLoopSongRange (*clip);
                return { std::max (0.0, loop.getStart()), loop.getEnd() };
            }

            if (auto* clip = getOpenClip())
                return { clips::getStartBeat (*clip), clips::getEndBeat (*clip) };

            if (auto* clip = getFocusedAudioClip())
                return { clips::getStartBeat (*clip), clips::getEndBeat (*clip) };

            return {};
        }

        if (loopRegion.has_value())
            return *loopRegion;

        const auto end = getSongEndBeat();

        if (end <= 0.0)
            return {};

        const auto beatsPerBar = getBeatsPerBar();
        return { 0.0, std::ceil (end / beatsPerBar - 1.0e-9) * beatsPerBar };
    }

    //==============================================================================
    void ArrangementModel::changed()
    {
        triggerAsyncUpdate();
    }

    void ArrangementModel::handleAsyncUpdate()
    {
        validateCurrent();

        // Forget clips that are gone.
        const auto before = selection.size();
        std::erase_if (selection, [this] (te::EditItemID id)
        {
            auto* clip = te::findClipForID (project.getEdit(), id);
            return clip == nullptr || ! clips::isInEdit (*clip);
        });

        if (selection.size() != before)
            clipsDirty = true;

        std::erase_if (selectedTracks, [this] (te::EditItemID id)
        {
            auto* track = te::findTrackForID (project.getEdit(), id);
            return track == nullptr || ! isTrackInEdit (*track);
        });

        listeners.call ([] (Listener& l) { l.arrangementChanged(); });
    }

    void ArrangementModel::treeChanged (const juce::ValueTree& tree)
    {
        if (affectsArrangement (tree))
        {
            clipsDirty = true;
            changed();
        }
    }

    void ArrangementModel::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
    {
        treeChanged (tree);
    }

    void ArrangementModel::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
    {
        treeChanged (parent);
        treeChanged (child);
    }

    void ArrangementModel::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
    {
        treeChanged (parent);
        treeChanged (child);
    }

    void ArrangementModel::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int)
    {
        treeChanged (parent);
    }
}
