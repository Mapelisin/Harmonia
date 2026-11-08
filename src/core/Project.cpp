#include "core/Project.h"

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

        // A new or freshly opened project starts with an empty undo history.
        edit->getUndoManager().clearUndoHistory();
    }

    Project::~Project() = default;

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

        return {};
    }

    int Project::getFormatVersion() const
    {
        return edit->state.getProperty (formatVersionId, 0);
    }

    juce::Array<te::AudioTrack*> Project::getTracks() const
    {
        return te::getAudioTracks (*edit);
    }

    int Project::getNumTracks() const
    {
        return getTracks().size();
    }

    te::AudioTrack* Project::addTrack (const juce::String& name)
    {
        auto track = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, getTracks().getLast()), nullptr);

        if (track != nullptr)
            track->setName (name);

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

    double Project::getTempoBpm() const
    {
        return edit->tempoSequence.getTempo (0)->getBpm();
    }

    void Project::setTempoBpm (double bpm)
    {
        edit->tempoSequence.getTempo (0)->setBpm (bpm);
    }

    void Project::beginGesture (const juce::String& name)
    {
        edit->getUndoManager().beginNewTransaction (name);
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
    }

    void Project::redo()
    {
        edit->redo();
    }
}
