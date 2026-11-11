#include "core/TakeClips.h"
#include "core/Clips.h"
#include "core/NoteEditor.h"
#include "core/Snap.h"
#include "core/Tracks.h"

namespace harmonia::core
{
    namespace
    {
        constexpr double epsilon = 1.0e-9;
        constexpr double shortestClipBeats = 1.0 / 16.0;

        double floorToBar (double beat, double bar)     { return std::floor (beat / bar + epsilon) * bar; }
        double ceilToBar (double beat, double bar)      { return std::ceil (beat / bar - epsilon) * bar; }
    }

    TakeClips::TakeClips (Project& p, te::AudioTrack& t)
        : project (p), track (&t)
    {
    }

    bool TakeClips::canRecordOn (const te::AudioTrack& t)
    {
        return tracks::getKind (t) == tracks::Kind::instrument && t.state.isAChildOf (t.edit.state);
    }

    bool TakeClips::isMade (const te::MidiClip* clip) const
    {
        return clip != nullptr && made.contains (clip) && clips::isInEdit (*clip);
    }

    juce::Array<te::MidiClip*> TakeClips::getMadeClips() const
    {
        juce::Array<te::MidiClip*> still;

        for (auto* clip : made)
            if (clips::isInEdit (*clip))
                still.add (clip);

        return still;
    }

    //==============================================================================
    te::MidiClip* TakeClips::clipFor (double beat, double endBeat)
    {
        if (! canRecordOn (*track))
            return nullptr;

        // The clip that plays at the beat; or the empty stretch the beat is in, from the clip
        // before it to the clip after it.
        te::MidiClip* playing = nullptr;
        te::MidiClip* before = nullptr;
        te::MidiClip* after = nullptr;

        for (auto* clip : clips::getMidiClips (*track))
        {
            const auto start = clips::getStartBeat (*clip), end = clips::getEndBeat (*clip);

            if (beat >= start - epsilon && beat < end - epsilon)
            {
                if (playing == nullptr)
                    playing = clip;
            }
            else if (end <= beat + epsilon)
            {
                if (before == nullptr || end > clips::getEndBeat (*before))
                    before = clip;
            }
            else if (after == nullptr || start < clips::getStartBeat (*after))
            {
                after = clip;
            }
        }

        const auto bar = clips::getBeatsPerBar (project.getEdit());
        const auto from = before != nullptr ? clips::getEndBeat (*before) : 0.0;
        const auto to = after != nullptr ? clips::getStartBeat (*after) : std::numeric_limits<double>::max();
        const auto start = std::max (from, floorToBar (beat, bar));
        const auto end = std::min (to, std::max (ceilToBar (endBeat, bar), floorToBar (beat, bar) + bar));

        if (playing != nullptr)
        {
            // A note that goes on past the end of the take's own clip: the clip grows with it.
            // In a clip that was there, the note stops where the clip does.
            if (isMade (playing) && end > clips::getEndBeat (*playing) + epsilon)
                clips::setEnd (*playing, end, shortestClipBeats);

            return playing;
        }

        // The take's own clip next to it grows to hold it.
        if (isMade (before))
        {
            clips::setEnd (*before, end, shortestClipBeats);
            return before;
        }

        if (isMade (after))
        {
            // Played before the clip's first bar (the song went round a loop, or a key held
            // from earlier came up): room at the clip's beginning, its notes staying where they
            // are in the song. Nothing is linked to a clip the take has just made.
            after->extendStart (project.getEdit().tempoSequence.toTime (te::BeatPosition::fromBeats (start)));

            // Now it plays at the beat: if the note goes on past its end as well, it grows there too.
            return clips::getStartBeat (*after) <= beat + epsilon ? clipFor (beat, endBeat) : after;
        }

        if (end - start < shortestClipBeats)
            return nullptr;

        auto* clip = project.addMidiClip (*track, start, end - start, track->getName());

        if (clip != nullptr)
            made.add (clip);

        return clip;
    }

    TakeClips::Place TakeClips::placeFor (double beat, double endBeat)
    {
        beat = std::max (0.0, beat);
        auto* clip = clipFor (beat, std::max (beat, endBeat));

        if (clip == nullptr)
            return {};

        // A looping clip: what is played in any of its times round belongs to the loop.
        if (clips::isLooping (*clip))
        {
            const auto loop = clips::getLoopRange (*clip);
            const auto length = loop.getLength();

            if (length <= 0.0)
                return {};

            auto offset = std::fmod (beat - clips::getLoopSongRange (*clip).getStart(), length);

            if (offset < 0.0)
                offset += length;

            if (offset > length - epsilon)
                offset = 0.0;

            return { clip, loop.getStart() + offset, length - offset };
        }

        return { clip, beat - clips::getContentStartBeat (*clip), clips::getEndBeat (*clip) - beat };
    }

    //==============================================================================
    te::MidiClip* TakeClips::addNote (const Note& songNote)
    {
        const auto place = placeFor (songNote.startBeat, songNote.getEndBeat());

        if (place.clip == nullptr)
            return nullptr;

        auto note = songNote;
        note.startBeat = snap::roundToTicks (std::max (0.0, place.contentBeat));
        note.lengthBeats = std::min (note.lengthBeats, place.roomBeats);

        // The same part played again on the loop's next time round doesn't stack copies.
        constexpr auto sameStart = 1.0 / snap::ticksPerBeat;

        for (auto* existing : place.clip->getSequence().getNotes())
            if (existing->getNoteNumber() == note.pitch && std::abs (existing->getStartBeat().inBeats() - note.startBeat) < sameStart)
                return place.clip;

        NoteEditor (*place.clip).add (note);
        return place.clip;
    }

    te::MidiClip* TakeClips::addController (controllers::Controller controller, double songBeat, float value)
    {
        const auto place = placeFor (songBeat, songBeat);

        if (place.clip != nullptr)
            controllers::add (*place.clip, controller, std::max (0.0, place.contentBeat), value);

        return place.clip;
    }
}
