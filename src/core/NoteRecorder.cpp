#include "core/NoteRecorder.h"
#include "core/Snap.h"

namespace harmonia::core
{
    const std::vector<InputQuantize>& getAllInputQuantizeModes()
    {
        static const std::vector<InputQuantize> modes { InputQuantize::off, InputQuantize::starts,
                                                        InputQuantize::startsAndEnds };
        return modes;
    }

    juce::String getName (InputQuantize q)
    {
        switch (q)
        {
            case InputQuantize::off:            return "As played";
            case InputQuantize::starts:         return "Quantize starts";
            case InputQuantize::startsAndEnds:  return "Quantize starts and ends";
        }

        return {};
    }

    //==============================================================================
    void NoteRecorder::start (note_ops::BeatRange loopRange)
    {
        active = true;
        sustain = false;
        loop = loopRange;
        sounding.clear();
    }

    std::optional<Note> NoteRecorder::noteOn (int pitch, int velocity, double beat)
    {
        if (! active)
            return {};

        std::optional<Note> previous;
        const auto it = std::find_if (sounding.begin(), sounding.end(), [pitch] (const Sounding& s) { return s.pitch == pitch; });

        if (it != sounding.end())
        {
            previous = finish (*it, beat);
            sounding.erase (it);
        }

        sounding.push_back ({ pitch, velocity, beat, false });
        return previous;
    }

    std::optional<Note> NoteRecorder::noteOff (int pitch, double beat)
    {
        const auto it = std::find_if (sounding.begin(), sounding.end(),
                                      [pitch] (const Sounding& s) { return s.pitch == pitch && ! s.released; });

        if (it == sounding.end())
            return {};

        if (sustain)
        {
            it->released = true;
            return {};
        }

        const auto note = finish (*it, beat);
        sounding.erase (it);
        return note;
    }

    std::vector<Note> NoteRecorder::setSustain (bool down, double beat)
    {
        sustain = down;
        std::vector<Note> finished;

        if (! down)
        {
            for (const auto& s : sounding)
                if (s.released)
                    finished.push_back (finish (s, beat));

            std::erase_if (sounding, [] (const Sounding& s) { return s.released; });
        }

        return finished;
    }

    std::vector<Note> NoteRecorder::stop (double beat)
    {
        std::vector<Note> finished;

        for (const auto& s : sounding)
            finished.push_back (finish (s, beat));

        sounding.clear();
        active = false;
        sustain = false;
        return finished;
    }

    std::vector<Note> NoteRecorder::getSoundingNotes (double beat) const
    {
        std::vector<Note> notes;

        for (const auto& s : sounding)
            notes.push_back (finish (s, beat));

        return notes;
    }

    double NoteRecorder::getEnd (double startBeat, double beat) const
    {
        // Earlier than the start: the loop wrapped while the key was held.
        if (beat < startBeat)
            return loop.getLength() > 0.0 ? loop.end : startBeat;

        if (loop.getLength() > 0.0)
            return std::min (beat, loop.end);

        return beat;
    }

    Note NoteRecorder::finish (const Sounding& s, double beat) const
    {
        Note n;
        n.pitch = s.pitch;
        n.velocity = s.velocity;
        n.startBeat = s.startBeat;
        n.lengthBeats = std::max (minLengthBeats, getEnd (s.startBeat, beat) - s.startBeat);
        return n;
    }

    //==============================================================================
    Note quantizeRecorded (const Note& n, InputQuantize mode, double gridBeats, note_ops::BeatRange loop)
    {
        auto q = mode == InputQuantize::off || gridBeats <= 0.0
                   ? n : note_ops::quantized (n, gridBeats, mode == InputQuantize::starts);

        if (loop.getLength() > 0.0 && q.startBeat >= loop.end - 1.0e-9)
            q.startBeat -= loop.getLength();

        // Stored positions are whole ticks, like everything else edited in the piano roll.
        q.startBeat = snap::roundToTicks (q.startBeat);
        q.lengthBeats = std::max (1.0 / snap::ticksPerBeat, snap::roundToTicks (q.lengthBeats));
        return q;
    }
}
