#include "ui/pianoroll/PianoRollModel.h"
#include "core/Clips.h"

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
        return clip != nullptr ? core::clips::getStartBeat (*clip) : 0.0;
    }

    double PianoRollModel::getClipEndBeat() const
    {
        return clip != nullptr ? core::clips::getEndBeat (*clip) : 0.0;
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

                if (other == nullptr || other == clip.get())
                    continue;

                const auto contentStart = core::clips::getContentStartBeat (*other);
                const auto clipStart = core::clips::getStartBeat (*other);
                const auto clipEnd = core::clips::getEndBeat (*other);

                for (auto* n : other->getSequence().getNotes())
                {
                    // Only the part of the note inside the clip plays.
                    const auto start = std::max (clipStart, contentStart + n->getStartBeat().inBeats());
                    const auto end = std::min (clipEnd, contentStart + n->getEndBeat().inBeats());

                    if (end <= start)
                        continue;

                    ghosts.push_back ({ start, end - start, n->getNoteNumber() });
                    longestGhost = std::max (longestGhost, end - start);
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
        const auto targets = getSelectedOrAll();
        auto values = readAll (targets);
        const auto step = snap::getStepBeats (snapMode, getSnapContext());
        const auto delta = note_ops::limitMove (values, gridSteps * step, getMinBeat());

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
        if (auditioner == nullptr || pitch == auditionPitch)
            return;

        auditionStop();
        auditioner->noteOn (pitch, velocity);
        auditionPitch = pitch;
    }

    void PianoRollModel::auditionStop()
    {
        if (auditioner != nullptr && auditionPitch >= 0)
            auditioner->noteOff (auditionPitch);

        auditionPitch = -1;
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
        else
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
