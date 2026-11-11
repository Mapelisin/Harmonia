#include <doctest.h>

#include "TestAudioRig.h"
#include "sandbox/TestPluginSong.h"
#include "audio/Effects.h"
#include "audio/Export.h"
#include "audio/Freeze.h"
#include "audio/Instruments.h"
#include "audio/TrackDelay.h"
#include "audio/Transport.h"
#include "core/Clips.h"
#include "core/Project.h"

using namespace harmonia;
namespace track_delay = audio::track_delay;
namespace effects = audio::effects;

// Two tracks play the same click at the same moment, one to the left and one to the right: where
// the click comes out in each channel says whether the tracks are in time.
namespace
{
    constexpr double rate = 44100.0;
    constexpr double clickSeconds = 0.5;
    constexpr int clickSample = 22050;

    void settle (te::Edit& edit)
    {
        edit.dispatchPendingUpdatesSynchronously();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    }

    /** The first sample of a channel that is louder than the threshold, or -1. */
    int onset (const juce::AudioBuffer<float>& sound, int channel, float threshold = 0.05f)
    {
        for (int i = 0; channel < sound.getNumChannels() && i < sound.getNumSamples(); ++i)
            if (std::abs (sound.getSample (channel, i)) > threshold)
                return i;

        return -1;
    }

    juce::AudioBuffer<float> read (const juce::File& file)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        REQUIRE (reader != nullptr);
        juce::AudioBuffer<float> sound ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&sound, 0, sound.getNumSamples(), 0, true, true);
        return sound;
    }

    juce::AudioBuffer<float> exported (te::Edit& edit, const juce::File& file)
    {
        settle (edit);
        const auto error = audio::exportWav (edit, file);
        REQUIRE_MESSAGE (error.isEmpty(), error.toStdString());
        return read (file);
    }

    /** What comes out of the rig's sound card while its song plays from the start. */
    juce::AudioBuffer<float> heard (test::AudioRig& rig, double seconds = 1.0)
    {
        auto& edit = rig.project->getEdit();
        settle (edit);
        audio::Transport transport (edit);
        const auto numSamples = (int) (seconds * rig.params.sampleRate);
        juce::AudioBuffer<float> sound (2, numSamples);

        // Twice: the first time the files may still be on their way from the disk.
        for (int pass = 0; pass < 2; ++pass)
        {
            sound.clear();
            transport.setPositionSeconds (0.0);
            transport.play();
            juce::MidiBuffer midi;

            for (int start = 0; start < numSamples; start += rig.params.blockSize)
            {
                const auto n = std::min (rig.params.blockSize, numSamples - start);
                juce::AudioBuffer<float> block (2, n);
                block.clear();
                rig.audioIO.processBlock (block, midi);

                for (int c = 0; c < 2; ++c)
                    sound.copyFrom (c, start, block, c, 0, n);
            }

            transport.stop();
            rig.process (0.2, 0.0f);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        }

        return sound;
    }

    const effects::EffectType& limiterType()
    {
        static const auto type = []
        {
            for (const auto& t : effects::getBuiltIn())
                if (t.name == "Limiter")
                    return t;

            return effects::EffectType();
        }();

        return type;
    }

    /** Two audio tracks with the click at half a second: "Left" all the way left, "Right" right. */
    struct TwoClicks
    {
        TwoClicks()
        {
            const auto file = test::writeClicks (rig.temp.getFile ("Click.wav"), { clickSeconds }, 0.5);
            juce::String error;

            for (auto* track : { &left, &right })
            {
                REQUIRE_MESSAGE (rig.project->addAudioClip (*track, file, 0.0, error) != nullptr, error.toStdString());
                track->getVolumePlugin()->setVolumeDb (0.0f);
                track->getVolumePlugin()->setPan (track == &left ? -1.0f : 1.0f);
            }

            rig.project->getEdit().getMasterVolumePlugin()->setVolumeDb (0.0f);
            settle (edit());
            rig.project->clearUndoHistory();
        }

        te::Edit& edit()    { return rig.project->getEdit(); }

        /** How many samples later than the right track the left one is: exported, and heard. */
        int exportedLag (const juce::String& name = "Song.wav")
        {
            const auto sound = exported (edit(), rig.temp.getFile (name));
            REQUIRE (sound.getNumChannels() == 2);
            const auto l = onset (sound, 0), r = onset (sound, 1);
            REQUIRE (l >= 0);
            REQUIRE (r >= 0);
            lastRight = r;
            return l - r;
        }

        int heardLag()
        {
            const auto sound = heard (rig);
            const auto l = onset (sound, 0), r = onset (sound, 1);
            REQUIRE (l >= 0);
            REQUIRE (r >= 0);
            return l - r;
        }

        test::AudioRig rig;
        te::AudioTrack& left = rig.addAudioTrack ("Left");
        te::AudioTrack& right = rig.addAudioTrack ("Right");
        int lastRight = -1;     // where the right track's click was in the last export
    };
}

TEST_SUITE ("audio: slow plugins and track delay")
{
    TEST_CASE ("a track with a slow effect plays in time with the others, exported and live")
    {
        TwoClicks song;
        CHECK (song.exportedLag() == 0);
        CHECK (std::abs (song.lastRight - clickSample) <= 1);

        // The limiter looks 2 ms ahead: it gives its sound 2 ms late, and says so.
        juce::String error;
        auto* limiter = effects::add (song.left.pluginList, limiterType(), error);
        REQUIRE_MESSAGE (limiter != nullptr, error.toStdString());
        CHECK (limiter->getLatencySeconds() > 0.0015);

        CHECK (song.exportedLag() == 0);
        CHECK (std::abs (song.lastRight - clickSample) <= 1);       // and nothing is late as a whole
        CHECK (song.heardLag() == 0);

        // Turned off, it still takes its time: the track doesn't jump.
        effects::setBypassed (*limiter, true);
        CHECK (song.exportedLag() == 0);
        CHECK (song.heardLag() == 0);

        // Two of them in a row on one track, one on the other.
        effects::setBypassed (*limiter, false);
        REQUIRE (effects::add (song.left.pluginList, limiterType(), error) != nullptr);
        REQUIRE (effects::add (song.right.pluginList, limiterType(), error) != nullptr);
        CHECK (song.exportedLag() == 0);
        CHECK (std::abs (song.lastRight - clickSample) <= 1);
        CHECK (song.heardLag() == 0);

        // On the master: everything is late together, and the export takes that off.
        REQUIRE (effects::add (song.edit().getMasterPluginList(), limiterType(), error) != nullptr);
        CHECK (song.exportedLag() == 0);
        CHECK (std::abs (song.lastRight - clickSample) <= 1);
    }

    TEST_CASE ("what a plugin in a process of its own says about being late reaches the engine")
    {
        // The test synth says it is 64 samples late, and isn't: an engine that hears that starts
        // its note 64 samples early. (A plugin that really is late lands on the beat: the limiter.)
        test::TempFolder temp;
        test::plugins::KnownTestPlugins known;
        auto project = core::Project::createEmpty (test::session());
        auto& synthTrack = *project->getTracks().getFirst();
        const auto problem = audio::instruments::useExternal (synthTrack, test::plugins::synth());
        REQUIRE_MESSAGE (problem.isEmpty(), problem.toStdString());

        auto* notes = project->addMidiClip (synthTrack, 0.0, 8.0, "Note");
        REQUIRE (notes != nullptr);
        notes->getSequence().addNote (60, te::BeatPosition::fromBeats (1.0), te::BeatDuration::fromBeats (2.0), 100, 0, nullptr);
        REQUIRE (project->getTempoBpm() == doctest::Approx (120.0));    // beat 1 is half a second

        auto* clickTrack = project->addTrack ("Click");
        REQUIRE (clickTrack != nullptr);
        core::tracks::setKind (*clickTrack, core::tracks::Kind::audio);
        juce::String error;
        REQUIRE (project->addAudioClip (*clickTrack, test::writeClicks (temp.getFile ("Click.wav"), { clickSeconds }, 0.5), 0.0, error) != nullptr);

        synthTrack.getVolumePlugin()->setVolumeDb (0.0f);
        synthTrack.getVolumePlugin()->setPan (-1.0f);
        clickTrack->getVolumePlugin()->setVolumeDb (0.0f);
        clickTrack->getVolumePlugin()->setPan (1.0f);

        const auto sound = exported (project->getEdit(), temp.getFile ("Song.wav"));
        REQUIRE (sound.getNumChannels() == 2);
        const auto note = onset (sound, 0), click = onset (sound, 1);
        REQUIRE (note >= 0);
        REQUIRE (click >= 0);

        // The click's first sample is silent, so it is "heard" a sample or two in.
        CHECK (std::abs (click - clickSample) <= 2);
        CHECK (std::abs (note - (clickSample - 64)) <= 2);
    }

    TEST_CASE ("a track's delay: later holds its sound back, earlier makes the rest of the song wait")
    {
        TwoClicks song;
        auto& project = *song.rig.project;
        const auto tenMs = 441;

        CHECK (track_delay::getMs (song.left) == 0.0);
        CHECK (track_delay::describe (0.0) == "0 ms");
        CHECK (track_delay::describe (12.0) == "+12 ms");
        CHECK (track_delay::describe (-3.5) == "-3.5 ms");
        CHECK (track_delay::parse ("12") == 12.0);
        CHECK (track_delay::parse (" -3,5 ms ") == -3.5);
        CHECK (track_delay::parse ("+7ms") == 7.0);
        CHECK_FALSE (track_delay::parse ("soon").has_value());
        CHECK_FALSE (track_delay::parse ("").has_value());
        CHECK_FALSE (track_delay::parse ("-").has_value());

        // Later.
        const auto pluginsBefore = song.left.pluginList.size();
        project.beginGesture ("Track delay");
        track_delay::setMs (song.left, 10.0);
        CHECK (track_delay::getMs (song.left) == 10.0);
        CHECK (track_delay::getMs (song.right) == 0.0);
        CHECK (song.left.pluginList.size() == pluginsBefore + 1);
        CHECK (song.exportedLag() == tenMs);
        CHECK (std::abs (song.lastRight - clickSample) <= 1);       // the other track stays where it was
        CHECK (song.heardLag() == tenMs);

        // It isn't one of the track's effects, and an effect goes before it.
        CHECK (effects::get (song.left.pluginList).isEmpty());
        juce::String error;
        auto* limiter = effects::add (song.left.pluginList, limiterType(), error);
        REQUIRE (limiter != nullptr);
        CHECK (effects::get (song.left.pluginList).size() == 1);
        CHECK (song.left.pluginList.indexOf (limiter) < effects::getFaderIndex (song.left.pluginList));
        CHECK (song.exportedLag() == tenMs);
        effects::remove (*limiter);

        // Earlier: nothing is cut, the other track comes later than this one.
        project.beginGesture ("Track delay");
        track_delay::setMs (song.left, -10.0);
        CHECK (track_delay::getMs (song.left) == -10.0);
        CHECK (song.left.pluginList.size() == pluginsBefore + 1);
        CHECK (song.exportedLag() == -tenMs);
        CHECK (std::abs (song.lastRight - clickSample) <= 1);       // still at its half second in the file
        CHECK (song.heardLag() == -tenMs);

        // Undo and redo.
        project.undo();
        CHECK (track_delay::getMs (song.left) == 10.0);
        CHECK (song.exportedLag() == tenMs);
        project.redo();
        CHECK (track_delay::getMs (song.left) == -10.0);
        CHECK (song.exportedLag() == -tenMs);

        // Saved with the song.
        const auto file = song.rig.temp.getFile ("Song.harmonia");
        REQUIRE (project.save (file).isEmpty());
        auto loaded = core::Project::load (song.rig.session, file, error);
        REQUIRE_MESSAGE (loaded != nullptr, error.toStdString());
        REQUIRE (loaded->getTracks().size() >= 2);
        te::AudioTrack* loadedLeft = nullptr;

        for (auto* t : loaded->getTracks())
            if (t->getName() == "Left")
                loadedLeft = t;

        REQUIRE (loadedLeft != nullptr);
        CHECK (track_delay::getMs (*loadedLeft) == -10.0);
        const auto again = exported (loaded->getEdit(), song.rig.temp.getFile ("Loaded.wav"));
        CHECK (onset (again, 0) - onset (again, 1) == -tenMs);
        loaded.reset();

        // Kept within its range, to a tenth of a millisecond.
        project.beginGesture ("Track delay");
        track_delay::setMs (song.left, 1000.0);
        CHECK (track_delay::getMs (song.left) == track_delay::maxMs);
        track_delay::setMs (song.left, -1000.0);
        CHECK (track_delay::getMs (song.left) == -track_delay::maxMs);
        track_delay::setMs (song.left, 2.26);
        CHECK (track_delay::getMs (song.left) == doctest::Approx (2.3));
        CHECK (song.exportedLag() == juce::roundToInt (2.3 * rate / 1000.0));

        // No delay: nothing of it is left in the song.
        project.beginGesture ("Track delay");
        track_delay::setMs (song.left, 0.0);
        CHECK (track_delay::getMs (song.left) == 0.0);
        CHECK (song.left.pluginList.size() == pluginsBefore);
        CHECK (song.exportedLag() == 0);
        CHECK (song.heardLag() == 0);
        project.undo();
        CHECK (track_delay::getMs (song.left) == doctest::Approx (2.3));
    }

    TEST_CASE ("a frozen track keeps its delay, and has it once")
    {
        TwoClicks song;
        auto& project = *song.rig.project;
        const auto tenMs = 441;

        for (const auto ms : { 10.0, -10.0 })
        {
            CAPTURE (ms);
            project.beginGesture ("Track delay");
            track_delay::setMs (song.left, ms);
            const auto lag = ms > 0.0 ? tenMs : -tenMs;
            REQUIRE (song.exportedLag() == lag);

            const auto error = audio::freeze::freeze (project, song.left);
            REQUIRE_MESSAGE (error.isEmpty(), error.toStdString());
            CHECK (track_delay::getMs (song.left) == ms);
            CHECK (song.exportedLag() == lag);
            CHECK (song.heardLag() == lag);

            audio::freeze::unfreeze (project, song.left);
            CHECK (song.exportedLag() == lag);
        }
    }
}
