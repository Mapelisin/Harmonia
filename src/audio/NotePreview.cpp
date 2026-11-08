#include "audio/NotePreview.h"

namespace harmonia::audio
{
    namespace
    {
        constexpr int previewChannel = 1;
    }

    NotePreview::~NotePreview()
    {
        allNotesOff();
    }

    te::AudioTrack* NotePreview::getTrack() const
    {
        return dynamic_cast<te::AudioTrack*> (track.get());
    }

    void NotePreview::setTrack (te::AudioTrack* newTrack)
    {
        if (newTrack == getTrack())
            return;

        allNotesOff();
        track = newTrack;

        // Live MIDI only reaches the instrument through an allocated playback graph.
        if (newTrack != nullptr)
            newTrack->edit.getTransport().ensureContextAllocated();
    }

    void NotePreview::noteOn (int pitch, int velocity)
    {
        pitch = juce::jlimit (0, 127, pitch);

        if (std::find (soundingPitches.begin(), soundingPitches.end(), pitch) != soundingPitches.end())
            noteOff (pitch);

        soundingPitches.push_back (pitch);
        send (juce::MidiMessage::noteOn (previewChannel, pitch, (juce::uint8) juce::jlimit (1, 127, velocity)));
    }

    void NotePreview::noteOff (int pitch)
    {
        const auto it = std::find (soundingPitches.begin(), soundingPitches.end(), pitch);

        if (it == soundingPitches.end())
            return;

        soundingPitches.erase (it);
        send (juce::MidiMessage::noteOff (previewChannel, pitch));
    }

    void NotePreview::allNotesOff()
    {
        while (! soundingPitches.empty())
            noteOff (soundingPitches.back());
    }

    void NotePreview::send (const juce::MidiMessage& message)
    {
        if (auto* audioTrack = getTrack())
            audioTrack->injectLiveMidiMessage (message, sourceId);
    }
}
