#pragma once

#include "core/BeatFinder.h"

namespace harmonia::core::beat_finder
{
    /** The beat of a sound that doesn't keep one tempo: its hits are followed along the sound,
        both ways from where they are steadiest, the grid of sixteenths giving way a little at
        each hit. `period`: the beat the sound has on the whole, in seconds. What comes out is
        where each beat is (Beat::beatSeconds). Nothing if no beat can be followed. */
    std::optional<Beat> followBeat (const std::vector<Hit>& hits, double period);

    /** Whether the beats followed along a sound leave the one grid found for it by more than a
        player's feel: bar by bar (a snare that is always late is feel, a tempo that gives way
        is not). */
    bool wanders (const Beat& followed, const Beat& steady);
}
