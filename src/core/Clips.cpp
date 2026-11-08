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

    juce::Array<te::Clip*> getClips (te::AudioTrack& track)
    {
        juce::Array<te::Clip*> result;

        for (auto* clip : track.getClips())
            if (dynamic_cast<te::MidiClip*> (clip) != nullptr || dynamic_cast<te::WaveAudioClip*> (clip) != nullptr)
                result.add (clip);

        std::stable_sort (result.begin(), result.end(), [] (te::Clip* a, te::Clip* b)
                          { return getStartBeat (*a) < getStartBeat (*b); });
        return result;
    }

    bool isAudio (const te::Clip& clip)
    {
        return dynamic_cast<const te::WaveAudioClip*> (&clip) != nullptr;
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

    void move (te::Clip& clip, double startBeat, te::AudioTrack& track)
    {
        clip.setStart (toTime (clip.edit, std::max (0.0, startBeat)), false, true);

        if (clip.getClipTrack() != &track)
            clip.moveTo (track);
    }

    void setStart (te::Clip& clip, double startBeat, double minLengthBeats)
    {
        const auto limited = juce::jlimit (getContentStartBeat (clip), std::max (getContentStartBeat (clip), getEndBeat (clip) - minLengthBeats),
                                           startBeat);
        clip.setStart (toTime (clip.edit, limited), true, false);
    }

    void setEnd (te::Clip& clip, double endBeat, double minLengthBeats)
    {
        auto end = toTime (clip.edit, std::max (endBeat, getStartBeat (clip) + minLengthBeats));

        // An audio clip can't play past the end of its file.
        if (auto* audio = dynamic_cast<te::AudioClipBase*> (&clip); audio != nullptr && ! audio->isLooping())
            end = std::min (end, clip.getPosition().getStartOfSource() + audio->getMaximumLength());

        clip.setEnd (end, true);
    }

    te::Clip* duplicate (te::Clip& clip, double startBeat, bool linked)
    {
        if (auto* midi = dynamic_cast<te::MidiClip*> (&clip))
            return clip_links::duplicate (*midi, startBeat, linked);

        auto copy = te::duplicateClip (clip);

        if (copy == nullptr)
            return nullptr;

        copy->setStart (toTime (clip.edit, startBeat), false, true);
        return copy.get();
    }

    void remove (const juce::Array<te::Clip*>& clips)
    {
        // Who is left in each group once these are gone.
        juce::Array<te::MidiClip*> survivors;

        for (auto* clip : clips)
        {
            auto* midi = dynamic_cast<te::MidiClip*> (clip);

            if (midi == nullptr || ! clip_links::isLinked (*midi))
                continue;

            for (auto* member : clip_links::getGroup (*midi))
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
