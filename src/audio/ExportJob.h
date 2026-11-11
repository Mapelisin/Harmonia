#pragma once

#include "audio/ExportFormat.h"
#include "core/Session.h"

namespace harmonia::audio
{
    struct StemsOptions;

    /** An export done a bit at a time on the message thread, so the window stays alive: it can
        show how far the export is and take a Cancel. Each `step` renders for a moment; between
        two steps the song must stay as it is and must not be played (whoever runs the job keeps
        the user out of it). A job that is destroyed before it has finished leaves nothing
        behind: no file, the song as it was.

        audio::exportSong and audio::exportStems are these jobs run to their end in one go. */
    class ExportJob
    {
    public:
        virtual ~ExportJob() = default;

        /** The song, or `part` of it, into one file (see audio::exportSong). */
        static std::unique_ptr<ExportJob> song (te::Edit&, const juce::File&, const ExportFormat&,
                                                std::optional<te::TimeRange> part = std::nullopt);

        /** Stems into a folder (see audio::exportStems). */
        static std::unique_ptr<ExportJob> stems (te::Edit&, const juce::File& folder, const StemsOptions&);

        /** Works for about `milliseconds`, and at least one audio block. False once the job has
            finished, well or not: then see getError. */
        bool step (int milliseconds = 20);

        bool isFinished() const noexcept                    { return finished; }

        /** Empty if all went well. */
        const juce::String& getError() const noexcept       { return error; }

        /** The files written (all of them once it has finished well). */
        const juce::Array<juce::File>& getFiles() const noexcept    { return files; }

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

        juce::String error;
        juce::Array<juce::File> files;
        bool finished = false;

    private:
        JUCE_DECLARE_NON_COPYABLE (ExportJob)
    };
}
