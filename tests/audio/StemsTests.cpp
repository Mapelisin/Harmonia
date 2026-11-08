#include <doctest.h>

#include "TestAudioFiles.h"
#include "audio/Export.h"
#include "audio/Instruments.h"
#include "audio/Sends.h"
#include "core/Project.h"
#include "core/Tracks.h"

using namespace harmonia;
namespace instruments = audio::instruments;
namespace sends = audio::sends;

namespace
{
    struct Rendered
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 44100.0;

        double getSeconds() const   { return buffer.getNumSamples() / sampleRate; }

        float getRms (double from, double to) const
        {
            const auto start = juce::jlimit (0, buffer.getNumSamples(), (int) (from * sampleRate));
            const auto end = juce::jlimit (start, buffer.getNumSamples(), (int) (to * sampleRate));
            auto sum = 0.0;

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                for (int i = start; i < end; ++i)
                    sum += juce::square ((double) buffer.getSample (channel, i));

            const auto count = (end - start) * buffer.getNumChannels();
            return count > 0 ? (float) std::sqrt (sum / count) : 0.0f;
        }
    };

    Rendered read (const juce::File& file)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        REQUIRE (reader != nullptr);

        Rendered result;
        result.sampleRate = reader->sampleRate;
        result.buffer.setSize (2, (int) reader->lengthInSamples);
        reader->read (&result.buffer, 0, (int) reader->lengthInSamples, 0, true, true);
        return result;
    }

    /** A track with the built-in synth and a clip with a chord. At 120 BPM a beat is half a second. */
    te::AudioTrack& addPart (core::Project& project, te::AudioTrack* track, const juce::String& name, double startBeat,
                             double chordBeats = 2.0)
    {
        if (track == nullptr)
            track = project.addTrack (name);

        REQUIRE (track != nullptr);
        track->setName (name);
        REQUIRE (instruments::useBuiltInSynth (*track).isEmpty());

        auto* clip = project.addMidiClip (*track, startBeat, 4.0, name);
        REQUIRE (clip != nullptr);

        for (int pitch : { 48, 55, 60, 64 })
            clip->getSequence().addNote (pitch, te::BeatPosition(), te::BeatDuration::fromBeats (chordBeats), 110, 0, nullptr);

        return *track;
    }

    /** Lead: a chord at 0–1 s with a reverb send; bass: a chord at 4–5 s; a muted part; a track
        without clips. The song ends at 6 s. */
    struct Band
    {
        Band()
        {
            lead = &addPart (*project, project->getTracks().getFirst(), "Lead", 0.0);
            bass = &addPart (*project, nullptr, "Bass", 8.0);
            muted = &addPart (*project, nullptr, "Muted", 0.0);
            muted->setMute (true);
            REQUIRE (instruments::useBuiltInSynth (*project->addTrack ("Empty")).isEmpty());

            juce::String error;
            reverb = sends::addReturnTrack (*project, sends::Preset::reverb, error);
            REQUIRE (reverb != nullptr);
            REQUIRE (sends::setLevelDb (*lead, *reverb, 0.0f));

            project->clearUndoHistory();
        }

        test::TempFolder temp;
        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        te::AudioTrack* lead = nullptr;
        te::AudioTrack* bass = nullptr;
        te::AudioTrack* muted = nullptr;
        te::AudioTrack* reverb = nullptr;
    };
}

TEST_SUITE ("audio::exportStems")
{
    TEST_CASE ("a stem per track that is heard and has clips: only its own sound, its reverb, as long as the song")
    {
        Band band;
        const auto result = audio::exportStems (band.project->getEdit(), band.temp.getFile ("Stems"), "Song");
        REQUIRE_MESSAGE (result.error.isEmpty(), result.error.toStdString());
        REQUIRE (result.files.size() == 2);
        CHECK (result.files[0].getFileName() == "Song - 01 Lead.wav");
        CHECK (result.files[1].getFileName() == "Song - 02 Bass.wav");

        const auto lead = read (result.files[0]);
        const auto bass = read (result.files[1]);
        CHECK (lead.getSeconds() == doctest::Approx (6.0 + audio::exportTailSeconds).epsilon (0.01));
        CHECK (bass.getSeconds() == doctest::Approx (lead.getSeconds()).epsilon (0.001));

        CHECK (lead.getRms (0.0, 1.0) > 0.01f);
        CHECK (lead.getRms (4.2, 4.9) < lead.getRms (0.0, 1.0) * 0.1f);   // no bass in it
        CHECK (bass.getRms (0.0, 1.0) < 0.0005f);                         // nor lead (nor its reverb) here
        CHECK (bass.getRms (4.0, 5.0) > 0.01f);

        // The lead's reverb is in the lead's stem.
        REQUIRE (sends::setLevelDb (*band.lead, *band.reverb, -100.0f));
        const auto dry = audio::exportStems (band.project->getEdit(), band.temp.getFile ("Dry"), "Song");
        REQUIRE (dry.error.isEmpty());
        CHECK (lead.getRms (1.3, 3.0) > read (dry.files[0]).getRms (1.3, 3.0) * 2.0f);
    }

    TEST_CASE ("the stems add up to the mix (which then goes through the master's volume)")
    {
        // Sound files rather than the synth: the synth starts its oscillators at random phases,
        // so two renders of it never quite match.
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        auto& first = *project->getTracks().getFirst();
        auto& second = *project->addTrack ("Second");
        core::tracks::setKind (first, core::tracks::Kind::audio);
        core::tracks::setKind (second, core::tracks::Kind::audio);

        juce::String error;
        REQUIRE (project->addAudioClip (first, test::writeTone (temp.getFile ("A.wav"), 2.0, 0.3f), 0.0, error) != nullptr);
        REQUIRE (project->addAudioClip (second, test::writeTone (temp.getFile ("B.wav"), 2.0, 0.2f, 1, true), 2.0, error) != nullptr);
        auto* reverb = sends::addReturnTrack (*project, sends::Preset::reverb, error);
        REQUIRE (reverb != nullptr);
        REQUIRE (sends::setLevelDb (first, *reverb, -6.0f));
        REQUIRE (sends::setLevelDb (second, *reverb, -12.0f));

        const auto stems = audio::exportStems (project->getEdit(), temp.getFile ("Stems"), "Song");
        REQUIRE (stems.error.isEmpty());
        REQUIRE (stems.files.size() == 2);
        const auto mixFile = temp.getFile ("Mix.wav");
        REQUIRE (audio::exportWav (project->getEdit(), mixFile).isEmpty());

        auto sum = read (stems.files[0]);
        const auto other = read (stems.files[1]);

        for (int channel = 0; channel < 2; ++channel)
            sum.buffer.addFrom (channel, 0, other.buffer, channel, 0, std::min (sum.buffer.getNumSamples(), other.buffer.getNumSamples()));

        const auto mix = read (mixFile);
        REQUIRE (mix.buffer.getNumSamples() == sum.buffer.getNumSamples());

        const auto ratioDb = [&] (double from, double to)
        {
            return juce::Decibels::gainToDecibels (mix.getRms (from, to) / sum.getRms (from, to));
        };

        // The same everywhere: each alone, both, the reverb's tail.
        const auto masterDb = project->getEdit().getMasterVolumePlugin()->getVolumeDb();
        CHECK (std::abs (ratioDb (0.2, 0.9) - masterDb) < 0.1f);
        CHECK (std::abs (ratioDb (1.1, 1.9) - masterDb) < 0.1f);
        CHECK (std::abs (ratioDb (2.2, 2.9) - masterDb) < 0.1f);
        CHECK (std::abs (ratioDb (2.9, 3.4) - masterDb) < 0.1f);   // below about -75 dB the reverb cuts off
    }

    TEST_CASE ("while some tracks are soloed only those; nothing heard says so; names are safe for files")
    {
        Band band;
        band.bass->setSolo (true);
        const auto soloed = audio::exportStems (band.project->getEdit(), band.temp.getFile ("Solo"), "Song");
        REQUIRE (soloed.error.isEmpty());
        REQUIRE (soloed.files.size() == 1);
        CHECK (soloed.files[0].getFileName() == "Song - 02 Bass.wav");

        band.bass->setSolo (false);
        band.lead->setMute (true);
        band.bass->setMute (true);
        CHECK (audio::exportStems (band.project->getEdit(), band.temp.getFile ("None"), "Song").error.startsWith ("Nothing to export"));

        auto empty = core::Project::createEmpty (test::session());
        CHECK (audio::exportStems (empty->getEdit(), band.temp.getFile ("Empty"), "Song").error.startsWith ("Nothing to export"));

        const auto name = audio::getStemFileName ("My:Song", 3, "Kick/Snare");
        CHECK_FALSE (name.containsAnyOf (":/\\\\"));
        CHECK (name.endsWith (".wav"));
        CHECK (name.contains ("03"));
        CHECK (audio::getStemFileName ("Song", 1, "  ") == "Song - 01 Track.wav");
    }

    TEST_CASE ("exporting changes nothing in the project: no step to undo, nothing unsaved, mute and solo as they were")
    {
        Band band;
        band.bass->setSolo (true);
        band.project->clearUndoHistory();

        REQUIRE (audio::exportWav (band.project->getEdit(), band.temp.getFile ("Mix.wav")).isEmpty());
        REQUIRE (audio::exportStems (band.project->getEdit(), band.temp.getFile ("Stems"), "Song").error.isEmpty());

        CHECK_FALSE (band.project->canUndo());
        CHECK_FALSE (band.project->hasUnsavedChanges());
        CHECK (band.muted->isMuted (false));
        CHECK (band.bass->isSolo (false));
        CHECK_FALSE (band.lead->isSoloIsolate (false));
    }
}
