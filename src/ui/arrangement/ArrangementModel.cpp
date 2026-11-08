#include "ui/arrangement/ArrangementModel.h"
#include "audio/Instruments.h"
#include "core/ClipLinks.h"
#include "core/Clips.h"
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
                    || tree.hasType (te::IDs::TEMPOSEQUENCE))
                    return true;

            return false;
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

    int ArrangementModel::indexOf (const te::Track* track) const
    {
        return getTracks().indexOf (dynamic_cast<te::AudioTrack*> (const_cast<te::Track*> (track)));
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
            changed();
        }

        setCurrent (dynamic_cast<te::AudioTrack*> (clip.getClipTrack()), nullptr);
    }

    te::Clip* ArrangementModel::getFocusedAudioClip() const
    {
        auto* clip = focusedAudioClip.get();

        if (clip == nullptr || ! clips::isInEdit (*clip) || clip->getClipTrack() != getCurrentTrack())
            return nullptr;

        return clip;
    }

    void ArrangementModel::setCurrent (te::AudioTrack* track, te::MidiClip* clip)
    {
        if (track == currentTrack.get() && clip == openedClip.get())
            return;

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
            auto* open = getOpenClip();

            for (int i = 0; i < tracks.size(); ++i)
            {
                for (auto* clip : clips::getClips (*tracks[i]))
                {
                    DrawnClip d;
                    d.clip = clip;
                    d.trackIndex = i;
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
            if (it->trackIndex == trackIndex && beat >= it->startBeat && beat < it->endBeat)
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

        if (ids != selection)
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

        for (const auto& d : getClips())
            all.add (d.clip);

        setSelection (all);
    }

    void ArrangementModel::deselectAll()
    {
        setSelection ({});
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
        auto start = 0.0, length = newTrackBars * getBeatsPerBar();

        if (auto* open = getOpenClip())
        {
            start = clips::getStartBeat (*open);
            length = clips::getEndBeat (*open) - start;
        }

        project.beginGesture ("Add track");
        auto* track = project.addTrack ("Track " + juce::String (getNumTracks() + 1));

        if (track == nullptr)
            return nullptr;

        audio::instruments::useBuiltInSynth (*track);
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
        auto* track = createAudioTrack ("Audio " + juce::String (getNumTracks() + 1), nullptr);

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
        te::Track* previous = target != nullptr ? target : getTracks().getLast();
        auto useTarget = target != nullptr && core::tracks::isAudio (*target);

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
                added.add (clip);
            else
                problems.add (error);

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

    void ArrangementModel::deleteTrack (te::AudioTrack& track)
    {
        const auto tracks = getTracks();
        const auto index = tracks.indexOf (&track);

        if (index < 0)
            return;

        const te::Track::Ptr keepAlive (&track);
        project.beginGesture ("Delete track");

        // Linked copies on other tracks stay linked only if more than one of them is left.
        clips::remove (clips::getClips (track));
        getEdit().deleteTrack (&track);

        const auto remaining = getTracks();

        if (currentTrack.get() == &track)
            setCurrentTrack (remaining.isEmpty() ? nullptr : remaining[std::min (index, remaining.size() - 1)]);

        clipsDirty = true;
        changed();
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

    void ArrangementModel::renameClip (te::Clip& clip, const juce::String& name)
    {
        const auto trimmed = name.trim();

        if (trimmed.isNotEmpty() && trimmed != clip.getName())
        {
            project.beginGesture ("Rename clip");
            clip.setName (trimmed);
        }
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
