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

    //==============================================================================
    namespace
    {
        te::TimePosition toTime (const te::Edit& edit, double beat)
        {
            return edit.tempoSequence.toTime (te::BeatPosition::fromBeats (beat));
        }
    }

    juce::Array<te::MidiClip*> getMidiClips (te::AudioTrack& track)
    {
        juce::Array<te::MidiClip*> clips;

        for (auto* clip : track.getClips())
            if (auto* midi = dynamic_cast<te::MidiClip*> (clip))
                clips.add (midi);

        std::stable_sort (clips.begin(), clips.end(), [] (te::MidiClip* a, te::MidiClip* b)
                          { return getStartBeat (*a) < getStartBeat (*b); });
        return clips;
    }

    bool isInEdit (const te::Clip& clip)
    {
        return clip.state.isAChildOf (clip.edit.state);
    }

    double getSongEndBeat (const te::Edit& edit)
    {
        auto end = 0.0;

        for (auto* track : te::getClipTracks (edit))
            for (auto* clip : track->getClips())
                end = std::max (end, getEndBeat (*clip));

        return end;
    }

    void move (te::MidiClip& clip, double startBeat, te::AudioTrack& track)
    {
        clip.setStart (toTime (clip.edit, std::max (0.0, startBeat)), false, true);

        if (clip.getClipTrack() != &track)
            clip.moveTo (track);
    }

    void setStart (te::MidiClip& clip, double startBeat, double minLengthBeats)
    {
        const auto limited = juce::jlimit (getContentStartBeat (clip), std::max (getContentStartBeat (clip), getEndBeat (clip) - minLengthBeats),
                                           startBeat);
        clip.setStart (toTime (clip.edit, limited), true, false);
    }

    void setEnd (te::MidiClip& clip, double endBeat, double minLengthBeats)
    {
        clip.setEnd (toTime (clip.edit, std::max (endBeat, getStartBeat (clip) + minLengthBeats)), true);
    }

    void remove (const juce::Array<te::MidiClip*>& clips)
    {
        // Who is left in each group once these are gone.
        juce::Array<te::MidiClip*> survivors;

        for (auto* clip : clips)
        {
            if (! clip_links::isLinked (*clip))
                continue;

            for (auto* member : clip_links::getGroup (*clip))
                if (! clips.contains (member))
                    survivors.addIfNotAlreadyThere (member);
        }

        for (auto* clip : clips)
        {
            te::Clip::Ptr keepAlive (clip);
            clip->removeFromParent();
        }

        for (auto* survivor : survivors)
            if (clip_links::getGroup (*survivor).size() == 1)
                clip_links::unlink (*survivor);
    }
}
