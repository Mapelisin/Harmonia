#include <doctest.h>

#include "TestProject.h"
#include "core/Scales.h"

using namespace harmonia;
namespace scales = core::scales;
namespace chords = core::chords;
using scales::Scale;
using scales::ScaleType;
using chords::ChordType;

TEST_SUITE ("core::scales")
{
    TEST_CASE ("a scale knows its notes, snaps to them and steps along them")
    {
        const Scale cMajor { 0, ScaleType::major };
        CHECK (cMajor.contains (60));
        CHECK_FALSE (cMajor.contains (61));
        CHECK (cMajor.contains (71));
        CHECK (cMajor.isRoot (48));
        CHECK_FALSE (cMajor.isRoot (62));

        CHECK (cMajor.snap (61) == 60);     // C# is as near to C as to D: the lower
        CHECK (cMajor.snap (63) == 62);
        CHECK (cMajor.snap (66) == 65);
        CHECK (cMajor.snap (64) == 64);

        CHECK (cMajor.step (60, 1) == 62);
        CHECK (cMajor.step (64, 1) == 65);
        CHECK (cMajor.step (71, 1) == 72);
        CHECK (cMajor.step (60, -1) == 59);
        CHECK (cMajor.step (60, 7) == 72);
        CHECK (cMajor.step (61, 0) == 60);
        CHECK (cMajor.step (127, 5) == 127);
        CHECK (cMajor.step (0, -3) == 0);

        CHECK (cMajor.countSteps (60, 67) == 4);
        CHECK (cMajor.countSteps (67, 60) == -4);
        CHECK (cMajor.countSteps (60, 61) == 0);

        const Scale aMinor { 9, ScaleType::minor };
        CHECK (aMinor.contains (60));     // C is in A minor
        CHECK_FALSE (aMinor.contains (61));
        CHECK (aMinor.isRoot (57));
        CHECK (aMinor.getName() == "A minor");
        CHECK (cMajor.getName() == "C major");
        CHECK (Scale().getName() == "No scale");

        // Chromatic: everything fits.
        for (int pitch = 0; pitch < 128; ++pitch)
            CHECK (Scale().snap (pitch) == pitch);

        // Pentatonic has five notes an octave.
        const Scale pentatonic { 0, ScaleType::majorPentatonic };
        CHECK (pentatonic.countSteps (60, 72) == 5);
    }

    TEST_CASE ("chords from a root, and the chords a scale gives")
    {
        const Scale none;
        CHECK (chords::build (60, ChordType::major, none) == std::vector<int> { 60, 64, 67 });
        CHECK (chords::build (60, ChordType::minor7, none) == std::vector<int> { 60, 63, 67, 70 });
        CHECK (chords::build (60, ChordType::power, none) == std::vector<int> { 60, 67, 72 });
        CHECK (chords::build (120, ChordType::add9, none) == std::vector<int> { 120, 124, 127 });

        const Scale cMajor { 0, ScaleType::major };
        CHECK (chords::build (62, ChordType::scaleTriad, cMajor) == std::vector<int> { 62, 65, 69 });         // D minor
        CHECK (chords::build (71, ChordType::scaleTriad, cMajor) == std::vector<int> { 71, 74, 77 });         // B diminished
        CHECK (chords::build (67, ChordType::scaleSeventh, cMajor) == std::vector<int> { 67, 71, 74, 77 });   // G7
        CHECK (chords::build (61, ChordType::scaleTriad, cMajor) == std::vector<int> { 60, 64, 67 });         // onto the scale first

        // No scale: major and dominant seventh.
        CHECK (chords::build (62, ChordType::scaleTriad, none) == std::vector<int> { 62, 66, 69 });
        CHECK (chords::build (62, ChordType::scaleSeventh, none) == std::vector<int> { 62, 66, 69, 72 });
    }

    TEST_CASE ("naming the chord some notes make")
    {
        CHECK (chords::nameOf ({ 60, 64, 67 }) == "C");
        CHECK (chords::nameOf ({ 57, 60, 64 }) == "Am");
        CHECK (chords::nameOf ({ 64, 67, 72 }) == "C/E");                 // with E in the bass
        CHECK (chords::nameOf ({ 55, 59, 62, 65 }) == "G7");
        CHECK (chords::nameOf ({ 57, 60, 64, 67 }) == "Am7");
        CHECK (chords::nameOf ({ 60, 64, 67, 69 }) == "C6");              // the same notes, C in the bass
        CHECK (chords::nameOf ({ 60, 64, 67, 71 }) == "Cmaj7");
        CHECK (chords::nameOf ({ 59, 62, 65 }) == "Bdim");
        CHECK (chords::nameOf ({ 62, 67, 69 }) == "Dsus4");
        CHECK (chords::nameOf ({ 40, 47, 52 }) == "E5");                  // a power chord, octaves don't matter
        CHECK (chords::nameOf ({ 50, 60, 65 }) == "Dm7");                 // no fifth
        CHECK (chords::nameOf ({ 61, 65, 68 }) == "C#");
        CHECK (chords::nameOf ({ 48, 60, 64, 67, 76 }) == "C");           // doubled notes

        CHECK (chords::nameOf ({}).isEmpty());
        CHECK (chords::nameOf ({ 60 }).isEmpty());
        CHECK (chords::nameOf ({ 60, 72 }).isEmpty());                    // one note in two octaves
        CHECK (chords::nameOf ({ 60, 61, 62 }).isEmpty());                // not a chord
    }

    TEST_CASE ("the song's scale is kept in the project and saved with it")
    {
        auto& session = test::session();
        juce::TemporaryFile file (".harmonia");

        {
            auto project = core::Project::createEmpty (session);
            CHECK (scales::read (project->getEdit()) == Scale());

            scales::write (project->getEdit(), { 9, ScaleType::minor }, &project->getEdit().getUndoManager());
            CHECK (scales::read (project->getEdit()) == Scale { 9, ScaleType::minor });
            CHECK (project->hasUnsavedChanges());

            REQUIRE (project->save (file.getFile()).isEmpty());
        }

        juce::String error;
        auto loaded = core::Project::load (session, file.getFile(), error);
        REQUIRE (loaded != nullptr);
        CHECK (scales::read (loaded->getEdit()) == Scale { 9, ScaleType::minor });

        // No scale again: nothing left in the file.
        scales::write (loaded->getEdit(), {}, nullptr);
        CHECK (scales::read (loaded->getEdit()) == Scale());
        CHECK_FALSE (loaded->getEdit().state.hasProperty ("hScaleType"));
    }
}
