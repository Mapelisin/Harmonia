#pragma once

#include "TestSession.h"
#include "core/NoteEditor.h"
#include "core/Project.h"

namespace harmonia::test
{
    /** An empty project with one track and one 4-bar MIDI clip at the start. */
    struct ProjectWithClip
    {
        ProjectWithClip()
            : project (core::Project::createEmpty (session()))
        {
            track = project->getTracks().getFirst();
            clip = project->addMidiClip (*track, 0.0, 16.0, "Clip");
        }

        std::unique_ptr<core::Project> project;
        te::AudioTrack* track = nullptr;
        te::MidiClip* clip = nullptr;
    };

    inline std::vector<core::Note> readNotes (const te::MidiClip& clip)
    {
        std::vector<core::Note> notes;

        for (auto* n : clip.getSequence().getNotes())
            notes.push_back (core::note::read (*n));

        return notes;
    }

    /** A note with every Harmonia property set away from its default. */
    inline core::Note makeRichNote (int pitch, double startBeat)
    {
        core::Note n;
        n.pitch = pitch;
        n.startBeat = startBeat;
        n.lengthBeats = 0.75;
        n.velocity = 90;
        n.releaseVelocity = 30;
        n.muted = true;
        n.colour = 5;
        n.pan = -0.25f;
        n.modX = 0.1f;
        n.modY = 0.9f;
        n.finePitchCents = 35.0f;
        n.slide = true;
        n.porta = true;
        n.group = 3;
        return n;
    }
}
