#include "core/Project.h"
#include "core/Tempo.h"
#include "core/Tracks.h"

namespace harmonia::core
{
    namespace
    {
        const juce::Identifier formatVersionId ("hFormatVersion");
        const juce::Identifier editTag ("EDIT");
    }

    Project::Project (std::unique_ptr<te::Edit> e)
        : edit (std::move (e))
    {
        jassert (edit != nullptr);

        // Tracktion creates the SCENES node lazily, from inside the listener callback of the
        // first track insertion. That nested undoable action is recorded before the track
        // insertion, so undoing it removes the wrong child index, and redo then adds a second
        // SCENES node. Creating it up front keeps track insertion cleanly undoable.
        edit->getSceneList();

        edit->getUndoManager().addChangeListener (this);

        // A new or freshly opened project starts with an empty undo history.
        clearUndoHistory();
    }

    Project::~Project()
    {
        edit->getUndoManager().removeChangeListener (this);
    }

    void Project::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        changedSinceSave = true;
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

        auto edit = te::loadEditFromFile (session.getEngine(), file);

        if (edit == nullptr)
        {
            error = "Could not open project: " + file.getFullPathName();
            return {};
        }

        edit->state.setProperty (formatVersionId, currentFormatVersion, nullptr);
        return std::unique_ptr<Project> (new Project (std::move (edit)));
    }

    juce::String Project::save (const juce::File& file)
    {
        edit->state.setProperty (formatVersionId, currentFormatVersion, nullptr);

        if (! file.getParentDirectory().createDirectory())
            return "Cannot create folder: " + file.getParentDirectory().getFullPathName();

        if (! te::EditFileOperations (*edit).writeToFile (file, false))
            return "Cannot write project file: " + file.getFullPathName();

        // Recordings from now on go next to the project (see Session::getFileForNewRecording).
        edit->editFileRetriever = [file] { return file; };
        markAsSaved();
        return {};
    }

    bool Project::hasUnsavedChanges() const
    {
        edit->getUndoManager().dispatchPendingMessages();
        return changedSinceSave;
    }

    int Project::getFormatVersion() const
    {
        return edit->state.getProperty (formatVersionId, 0);
    }

    juce::Array<te::AudioTrack*> Project::getTracks() const
    {
        auto result = te::getAudioTracks (*edit);
        result.removeIf ([] (te::AudioTrack* track) { return tracks::isReturn (*track); });
        return result;
    }

    juce::Array<te::AudioTrack*> Project::getReturnTracks() const
    {
        auto result = te::getAudioTracks (*edit);
        result.removeIf ([] (te::AudioTrack* track) { return ! tracks::isReturn (*track); });
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
        auto track = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, te::getAudioTracks (*edit).getLast()), nullptr);

        if (track == nullptr)
            return nullptr;

        track->setName (name);
        tracks::setKind (*track, tracks::Kind::fxReturn);
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
            error = file.getFileName() + " could not be added";

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
