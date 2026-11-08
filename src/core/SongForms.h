#pragma once

#include "core/Tracktion.h"

/** A song made from a loop: the loop's clips laid out section after section (intro, verse,
    chorus...), each with only some of the parts playing, the way songs build up and break down.
    Beats are song beats. */
namespace harmonia::core::song_forms
{
    /** What a track plays in the loop. */
    enum class Role
    {
        drums,      // a drum track
        bass,       // the lowest of the others, if it's low
        harmony,    // chords, pads...
        lead        // the highest of the others, if there are a few
    };

    juce::String getName (Role);

    /** The tracks with notes or audio in the stretch, and what each plays there: a drum track is
        drums; of the others, the lowest is the bass if its notes are under C3 on average (an audio
        track never is); with two or more left, the highest is the lead; the rest is harmony. */
    std::vector<std::pair<te::AudioTrack*, Role>> getRoles (te::Edit&, double startBeat, double endBeat);

    struct Section
    {
        juce::String name;      // its marker
        double length;          // in parts: a part is about 8 bars of the loop played again
        std::vector<Role> roles;
    };

    struct Form
    {
        juce::String name;
        std::vector<Section> sections;
    };

    /** Pop (intro, verse, chorus...), dance (build, drop, break...), short. */
    const std::vector<Form>& getForms();

    /** How many times the loop plays in a section: `length` parts, a part being the loop played
        to about 8 bars (a 4-bar loop twice, an 8-bar one once); at least once. */
    int getLoops (const Section&, double loopBars);

    /** The song: from where the stretch starts, section after section, each the stretch played
        `getLoops` times by the tracks of its roles (all its tracks if none of them plays a role
        it has), a marker at each; what came after the stretch moves later. Copies of MIDI clips
        are linked to the stretch's clips. Into the current undo step. Returns how long the song
        is, in beats (0 if the stretch has nothing in it). */
    double build (te::Edit&, double startBeat, double endBeat, const Form&, double beatsPerBar);
}
