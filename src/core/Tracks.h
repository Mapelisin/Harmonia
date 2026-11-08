#pragma once

#include "core/Tracktion.h"

/** Kinds of tracks. An instrument track plays its MIDI clips through an instrument; an audio
    track plays audio clips (samples, recordings). They are kept apart because an instrument on
    the track would drown the audio out: most synths overwrite what comes into them. */
namespace harmonia::core::tracks
{
    enum class Kind { instrument, audio };

    Kind getKind (const te::AudioTrack&);

    /** Undoable. Saved with the project. */
    void setKind (te::AudioTrack&, Kind);

    inline bool isAudio (const te::AudioTrack& track)  { return getKind (track) == Kind::audio; }
}
