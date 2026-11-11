#include "core/Project.h"
#include "core/AudioClips.h"
#include "core/PluginState.h"
#include "core/SongFiles.h"
#include "core/Tempo.h"
#include "core/Tracks.h"

namespace harmonia::core
{
    namespace
    {
        const juce::Identifier formatVersionId ("hFormatVersion");
        const juce::Identifier editTag ("EDIT");

        /** The node in `copy` at the place `node` has in `root`. */
        juce::ValueTree findCopyOf (const juce::ValueTree& node, const juce::ValueTree& root, const juce::ValueTree& copy)
        {
            if (node == root)
                return copy;

            const auto parent = node.getParent();

            if (! parent.isValid())
                return {};

            const auto copiedParent = findCopyOf (parent, root, copy);
            return copiedParent.isValid() ? copiedParent.getChild (parent.indexOf (node)) : juce::ValueTree();
        }
    }

    //==============================================================================
    /** Keeps what the engine does a moment after a change in that change's undo step.

        The engine starts a new undo step by itself 350 ms after any change. It also finishes some
        changes later, from a message: a clip added or moved has its track's nodes put in order
        then, with undo. When the timer's moment fell between the two (a command that reads a file
        before it adds its clip leaves room for that), the tidying became a step of its own, and
        the first Ctrl+Z seemed to do nothing.

        So from any change to a track (its clips, its plugins) until the tracks have been quiet
        for a moment, and the messages posted meanwhile have run, the engine's timer is held back.
        Never for long: something that keeps changing the tracks (a line of automation being
        written while the song plays) must not keep every later change in one undo step. */
    class Project::StepKeeper final : private juce::ValueTree::Listener,
                                      private juce::Timer,
                                      private juce::AsyncUpdater
    {
    public:
        explicit StepKeeper (te::Edit& e)
            : edit (e), state (e.state)
        {
            state.addListener (this);
        }

        ~StepKeeper() override
        {
            state.removeListener (this);
            stopTimer();
            cancelPendingUpdate();
            hold.reset();
        }

        bool isHolding() const noexcept     { return hold != nullptr; }

    private:
        static constexpr int quietMs = 40;
        static constexpr int longestHoldMs = 1500;
        static constexpr int restMs = 500;          // after the longest hold: the engine's timer (350 ms) gets its turn

        te::Edit& edit;
        juce::ValueTree state;
        std::unique_ptr<te::Edit::UndoTransactionInhibitor> hold;
        juce::uint32 lastChange = 0, heldSince = 0, restUntil = 0;
        bool resting = false;
        juce::uint64 changes = 0, changesAtTimer = 0;

        /** What the engine finishes later is inside tracks; the song's other nodes (where the
            cursor is, when it was last changed...) are written all the time and need no waiting. */
        static bool isInsideTrack (juce::ValueTree node)
        {
            for (; node.isValid(); node = node.getParent())
                if (te::TrackList::isTrack (node))
                    return true;

            return false;
        }

        void changed (const juce::ValueTree& where)
        {
            if (! isInsideTrack (where))
                return;

            ++changes;
            lastChange = juce::Time::getMillisecondCounter();

            if (resting && lastChange - restUntil < 0x80000000u)       // the rest is over
                resting = false;

            if (hold == nullptr && ! resting)
            {
                hold = std::make_unique<te::Edit::UndoTransactionInhibitor> (edit);
                heldSince = lastChange;
                startTimer (quietMs);
            }
        }

        void timerCallback() override
        {
            const auto now = juce::Time::getMillisecondCounter();

            if (now - heldSince > (juce::uint32) longestHoldMs)
            {
                stopTimer();
                cancelPendingUpdate();
                hold.reset();
                resting = true;
                restUntil = now + (juce::uint32) restMs;
                return;
            }

            if (now - lastChange < (juce::uint32) quietMs)
                return;

            // Quiet by the clock; but the window may have been busy all that time, with the
            // engine's messages still waiting: a message posted now runs after them.
            stopTimer();
            changesAtTimer = changes;
            triggerAsyncUpdate();
        }

        void handleAsyncUpdate() override
        {
            if (hold == nullptr)
                return;

            if (changes == changesAtTimer)
                hold.reset();
            else
                startTimer (quietMs);       // those messages changed the tracks: wait again
        }

        void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&) override     { changed (tree); }
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child) override                { changed (child); }
        void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int) override  { changed (te::TrackList::isTrack (child) ? child : parent); }
        void valueTreeChildOrderChanged (juce::ValueTree& parent, int, int) override                { changed (parent); }

        JUCE_DECLARE_NON_COPYABLE (StepKeeper)
    };

    //==============================================================================
    Project::Project (std::unique_ptr<te::Edit> e)
        : edit (std::move (e))
    {
        jassert (edit != nullptr);

        // Tracktion creates the SCENES node lazily, from inside the listener callback of the
        // first track insertion. That nested undoable action is recorded before the track
        // insertion, so undoing it removes the wrong child index, and redo then adds a second
        // SCENES node. Creating it up front keeps track insertion cleanly undoable.
        edit->getSceneList();

        // Clips saved before Harmonia stretched sound, or set up by the engine on its own.
        for (auto* track : te::getAudioTracks (*edit))
            for (auto* clip : track->getClips())
                if (auto* audioClip = dynamic_cast<te::AudioClipBase*> (clip))
                    audio_clips::prepareForPlayback (*audioClip);

        edit->getUndoManager().addChangeListener (this);
        stepKeeper = std::make_unique<StepKeeper> (*edit);

        // A new or freshly opened project starts with an empty undo history.
        clearUndoHistory();
    }

    Project::~Project()
    {
        stepKeeper.reset();
        edit->getUndoManager().removeChangeListener (this);
    }

    bool Project::isSettling() const
    {
        return stepKeeper->isHolding();
    }

    void Project::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        changedSinceSave = true;
        ++changeCount;
    }

    void Project::markAsSaved()
    {
        // Change messages arrive asynchronously: take the pending ones before forgetting them.
        edit->getUndoManager().dispatchPendingMessages();
        changedSinceSave = false;
        edit->resetChangedStatus();
    }

    std::unique_ptr<Project> Project::createEmpty (Session& session)
    {
        auto edit = te::createEmptyEdit (session.getEngine(), juce::File());
        edit->state.setProperty (formatVersionId, currentFormatVersion, nullptr);
        return std::unique_ptr<Project> (new Project (std::move (edit)));
    }

    std::unique_ptr<Project> Project::load (Session& session, const juce::File& file, juce::String& error)
    {
        return open (session, file, file, error);
    }

    std::unique_ptr<Project> Project::open (Session& session, const juce::File& file, const juce::File& asFile, juce::String& error)
    {
        if (! file.existsAsFile())
        {
            error = "Project file not found: " + file.getFullPathName();
            return {};
        }

        const auto xml = juce::parseXML (file);

        if (xml == nullptr || ! xml->hasTagName (editTag.toString()))
        {
            error = "Not a Harmonia project: " + file.getFullPathName();
            return {};
        }

        const auto version = xml->getIntAttribute (formatVersionId, 0);

        if (version > currentFormatVersion)
        {
            error = "This project was saved by a newer version of Harmonia (format "
                  + juce::String (version) + ")";
            return {};
        }

        // As te::loadEditFromFile does, but the project's file is `asFile` from the start: its
        // clips look for their sounds while they are being made.
        std::unique_ptr<te::Edit> edit;

        if (const auto state = te::loadEditFromFile (session.getEngine(), file, te::ProjectItemID()); state.isValid())
        {
            edit = te::Edit::createEdit ({ session.getEngine(), state, te::ProjectItemID::createNewID (te::ProjectID()),
                                           te::Edit::forEditing, nullptr, te::Edit::getDefaultNumUndoLevels(),
                                           [asFile] { return asFile; }, {} });
        }

        if (edit == nullptr)
        {
            error = "Could not open project: " + file.getFullPathName();
            return {};
        }

        edit->state.setProperty (formatVersionId, currentFormatVersion, nullptr);
        return std::unique_ptr<Project> (new Project (std::move (edit)));
    }

    std::unique_ptr<Project> Project::loadRecovered (Session& session, const juce::File& copy, const juce::File& original,
                                                     juce::String& error)
    {
        // A copy for restoring names its sounds by full path; an earlier version of the project
        // file, by paths from the project file: both are found with the project at `original`.
        auto project = open (session, copy, original, error);

        if (project != nullptr)
            project->changedSinceSave = true;

        return project;
    }

    std::unique_ptr<Project> Project::loadTemplate (Session& session, const juce::File& templateFile, juce::String& error)
    {
        // A song of its own from here on: no file yet (what it records goes where an unsaved
        // song's recordings go), and Ctrl+S asks where to save it.
        return open (session, templateFile, {}, error);
    }

    void Project::makeSoundPathsAbsolute()
    {
        // Tracktion stores a sound relative to the project file when it's near it, found from where
        // the project file is now. Before the project moves to another file, give each its full path.
        song_files::forEachReference (edit->state, [this] (juce::ValueTree& node, const juce::Identifier& property)
        {
            const auto path = node[property].toString();

            if (juce::File::isAbsolutePath (path))
                return;

            if (const auto sound = song_files::resolve (*edit, path); sound != juce::File())
                node.setProperty (property, sound.getFullPathName(), nullptr);
        });
    }

    juce::String Project::write (const juce::File& file, const juce::File& relativeTo)
    {
        edit->state.setProperty (formatVersionId, currentFormatVersion, nullptr);

        if (! file.getParentDirectory().createDirectory())
            return "Cannot create folder: " + file.getParentDirectory().getFullPathName();

        // The open project stays as it is: what's written is a copy with the plugins' settings
        // and the sounds' paths for where it goes.
        auto state = edit->state.createCopy();
        state.setProperty (te::IDs::appVersion, edit->engine.getPropertyStorage().getApplicationVersion(), nullptr);
        state.setProperty (te::IDs::projectID, edit->getProjectItemRef().toString(), nullptr);

        for (auto* plugin : te::getAllPlugins (*edit, true))
            if (auto node = findCopyOf (plugin->state, edit->state, state); node.isValid())
                plugin_state::write (*plugin, node);

        const auto folder = relativeTo != juce::File() ? relativeTo.getParentDirectory() : juce::File();

        song_files::forEachReference (state, [this, &folder] (juce::ValueTree& node, const juce::Identifier& property)
        {
            const auto sound = song_files::resolve (*edit, node[property].toString());

            if (sound == juce::File())
                return;

            const auto inFolder = folder != juce::File() && sound.isAChildOf (folder);
            node.setProperty (property, inFolder ? sound.getRelativePathFrom (folder).replaceCharacter ('\\', '/')
                                                 : sound.getFullPathName(), nullptr);
        });

        const auto xml = state.createXml();
        juce::TemporaryFile temp (file);

        if (xml == nullptr || ! xml->writeTo (temp.getFile()) || ! temp.overwriteTargetFileWithTemporary())
            return "Cannot write project file: " + file.getFullPathName();

        return {};
    }

    juce::String Project::save (const juce::File& file)
    {
        makeSoundPathsAbsolute();

        if (const auto error = write (file, file); error.isNotEmpty())
            return error;

        // Relative paths and recordings from now on go from here (see Session::getFileForNewRecording).
        edit->editFileRetriever = [file] { return file; };
        markAsSaved();
        return {};
    }

    juce::String Project::saveCopy (const juce::File& file)
    {
        return write (file, {});
    }

    void Project::takeInChanges() const
    {
        edit->getUndoManager().dispatchPendingMessages();

        if (edit->hasChangedSinceSaved())
        {
            edit->resetChangedStatus();
            changedSinceSave = true;
            ++changeCount;
        }
    }

    bool Project::hasUnsavedChanges() const
    {
        takeInChanges();
        return changedSinceSave;
    }

    juce::uint64 Project::getChangeCount() const
    {
        takeInChanges();
        return changeCount;
    }

    int Project::getFormatVersion() const
    {
        return edit->state.getProperty (formatVersionId, 0);
    }

    juce::Array<te::AudioTrack*> Project::getTracks() const
    {
        auto result = te::getAudioTracks (*edit);
        result.removeIf ([] (te::AudioTrack* track) { return tracks::isBus (*track); });
        return result;
    }

    juce::Array<te::AudioTrack*> Project::getReturnTracks() const
    {
        auto result = te::getAudioTracks (*edit);
        result.removeIf ([] (te::AudioTrack* track) { return ! tracks::isReturn (*track); });
        return result;
    }

    juce::Array<te::AudioTrack*> Project::getGroupTracks() const
    {
        auto result = te::getAudioTracks (*edit);
        result.removeIf ([] (te::AudioTrack* track) { return ! tracks::isGroup (*track); });
        return result;
    }

    int Project::getNumTracks() const
    {
        return getTracks().size();
    }

    te::AudioTrack* Project::addTrack (const juce::String& name, te::Track* after)
    {
        te::Track* preceding = after != nullptr ? after : getTracks().getLast();
        auto track = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, preceding), nullptr);

        if (track != nullptr)
            track->setName (name);

        return track.get();
    }

    te::AudioTrack* Project::addReturnTrack (const juce::String& name)
    {
        return addBusTrack (name, tracks::Kind::fxReturn);
    }

    te::AudioTrack* Project::addGroupTrack (const juce::String& name)
    {
        return addBusTrack (name, tracks::Kind::group);
    }

    te::AudioTrack* Project::addBusTrack (const juce::String& name, tracks::Kind kind)
    {
        auto track = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, te::getAudioTracks (*edit).getLast()), nullptr);

        if (track == nullptr)
            return nullptr;

        track->setName (name);
        tracks::setKind (*track, kind);
        return track.get();
    }

    te::MidiClip* Project::addMidiClip (te::AudioTrack& track, double startBeat, double lengthBeats,
                                        const juce::String& name)
    {
        jassert (lengthBeats > 0.0);

        const auto start = te::BeatPosition::fromBeats (startBeat);
        const auto end = start + te::BeatDuration::fromBeats (lengthBeats);
        const te::TimeRange range (edit->tempoSequence.toTime (start), edit->tempoSequence.toTime (end));

        return track.insertMIDIClip (name, range, nullptr).get();
    }

    bool Project::canImport (const juce::File& file) const
    {
        auto& formats = edit->engine.getAudioFileFormatManager().readFormatManager;
        return formats.findFormatForFileExtension (file.getFileExtension()) != nullptr;
    }

    te::WaveAudioClip* Project::addAudioClip (te::AudioTrack& track, const juce::File& file, double startBeat,
                                              juce::String& error)
    {
        error = {};

        if (! file.existsAsFile())
        {
            error = "File not found: " + file.getFullPathName();
            return nullptr;
        }

        const te::AudioFile audioFile (edit->engine, file);

        if (! canImport (file) || ! audioFile.isValid() || audioFile.getLength() <= 0.0)
        {
            error = "Can't read " + file.getFileName() + " as audio";
            return nullptr;
        }

        const auto start = edit->tempoSequence.toTime (te::BeatPosition::fromBeats (std::max (0.0, startBeat)));
        const te::ClipPosition position { { start, start + te::TimeDuration::fromSeconds (audioFile.getLength()) }, {} };
        auto clip = track.insertWaveClip (file.getFileNameWithoutExtension(), file, position, false);

        if (clip == nullptr)
        {
            error = file.getFileName() + " could not be added";
            return nullptr;
        }

        // The file at its own speed: whether a loop follows the song is the caller's to say (the
        // engine makes a file whose name or data gives its tempo follow by itself).
        if (clip->getAutoTempo())
        {
            clip->setAutoTempo (false);
            clip->setPosition (position);
        }

        audio_clips::prepareForPlayback (*clip);
        return clip.get();
    }

    double Project::getTempoBpm() const
    {
        return edit->tempoSequence.getTempo (0)->getBpm();
    }

    void Project::setTempoBpm (double bpm)
    {
        edit->tempoSequence.getTempo (0)->setBpm (tempo::clampBpm (bpm));
    }

    int Project::getBeatsPerBar() const
    {
        return edit->tempoSequence.getTimeSig (0)->numerator;
    }

    void Project::setBeatsPerBar (int beats)
    {
        auto& timeSig = *edit->tempoSequence.getTimeSig (0);
        timeSig.numerator = juce::jlimit (1, 16, beats);
        timeSig.denominator = 4;
    }

    void Project::beginGesture (const juce::String& name)
    {
        edit->getUndoManager().beginNewTransaction (name);
    }

    void Project::clearUndoHistory()
    {
        edit->getUndoManager().clearUndoHistory();
        markAsSaved();
    }

    bool Project::canUndo() const
    {
        return edit->getUndoManager().canUndo();
    }

    bool Project::canRedo() const
    {
        return edit->getUndoManager().canRedo();
    }

    juce::String Project::getUndoDescription() const
    {
        return canUndo() ? edit->getUndoManager().getUndoDescription() : juce::String();
    }

    juce::String Project::getRedoDescription() const
    {
        return canRedo() ? edit->getUndoManager().getRedoDescription() : juce::String();
    }

    void Project::undo()
    {
        edit->undo();
        syncPluginParameters();
    }

    void Project::redo()
    {
        edit->redo();
        syncPluginParameters();
    }

    void Project::syncPluginParameters()
    {
        // Undo puts plugin settings (volume, pan, effect knobs) back into the edit's state, but
        // Tracktion doesn't hand them to the running parameters: the sound would stay as it was.
        auto& undoManager = edit->getUndoManager();

        for (auto* plugin : te::getAllPlugins (*edit, true))
            for (auto* parameter : plugin->getAutomatableParameters())
                parameter->updateFromAttachedValue();

        // A setting that undo took back to its default (the property removed) is written again by
        // Tracktion as a new undo step, which would also drop the redo steps. Take that step back out:
        // the value stays the same, the redo steps come back.
        if (undoManager.getNumActionsInCurrentTransaction() > 0)
            undoManager.undoCurrentTransactionOnly();
    }
}
