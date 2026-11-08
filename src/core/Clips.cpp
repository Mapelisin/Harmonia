#include "core/Clips.h"
#include "core/ClipLinks.h"

namespace harmonia::core::clips
{
    double getBeatsPerBar (const te::Edit& edit)
    {
        auto& timeSig = edit.tempoSequence.getTimeSigAt (te::BeatPosition());
        const int numerator = timeSig.numerator;
        const int denominator = timeSig.denominator;

        if (numerator <= 0 || denominator <= 0)
            return 4.0;

        return numerator * 4.0 / denominator;
    }

    double getContentStartBeat (const te::Clip& clip)
    {
        return clip.getContentStartBeat().inBeats();
    }

    double getStartBeat (const te::Clip& clip)
    {
        return clip.getStartBeat().inBeats();
    }

    double getEndBeat (const te::Clip& clip)
    {
        return clip.getEndBeat().inBeats();
    }

    te::MidiClip* getMainClip (te::AudioTrack& track)
    {
        for (auto* clip : track.getClips())
            if (auto* midi = dynamic_cast<te::MidiClip*> (clip))
                return midi;

        return nullptr;
    }

    te::MidiClip* getOrAddMainClip (Project& project, te::AudioTrack& track, double lengthBeats)
    {
        if (auto* existing = getMainClip (track))
            return existing;

        return project.addMidiClip (track, 0.0, lengthBeats, track.getName());
    }

    void extendToCover (te::MidiClip& clip, double contentEndBeat)
    {
        const auto beatsPerBar = getBeatsPerBar (clip.edit);
        auto& tempo = clip.edit.tempoSequence;

        for (auto* member : clip_links::getGroup (clip))
        {
            const auto contentStart = getContentStartBeat (*member);
            const auto neededEnd = contentStart + contentEndBeat;

            if (neededEnd <= getEndBeat (*member) + 1.0e-9)
                continue;

            // Round up to a whole bar, counted from the clip's start.
            const auto start = getStartBeat (*member);
            const auto bars = std::ceil ((neededEnd - start) / beatsPerBar - 1.0e-9);
            const auto newEnd = start + bars * beatsPerBar;

            member->setEnd (tempo.toTime (te::BeatPosition::fromBeats (newEnd)), false);
        }
    }
}
