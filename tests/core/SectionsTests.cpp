#include <doctest.h>

#include "TestSession.h"
#include "core/AutomationPieces.h"
#include "core/ClipLinks.h"
#include "core/Clips.h"
#include "core/Markers.h"
#include "core/NoteEditor.h"
#include "core/Project.h"
#include "core/Sections.h"

using namespace harmonia;
using namespace harmonia::core;

namespace
{
    /** Keys: "Verse" in bars 1-2, "Chorus" in bars 3-4; bass: one "Line" across all four.
        Markers at the verse and the chorus. */
    struct Song
    {
        Song()
        {
            keys = project->getTracks().getFirst();
            keys->setName ("Keys");
            bass = project->addTrack ("Bass");
            REQUIRE (bass != nullptr);

            for (auto [track, start, length, name] : { std::tuple { keys, 0.0, 8.0, "Verse" }, std::tuple { keys, 8.0, 8.0, "Chorus" },
                                                       std::tuple { bass, 0.0, 16.0, "Line" } })
            {
                auto* clip = project->addMidiClip (*track, start, length, name);
                REQUIRE (clip != nullptr);
                NoteEditor (*clip).add (Note {});
            }

            markers::add (edit(), 0.0, "Verse");
            markers::add (edit(), 8.0, "Chorus");
            project->clearUndoHistory();
        }

        te::Edit& edit()    { return project->getEdit(); }

        static juce::String beats (double beat)    { return juce::String (std::round (beat * 1000.0) / 1000.0); }

        /** "name start-end" for each clip of the track, in time order. */
        static juce::StringArray layout (te::AudioTrack& track)
        {
            juce::StringArray result;

            for (auto* clip : clips::getClips (track))
                result.add (clip->getName() + " " + beats (clips::getStartBeat (*clip)) + "-" + beats (clips::getEndBeat (*clip)));

            return result;
        }

        juce::StringArray markerList()
        {
            juce::StringArray result;

            for (const auto& m : markers::getAll (edit()))
                result.add (m.name + "@" + beats (m.beat));

            return result;
        }

        void checkUntouched()
        {
            CHECK (layout (*keys) == juce::StringArray { "Verse 0-8", "Chorus 8-16" });
            CHECK (layout (*bass) == juce::StringArray { "Line 0-16" });
            CHECK (markerList() == juce::StringArray { "Verse@0", "Chorus@8" });
        }

        std::unique_ptr<Project> project = Project::createEmpty (test::session());
        te::AudioTrack* keys = nullptr;
        te::AudioTrack* bass = nullptr;
    };

    /** The curve's value every half beat from 0 to `endBeat`, a quarter beat off: never right at
        an edge, where a curve may jump (and Tracktion gives the value before the jump). */
    std::vector<float> sample (te::Edit& edit, te::AutomatableParameter& parameter, double endBeat)
    {
        std::vector<float> values;

        for (double beat = 0.25; beat < endBeat; beat += 0.5)
            values.push_back (parameter.getCurve().getValueAt (edit.tempoSequence.toTime (te::BeatPosition::fromBeats (beat)), 0.0f));

        return values;
    }

    size_t sampleAt (double beat)    { return (size_t) juce::roundToInt ((beat - 0.25) * 2.0); }

    /** `after` from `toBeat` on is `before` from `fromBeat` on, for `lengthBeats`. */
    void checkSame (const std::vector<float>& before, double fromBeat, const std::vector<float>& after, double toBeat,
                    double lengthBeats)
    {
        for (double b = 0.25; b < lengthBeats; b += 0.5)
        {
            const auto i = sampleAt (fromBeat + b), j = sampleAt (toBeat + b);
            REQUIRE (i < before.size());
            REQUIRE (j < after.size());
            CAPTURE (b);
            CHECK (after[j] == doctest::Approx (before[i]).epsilon (1.0e-4));
        }
    }
}

TEST_SUITE ("core::sections")
{
    TEST_CASE ("inserting bars moves everything after them later, splitting what crosses the point")
    {
        Song song;
        song.project->beginGesture ("Insert");
        sections::insertSpace (song.edit(), 8.0, 4.0);
        CHECK (Song::layout (*song.keys) == juce::StringArray { "Verse 0-8", "Chorus 12-20" });
        CHECK (Song::layout (*song.bass) == juce::StringArray { "Line 0-8", "Line 12-20" });
        CHECK (song.markerList() == juce::StringArray { "Verse@0", "Chorus@12" });

        song.project->undo();
        song.checkUntouched();

        // Nothing to insert: nothing changes.
        sections::insertSpace (song.edit(), 8.0, 0.0);
        song.checkUntouched();
    }

    TEST_CASE ("deleting bars takes them out of every track and closes the gap")
    {
        Song song;
        markers::add (song.edit(), 6.0, "Fill");
        song.project->clearUndoHistory();

        song.project->beginGesture ("Delete");
        sections::deleteRange (song.edit(), 4.0, 8.0);
        CHECK (Song::layout (*song.keys) == juce::StringArray { "Verse 0-4", "Chorus 4-12" });
        CHECK (Song::layout (*song.bass) == juce::StringArray { "Line 0-4", "Line 4-12" });
        CHECK (song.markerList() == juce::StringArray { "Verse@0", "Chorus@4" });

        song.project->undo();
        CHECK (song.markerList() == juce::StringArray { "Verse@0", "Fill@6", "Chorus@8" });
        CHECK (Song::layout (*song.keys) == juce::StringArray { "Verse 0-8", "Chorus 8-16" });
    }

    TEST_CASE ("duplicating bars plays them twice: linked copies right after, the rest later, one undo step")
    {
        Song song;
        song.project->beginGesture ("Duplicate");
        const auto copies = sections::duplicateRange (song.edit(), 0.0, 8.0);
        CHECK (copies.size() == 2);
        CHECK (Song::layout (*song.keys) == juce::StringArray { "Verse 0-8", "Verse 8-16", "Chorus 16-24" });
        CHECK (Song::layout (*song.bass) == juce::StringArray { "Line 0-8", "Line 8-16", "Line 16-24" });
        CHECK (song.markerList() == juce::StringArray { "Verse@0", "Verse@8", "Chorus@16" });

        // The copies are linked to what they copy (as Ctrl+B).
        auto* verseCopy = dynamic_cast<te::MidiClip*> (clips::getClips (*song.keys)[1]);
        REQUIRE (verseCopy != nullptr);
        CHECK (clip_links::isLinked (*verseCopy));
        CHECK (clip_links::getGroup (*verseCopy).contains (dynamic_cast<te::MidiClip*> (clips::getClips (*song.keys)[0])));

        song.project->undo();
        song.checkUntouched();

        // The last part: nothing after it to move.
        song.project->beginGesture ("Duplicate");
        sections::duplicateRange (song.edit(), 8.0, 16.0);
        CHECK (Song::layout (*song.keys) == juce::StringArray { "Verse 0-8", "Chorus 8-16", "Chorus 16-24" });
        CHECK (Song::layout (*song.bass) == juce::StringArray { "Line 0-8", "Line 8-16", "Line 16-24" });
        CHECK (song.markerList() == juce::StringArray { "Verse@0", "Chorus@8", "Chorus@16" });
    }

    TEST_CASE ("automation moves with the bars, and is copied with them")
    {
        Song song;
        auto& volume = *song.keys->getVolumePlugin()->volParam;
        auto& edit = song.edit();
        auto& curve = volume.getCurve();
        const auto at = [&edit] (double beat) { return edit.tempoSequence.toTime (te::BeatPosition::fromBeats (beat)); };

        // Flat, a ramp from beat 2 to beat 10 across the chorus's start, a jump at 12, flat.
        curve.addPoint (at (2.0), 0.3f, 0.0f, &edit.getUndoManager());
        curve.addPoint (at (10.0), 0.7f, 0.0f, &edit.getUndoManager());
        curve.addPoint (at (12.0), 0.7f, 0.0f, &edit.getUndoManager());
        curve.addPoint (at (12.0), 0.2f, 0.0f, &edit.getUndoManager());
        song.project->clearUndoHistory();
        const auto original = sample (edit, volume, 40.0);
        const auto points = curve.getNumPoints();

        SUBCASE ("insert: the new bars hold the value there")
        {
            song.project->beginGesture ("Insert");
            sections::insertSpace (edit, 8.0, 4.0);
            const auto after = sample (edit, volume, 40.0);
            checkSame (original, 0.0, after, 0.0, 8.0);
            checkSame (original, 8.0, after, 12.0, 20.0);

            for (double b = 8.25; b < 12.0; b += 0.5)
                CHECK (after[sampleAt (b)] == doctest::Approx (0.6f).epsilon (1.0e-4));
        }

        SUBCASE ("delete: what came after comes earlier")
        {
            song.project->beginGesture ("Delete");
            sections::deleteRange (edit, 0.0, 8.0);
            const auto after = sample (edit, volume, 30.0);
            checkSame (original, 8.0, after, 0.0, 24.0);
        }

        SUBCASE ("duplicate: the stretch plays twice, ramps and jumps too")
        {
            for (auto [start, end] : { std::pair { 0.0, 8.0 }, std::pair { 8.0, 16.0 }, std::pair { 4.0, 12.0 }, std::pair { 12.0, 16.0 } })
            {
                CAPTURE (start);
                song.project->beginGesture ("Duplicate");
                sections::duplicateRange (edit, start, end);
                const auto after = sample (edit, volume, 48.0);
                const auto length = end - start;
                checkSame (original, 0.0, after, 0.0, end);
                checkSame (original, start, after, end, length);
                checkSame (original, end, after, end + length, 16.0);

                song.project->undo();
                CHECK (curve.getNumPoints() == points);
                CHECK (sample (edit, volume, 40.0) == original);
            }

            // A flat curve gets no points to keep its shape: just the copy of its point.
            song.project->beginGesture ("Flat");
            while (curve.getNumPoints() > 1)
                curve.removePoint (curve.getNumPoints() - 1, &edit.getUndoManager());

            song.project->beginGesture ("Duplicate");
            sections::duplicateRange (edit, 0.0, 8.0);
            CHECK (curve.getNumPoints() == 2);
        }
    }
}

TEST_SUITE ("core: the automation of a piece of time")
{
    TEST_CASE ("a track's lines over a stretch are taken out and put elsewhere; outside both stretches they keep their shape")
    {
        Song song;
        auto& volume = *song.keys->getVolumePlugin()->volParam;
        auto& edit = song.edit();
        auto& curve = volume.getCurve();
        const auto at = [&edit] (double beat) { return edit.tempoSequence.toTime (te::BeatPosition::fromBeats (beat)); };

        // Flat, a ramp from beat 2 to beat 10, a jump at 12, flat.
        curve.addPoint (at (2.0), 0.3f, 0.0f, &edit.getUndoManager());
        curve.addPoint (at (10.0), 0.7f, 0.0f, &edit.getUndoManager());
        curve.addPoint (at (12.0), 0.7f, 0.0f, &edit.getUndoManager());
        curve.addPoint (at (12.0), 0.2f, 0.0f, &edit.getUndoManager());
        song.project->clearUndoHistory();
        const auto original = sample (edit, volume, 40.0);
        const auto points = curve.getNumPoints();

        // What it does where: the track it came from, how long, one line (the bass has none).
        const auto piece = automation_pieces::copy (*song.keys, 4.0, 8.0);
        CHECK (piece.track == song.keys->itemID);
        CHECK (piece.lengthBeats == doctest::Approx (4.0));
        REQUIRE (piece.lines.size() == 1);
        CHECK (piece.lines[0].points.front().value == doctest::Approx (0.4f));
        CHECK (piece.lines[0].points.back().value == doctest::Approx (0.6f));
        CHECK (automation_pieces::copy (*song.bass, 4.0, 8.0).isEmpty());
        CHECK (sample (edit, volume, 40.0) == original);        // a copy changes nothing

        SUBCASE ("copied later: there it does what it did here; before and after it, the line is as it was")
        {
            song.project->beginGesture ("Copy time");
            automation_pieces::paste (*song.keys, piece, 20.0);
            const auto after = sample (edit, volume, 40.0);
            checkSame (original, 0.0, after, 0.0, 20.0);
            checkSame (original, 4.0, after, 20.0, 4.0);
            checkSame (original, 24.0, after, 24.0, 16.0);

            song.project->undo();
            CHECK (curve.getNumPoints() == points);
            CHECK (sample (edit, volume, 40.0) == original);
        }

        SUBCASE ("moved: where it was the line runs straight from edge to edge")
        {
            // The stretch with the jump in it: beats 10 to 14 go to 20.
            const auto jump = automation_pieces::copy (*song.keys, 10.0, 14.0);
            song.project->beginGesture ("Move time");
            automation_pieces::clear (*song.keys, 10.0, 14.0);
            automation_pieces::paste (*song.keys, jump, 20.0);
            const auto after = sample (edit, volume, 40.0);
            checkSame (original, 0.0, after, 0.0, 10.0);
            checkSame (original, 10.0, after, 20.0, 4.0);
            checkSame (original, 24.0, after, 24.0, 16.0);
            checkSame (original, 14.0, after, 14.0, 6.0);

            // From 0.7 at beat 10 straight down to 0.2 at beat 14.
            CHECK (after[sampleAt (12.25)] == doctest::Approx (0.7f - 0.5f * 2.25f / 4.0f).epsilon (1.0e-3));

            song.project->undo();
            CHECK (sample (edit, volume, 40.0) == original);
        }

        SUBCASE ("moved a little: the two stretches overlap")
        {
            const auto moved = automation_pieces::copy (*song.keys, 8.0, 12.0);
            song.project->beginGesture ("Move time");
            automation_pieces::clear (*song.keys, 8.0, 12.0);
            automation_pieces::paste (*song.keys, moved, 10.0);
            const auto after = sample (edit, volume, 40.0);
            checkSame (original, 0.0, after, 0.0, 8.0);
            checkSame (original, 8.0, after, 10.0, 4.0);
            checkSame (original, 14.0, after, 14.0, 20.0);
        }

        SUBCASE ("only onto the track it came from, and onto lines that are there")
        {
            song.project->beginGesture ("Paste time");
            automation_pieces::paste (*song.bass, piece, 20.0);
            CHECK (sample (edit, volume, 40.0) == original);
            CHECK (song.bass->getVolumePlugin()->volParam->getCurve().getNumPoints() == 0);
            CHECK_FALSE (song.project->canUndo());
        }

        SUBCASE ("a line all of whose points are taken out stays a line")
        {
            song.project->beginGesture ("Cut time");
            automation_pieces::clear (*song.keys, 0.0, 16.0);
            CHECK (curve.getNumPoints() >= 1);

            // From the 0.3 it started with to the 0.2 it ended with.
            const auto after = sample (edit, volume, 40.0);
            CHECK (after[sampleAt (0.25)] == doctest::Approx (0.3f).epsilon (0.02));
            CHECK (after[sampleAt (20.25)] == doctest::Approx (0.2f).epsilon (1.0e-3));
        }
    }
}
