#include <doctest.h>

#include "TestAudioFiles.h"
#include "audio/Consolidate.h"
#include "audio/Export.h"
#include "audio/Instruments.h"
#include "core/ClipLinks.h"
#include "core/Clips.h"
#include "core/NoteEditor.h"
#include "core/Project.h"
#include "core/Tracks.h"

using namespace harmonia;
namespace clips = core::clips;
namespace consolidate = audio::consolidate;

namespace
{
    void dispatch (int ms = 50)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    te::MidiClip* addNotes (core::Project& project, te::AudioTrack& track, double startBeat, double lengthBeats,
                            const juce::String& name, std::initializer_list<std::pair<double, int>> notes)
    {
        auto* clip = project.addMidiClip (track, startBeat, lengthBeats, name);
        REQUIRE (clip != nullptr);
        std::vector<core::Note> made;

        for (const auto& [beat, pitch] : notes)
        {
            core::Note n;
            n.pitch = pitch;
            n.startBeat = beat;
            n.lengthBeats = 0.5;
            made.push_back (n);
        }

        core::NoteEditor (*clip).add (made);
        return clip;
    }

    /** "2:62 3:63": where the clip plays which pitch, in song beats. */
    juce::String played (const te::MidiClip& clip)
    {
        auto notes = clips::getPlayedNotes (clip);
        std::sort (notes.begin(), notes.end(), [] (const auto& a, const auto& b) { return a.startBeat < b.startBeat; });
        juce::StringArray result;

        for (const auto& n : notes)
            result.add (juce::String (n.startBeat, 2).upToLastOccurrenceOf (".00", false, false) + ":" + juce::String (n.note.pitch));

        return result.joinIntoString (" ");
    }

    /** "0-2, 2-12": where the track's clips are, in beats. */
    juce::String spans (te::AudioTrack& track)
    {
        juce::StringArray result;

        for (auto* clip : clips::getClips (track))
            result.add (juce::String (clips::getStartBeat (*clip), 2).upToLastOccurrenceOf (".00", false, false) + "-"
                          + juce::String (clips::getEndBeat (*clip), 2).upToLastOccurrenceOf (".00", false, false));

        return result.joinIntoString (", ");
    }

    /** How loud the exported song is, quarter of a second by quarter (the first channel's RMS). */
    std::vector<float> exportedLevels (core::Project& project, const juce::File& file)
    {
        REQUIRE (audio::exportWav (project.getEdit(), file).isEmpty());
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        REQUIRE (reader != nullptr);
        juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);

        std::vector<float> levels;
        const auto window = (int) (reader->sampleRate / 4.0);

        for (int start = 0; start + window <= buffer.getNumSamples(); start += window)
            levels.push_back (buffer.getRMSLevel (0, start, window));

        return levels;
    }
}

TEST_SUITE ("audio: one clip of a stretch of time")
{
    TEST_CASE ("notes: the clips of the stretch become one clip with all they play, repeats written out, note properties kept")
    {
        auto project = core::Project::createEmpty (test::session());
        auto* track = project->getTracks().getFirst();
        REQUIRE (track != nullptr);

        // A melody over beats 0..4; a two-beat figure repeating over 4..8; a late note with its own pan.
        auto* melody = addNotes (*project, *track, 0.0, 4.0, "Melody", { { 0.0, 60 }, { 1.0, 61 }, { 2.0, 62 }, { 3.0, 63 } });
        auto* figure = addNotes (*project, *track, 4.0, 2.0, "Figure", { { 0.0, 64 }, { 1.0, 65 } });
        clips::setLooping (*figure, true);
        clips::setEnd (*figure, 8.0, 0.25);
        auto* late = addNotes (*project, *track, 10.0, 2.0, "Late", {});
        {
            core::Note n;
            n.pitch = 70;
            n.startBeat = 0.5;
            n.lengthBeats = 1.0;
            n.pan = -0.5f;
            core::NoteEditor (*late).add (n);
        }
        clips::setColour (*melody, juce::Colours::orange);
        dispatch();
        project->clearUndoHistory();
        CHECK (played (*figure) == "4:64 5:65 6:64 7:65");
        CHECK (consolidate::countClips (*track, 2.0, 12.0) == 3);

        // From beat 2 to beat 12.
        juce::String problem;
        const auto piece = consolidate::prepare (*track, 2.0, 12.0, problem);
        REQUIRE (piece.has_value());
        CHECK (problem.isEmpty());
        CHECK (spans (*track) == "0-4, 4-8, 10-12");         // nothing changed yet

        project->beginGesture ("Consolidate");
        auto* joined = dynamic_cast<te::MidiClip*> (consolidate::apply (*project, *piece));
        REQUIRE (joined != nullptr);
        dispatch();

        // What the melody had before beat 2 stays a clip of its own; the rest is one clip.
        CHECK (spans (*track) == "0-2, 2-12");
        CHECK (played (*joined) == "2:62 3:63 4:64 5:65 6:64 7:65 10.50:70");
        CHECK_FALSE (clips::isLooping (*joined));
        CHECK_FALSE (core::clip_links::isLinked (*joined));
        CHECK (joined->getName() == "Melody");
        CHECK (clips::getColour (*joined) == std::optional<juce::Colour> (juce::Colours::orange));

        const auto notes = clips::getPlayedNotes (*joined);
        const auto panned = std::find_if (notes.begin(), notes.end(), [] (const auto& n) { return n.note.pitch == 70; });
        REQUIRE (panned != notes.end());
        CHECK (panned->note.pan == doctest::Approx (-0.5f));
        CHECK (panned->note.lengthBeats == doctest::Approx (1.0));

        // One Ctrl+Z, and back again.
        CHECK (project->getUndoDescription() == "Consolidate");
        project->undo();
        dispatch();
        CHECK (spans (*track) == "0-4, 4-8, 10-12");
        project->redo();
        dispatch();
        CHECK (spans (*track) == "0-2, 2-12");

        // Where the track has nothing, there is nothing to make a clip of.
        CHECK (consolidate::countClips (*track, 20.0, 24.0) == 0);
        CHECK_FALSE (consolidate::prepare (*track, 20.0, 24.0, problem).has_value());
        CHECK (problem.isEmpty());
    }

    TEST_CASE ("sound: the clips of the stretch become one clip of a new file that sounds as they did; the track's effects aren't in it")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        project->setTempoBpm (120.0);
        auto* track = project->addTrack ("Guitar");
        core::tracks::setKind (*track, core::tracks::Kind::audio);

        // Two bars: a loud tone made quieter by its clip, then a soft one.
        juce::String error;
        auto* loud = project->addAudioClip (*track, test::writeTone (temp.getFile ("Loud.wav"), 2.0, 0.5f), 0.0, error);
        auto* soft = project->addAudioClip (*track, test::writeTone (temp.getFile ("Soft.wav"), 2.0, 0.2f), 4.0, error);
        REQUIRE (loud != nullptr);
        REQUIRE (soft != nullptr);
        loud->setName ("Riff");
        clips::setGainDb (*loud, -6.0f);

        // The track's fader down: were it in the new file too, the stretch would be quieter twice.
        track->getVolumePlugin()->setVolumeDb (-12.0f);
        dispatch();
        project->clearUndoHistory();
        const auto before = exportedLevels (*project, temp.getFile ("Before.wav"));

        // From beat 2 to beat 6: the end of one clip and the start of the other.
        CHECK (consolidate::countClips (*track, 2.0, 6.0) == 2);
        juce::String problem;
        const auto piece = consolidate::prepare (*track, 2.0, 6.0, problem);
        REQUIRE_MESSAGE (piece.has_value(), problem.toStdString());
        CHECK (piece->sound.existsAsFile());
        CHECK_FALSE (piece->followsTempo);

        project->beginGesture ("Consolidate");
        auto* joined = dynamic_cast<te::WaveAudioClip*> (consolidate::apply (*project, *piece));
        REQUIRE (joined != nullptr);
        dispatch();
        CHECK (spans (*track) == "0-2, 2-6, 6-8");
        CHECK (joined->getOriginalFile() == piece->sound);
        CHECK (joined->getName() == "Riff");
        CHECK (clips::getGainDb (*joined) == doctest::Approx (0.0f));      // its gain is in the file now

        // The song sounds as it did.
        const auto after = exportedLevels (*project, temp.getFile ("After.wav"));
        REQUIRE (after.size() == before.size());
        REQUIRE (before.size() >= 16);

        for (size_t i = 0; i < 16; ++i)
        {
            CAPTURE (i);
            CHECK (after[i] == doctest::Approx (before[i]).epsilon (0.03));
        }

        CHECK (before[2] > 1.2f * before[10]);       // (the first bar and the softer second are told apart)

        // One Ctrl+Z: the two clips as they were.
        project->undo();
        dispatch();
        CHECK (spans (*track) == "0-4, 4-8");
        piece->sound.deleteFile();
    }
}
