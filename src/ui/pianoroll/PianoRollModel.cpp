#include "ui/pianoroll/PianoRollModel.h"
#include "core/Drums.h"
#include "core/Clips.h"
#include "core/Progressions.h"

namespace harmonia::ui::pianoroll
{
    namespace snap = core::snap;
    namespace note_ops = core::note_ops;

    namespace
    {
        constexpr double epsilon = 1.0e-9;

        /** Notes copied with Ctrl+C, relative to the start of the bar the first one is in.
            Shared by all piano rolls of the process. */
        std::vector<core::Note>& clipboard()
        {
            static std::vector<core::Note> notes;
            return notes;
        }

        bool isInsideTrackOrTempo (juce::ValueTree tree)
        {
            for (; tree.isValid(); tree = tree.getParent())
                if (tree.hasType (te::IDs::TRACK) || tree.hasType (te::IDs::TEMPOSEQUENCE))
                    return true;

            return false;
        }

        template <typename Item>
        std::pair<size_t, size_t> findInRange (const std::vector<Item>& items, double longest,
                                               double startBeat, double endBeat,
                                               double Item::* start)
        {
            const auto first = std::partition_point (items.begin(), items.end(), [&] (const Item& i)
                                                     { return i.*start < startBeat - longest - epsilon; });
            const auto last = std::partition_point (first, items.end(), [&] (const Item& i)
                                                    { return i.*start < endBeat; });
            return { (size_t) (first - items.begin()), (size_t) (last - items.begin()) };
        }
    }

    const std::vector<Tool>& getAllTools()
    {
        static const std::vector<Tool> tools { Tool::draw, Tool::paint, Tool::erase, Tool::mute, Tool::slice, Tool::select };
        return tools;
    }

    juce::String getName (Tool t)
    {
        switch (t)
        {
            case Tool::draw:   return "Draw";
            case Tool::paint:  return "Paint";
            case Tool::erase:  return "Delete";
            case Tool::mute:   return "Mute";
            case Tool::slice:  return "Slice";
            case Tool::select: return "Select";
        }

        return {};
    }

    juce::juce_wchar getShortcutKey (Tool t)
    {
        switch (t)
        {
            case Tool::draw:   return 'P';
            case Tool::paint:  return 'B';
            case Tool::erase:  return 'D';
            case Tool::mute:   return 'T';
            case Tool::slice:  return 'C';
            case Tool::select: return 'E';
        }

        return 0;
    }

    //==============================================================================
    PianoRollModel::PianoRollModel (core::Project& p)
        : project (p)
    {
        getEdit().state.addListener (this);
    }

    PianoRollModel::~PianoRollModel()
    {
        auditionStop();
        getEdit().state.removeListener (this);
    }

    void PianoRollModel::setClip (te::MidiClip* newClip)
    {
        if (newClip == clip)
            return;

        auditionStop();

        clip = newClip;
        editor = clip != nullptr ? std::make_unique<core::NoteEditor> (*clip) : nullptr;
        sequenceState = clip != nullptr ? clip->getSequence().state : juce::ValueTree();

        selection.clear();
        timeSelection.reset();
        clipRemoved = false;
        notesDirty = ghostsDirty = selectionDirty = true;
        notifyChanged();
    }

    double PianoRollModel::getContentStartBeat() const
    {
        return clip != nullptr ? core::clips::getContentStartBeat (*clip) : 0.0;
    }

    double PianoRollModel::getClipStartBeat() const
    {
        if (isLooping())
            return core::clips::getLoopSongRange (*clip).getStart();

        return clip != nullptr ? core::clips::getStartBeat (*clip) : 0.0;
    }

    double PianoRollModel::getClipEndBeat() const
    {
        if (isLooping())
            return core::clips::getLoopSongRange (*clip).getEnd();

        return clip != nullptr ? core::clips::getEndBeat (*clip) : 0.0;
    }

    bool PianoRollModel::isLooping() const
    {
        return clip != nullptr && core::clips::isLooping (*clip);
    }

    juce::Range<double> PianoRollModel::getWholeClipBeats() const
    {
        if (clip == nullptr)
            return {};

        return { core::clips::getStartBeat (*clip), core::clips::getEndBeat (*clip) };
    }

    void PianoRollModel::setLoopLength (double beats)
    {
        if (canEdit() && isLooping())
            core::clips::setLoopLengthBeats (*clip, beats);
    }

    std::vector<core::markers::Marker> PianoRollModel::getMarkers() const
    {
        return core::markers::getAll (project.getEdit());
    }

    bool PianoRollModel::areDrumNamesShown() const
    {
        auto* track = clip != nullptr ? clip->getTrack() : nullptr;
        return track != nullptr && core::drums::showsNames (*track);
    }

    void PianoRollModel::setDrumNamesShown (bool shouldShow)
    {
        auto* track = clip != nullptr ? clip->getTrack() : nullptr;

        if (track == nullptr || shouldShow == areDrumNamesShown())
            return;

        beginGesture (shouldShow ? "Show drum names" : "Hide drum names");
        core::drums::setShowsNames (*track, shouldShow);
    }

    juce::String PianoRollModel::getDrumName (int pitch) const
    {
        return areDrumNamesShown() ? core::drums::getName (pitch) : juce::String();
    }

    double PianoRollModel::getBeatsPerBar() const
    {
        return core::clips::getBeatsPerBar (project.getEdit());
    }

    //==============================================================================
    const std::vector<DrawnNote>& PianoRollModel::getNotes()
    {
        ensureNotes();
        return drawn;
    }

    std::pair<size_t, size_t> PianoRollModel::getNotesInRange (double startBeat, double endBeat)
    {
        ensureNotes();

        const auto first = std::partition_point (drawn.begin(), drawn.end(), [&] (const DrawnNote& d)
                                                 { return d.values.startBeat < startBeat - longestNote - epsilon; });
        const auto last = std::partition_point (first, drawn.end(), [&] (const DrawnNote& d)
                                                { return d.values.startBeat < endBeat; });
        return { (size_t) (first - drawn.begin()), (size_t) (last - drawn.begin()) };
    }

    const DrawnNote* PianoRollModel::findNoteAt (double beat, int pitch)
    {
        const auto [first, last] = getNotesInRange (beat, beat + epsilon);

        for (auto i = last; i > first; --i)
        {
            const auto& d = drawn[i - 1];

            if (d.values.pitch == pitch && beat >= d.values.startBeat && beat < d.values.getEndBeat())
                return &d;
        }

        return nullptr;
    }

    const std::vector<GhostNote>& PianoRollModel::getGhostNotes()
    {
        if (ghostsDirty)
            rebuildGhosts();

        return ghosts;
    }

    std::pair<size_t, size_t> PianoRollModel::getGhostNotesInRange (double startBeat, double endBeat)
    {
        getGhostNotes();
        return findInRange (ghosts, longestGhost, startBeat, endBeat, &GhostNote::startBeat);
    }

    core::Note PianoRollModel::read (const te::MidiNote& n) const
    {
        auto values = core::note::read (n);
        values.startBeat += getContentStartBeat();
        return values;
    }

    void PianoRollModel::ensureNotes()
    {
        if (notesDirty)
        {
            drawn.clear();
            longestNote = 0.0;

            if (canEdit())
            {
                const auto offset = getContentStartBeat();
                const auto& notes = editor->getNotes();
                drawn.reserve ((size_t) notes.size());

                for (auto* n : notes)
                {
                    DrawnNote d;
                    d.note = n;
                    d.values = core::note::read (*n);
                    d.values.startBeat += offset;
                    longestNote = std::max (longestNote, d.values.lengthBeats);
                    drawn.push_back (d);
                }

                std::stable_sort (drawn.begin(), drawn.end(), [] (const DrawnNote& a, const DrawnNote& b)
                                  { return a.values.startBeat < b.values.startBeat; });
            }

            std::vector<core::Note> values;
            values.reserve (drawn.size());

            for (const auto& d : drawn)
                values.push_back (d.values);

            chords = core::chords::findChords (values);

            notesDirty = false;
            selectionDirty = true;
        }

        ensureSelectionFlags();
    }

    void PianoRollModel::ensureSelectionFlags()
    {
        if (! selectionDirty)
            return;

        // Also drops entries of notes that no longer exist.
        std::unordered_map<const te::MidiNote*, juce::ValueTree> live;
        numSelected = 0;

        for (auto& d : drawn)
        {
            d.selected = isSelected (*d.note);

            if (d.selected)
            {
                ++numSelected;
                live.emplace (d.note, d.note->state);
            }
        }

        selection.swap (live);
        selectionDirty = false;
    }

    void PianoRollModel::rebuildGhosts()
    {
        ghosts.clear();
        longestGhost = 0.0;

        for (auto* track : te::getAudioTracks (getEdit()))
        {
            for (auto* c : track->getClips())
            {
                auto* other = dynamic_cast<te::MidiClip*> (c);

                if (other == nullptr)
                    continue;

                // This clip's own repeats are ghosts too: they play but are edited in the loop.
                const auto isThis = other == clip.get();

                if (isThis && ! core::clips::isLooping (*other))
                    continue;

                // Only the part of a note inside the clip plays.
                for (const auto& played : core::clips::getPlayedNotes (*other))
                {
                    if (isThis && played.repeat == 0)
                        continue;

                    const auto length = played.endBeat - played.startBeat;
                    ghosts.push_back ({ played.startBeat, length, played.note.pitch });
                    longestGhost = std::max (longestGhost, length);
                }
            }
        }

        std::sort (ghosts.begin(), ghosts.end(), [] (const GhostNote& a, const GhostNote& b)
                   { return a.startBeat < b.startBeat; });
        ghostsDirty = false;
    }

    //==============================================================================
    void PianoRollModel::setTool (Tool t)
    {
        if (tool != t)
        {
            tool = t;
            notifyChanged();
        }
    }

    void PianoRollModel::setSnap (snap::Mode m)
    {
        if (snapMode != m)
        {
            snapMode = m;
            notifyChanged();
        }
    }

    void PianoRollModel::setPixelsPerBeat (double ppb)
    {
        pixelsPerBeat = ppb;
    }

    snap::Context PianoRollModel::getSnapContext() const
    {
        snap::Context context;
        context.beatsPerBar = getBeatsPerBar();
        context.lineBeats = snap::getLineBeats (pixelsPerBeat, context.beatsPerBar);
        return context;
    }

    double PianoRollModel::getGridBeats() const
    {
        return snap::getGridBeats (snapMode, getSnapContext());
    }

    void PianoRollModel::setLaneProperty (core::NoteProperty p)
    {
        if (laneProperty != p)
        {
            laneProperty = p;
            notifyChanged();
        }
    }

    void PianoRollModel::cycleLaneProperty (int direction)
    {
        const auto& all = core::note_property::getAll();
        const auto current = (int) (std::find (all.begin(), all.end(), laneProperty) - all.begin());
        const auto count = (int) all.size();
        setLaneProperty (all[(size_t) (((current + direction) % count + count) % count)]);
    }

    const std::vector<core::chords::Span>& PianoRollModel::getChords()
    {
        ensureNotes();
        return chords;
    }

    void PianoRollModel::setChordNamesShown (bool shouldShow)
    {
        if (chordNamesShown != shouldShow)
        {
            chordNamesShown = shouldShow;
            notifyChanged();
        }
    }

    void PianoRollModel::setGhostsVisible (bool shouldBeVisible)
    {
        if (ghostsVisible != shouldBeVisible)
        {
            ghostsVisible = shouldBeVisible;
            notifyChanged();
        }
    }

    void PianoRollModel::setNewNoteLength (double beats)
    {
        newNoteLength = std::max (1.0 / snap::ticksPerBeat, beats);
    }

    //==============================================================================
    bool PianoRollModel::isSelected (const te::MidiNote& n) const
    {
        const auto found = selection.find (&n);
        return found != selection.end() && found->second == n.state;
    }

    bool PianoRollModel::hasSelection()
    {
        return getNumSelected() > 0;
    }

    int PianoRollModel::getNumSelected()
    {
        ensureNotes();
        return numSelected;
    }

    juce::Array<te::MidiNote*> PianoRollModel::getSelectedNotes()
    {
        ensureNotes();
        juce::Array<te::MidiNote*> result;

        for (const auto& d : drawn)
            if (d.selected)
                result.add (d.note);

        return result;
    }

    juce::Array<te::MidiNote*> PianoRollModel::getSelectedOrAll()
    {
        if (hasSelection())
            return getSelectedNotes();

        juce::Array<te::MidiNote*> all;

        for (const auto& d : getNotes())
            all.add (d.note);

        return all;
    }

    void PianoRollModel::setSelection (const juce::Array<te::MidiNote*>& notes)
    {
        selection.clear();
        addToSelection (notes);
    }

    void PianoRollModel::addToSelection (const juce::Array<te::MidiNote*>& notes)
    {
        for (auto* n : notes)
            if (n != nullptr)
                selection[n] = n->state;

        selectionChanged();
    }

    void PianoRollModel::setSelected (te::MidiNote& n, bool shouldBeSelected)
    {
        if (shouldBeSelected)
            selection[&n] = n.state;
        else
            selection.erase (&n);

        selectionChanged();
    }

    void PianoRollModel::selectAll()
    {
        juce::Array<te::MidiNote*> all;

        for (const auto& d : getNotes())
            all.add (d.note);

        setSelection (all);
    }

    void PianoRollModel::deselectAll()
    {
        selection.clear();
        timeSelection.reset();
        selectionChanged();
    }

    void PianoRollModel::setTimeSelection (note_ops::BeatRange range)
    {
        if (range.end < range.start)
            std::swap (range.start, range.end);

        if (range.getLength() <= epsilon)
        {
            clearTimeSelection();
            return;
        }

        timeSelection = range;

        juce::Array<te::MidiNote*> inside;

        for (const auto& d : getNotes())
            if (d.values.startBeat >= range.start - epsilon && d.values.startBeat < range.end - epsilon)
                inside.add (d.note);

        setSelection (inside);
    }

    void PianoRollModel::clearTimeSelection()
    {
        if (timeSelection.has_value())
        {
            timeSelection.reset();
            notifyChanged();
        }
    }

    void PianoRollModel::selectionChanged()
    {
        selectionDirty = true;
        notifyChanged();
    }

    //==============================================================================
    void PianoRollModel::deleteSelectedOrAll()
    {
        const auto targets = getSelectedOrAll();

        if (targets.isEmpty())
            return;

        beginGesture ("Delete notes");
        removeNotes (targets);
    }

    void PianoRollModel::transpose (int semitones)
    {
        const auto targets = getSelectedOrAll();
        auto values = readAll (targets);
        const auto scale = getScale();

        // A step up or down goes by scale degrees while snapping to the scale.
        if (std::abs (semitones) == 1 && snapToScale && ! scale.isChromatic())
        {
            auto moved = values;

            for (auto& n : moved)
                n.pitch = scale.step (n.pitch, semitones);

            applyChanges ("Transpose", targets, values, moved);
            return;
        }

        const auto delta = note_ops::limitTransposition (values, semitones);

        if (delta == 0)
            return;

        std::vector<core::NoteEditor::Change> changes;

        for (int i = 0; i < targets.size(); ++i)
        {
            values[(size_t) i].pitch += delta;
            changes.push_back ({ targets[i], values[(size_t) i] });
        }

        beginGesture ("Transpose");
        updateNotes (changes);
    }

    void PianoRollModel::nudge (int gridSteps)
    {
        moveBy (gridSteps * snap::getStepBeats (snapMode, getSnapContext()));
    }

    void PianoRollModel::nudgeFine (int direction)
    {
        constexpr double pixels = 2.0;
        const auto step = std::max (1.0 / snap::ticksPerBeat, snap::roundToTicks (pixels / pixelsPerBeat));
        moveBy (direction * step);
    }

    void PianoRollModel::moveBy (double deltaBeats)
    {
        const auto targets = getSelectedOrAll();
        auto values = readAll (targets);
        const auto delta = note_ops::limitMove (values, deltaBeats, getMinBeat());

        if (std::abs (delta) <= epsilon)
            return;

        std::vector<core::NoteEditor::Change> changes;

        for (int i = 0; i < targets.size(); ++i)
        {
            values[(size_t) i].startBeat = snap::roundToTicks (values[(size_t) i].startBeat + delta);
            changes.push_back ({ targets[i], values[(size_t) i] });
        }

        beginGesture ("Move notes");
        updateNotes (changes);

        if (timeSelection.has_value())
            timeSelection = note_ops::BeatRange { timeSelection->start + delta, timeSelection->end + delta };
    }

    void PianoRollModel::quantize (bool startsOnly)
    {
        const auto targets = getSelectedOrAll();
        const auto values = readAll (targets);
        const auto grid = snap::getStepBeats (snapMode, getSnapContext());
        std::vector<core::NoteEditor::Change> changes;

        for (int i = 0; i < targets.size(); ++i)
        {
            auto q = note_ops::quantized (values[(size_t) i], grid, startsOnly);

            while (q.startBeat < getMinBeat() - epsilon)
                q.startBeat += grid;

            if (! (q == values[(size_t) i]))
                changes.push_back ({ targets[i], q });
        }

        if (changes.empty())
            return;

        beginGesture (startsOnly ? "Quantize start times" : "Quantize");
        updateNotes (changes);
    }

    void PianoRollModel::applyChanges (const juce::String& gestureName, const juce::Array<te::MidiNote*>& targets,
                                       const std::vector<core::Note>& before, const std::vector<core::Note>& after)
    {
        std::vector<core::NoteEditor::Change> changes;

        for (int i = 0; i < targets.size(); ++i)
            if (! (after[(size_t) i] == before[(size_t) i]))
                changes.push_back ({ targets[i], after[(size_t) i] });

        if (changes.empty())
            return;

        beginGesture (gestureName);
        updateNotes (changes);
    }

    void PianoRollModel::legato()
    {
        const auto targets = getSelectedOrAll();
        const auto values = readAll (targets);
        applyChanges ("Legato", targets, values, note_ops::legato (values));
    }

    void PianoRollModel::chop()
    {
        const auto targets = getSelectedOrAll();
        const auto values = readAll (targets);
        const auto step = snap::getStepBeats (snapMode, getSnapContext());
        const auto keepSelected = hasSelection();

        std::vector<core::NoteEditor::Change> changes;
        std::vector<core::Note> pieces;

        for (int i = 0; i < targets.size(); ++i)
        {
            const auto chopped = note_ops::chop (values[(size_t) i], step);

            if (chopped.size() < 2)
                continue;

            changes.push_back ({ targets[i], chopped.front() });
            pieces.insert (pieces.end(), chopped.begin() + 1, chopped.end());
        }

        if (changes.empty())
            return;

        beginGesture ("Chop");
        updateNotes (changes);
        const auto added = addNotes (pieces);

        if (keepSelected)
            addToSelection (added);
    }

    void PianoRollModel::glue()
    {
        const auto targets = getSelectedOrAll();
        const auto values = readAll (targets);
        const auto glued = note_ops::glue (values);

        if (glued.removed.empty())
            return;

        juce::Array<te::MidiNote*> absorbed;

        for (auto i : glued.removed)
            absorbed.add (targets[(int) i]);

        std::vector<core::NoteEditor::Change> changes;

        for (int i = 0; i < targets.size(); ++i)
            if (! absorbed.contains (targets[i]) && ! (glued.notes[(size_t) i] == values[(size_t) i]))
                changes.push_back ({ targets[i], glued.notes[(size_t) i] });

        beginGesture ("Glue");
        updateNotes (changes);
        removeNotes (absorbed);
    }

    void PianoRollModel::discardLengths()
    {
        const auto targets = getSelectedOrAll();
        const auto values = readAll (targets);
        const auto step = snap::getStepBeats (snapMode, getSnapContext());
        auto shortened = values;

        for (auto& n : shortened)
            n.lengthBeats = step;

        applyChanges ("Discard lengths", targets, values, shortened);
    }

    void PianoRollModel::rotate (int gridSteps)
    {
        juce::Array<te::MidiNote*> targets;
        note_ops::BeatRange range;

        if (timeSelection.has_value())
        {
            range = *timeSelection;

            for (const auto& d : getNotes())
                if (d.values.startBeat >= range.start - epsilon && d.values.startBeat < range.end - epsilon)
                    targets.add (d.note);
        }
        else
        {
            targets = getSelectedOrAll();
            range = note_ops::getWholeBars (readAll (targets), getBeatsPerBar());
        }

        range.start = std::max (range.start, getMinBeat());

        if (targets.isEmpty() || range.getLength() <= 0.0)
            return;

        const auto values = readAll (targets);
        const auto delta = gridSteps * snap::getStepBeats (snapMode, getSnapContext());
        applyChanges ("Rotate", targets, values, note_ops::rotate (values, range, delta));
    }

    void PianoRollModel::setMuted (bool shouldBeMuted)
    {
        const auto targets = getSelectedOrAll();
        const auto values = readAll (targets);
        auto changed = values;

        for (auto& n : changed)
            n.muted = shouldBeMuted;

        applyChanges (shouldBeMuted ? "Mute notes" : "Unmute notes", targets, values, changed);
    }

    void PianoRollModel::invertSelection()
    {
        juce::Array<te::MidiNote*> others;

        for (const auto& d : getNotes())
            if (! d.selected)
                others.add (d.note);

        setSelection (others);
    }

    juce::Array<te::MidiNote*> PianoRollModel::getAll()
    {
        juce::Array<te::MidiNote*> all;

        for (const auto& d : getNotes())
            all.add (d.note);

        return all;
    }

    std::optional<note_ops::BeatRange> PianoRollModel::getSpaceRange() const
    {
        if (! timeSelection.has_value())
            return std::nullopt;

        auto range = *timeSelection;
        range.start = std::max (range.start, getMinBeat());

        if (range.getLength() <= epsilon)
            return std::nullopt;

        return range;
    }

    void PianoRollModel::insertSpace()
    {
        if (const auto range = getSpaceRange())
        {
            const auto targets = getAll();
            const auto values = readAll (targets);
            applyChanges ("Insert space", targets, values, note_ops::insertSpace (values, *range));
        }
    }

    void PianoRollModel::deleteSpace()
    {
        const auto range = getSpaceRange();

        if (! range.has_value())
            return;

        const auto targets = getAll();
        const auto values = readAll (targets);
        const auto result = note_ops::deleteSpace (values, *range);

        juce::Array<te::MidiNote*> removed;

        for (auto i : result.removed)
            removed.add (targets[(int) i]);

        std::vector<core::NoteEditor::Change> changes;

        for (int i = 0; i < targets.size(); ++i)
            if (! removed.contains (targets[i]) && ! (result.notes[(size_t) i] == values[(size_t) i]))
                changes.push_back ({ targets[i], result.notes[(size_t) i] });

        if (changes.empty() && removed.isEmpty())
            return;

        beginGesture ("Delete space");
        updateNotes (changes);
        removeNotes (removed);
    }

    void PianoRollModel::duplicate()
    {
        juce::Array<te::MidiNote*> targets;
        double offset = 0.0;

        if (timeSelection.has_value())
        {
            offset = timeSelection->getLength();

            for (const auto& d : getNotes())
                if (d.values.startBeat >= timeSelection->start - epsilon && d.values.startBeat < timeSelection->end - epsilon)
                    targets.add (d.note);
        }
        else
        {
            targets = getSelectedOrAll();
            offset = note_ops::getDuplicateOffset (readAll (targets));
        }

        if (targets.isEmpty() || offset <= 0.0)
            return;

        auto copies = readAll (targets);

        for (auto& n : copies)
            n.startBeat = snap::roundToTicks (n.startBeat + offset);

        beginGesture ("Duplicate");
        const auto added = addNotes (copies);

        if (timeSelection.has_value())
        {
            const note_ops::BeatRange moved { timeSelection->start + offset, timeSelection->end + offset };
            timeSelection = moved;
        }

        setSelection (added);
    }

    void PianoRollModel::copy()
    {
        auto values = readAll (getSelectedOrAll());

        if (values.empty())
            return;

        const auto beatsPerBar = getBeatsPerBar();
        const auto origin = std::floor (note_ops::getSpan (values).start / beatsPerBar + epsilon) * beatsPerBar;

        for (auto& n : values)
            n.startBeat -= origin;

        clipboard() = std::move (values);
    }

    void PianoRollModel::cut()
    {
        const auto targets = getSelectedOrAll();

        if (targets.isEmpty())
            return;

        copy();
        beginGesture ("Cut");
        removeNotes (targets);
    }

    bool PianoRollModel::canPaste() const
    {
        return ! clipboard().empty() && canEdit();
    }

    void PianoRollModel::paste (double atBeat)
    {
        if (! canPaste())
            return;

        auto values = clipboard();

        for (auto& n : values)
            n.startBeat = snap::roundToTicks (std::max (getMinBeat(), n.startBeat + atBeat));

        beginGesture ("Paste");
        timeSelection.reset();
        setSelection (addNotes (values));
    }

    double PianoRollModel::getPastePosition (double leftVisibleBeat) const
    {
        const auto beatsPerBar = getBeatsPerBar();
        const auto bar = std::ceil (leftVisibleBeat / beatsPerBar - epsilon) * beatsPerBar;
        return std::max (bar, getMinBeat());
    }

    void PianoRollModel::undo()
    {
        auditionStop();
        project.undo();
    }

    void PianoRollModel::redo()
    {
        auditionStop();
        project.redo();
    }

    //==============================================================================
    bool PianoRollModel::isLaneEditable (const DrawnNote& d)
    {
        return d.selected || ! hasSelection();
    }

    const DrawnNote* PianoRollModel::findLaneNoteNear (double beat, double radiusBeats)
    {
        const auto [first, last] = getNotesInRange (beat - radiusBeats, beat + radiusBeats + epsilon);
        const DrawnNote* nearest = nullptr;
        auto nearestDistance = radiusBeats + epsilon;

        for (auto i = first; i < last; ++i)
        {
            const auto& d = drawn[i];
            const auto distance = std::abs (d.values.startBeat - beat);

            if (distance < nearestDistance && isLaneEditable (d))
            {
                nearest = &d;
                nearestDistance = distance;
            }
        }

        return nearest;
    }

    void PianoRollModel::setLaneValues (double beatA, float valueA, double beatB, float valueB, double grabBeats)
    {
        const auto* grabbed = findLaneNoteNear (beatB, grabBeats);
        const auto grabbedStart = grabbed != nullptr ? grabbed->values.startBeat : 0.0;

        const auto from = std::min (beatA, beatB);
        const auto to = std::max (beatA, beatB);
        const auto lookFrom = grabbed != nullptr ? std::min (from, grabbedStart) : from;
        const auto lookTo = grabbed != nullptr ? std::max (to, grabbedStart) : to;
        const auto [first, last] = getNotesInRange (lookFrom, lookTo + epsilon);

        std::vector<core::NoteEditor::Change> changes;

        for (auto i = first; i < last; ++i)
        {
            const auto& d = drawn[i];
            const auto start = d.values.startBeat;

            if (! isLaneEditable (d))
                continue;

            float value;

            if (grabbed != nullptr && std::abs (start - grabbedStart) < epsilon)
                value = valueB;
            else if (start >= from && start <= to)
                value = to - from < epsilon ? valueB
                                            : valueA + (valueB - valueA) * (float) ((start - beatA) / (beatB - beatA));
            else
                continue;

            auto v = d.values;
            core::note_property::set (v, laneProperty, value);

            if (! (v == d.values))
                changes.push_back ({ d.note, v });
        }

        updateNotes (changes);
    }

    void PianoRollModel::nudgeProperty (te::MidiNote& target, float delta)
    {
        const auto targets = isSelected (target) ? getSelectedNotes() : juce::Array<te::MidiNote*> { &target };
        const auto now = juce::Time::getMillisecondCounter();

        if (target.state != lastNudgedNote || now - lastNudgeTime > 800)
            beginGesture ("Change " + core::note_property::getName (laneProperty).toLowerCase());

        lastNudgedNote = target.state;
        lastNudgeTime = now;

        std::vector<core::NoteEditor::Change> changes;

        for (auto* n : targets)
        {
            auto v = read (*n);
            core::note_property::set (v, laneProperty, core::note_property::get (v, laneProperty) + delta);
            changes.push_back ({ n, v });
        }

        updateNotes (changes);
    }

    //==============================================================================
    void PianoRollModel::beginGesture (const juce::String& name)
    {
        project.beginGesture (name);
    }

    juce::Array<te::MidiNote*> PianoRollModel::addNotes (const std::vector<core::Note>& songValues)
    {
        if (! canEdit() || songValues.empty())
            return {};

        std::vector<core::Note> content;
        content.reserve (songValues.size());

        for (const auto& n : songValues)
            content.push_back (toContent (n));

        auto added = editor->add (content);
        growClipToFit (songValues);
        return added;
    }

    te::MidiNote* PianoRollModel::addNote (const core::Note& songValues)
    {
        return addNotes ({ songValues }).getFirst();
    }

    void PianoRollModel::updateNotes (const std::vector<core::NoteEditor::Change>& songChanges)
    {
        if (! canEdit() || songChanges.empty())
            return;

        std::vector<core::NoteEditor::Change> content;
        std::vector<core::Note> values;
        content.reserve (songChanges.size());

        for (const auto& c : songChanges)
        {
            content.push_back ({ c.note, toContent (c.values) });
            values.push_back (c.values);
        }

        editor->update (content);
        growClipToFit (values);
    }

    void PianoRollModel::removeNotes (const juce::Array<te::MidiNote*>& notes)
    {
        if (! canEdit() || notes.isEmpty())
            return;

        for (auto* n : notes)
            selection.erase (n);

        editor->remove (notes);
        selectionChanged();
    }

    core::Note PianoRollModel::toContent (const core::Note& songValues) const
    {
        auto n = songValues;
        n.startBeat = snap::roundToTicks (std::max (0.0, n.startBeat - getContentStartBeat()));
        return n;
    }

    std::vector<core::Note> PianoRollModel::readAll (const juce::Array<te::MidiNote*>& notes) const
    {
        std::vector<core::Note> values;
        values.reserve ((size_t) notes.size());

        for (auto* n : notes)
            values.push_back (read (*n));

        return values;
    }

    void PianoRollModel::growClipToFit (const std::vector<core::Note>& songValues)
    {
        if (clip == nullptr || songValues.empty())
            return;

        auto end = 0.0;

        for (const auto& n : songValues)
            end = std::max (end, n.getEndBeat());

        core::clips::extendToCover (*clip, end - getContentStartBeat());
    }

    //==============================================================================
    void PianoRollModel::setLiveNotes (std::vector<core::Note> notes)
    {
        if (notes != liveNotes)
        {
            liveNotes = std::move (notes);
            notifyChanged();
        }
    }

    void PianoRollModel::setHeldPitches (const std::array<bool, 128>& pitches)
    {
        if (pitches != heldPitches)
        {
            heldPitches = pitches;
            notifyChanged();
        }
    }

    juce::Array<te::MidiNote*> PianoRollModel::addRecordedNotes (const std::vector<core::Note>& songValues)
    {
        if (! canEdit())
            return {};

        constexpr auto sameStart = 1.0 / core::snap::ticksPerBeat;
        std::vector<core::Note> fresh;

        for (const auto& n : songValues)
        {
            const auto [first, last] = getNotesInRange (n.startBeat - sameStart, n.startBeat + sameStart);
            auto repeated = false;

            for (auto i = first; i < last && ! repeated; ++i)
                repeated = drawn[i].values.pitch == n.pitch && std::abs (drawn[i].values.startBeat - n.startBeat) < sameStart;

            const auto inThisBatch = std::any_of (fresh.begin(), fresh.end(), [&] (const core::Note& f)
                                                  { return f.pitch == n.pitch && std::abs (f.startBeat - n.startBeat) < sameStart; });

            if (! repeated && ! inThisBatch)
                fresh.push_back (n);
        }

        return addNotes (fresh);
    }

    //==============================================================================
    void PianoRollModel::auditionStart (int pitch, int velocity)
    {
        auditionStart (std::vector<int> { pitch }, velocity);
    }

    void PianoRollModel::auditionStart (const std::vector<int>& pitches, int velocity)
    {
        if (auditioner == nullptr || pitches == auditionPitches)
            return;

        auditionStop();

        for (auto pitch : pitches)
            auditioner->noteOn (pitch, velocity);

        auditionPitches = pitches;
    }

    void PianoRollModel::auditionStop()
    {
        if (auditioner != nullptr)
            for (auto pitch : auditionPitches)
                auditioner->noteOff (pitch);

        auditionPitches.clear();
    }

    //==============================================================================
    core::scales::Scale PianoRollModel::getScale() const
    {
        return core::scales::read (project.getEdit());
    }

    void PianoRollModel::setScale (const core::scales::Scale& scale)
    {
        if (scale == getScale())
            return;

        beginGesture ("Set scale");
        core::scales::write (project.getEdit(), scale, &getEdit().getUndoManager());
        notifyChanged();
    }

    void PianoRollModel::setSnapToScale (bool shouldSnap)
    {
        if (snapToScale != shouldSnap)
        {
            snapToScale = shouldSnap;
            notifyChanged();
        }
    }

    void PianoRollModel::setChordStamp (std::optional<core::chords::ChordType> type)
    {
        if (chordStamp != type)
        {
            chordStamp = type;
            notifyChanged();
        }
    }

    int PianoRollModel::fitPitch (int pitch) const
    {
        pitch = juce::jlimit (0, 127, pitch);
        const auto scale = getScale();
        return snapToScale && ! scale.isChromatic() ? scale.snap (pitch) : pitch;
    }

    std::vector<int> PianoRollModel::getStampPitches (int pitch) const
    {
        if (! chordStamp.has_value())
            return { fitPitch (pitch) };

        const auto chord = core::chords::build (fitPitch (pitch), *chordStamp, getScale());
        return chord.empty() ? std::vector<int> { fitPitch (pitch) } : chord;
    }

    std::vector<core::Note> PianoRollModel::moveVertically (std::vector<core::Note> notes, int grabbedPitch, int rows) const
    {
        if (rows == 0 || notes.empty())
            return notes;

        const auto scale = getScale();

        if (snapToScale && ! scale.isChromatic())
        {
            // The grabbed note goes to the scale note nearest the mouse; the others move as many
            // scale degrees, so a chord stays a chord of the scale.
            const auto degrees = scale.countSteps (grabbedPitch, scale.snap (grabbedPitch + rows));

            for (auto& n : notes)
                n.pitch = scale.step (n.pitch, degrees);

            return notes;
        }

        const auto delta = note_ops::limitTransposition (notes, rows);

        for (auto& n : notes)
            n.pitch += delta;

        return notes;
    }

    bool PianoRollModel::detectScale()
    {
        const auto notes = readAll (getSelectedOrAll());

        if (notes.empty())
            return false;

        setScale (core::scales::detect (notes));
        return true;
    }

    void PianoRollModel::fitToScale()
    {
        const auto scale = getScale();

        if (scale.isChromatic())
            return;

        const auto targets = getSelectedOrAll();
        const auto values = readAll (targets);
        auto fitted = values;

        for (auto& n : fitted)
            n.pitch = scale.snap (n.pitch);

        applyChanges ("Fit to scale", targets, values, fitted);
    }

    //==============================================================================
    void PianoRollModel::notifyChanged()
    {
        triggerAsyncUpdate();
    }

    void PianoRollModel::handleAsyncUpdate()
    {
        // A clip moved to another track is taken out and put back in: only a clip that is
        // still out of the edit is gone.
        if (clipRemoved)
        {
            if (clip != nullptr && clip->state.isAChildOf (getEdit().state))
            {
                clipRemoved = false;
                notesDirty = ghostsDirty = true;
            }
            else
            {
                setClip (nullptr);
            }
        }

        listeners.call ([] (Listener& l) { l.pianoRollChanged(); });
    }

    void PianoRollModel::treeChanged (const juce::ValueTree& tree)
    {
        if (sequenceState.isValid() && (tree == sequenceState || tree.isAChildOf (sequenceState)))
        {
            notesDirty = true;
            ghostsDirty = ghostsDirty || isLooping();     // the repeats show the notes too
        }
        else if (clip != nullptr && tree == clip->state)
        {
            // The clip moved or changed length: its notes move with it.
            notesDirty = true;
            ghostsDirty = true;
        }
        else if (isInsideTrackOrTempo (tree))
        {
            ghostsDirty = true;
        }
        else if (core::markers::isMarkerTree (tree))
        {
            // The song's markers: the ruler shows them.
        }
        else if (tree != getEdit().state)     // the edit's own properties: the scale (undo too)
        {
            return;
        }

        notifyChanged();
    }

    void PianoRollModel::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
    {
        treeChanged (tree);
    }

    void PianoRollModel::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
    {
        if (child.hasType (te::IDs::TRACK))
            ghostsDirty = true;

        treeChanged (parent);
    }

    void PianoRollModel::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
    {
        if (child.hasType (te::IDs::TRACK))
        {
            ghostsDirty = true;
            notifyChanged();
        }

        // The edited clip may have been removed, e.g. by undoing the track that held it.
        if (clip != nullptr && ! clipRemoved && ! clip->state.isAChildOf (getEdit().state))
        {
            clipRemoved = true;
            notesDirty = ghostsDirty = true;
            notifyChanged();
        }

        treeChanged (parent);
    }

    void PianoRollModel::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int)
    {
        treeChanged (parent);
    }
}
