#include "ui/NoteInput.h"

namespace harmonia::ui
{
    NoteInput::NoteInput (pianoroll::PianoRollModel& m, audio::NotePreview& p, audio::SongClock& c)
        : model (m), preview (p), clock (c)
    {
        computerKeyboard.onNoteOn = [this] (int pitch, int velocity) { noteOn (pitch, velocity, Source::computerKeyboard); };
        computerKeyboard.onNoteOff = [this] (int pitch) { noteOff (pitch, Source::computerKeyboard); };
        computerKeyboard.onSettingsChanged = [this] { stateChanged(); };

        startTimerHz (30);
    }

    NoteInput::~NoteInput()
    {
        stopTimer();
        computerKeyboard.onSettingsChanged = nullptr;
        computerKeyboard.releaseAll();
        finishTake();
        model.setHeldPitches ({});
    }

    //==============================================================================
    void NoteInput::noteOn (int pitch, int velocity, Source source, double wallMs)
    {
        if (pitch < 0 || pitch > 127)
            return;

        if (source == Source::computerKeyboard)
            preview.noteOn (pitch, velocity);

        ++held[(size_t) pitch];
        showHeld();

        update();   // playback may have started since the last timer tick

        if (recorder.isActive())
            if (const auto beat = heardBeat (wallMs))
                commit (recorder.noteOn (pitch, velocity, *beat));
    }

    void NoteInput::noteOff (int pitch, Source source, double wallMs)
    {
        if (pitch < 0 || pitch > 127)
            return;

        if (source == Source::computerKeyboard)
            preview.noteOff (pitch);

        if (held[(size_t) pitch] > 0)
            --held[(size_t) pitch];

        showHeld();

        if (recorder.isActive())
            if (const auto beat = heardBeat (wallMs))
                commit (recorder.noteOff (pitch, *beat));
    }

    void NoteInput::setSustain (bool down, double wallMs)
    {
        sustainDown = down;
        update();

        if (recorder.isActive())
            if (const auto beat = heardBeat (wallMs))
                commit (recorder.setSustain (down, *beat));
    }

    void NoteInput::handleMidiMessage (const juce::MidiMessage& message, double wallMs)
    {
        if (message.isNoteOn())
            noteOn (message.getNoteNumber(), message.getVelocity(), Source::midiKeyboard, wallMs);
        else if (message.isNoteOff())
            noteOff (message.getNoteNumber(), Source::midiKeyboard, wallMs);
        else if (message.isSustainPedalOn() || message.isSustainPedalOff())
            setSustain (message.isSustainPedalOn(), wallMs);
    }

    //==============================================================================
    void NoteInput::setRecordArmed (bool shouldBeArmed)
    {
        if (armed != shouldBeArmed)
        {
            armed = shouldBeArmed;
            update();
            stateChanged();
        }
    }

    void NoteInput::setQuantize (core::InputQuantize q)
    {
        if (quantize != q)
        {
            quantize = q;
            stateChanged();
        }
    }

    void NoteInput::update()
    {
        // Not while a count-in plays before the song's start: those notes would be before beat 0.
        const auto heard = clock.isPlaying() ? clock.getHeardBeat (now()) : std::optional<double>();
        const auto shouldRecord = armed && clock.isPlaying() && model.getClip() != nullptr && heard.value_or (0.0) >= 0.0;

        if (shouldRecord && ! recorder.isActive())
            startTake();
        else if (! shouldRecord && recorder.isActive())
            finishTake();

        if (recorder.isActive())
        {
            if (const auto beat = clock.getHeardBeat (now()))
                lastHeardBeat = *beat;

            model.setLiveNotes (keepInsideClip (recorder.getSoundingNotes (lastHeardBeat)));
        }
    }

    void NoteInput::startTake()
    {
        model.beginGesture ("Record notes");
        keepOneUndoStep.emplace (model.getEdit());

        // Clip mode loops the clip; song mode the loop region or the whole song.
        const auto loop = clock.getLoopBeats();
        takeLoop = { loop.getStart(), loop.getEnd() };
        takeClip = model.getClip();
        takeClipRange = { model.getClipStartBeat(), model.getClipEndBeat() };
        recorder.start (takeLoop);
        lastHeardBeat = clock.getHeardBeat (now()).value_or (takeLoop.start);

        if (sustainDown)
            recorder.setSustain (true, lastHeardBeat);
        stateChanged();
    }

    void NoteInput::finishTake()
    {
        if (! recorder.isActive())
            return;

        commit (recorder.stop (lastHeardBeat));
        model.setLiveNotes ({});
        keepOneUndoStep.reset();
        takeClip = nullptr;
        stateChanged();
    }

    std::optional<double> NoteInput::heardBeat (double wallMs)
    {
        // Stopped a moment ago and the timer hasn't finished the take yet: the last known place.
        return clock.getHeardBeat (wallMs).value_or (lastHeardBeat);
    }

    void NoteInput::commit (const std::optional<core::Note>& note)
    {
        if (note.has_value())
            commit (std::vector<core::Note> { *note });
    }

    void NoteInput::commit (const std::vector<core::Note>& notes)
    {
        // The clip was switched without finishing the take (shouldn't happen): don't write into another one.
        if (notes.empty() || model.getClip() == nullptr || model.getClip() != takeClip.get())
            return;

        auto grid = model.getGridBeats();

        if (grid <= 0.0)
            grid = 0.25;

        std::vector<core::Note> placed;

        for (const auto& n : notes)
            placed.push_back (core::quantizeRecorded (n, quantize, grid, takeLoop));

        placed = keepInsideClip (std::move (placed));

        if (! placed.empty())
            model.addRecordedNotes (placed);
    }

    std::vector<core::Note> NoteInput::keepInsideClip (std::vector<core::Note> notes) const
    {
        // In song mode the song plays past the clip: notes outside it aren't the clip's, and a
        // note held past its end stops there.
        std::erase_if (notes, [this] (const core::Note& n)
                       { return n.startBeat < takeClipRange.start - 1.0e-9 || n.startBeat >= takeClipRange.end - 1.0e-9; });

        for (auto& n : notes)
            n.lengthBeats = std::min (n.lengthBeats, takeClipRange.end - n.startBeat);

        return notes;
    }

    void NoteInput::showHeld()
    {
        std::array<bool, 128> pitches {};

        for (size_t i = 0; i < held.size(); ++i)
            pitches[i] = held[i] > 0;

        model.setHeldPitches (pitches);
    }

    void NoteInput::stateChanged()
    {
        if (onStateChanged != nullptr)
            onStateChanged();
    }

    void NoteInput::timerCallback()
    {
        update();
    }
}
