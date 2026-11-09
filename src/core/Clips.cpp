#include "core/Clips.h"
#include "core/Tracks.h"
#include "core/ClipLinks.h"
#include "core/Controllers.h"
#include "core/NoteEditor.h"

namespace harmonia::core::clips
{
    namespace
    {
        te::TimePosition timeAt (const te::Edit& edit, double beat)
        {
            return edit.tempoSequence.toTime (te::BeatPosition::fromBeats (beat));
        }

        double beatAt (const te::Edit& edit, te::TimePosition time)
        {
            return edit.tempoSequence.toBeats (time).inBeats();
        }

        const juce::Identifier colourId ("hClipColour");

        te::AudioFadeCurve::Type toEngine (FadeShape shape)
        {
            switch (shape)
            {
                case FadeShape::sCurve:     return te::AudioFadeCurve::sCurve;
                case FadeShape::fast:       return te::AudioFadeCurve::convex;
                case FadeShape::slow:       return te::AudioFadeCurve::concave;
                case FadeShape::linear:
                default:                    return te::AudioFadeCurve::linear;
            }
        }
    }

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
        return clip.getContentStartBeat().inBeats() - getLoopRange (clip).getStart();
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
            if (isLooping (*member))
            {
                // A longer loop, in whole bars from where it starts.
                const auto loop = getLoopRange (*member);

                if (contentEndBeat > loop.getEnd() + 1.0e-9)
                {
                    const auto bars = std::ceil ((contentEndBeat - loop.getStart()) / beatsPerBar - 1.0e-9);
                    setLoopLengthBeats (*member, bars * beatsPerBar);
                }

                continue;
            }

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
        te::TimePosition toTime (const te::Edit& edit, double beat);

        /** Sets a looping clip starting at `startBeat` so that a time round of its loop begins at
            song beat `loopStart` (or a whole number of loops from it): the offset into the loop. */
        void setLoopOffset (te::Clip& clip, double startBeat, double loopStart)
        {
            const auto length = getLoopRange (clip).getLength();

            if (length <= 0.0)
                return;

            auto offset = std::fmod (startBeat - loopStart, length);

            if (offset < 0.0)
                offset += length;

            if (offset > length - 1.0e-9)
                offset = 0.0;

            const auto start = toTime (clip.edit, startBeat);
            clip.setOffset (start - toTime (clip.edit, startBeat - offset));
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
        if (isLooping (clip))
        {
            // The loop goes on before the clip too: any start, the repeats staying where they are.
            const auto loop = getLoopSongRange (clip);
            const auto start = juce::jlimit (0.0, std::max (0.0, getEndBeat (clip) - minLengthBeats), startBeat);
            clip.setStart (toTime (clip.edit, start), false, false);
            setLoopOffset (clip, start, loop.getStart());
            return;
        }

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

    te::Clip* split (te::Clip& clip, double beat)
    {
        // Not right at an edge: both halves must be something.
        constexpr double minPartBeats = 1.0e-3;

        if (beat <= getStartBeat (clip) + minPartBeats || beat >= getEndBeat (clip) - minPartBeats)
            return nullptr;

        auto* second = te::split (clip, toTime (clip.edit, beat));

        // Each half of a looping clip still shows its first time round.
        if (second != nullptr && isLooping (*second))
            setLoopOffset (*second, getStartBeat (*second), getLoopSongRange (clip).getStart());

        return second;
    }

    bool canTake (const te::AudioTrack& track, const te::ClipCopy& copy)
    {
        const auto& state = copy.getState();

        if (tracks::isBus (track))
            return false;

        if (state.hasType (te::IDs::AUDIOCLIP))
            return tracks::isAudio (track);

        return state.hasType (te::IDs::MIDICLIP) && ! tracks::isAudio (track);
    }

    te::Clip* insertCopy (te::AudioTrack& track, const te::ClipCopy& copy, double startBeat)
    {
        if (! canTake (track, copy))
            return nullptr;

        auto* clip = te::insertClipCopy (track, copy.withNewItemID (track.edit));

        if (clip == nullptr)
            return nullptr;

        if (auto* midi = dynamic_cast<te::MidiClip*> (clip); midi != nullptr && clip_links::isLinked (*midi))
            clip_links::unlink (*midi);

        clip->setStart (toTime (track.edit, std::max (0.0, startBeat)), false, true);
        return clip;
    }

    //==============================================================================
    const std::vector<FadeShape>& getFadeShapes()
    {
        static const std::vector<FadeShape> shapes { FadeShape::linear, FadeShape::sCurve, FadeShape::fast, FadeShape::slow };
        return shapes;
    }

    juce::String getFadeShapeName (FadeShape shape)
    {
        switch (shape)
        {
            case FadeShape::sCurve:     return "S-curve";
            case FadeShape::fast:       return "Fast";
            case FadeShape::slow:       return "Slow";
            case FadeShape::linear:
            default:                    return "Linear";
        }
    }

    double getFadeInBeats (const te::AudioClipBase& clip)
    {
        const auto start = clip.getPosition().getStart();
        return beatAt (clip.edit, start + clip.getFadeIn()) - beatAt (clip.edit, start);
    }

    double getFadeOutBeats (const te::AudioClipBase& clip)
    {
        const auto end = clip.getPosition().getEnd();
        return beatAt (clip.edit, end) - beatAt (clip.edit, end - clip.getFadeOut());
    }

    void setFadeInBeats (te::AudioClipBase& clip, double beats)
    {
        const auto position = clip.getPosition();
        const auto start = position.getStart();
        const auto room = position.getLength() - clip.getFadeOut();
        auto length = timeAt (clip.edit, beatAt (clip.edit, start) + std::max (0.0, beats)) - start;
        length = juce::jlimit (te::TimeDuration(), std::max (te::TimeDuration(), room), length);
        clip.setFadeIn (length);
    }

    void setFadeOutBeats (te::AudioClipBase& clip, double beats)
    {
        const auto position = clip.getPosition();
        const auto end = position.getEnd();
        const auto room = position.getLength() - clip.getFadeIn();
        auto length = end - timeAt (clip.edit, beatAt (clip.edit, end) - std::max (0.0, beats));
        length = juce::jlimit (te::TimeDuration(), std::max (te::TimeDuration(), room), length);
        clip.setFadeOut (length);
    }

    FadeShape getFadeShape (const te::AudioClipBase& clip)
    {
        for (auto shape : getFadeShapes())
            if (toEngine (shape) == clip.getFadeInType())
                return shape;

        return FadeShape::linear;
    }

    void setFadeShape (te::AudioClipBase& clip, FadeShape shape)
    {
        clip.setFadeInType (toEngine (shape));
        clip.setFadeOutType (toEngine (shape));
    }

    float getGainDb (const te::AudioClipBase& clip)
    {
        return clip.getGainDB();
    }

    void setGainDb (te::AudioClipBase& clip, float db)
    {
        clip.setGainDB (juce::jlimit (silentGainDb, maxGainDb, db));
    }

    std::optional<float> getPeakDb (const te::AudioClipBase& clip)
    {
        // A reversed clip plays the original backwards: read the same stretch of the original.
        const auto reversed = clip.getIsReversed();
        const auto file = reversed ? clip.getOriginalFile() : clip.getCurrentSourceFile();
        auto& formats = clip.edit.engine.getAudioFileFormatManager().readFormatManager;
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
            return std::nullopt;

        const auto position = clip.getPosition();
        const auto rate = reader->sampleRate;
        const auto length = (juce::int64) std::ceil (position.getLength().inSeconds() * rate);
        auto start = (juce::int64) std::floor (position.getOffset().inSeconds() * rate);

        if (reversed)
            start = reader->lengthInSamples - start - length;

        const auto range = juce::Range<juce::int64> (start, start + length).getIntersectionWith ({ 0, reader->lengthInSamples });

        if (range.isEmpty())
            return silentGainDb;

        const auto channels = (int) reader->numChannels;
        std::vector<juce::Range<float>> levels ((size_t) channels);
        reader->readMaxLevels (range.getStart(), range.getLength(), levels.data(), channels);

        float peak = 0.0f;

        for (const auto& level : levels)
            peak = std::max ({ peak, std::abs (level.getStart()), std::abs (level.getEnd()) });

        return juce::Decibels::gainToDecibels (peak, silentGainDb);
    }

    bool normalize (te::AudioClipBase& clip, float targetDb)
    {
        const auto peak = getPeakDb (clip);

        if (! peak.has_value() || *peak <= silentGainDb + 1.0f)
            return false;

        clip.setGainDB (juce::jlimit (silentGainDb, maxNormalizeGainDb, targetDb - *peak));
        return true;
    }

    bool isReversed (const te::AudioClipBase& clip)
    {
        return clip.getIsReversed();
    }

    void setReversed (te::AudioClipBase& clip, bool shouldBeReversed)
    {
        if (clip.getIsReversed() != shouldBeReversed)
            clip.setIsReversed (shouldBeReversed);
    }

    bool isReversing (const te::AudioClipBase& clip)
    {
        return clip.getIsReversed() && ! clip.getCurrentSourceFile().existsAsFile();
    }

    //==============================================================================
    bool isLooping (const te::Clip& clip)
    {
        return dynamic_cast<const te::MidiClip*> (&clip) != nullptr && clip.isLooping();
    }

    juce::Range<double> getLoopRange (const te::Clip& clip)
    {
        if (! isLooping (clip))
            return {};

        const auto start = clip.getLoopStartBeats().inBeats();
        return { start, start + clip.getLoopLengthBeats().inBeats() };
    }

    juce::Range<double> getLoopSongRange (const te::Clip& clip)
    {
        if (! isLooping (clip))
            return {};

        const auto loop = getLoopRange (clip);
        const auto first = getContentStartBeat (clip) + loop.getStart();
        return { first, first + loop.getLength() };
    }

    void setLooping (te::MidiClip& clip, bool shouldLoop)
    {
        if (shouldLoop == isLooping (clip))
            return;

        const auto start = getStartBeat (clip);

        if (shouldLoop)
        {
            // What shows now: from the content at the clip's start, as long as the clip.
            const auto from = std::max (0.0, start - getContentStartBeat (clip));
            const auto length = std::max (1.0 / 16.0, getEndBeat (clip) - start);
            clip.setLoopRangeBeats ({ te::BeatPosition::fromBeats (from), te::BeatDuration::fromBeats (length) });
            clip.setOffset ({});
        }
        else
        {
            // The content plays once more, as it does in the loop's first time round.
            const auto contentStart = getContentStartBeat (clip);
            clip.setLoopRangeBeats ({});
            clip.setOffset (clip.getPosition().getStart() - toTime (clip.edit, std::min (start, contentStart)));
        }
    }

    void setLoopLengthBeats (te::MidiClip& clip, double lengthBeats)
    {
        if (! isLooping (clip))
            return;

        const auto loop = getLoopRange (clip);
        const auto songLoop = getLoopSongRange (clip);
        const auto length = std::max (1.0 / 16.0, lengthBeats);
        clip.setLoopRangeBeats ({ te::BeatPosition::fromBeats (loop.getStart()), te::BeatDuration::fromBeats (length) });

        // The first time round stays where it was.
        setLoopOffset (clip, getStartBeat (clip), songLoop.getStart());

        // At least one whole time round.
        if (getEndBeat (clip) < songLoop.getStart() + length - 1.0e-9)
            clip.setEnd (toTime (clip.edit, songLoop.getStart() + length), true);
    }

    std::vector<PlayedNote> getPlayedNotes (const te::MidiClip& clip)
    {
        std::vector<PlayedNote> played;
        const auto clipStart = getStartBeat (clip), clipEnd = getEndBeat (clip);
        const auto contentStart = getContentStartBeat (clip);
        constexpr double tiny = 1.0e-9;

        const auto add = [&] (const Note& note, double start, double end, int repeat)
        {
            if (end <= clipStart + tiny || start >= clipEnd - tiny)
                return;

            PlayedNote p;
            p.note = note;
            p.cutAtClipStart = start < clipStart - tiny;
            p.startBeat = std::max (start, clipStart);
            p.endBeat = std::min (end, clipEnd);
            p.repeat = repeat;
            played.push_back (p);
        };

        const auto& notes = clip.getSequence().getNotes();

        if (! isLooping (clip))
        {
            for (auto* n : notes)
            {
                const auto note = note::read (*n);
                add (note, contentStart + note.startBeat, contentStart + note.getEndBeat(), 0);
            }

            return played;
        }

        // As Tracktion plays a loop: a note sticking out of the loop's start begins there, one
        // going past its end stops there, and the notes outside it don't play.
        const auto loop = getLoopRange (clip);
        const auto length = loop.getLength();
        const auto first = contentStart + loop.getStart();
        const auto firstRepeat = (int) std::floor ((clipStart - first) / length + tiny);
        const auto lastRepeat = (int) std::ceil ((clipEnd - first) / length - tiny);

        std::vector<Note> inLoop;

        for (auto* n : notes)
        {
            auto note = note::read (*n);
            auto start = std::max (note.startBeat, loop.getStart());
            const auto end = std::min (note.getEndBeat(), loop.getEnd());

            if (end - start > tiny && start < loop.getEnd() - tiny)
            {
                note.lengthBeats = end - start;
                note.startBeat = start;
                inLoop.push_back (note);
            }
        }

        for (int repeat = firstRepeat; repeat < lastRepeat; ++repeat)
        {
            const auto shift = first + repeat * length - loop.getStart();

            for (const auto& note : inLoop)
                add (note, shift + note.startBeat, shift + note.getEndBeat(), repeat);
        }

        return played;
    }

    void bakeLoop (te::MidiClip& clip)
    {
        if (! isLooping (clip))
            return;

        if (clip_links::isLinked (clip))
            clip_links::unlink (clip);

        const auto played = getPlayedNotes (clip);
        const auto start = getStartBeat (clip);

        // The wheels and the pedal as well.
        std::vector<std::pair<controllers::Controller, std::vector<controllers::Point>>> moves;

        for (auto c : controllers::getAll())
        {
            auto points = controllers::getPlayed (clip, c);

            for (auto& p : points)
                p.beat -= start;

            if (controllers::hasPoints (clip, c))
                moves.emplace_back (c, std::move (points));
        }

        // Each note written where it plays, from the clip's start (one cut by the clip's start
        // doesn't play).
        std::vector<Note> notes;

        for (const auto& p : played)
        {
            if (p.cutAtClipStart)
                continue;

            auto note = p.note;
            note.startBeat = p.startBeat - start;
            note.lengthBeats = p.endBeat - p.startBeat;
            notes.push_back (note);
        }

        clip.setLoopRangeBeats ({});
        clip.setOffset ({});

        NoteEditor editor (clip);
        juce::Array<te::MidiNote*> old;

        for (auto* n : clip.getSequence().getNotes())
            old.add (n);

        editor.remove (old);
        editor.add (notes);

        for (const auto& [c, points] : moves)
            controllers::set (clip, c, points);
    }

    //==============================================================================
    std::optional<juce::Colour> getColour (const te::Clip& clip)
    {
        const auto text = clip.state[colourId].toString();

        if (text.isEmpty())
            return std::nullopt;

        return juce::Colour::fromString (text);
    }

    void setColour (te::Clip& clip, std::optional<juce::Colour> colour)
    {
        auto* undoManager = &clip.edit.getUndoManager();

        if (colour.has_value() && ! colour->isTransparent())
            clip.state.setProperty (colourId, colour->toString(), undoManager);
        else
            clip.state.removeProperty (colourId, undoManager);
    }
}
