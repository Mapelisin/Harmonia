#pragma once

#include "core/Tracktion.h"

/** Kinds of tracks. An instrument track plays its MIDI clips through an instrument; an audio
    track plays audio clips (samples, recordings). They are kept apart because an instrument on
    the track would drown the audio out: most synths overwrite what comes into them.
    A return track has no clips: it plays what other tracks send to it through its effects
    (see audio::sends). It lives in the mixer, not in the arrangement. */
namespace harmonia::core::tracks
{
    enum class Kind { instrument, audio, fxReturn };

    Kind getKind (const te::AudioTrack&);

    /** Undoable. Saved with the project. */
    void setKind (te::AudioTrack&, Kind);

    inline bool isAudio (const te::AudioTrack& track)  { return getKind (track) == Kind::audio; }
    inline bool isReturn (const te::AudioTrack& track) { return getKind (track) == Kind::fxReturn; }

    /** The colour the user chose for the track; none means it is coloured by its place in the song. */
    std::optional<juce::Colour> getColour (const te::Track&);

    /** Undoable (the caller starts the gesture). Saved with the project. */
    void setColour (te::Track&, std::optional<juce::Colour>);
}
