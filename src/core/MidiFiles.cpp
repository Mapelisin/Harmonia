#include "core/MidiFiles.h"
#include "core/Clips.h"
#include "core/TempoChanges.h"
#include "core/Tracks.h"

namespace harmonia::core::midi_files
{
    namespace
    {
        constexpr double minNoteBeats = 1.0 / 64.0;
        constexpr double tolerance = 1.0e-6;

        /** When the sustain pedal was down, on each MIDI channel (from, to; in beats). */
        using PedalTimes = std::array<std::vector<std::pair<double, double>>, 16>;

        /** Lengthens the notes let go while the pedal was down, to where it came up (or where the
            same key is struck again). True if any changed. */
        bool applySustainPedal (std::vector<Note>& notes, const PedalTimes& pedal)
        {
            auto changed = false;

            for (auto& note : notes)
            {
                const auto release = note.getEndBeat();

                for (const auto& [down, up] : pedal[(size_t) juce::jlimit (0, 15, note.colour)])
                {
                    if (release < down - tolerance || release >= up)
                        continue;

                    auto end = up;

                    for (const auto& other : notes)
                        if (other.pitch == note.pitch && other.colour == note.colour
                            && other.startBeat > note.startBeat + tolerance && other.startBeat < end)
                            end = other.startBeat;

                    if (end > release + tolerance)
                    {
                        note.lengthBeats = end - note.startBeat;
                        changed = true;
                    }

                    break;
                }
            }

            return changed;
        }

        /** A note as it plays in the song: its beats there. */
        struct SongNote
        {
            Note note;
            double songStart = 0.0, songEnd = 0.0;
        };

        /** The notes the clip plays, trimmed to the clip (muted ones left out). */
        std::vector<SongNote> getPlayedNotes (te::MidiClip& clip)
        {
            std::vector<SongNote> result;

            // A note whose start the clip hides doesn't play.
            for (const auto& played : clips::getPlayedNotes (clip))
                if (! played.note.muted && ! played.cutAtClipStart)
                    result.push_back ({ played.note, played.startBeat, played.endBeat });

            return result;
        }

        int toTicks (double beats)
        {
            return (int) std::llround (beats * ticksPerBeat);
        }

        /** The tempo of the song where the file starts (`fromBeat`); for a whole song, every
            tempo change after it too, a glide as a step each half beat. */
        juce::MidiMessageSequence makeConductor (te::Edit& edit, const juce::String& name, double fromBeat, bool withChanges)
        {
            juce::MidiMessageSequence track;

            if (name.isNotEmpty())
                track.addEvent (juce::MidiMessage::textMetaEvent (3, name), 0.0);

            const auto addTempo = [&] (double beat)
            {
                const auto bpm = tempo_changes::getBpmAt (edit, beat);
                track.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::llround (60000000.0 / bpm)), toTicks (beat - fromBeat));
            };

            addTempo (fromBeat);

            if (withChanges)
            {
                const auto changes = tempo_changes::getAll (edit);

                for (size_t i = 1; i < changes.size(); ++i)
                {
                    if (changes[i].gradual)
                        for (auto beat = changes[i - 1].beat + 0.5; beat < changes[i].beat - 1.0e-6; beat += 0.5)
                            if (beat > fromBeat)
                                addTempo (beat);

                    if (changes[i].beat > fromBeat)
                        addTempo (changes[i].beat);
                }
            }

            track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (edit.tempoSequence.getTimeSig (0)->numerator, 4), 0.0);
            track.addEvent (juce::MidiMessage::endOfTrack(), track.getEndTime());
            return track;
        }

        /** Each controller's events where they play in the song. */
        using SongControllers = std::map<controllers::Controller, std::vector<controllers::Point>>;

        void addPlayedControllers (SongControllers& moves, const te::MidiClip& clip)
        {
            for (auto c : controllers::getAll())
                for (const auto& p : controllers::getPlayed (clip, c))
                    moves[c].push_back (p);
        }

        juce::MidiMessageSequence makeTrack (const juce::String& name, const std::vector<SongNote>& notes,
                                             const SongControllers& moves, double offsetBeats)
        {
            juce::MidiMessageSequence track;
            track.addEvent (juce::MidiMessage::textMetaEvent (3, name), 0.0);
            auto lastTick = 0;

            // A wheel bends every note of the part: sent on each channel its notes use.
            std::set<int> channels;

            for (const auto& n : notes)
                channels.insert (juce::jlimit (1, 16, n.note.colour + 1));

            for (const auto& [c, points] : moves)
            {
                for (const auto& p : points)
                {
                    const auto tick = std::max (0, toTicks (p.beat - offsetBeats));

                    for (auto channel : channels)
                        track.addEvent (controllers::toMidi (c, p.value, channel), tick);

                    lastTick = std::max (lastTick, tick);
                }
            }

            for (const auto& n : notes)
            {
                const auto channel = juce::jlimit (1, 16, n.note.colour + 1);
                const auto onTick = toTicks (n.songStart - offsetBeats);
                const auto offTick = std::max (onTick + 1, toTicks (n.songEnd - offsetBeats));

                track.addEvent (juce::MidiMessage::noteOn (channel, n.note.pitch, (juce::uint8) juce::jlimit (1, 127, n.note.velocity)), onTick);
                track.addEvent (juce::MidiMessage::noteOff (channel, n.note.pitch, (juce::uint8) juce::jlimit (0, 127, n.note.releaseVelocity)), offTick);
                lastTick = std::max (lastTick, offTick);
            }

            track.addEvent (juce::MidiMessage::endOfTrack(), lastTick);
            track.sort();
            track.updateMatchedPairs();
            return track;
        }

        juce::String writeFile (juce::MidiFile& midi, const juce::File& file)
        {
            juce::TemporaryFile temp (file);

            {
                juce::FileOutputStream out (temp.getFile());

                if (! out.openedOk() || ! midi.writeTo (out, 1))
                    return "Couldn't write " + file.getFullPathName();
            }

            if (! temp.overwriteTargetFileWithTemporary())
                return "Couldn't write " + file.getFullPathName();

            return {};
        }
    }

    bool isMidiFile (const juce::File& file)
    {
        return file.hasFileExtension ("mid;midi");
    }

    std::optional<Contents> read (const juce::File& file, juce::String& error)
    {
        error = {};
        juce::FileInputStream in (file);
        juce::MidiFile midi;

        if (! in.openedOk() || ! midi.readFrom (in, true))
        {
            error = "Can't read " + file.getFileName() + " as a MIDI file";
            return {};
        }

        const auto ppq = (int) midi.getTimeFormat();

        if (ppq <= 0)
        {
            error = file.getFileName() + ": MIDI files timed in SMPTE frames aren't supported";
            return {};
        }

        Contents contents;

        for (int t = 0; t < midi.getNumTracks(); ++t)
        {
            juce::MidiMessageSequence sequence (*midi.getTrack (t));
            sequence.updateMatchedPairs();
            Part part;
            PedalTimes pedal;
            std::array<std::optional<double>, 16> pedalDown;

            for (int i = 0; i < sequence.getNumEvents(); ++i)
            {
                const auto* event = sequence.getEventPointer (i);
                const auto& message = event->message;
                const auto beat = message.getTimeStamp() / ppq;

                if (message.isTrackNameEvent() && part.name.isEmpty())
                {
                    part.name = message.getTextFromTextMetaEvent().trim();
                }
                else if (message.isTempoMetaEvent() && ! contents.bpm.has_value())
                {
                    const auto secondsPerBeat = message.getTempoSecondsPerQuarterNote();

                    if (secondsPerBeat > 0.0)
                        contents.bpm = 60.0 / secondsPerBeat;
                }
                else if (message.isTimeSignatureMetaEvent() && ! contents.beatsPerBar.has_value())
                {
                    int numerator = 4, denominator = 4;
                    message.getTimeSignatureInfo (numerator, denominator);

                    if (denominator == 4 && numerator > 0)
                        contents.beatsPerBar = numerator;
                }
                else if (message.isControllerOfType (64))
                {
                    const auto channel = (size_t) juce::jlimit (0, 15, message.getChannel() - 1);

                    if (message.getControllerValue() >= 64)
                    {
                        if (! pedalDown[channel].has_value())
                            pedalDown[channel] = beat;
                    }
                    else if (pedalDown[channel].has_value())
                    {
                        pedal[channel].push_back ({ *pedalDown[channel], beat });
                        pedalDown[channel].reset();
                    }
                }
                else if (const auto moved = controllers::fromMidi (message); moved.has_value())
                {
                    part.controllerEvents[moved->first].push_back ({ beat, moved->second });
                }
                else if (message.isNoteOn())
                {
                    Note note;
                    note.pitch = message.getNoteNumber();
                    note.velocity = message.getVelocity();
                    note.colour = juce::jlimit (0, 15, message.getChannel() - 1);
                    note.startBeat = beat;

                    if (event->noteOffObject != nullptr)
                    {
                        const auto& off = event->noteOffObject->message;
                        note.lengthBeats = off.getTimeStamp() / ppq - beat;
                        note.releaseVelocity = off.isNoteOff() ? off.getVelocity() : 64;
                    }
                    else
                    {
                        note.lengthBeats = 0.25;
                    }

                    note.lengthBeats = std::max (minNoteBeats, note.lengthBeats);
                    part.endBeat = std::max (part.endBeat, note.getEndBeat());
                    part.notes.push_back (note.clamped());
                }
            }

            // A pedal still down at the end holds to the last note's end.
            for (size_t channel = 0; channel < pedal.size(); ++channel)
                if (pedalDown[channel].has_value())
                    pedal[channel].push_back ({ *pedalDown[channel], part.endBeat });

            if (applySustainPedal (part.notes, pedal))
            {
                part.pedalled = true;

                for (const auto& note : part.notes)
                    part.endBeat = std::max (part.endBeat, note.getEndBeat());
            }

            if (! part.notes.empty())
                contents.parts.push_back (std::move (part));
        }

        if (contents.parts.empty())
        {
            error = file.getFileName() + " has no notes";
            return {};
        }

        return contents;
    }

    juce::String writeClip (te::MidiClip& clip, const juce::File& file)
    {
        const auto notes = getPlayedNotes (clip);

        if (notes.empty())
            return "Nothing to export: the clip has no notes";

        juce::MidiFile midi;
        midi.setTicksPerQuarterNote (ticksPerBeat);
        midi.addTrack (makeConductor (clip.edit, clip.getName(), clips::getStartBeat (clip), false));

        SongControllers moves;
        addPlayedControllers (moves, clip);
        midi.addTrack (makeTrack (clip.getName(), notes, moves, clips::getStartBeat (clip)));
        return writeFile (midi, file);
    }

    juce::String writeSong (Project& project, const juce::File& file)
    {
        juce::MidiFile midi;
        midi.setTicksPerQuarterNote (ticksPerBeat);
        midi.addTrack (makeConductor (project.getEdit(), file.getFileNameWithoutExtension(), 0.0, true));
        auto anyNotes = false;

        for (auto* track : project.getTracks())
        {
            if (tracks::isAudio (*track))
                continue;

            std::vector<SongNote> notes;
            SongControllers moves;

            for (auto* clip : clips::getMidiClips (*track))
            {
                for (const auto& n : getPlayedNotes (*clip))
                    notes.push_back (n);

                addPlayedControllers (moves, *clip);
            }

            if (notes.empty())
                continue;

            std::stable_sort (notes.begin(), notes.end(), [] (const SongNote& a, const SongNote& b) { return a.songStart < b.songStart; });
            midi.addTrack (makeTrack (track->getName(), notes, moves, 0.0));
            anyNotes = true;
        }

        if (! anyNotes)
            return "Nothing to export: no instrument track has notes";

        return writeFile (midi, file);
    }
}
