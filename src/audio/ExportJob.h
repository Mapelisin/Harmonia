#pragma once

#include "audio/ExportFormat.h"
#include "audio/SongExport.h"
#include "core/Session.h"

namespace harmonia::audio
{
    struct StemsOptions;

    /** An export, or anything else that renders the song (freezing a track, measuring how loud it
        is...), done a bit at a time on the message thread, so the window stays alive: it can
        show how far the job is and take a Cancel. Each `step` renders for a moment; between
        two steps the song must stay as it is and must not be played (whoever runs the job keeps
        the user out of it). A job that is destroyed before it has finished leaves nothing
        behind: no file, the song as it was. (The few that change the song on their way, a step
        of undo already begun for them, say so: that step is then taken back.)

        audio::exportSong, audio::exportStems, audio::renderTrack... are these jobs run to their
        end in one go (`finish`). A job made of several renders: audio::RoutineJob. */
    class ExportJob
    {
    public:
        virtual ~ExportJob() = default;

        /** The song, or `part` of it, into one file (see audio::exportSong); with tracks to
            leave out, a second file of the song without them next to it (audio::SongExport). */
        static std::unique_ptr<ExportJob> song (te::Edit&, const juce::File&, const SongExport&,
                                                std::optional<te::TimeRange> part = std::nullopt);

        /** Stems into a folder (see audio::exportStems). */
        static std::unique_ptr<ExportJob> stems (te::Edit&, const juce::File& folder, const StemsOptions&);

        /** One track as it sounds, into a 24-bit WAV (see audio::renderTrack). */
        static std::unique_ptr<ExportJob> track (te::AudioTrack&, const juce::File&, te::TimeRange, bool beforeFader = false);

        /** What the clips of an audio track play, into a 24-bit WAV (see audio::renderClips). */
        static std::unique_ptr<ExportJob> clips (te::AudioTrack&, const juce::File&, te::TimeRange);

        /** A job that has finished already, with this error: for what cannot even begin. */
        static std::unique_ptr<ExportJob> failed (const juce::String& problem);

        /** Works for about `milliseconds`, and at least one audio block. False once the job has
            finished, well or not: then see getError. */
        bool step (int milliseconds = 20);

        /** As step, until the clock (juce::Time::getMillisecondCounterHiRes) says `untilMs`. */
        bool stepUntil (double untilMs);

        /** Does all the rest of it right here: whoever calls waits. `said` hears each new thing
            getStage says, before the work on it. */
        void finish (const std::function<void (const juce::String& stage)>& said = {});

        bool isFinished() const noexcept                    { return finished; }

        /** Empty if all went well. */
        const juce::String& getError() const noexcept       { return error; }

        /** The files written (all of them once it has finished well). */
        const juce::Array<juce::File>& getFiles() const noexcept    { return files; }

        /** What else there is to tell once it has finished well: of a song brought to a loudness,
            where it is now and by how much it was turned. Empty if nothing. */
        const juce::String& getNote() const noexcept        { return note; }

        /** 0..1. */
        virtual float getProgress() const = 0;

        /** What is being done now, for the user: "Rendering the song", "Stem 2 of 5: Drums"... */
        virtual juce::String getStage() const = 0;

        /** The files it makes one after another (a song: one), and how many of them are rendered. */
        virtual int getNumParts() const         { return 1; }
        virtual int getNumPartsDone() const     { return finished ? 1 : 0; }

    protected:
        ExportJob() = default;

        /** One more bit of the work, until the clock (juce::Time::getMillisecondCounterHiRes)
            says `untilMs`. True once there is nothing left to do. */
        virtual bool work (double untilMs) = 0;

        juce::String error, note;
        juce::Array<juce::File> files;
        bool finished = false;

    private:
        JUCE_DECLARE_NON_COPYABLE (ExportJob)
    };
}
