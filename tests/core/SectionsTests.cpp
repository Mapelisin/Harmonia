#include <doctest.h>

#include "TestSession.h"
#include "core/AutomationCurves.h"
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

    /** The most `after` from `toBeat` on is away from `before` from `fromBeat` on, over `lengthBeats`. */
    float mostApart (const std::vector<float>& before, double fromBeat, const std::vector<float>& after, double toBeat,
                     double lengthBeats)
    {
        auto most = 0.0f;

        for (double b = 0.25; b < lengthBeats; b += 0.5)
        {
            const auto i = sampleAt (fromBeat + b), j = sampleAt (toBeat + b);
            REQUIRE (i < before.size());
            REQUIRE (j < after.size());
            most = std::max (most, std::abs (after[j] - before[i]));
        }

        return most;
    }

    /** A cut bent stretch stays this near the shape it had (automation_curves::shapeTolerance,
        which is measured at other places than these samples). A straight line is several times
        further away. */
    constexpr float nearEnough = 0.01f;
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
    TEST_CASE ("a bent stretch cut by a piece of time keeps its shape: in the piece, where it is put, and where it was")
    {
        for (const auto bend : { 0.25f, 0.5f, -0.5f, 0.85f, -0.85f })
        {
            CAPTURE (bend);
            Song song;
            auto& volume = *song.keys->getVolumePlugin()->volParam;
            auto& edit = song.edit();
            auto& curve = volume.getCurve();
            const auto at = [&edit] (double beat) { return edit.tempoSequence.toTime (te::BeatPosition::fromBeats (beat)); };

            // Flat, a bent rise from beat 2 to beat 10, a fall bent the other way to beat 14, flat.
            curve.addPoint (at (2.0), 0.3f, bend, &edit.getUndoManager());
            curve.addPoint (at (10.0), 0.7f, -bend, &edit.getUndoManager());
            curve.addPoint (at (14.0), 0.4f, 0.0f, &edit.getUndoManager());
            song.project->clearUndoHistory();
            const auto original = sample (edit, volume, 40.0);
            const auto points = curve.getNumPoints();

            // It is bent: in the middle of the rise well away from the straight line.
            CHECK (std::abs (original[sampleAt (6.25)] - (0.3f + 0.4f * 4.25f / 8.0f)) > 0.02f);

            // A whole stretch is its point with its bend; so is a part where one bend follows it,
            // and where none can there are points along it. Level before, level after.
            const auto whole = automation_curves::getPart (edit, volume, 2.0);
            REQUIRE (whole.size() == 1);
            CHECK (whole[0].beat == doctest::Approx (2.0));
            CHECK (whole[0].value == doctest::Approx (0.3f));
            CHECK (whole[0].curve == bend);
            CHECK (automation_curves::getPart (edit, volume, 2.0, 10.0)[0].curve == bend);
            CHECK (automation_curves::getPart (edit, volume, 0.0, 2.0)[0].curve == 0.0f);
            CHECK (automation_curves::getPart (edit, volume, 14.0)[0].curve == 0.0f);

            const auto part = automation_curves::getPart (edit, volume, 4.0, 8.0);
            CHECK (part.size() <= (std::abs (bend) < 0.3f ? 1u : 8u));      // a gentle bend needs no more points
            CHECK (part[0].beat == doctest::Approx (4.0));
            CHECK (part[0].value == doctest::Approx (curve.getValueAt (at (4.0), 0.0f)));

            for (const auto& p : part)
            {
                CHECK (p.added);
                CHECK (p.beat < 8.0);
            }

            SUBCASE ("copied: the copy does what the part did; the line it came from is as it was")
            {
                const auto piece = automation_pieces::copy (*song.keys, 4.0, 8.0);
                song.project->beginGesture ("Copy time");
                automation_pieces::paste (*song.keys, piece, 20.0);
                const auto after = sample (edit, volume, 40.0);
                checkSame (original, 0.0, after, 0.0, 20.0);
                CHECK (mostApart (original, 4.0, after, 20.0, 4.0) < nearEnough);
                checkSame (original, 24.0, after, 24.0, 16.0);

                song.project->undo();
                CHECK (curve.getNumPoints() == points);
                CHECK (sample (edit, volume, 40.0) == original);
            }

            SUBCASE ("copied into the bent stretch itself: before and after the copy the stretch goes on as it went")
            {
                const auto piece = automation_pieces::copy (*song.keys, 16.0, 18.0);        // flat at 0.4
                song.project->beginGesture ("Copy time");
                automation_pieces::paste (*song.keys, piece, 5.0);
                const auto after = sample (edit, volume, 40.0);
                checkSame (original, 0.0, after, 0.0, 2.0);
                CHECK (mostApart (original, 2.0, after, 2.0, 3.0) < nearEnough);
                CHECK (after[sampleAt (5.25)] == doctest::Approx (0.4f).epsilon (1.0e-4));
                CHECK (after[sampleAt (6.75)] == doctest::Approx (0.4f).epsilon (1.0e-4));
                CHECK (mostApart (original, 7.0, after, 7.0, 3.0) < nearEnough);
                checkSame (original, 10.0, after, 10.0, 30.0);
            }

            SUBCASE ("moved from inside one stretch: nothing to take out there, the line stays whole")
            {
                const auto piece = automation_pieces::copy (*song.keys, 4.0, 8.0);
                song.project->beginGesture ("Move time");
                automation_pieces::clear (*song.keys, 4.0, 8.0);
                automation_pieces::paste (*song.keys, piece, 20.0);
                const auto after = sample (edit, volume, 40.0);
                checkSame (original, 0.0, after, 0.0, 20.0);
                CHECK (mostApart (original, 4.0, after, 20.0, 4.0) < nearEnough);
            }

            SUBCASE ("moved with a point in it: straight where it was, what is left of both stretches keeps its shape")
            {
                const auto piece = automation_pieces::copy (*song.keys, 8.0, 12.0);
                song.project->beginGesture ("Move time");
                automation_pieces::clear (*song.keys, 8.0, 12.0);
                automation_pieces::paste (*song.keys, piece, 20.0);
                const auto after = sample (edit, volume, 40.0);
                CHECK (mostApart (original, 8.0, after, 20.0, 4.0) < nearEnough);
                CHECK (mostApart (original, 0.0, after, 0.0, 8.0) < nearEnough);
                CHECK (mostApart (original, 12.0, after, 12.0, 8.0) < nearEnough);

                // Where it was: straight from the value at beat 8 to the value at beat 12.
                const auto from = curve.getValueAt (at (8.0), 0.0f), to = curve.getValueAt (at (12.0), 0.0f);
                CHECK (from == doctest::Approx (original[sampleAt (7.75)]).epsilon (0.05));
                CHECK (after[sampleAt (9.25)] == doctest::Approx (from + (to - from) * 1.25f / 4.0f).epsilon (1.0e-3));

                song.project->undo();
                CHECK (curve.getNumPoints() == points);
                CHECK (sample (edit, volume, 40.0) == original);
            }

            SUBCASE ("bars repeated: the copy of the bars bends as they do, the rest of the line too")
            {
                for (auto [start, end] : { std::pair { 4.0, 8.0 }, std::pair { 8.0, 12.0 }, std::pair { 2.0, 10.0 }, std::pair { 0.0, 16.0 } })
                {
                    CAPTURE (start);
                    song.project->beginGesture ("Duplicate");
                    sections::duplicateRange (edit, start, end);
                    const auto after = sample (edit, volume, 56.0);
                    const auto length = end - start;
                    CHECK (mostApart (original, 0.0, after, 0.0, end) < nearEnough);
                    CHECK (mostApart (original, start, after, end, length) < nearEnough);
                    CHECK (mostApart (original, end, after, end + length, 16.0) < nearEnough);

                    song.project->undo();
                    CHECK (curve.getNumPoints() == points);
                    CHECK (sample (edit, volume, 40.0) == original);
                }
            }
        }
    }

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

    TEST_CASE ("the automation of a clip goes where the clip went: the lines with points of their own under it, and no others")
    {
        Song song;
        auto& edit = song.edit();
        auto& volume = *song.keys->getVolumePlugin()->volParam;
        auto& pan = *song.keys->getVolumePlugin()->panParam;
        const auto at = [&edit] (double beat) { return edit.tempoSequence.toTime (te::BeatPosition::fromBeats (beat)); };

        // The volume dips under beats 8..12 (where a clip lies); the pan goes slowly from one
        // side to the other across the whole song.
        for (auto [beat, value] : { std::pair { 8.0, 0.8f }, std::pair { 9.0, 0.3f }, std::pair { 11.0, 0.3f }, std::pair { 12.0, 0.8f } })
            volume.getCurve().addPoint (at (beat), value, 0.0f, &edit.getUndoManager());

        pan.getCurve().addPoint (at (0.0), -0.5f, 0.0f, &edit.getUndoManager());
        pan.getCurve().addPoint (at (32.0), 0.5f, 0.0f, &edit.getUndoManager());
        song.project->clearUndoHistory();

        const auto volumeBefore = sample (edit, volume, 40.0), panBefore = sample (edit, pan, 40.0);
        const auto volumePoints = volume.getCurve().getNumPoints(), panPoints = pan.getCurve().getNumPoints();

        // What is the clip's: the dip, not the pan that passes under it.
        const auto own = automation_pieces::copyOwn (*song.keys, 8.0, 12.0);
        REQUIRE (own.lines.size() == 1);
        CHECK (own.lines[0].parameter == volume.paramID);
        CHECK (own.lengthBeats == doctest::Approx (4.0));
        CHECK (automation_pieces::copy (*song.keys, 8.0, 12.0).lines.size() == 2);
        CHECK (automation_pieces::copyOwn (*song.keys, 16.0, 20.0).isEmpty());      // nothing of its own there
        CHECK (automation_pieces::copyOwn (*song.keys, 4.0, 8.0).isEmpty());        // the point at its end starts what comes next
        CHECK (automation_pieces::copyOwn (*song.bass, 8.0, 12.0).isEmpty());
        CHECK (sample (edit, volume, 40.0) == volumeBefore);

        SUBCASE ("moved: the dip is where the clip is now; where it was the line runs straight; the pan is as it was")
        {
            song.project->beginGesture ("Move clips");
            automation_pieces::carry ({ { song.keys, 8.0, 20.0, 4.0 } }, false);
            const auto after = sample (edit, volume, 40.0);
            checkSame (volumeBefore, 0.0, after, 0.0, 8.0);
            checkSame (volumeBefore, 8.0, after, 20.0, 4.0);
            checkSame (volumeBefore, 24.0, after, 24.0, 16.0);

            for (double b = 8.25; b < 20.0; b += 0.5)
                CHECK (after[sampleAt (b)] == doctest::Approx (0.8f).epsilon (1.0e-4));

            CHECK (sample (edit, pan, 40.0) == panBefore);
            CHECK (pan.getCurve().getNumPoints() == panPoints);

            song.project->undo();
            CHECK (sample (edit, volume, 40.0) == volumeBefore);
            CHECK (volume.getCurve().getNumPoints() == volumePoints);
        }

        SUBCASE ("copied: the dip is in both places")
        {
            song.project->beginGesture ("Copy clips");
            automation_pieces::carry ({ { song.keys, 8.0, 20.0, 4.0 } }, true);
            const auto after = sample (edit, volume, 40.0);
            checkSame (volumeBefore, 0.0, after, 0.0, 20.0);
            checkSame (volumeBefore, 8.0, after, 20.0, 4.0);
            checkSame (volumeBefore, 24.0, after, 24.0, 16.0);
            CHECK (sample (edit, pan, 40.0) == panBefore);
        }

        SUBCASE ("moved a little, onto a part of where it was: the dip is a beat later")
        {
            song.project->beginGesture ("Move clips");
            automation_pieces::carry ({ { song.keys, 8.0, 9.0, 4.0 } }, false);
            const auto after = sample (edit, volume, 40.0);
            checkSame (volumeBefore, 0.0, after, 0.0, 8.0);
            checkSame (volumeBefore, 8.0, after, 9.0, 4.0);
            checkSame (volumeBefore, 13.0, after, 13.0, 27.0);
            CHECK (after[sampleAt (8.25)] == doctest::Approx (0.8f).epsilon (1.0e-4));
        }

        SUBCASE ("two clips that change places: each takes its own")
        {
            // A rise under beats 16..20 too.
            for (auto [beat, value] : { std::pair { 16.0, 0.8f }, std::pair { 17.0, 0.9f }, std::pair { 19.0, 0.9f }, std::pair { 20.0, 0.8f } })
                volume.getCurve().addPoint (at (beat), value, 0.0f, &edit.getUndoManager());

            const auto before = sample (edit, volume, 40.0);
            song.project->beginGesture ("Move clips");
            automation_pieces::carry ({ { song.keys, 8.0, 16.0, 4.0 }, { song.keys, 16.0, 8.0, 4.0 } }, false);
            const auto after = sample (edit, volume, 40.0);
            checkSame (before, 0.0, after, 0.0, 8.0);
            checkSame (before, 16.0, after, 8.0, 4.0);
            checkSame (before, 12.0, after, 12.0, 4.0);
            checkSame (before, 8.0, after, 16.0, 4.0);
            checkSame (before, 20.0, after, 20.0, 20.0);
        }

        SUBCASE ("cut: where the clip was its own lines run straight, and the piece goes where it is pasted")
        {
            song.project->beginGesture ("Cut clips");
            automation_pieces::clear (*song.keys, own, 8.0);
            auto after = sample (edit, volume, 40.0);

            for (double b = 0.25; b < 40.0; b += 0.5)
                CHECK (after[sampleAt (b)] == doctest::Approx (0.8f).epsilon (1.0e-4));

            CHECK (sample (edit, pan, 40.0) == panBefore);
            CHECK (pan.getCurve().getNumPoints() == panPoints);

            automation_pieces::paste (*song.keys, own, 24.0);
            after = sample (edit, volume, 40.0);
            checkSame (volumeBefore, 8.0, after, 24.0, 4.0);
            CHECK (after[sampleAt (20.25)] == doctest::Approx (0.8f).epsilon (1.0e-4));
            CHECK (after[sampleAt (30.25)] == doctest::Approx (0.8f).epsilon (1.0e-4));
            CHECK (sample (edit, pan, 40.0) == panBefore);

            // Not onto another track.
            automation_pieces::clear (*song.bass, own, 8.0);
            automation_pieces::paste (*song.bass, own, 8.0);
            CHECK (song.bass->getVolumePlugin()->volParam->getCurve().getNumPoints() == 0);
        }
    }
}
