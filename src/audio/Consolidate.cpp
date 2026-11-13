#include "audio/Consolidate.h"
#include "audio/Export.h"
#include "core/AudioTempo.h"
#include "core/Clips.h"
#include "core/Tracks.h"

namespace harmonia::audio::consolidate
{
    namespace clips = core::clips;

    namespace
    {
        // A clip reaching less than this into the stretch isn't in it.
        constexpr double smallestPieceBeats = 1.0 / 64.0;

        /** The clips of the track in the stretch, or reaching into it, in time order. */
        juce::Array<te::Clip*> getClipsIn (te::AudioTrack& track, double fromBeat, double toBeat)
        {
            juce::Array<te::Clip*> in;

            for (auto* clip : clips::getClips (track))
                if (std::min (clips::getEndBeat (*clip), toBeat) - std::max (clips::getStartBeat (*clip), fromBeat) >= smallestPieceBeats)
                    in.add (clip);

            return in;
        }

        te::TimePosition toTime (te::Edit& edit, double beat)
        {
            return edit.tempoSequence.toTime (te::BeatPosition::fromBeats (beat));
        }

        /** Where the song's beats are in a file rendered from `fromBeat` to `toBeat` of it (seconds
            from the file's start): what a clip of that file needs to stay on those beats. */
        core::beat_finder::Beat getSongBeats (te::Edit& edit, double fromBeat, double toBeat)
        {
            core::beat_finder::Beat beat;
            const auto start = toTime (edit, fromBeat);

            // A beat past the end too: the tempo at the end is the tempo after it.
            for (auto b = fromBeat; b < toBeat + 1.0; b += 1.0)
                beat.beatSeconds.push_back ((toTime (edit, b) - start).inSeconds());

            beat.bpm = 60.0 * (double) (beat.beatSeconds.size() - 1) / beat.beatSeconds.back();
            return beat;
        }

        te::Clip* joinSound (core::Project& project, const Piece& piece)
        {
            auto& track = *piece.track;
            const auto old = getClipsIn (track, piece.fromBeat, piece.toBeat);

            if (old.isEmpty() || ! piece.sound.existsAsFile())
                return nullptr;

            const auto name = old.getFirst()->getName();
            const auto colour = clips::getColour (*old.getFirst());

            clips::remove (clips::isolate (track, piece.fromBeat, piece.toBeat, smallestPieceBeats));

            juce::String error;
            auto* joined = project.addAudioClip (track, piece.sound, piece.fromBeat, error);

            if (joined == nullptr)
                return nullptr;

            joined->setName (name);
            clips::setColour (*joined, colour);

            if (piece.followsTempo)
                core::audio_tempo::setFollowsTempo (*joined, getSongBeats (track.edit, piece.fromBeat, piece.toBeat));

            return joined;
        }

        te::Clip* joinNotes (core::Project& project, const Piece& piece)
        {
            auto& track = *piece.track;
            auto& edit = track.edit;

            // The clips cut at the edges of the stretch: what is between is what is joined.
            juce::Array<te::MidiClip*> old;

            for (auto* clip : clips::isolate (track, piece.fromBeat, piece.toBeat, smallestPieceBeats))
                if (auto* midi = dynamic_cast<te::MidiClip*> (clip))
                    old.add (midi);

            if (old.isEmpty())
                return nullptr;

            // Each clip's notes and controllers as it plays them (its repeats written out), in
            // beats from the start of the stretch.
            auto* first = old.getFirst();
            te::MidiList all;
            all.setMidiChannel (first->getMidiChannel());
            const auto from = te::BeatPosition::fromBeats (piece.fromBeat);

            for (auto* clip : old)
            {
                te::MidiList played;
                played.copyFrom (clip->getSequenceLooped(), nullptr);

                const auto position = clip->getPosition();
                const auto offset = te::BeatDuration::fromBeats (position.getOffset().inSeconds()
                                                                     * edit.tempoSequence.getBeatsPerSecondAt (position.getStart(), true));
                played.trimOutside (te::toPosition (offset), te::toPosition (offset + clip->getLengthInBeats()), nullptr);
                played.moveAllBeatPositions (clip->getStartBeat() - from - offset, nullptr);
                all.addFrom (played, nullptr);
            }

            const auto name = first->getName();
            const auto colour = clips::getColour (*first);
            const auto channel = first->getMidiChannel();

            juce::Array<te::Clip*> gone;

            for (auto* clip : old)
                gone.add (clip);

            clips::remove (gone);

            auto* joined = project.addMidiClip (track, piece.fromBeat, piece.toBeat - piece.fromBeat, name);

            if (joined == nullptr)
                return nullptr;

            joined->setMidiChannel (channel);
            joined->getSequence().addFrom (all, &edit.getUndoManager());
            clips::setColour (*joined, colour);
            return joined;
        }
    }

    int countClips (te::AudioTrack& track, double fromBeat, double toBeat)
    {
        return getClipsIn (track, fromBeat, toBeat).size();
    }

    namespace
    {
        class Preparation final : public PrepareJob
        {
        public:
            Preparation (te::AudioTrack& t, double from, double to)
                : track (t), fromBeat (from), toBeat (to)
            {
                begin (run());
            }

            ~Preparation() override
            {
                stop();
            }

        private:
            te::AudioTrack& track;
            const double fromBeat, toBeat;

            Steps run()
            {
                const auto in = getClipsIn (track, fromBeat, toBeat);

                if (in.isEmpty() || toBeat - fromBeat < smallestPieceBeats)
                    co_return;

                Piece piece { &track, fromBeat, toBeat, {}, false };

                if (core::tracks::isAudio (track))
                {
                    for (auto* clip : in)
                        if (auto* audio = dynamic_cast<te::AudioClipBase*> (clip))
                            piece.followsTempo = piece.followsTempo || core::audio_tempo::followsTempo (*audio);

                    auto& edit = track.edit;
                    piece.sound = edit.engine.getEngineBehaviour().getFileForNewAudioRecording (track, ".wav");
                    say ("Consolidating " + track.getName());

                    const auto render = ExportJob::clips (track, piece.sound, { toTime (edit, fromBeat), toTime (edit, toBeat) });
                    co_yield *render;

                    if (render->getError().isNotEmpty())
                    {
                        fail (render->getError());
                        co_return;
                    }
                }

                result = piece;
            }
        };
    }

    std::unique_ptr<PrepareJob> makePrepareJob (te::AudioTrack& track, double fromBeat, double toBeat)
    {
        return std::make_unique<Preparation> (track, fromBeat, toBeat);
    }

    std::optional<Piece> prepare (te::AudioTrack& track, double fromBeat, double toBeat, juce::String& error)
    {
        const auto job = makePrepareJob (track, fromBeat, toBeat);
        job->finish();
        error = job->getError();
        return job->getResult();
    }

    te::Clip* apply (core::Project& project, const Piece& piece)
    {
        if (piece.track == nullptr)
            return nullptr;

        return core::tracks::isAudio (*piece.track) ? joinSound (project, piece) : joinNotes (project, piece);
    }
}
