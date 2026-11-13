#pragma once

#include "audio/ExportJob.h"

#include <coroutine>

namespace harmonia::audio
{
    /** What a job of several renders does, written as one function from top to bottom: where it
        says `co_yield *job`, that job is done a bit at a time, and the function goes on once it
        has finished (then see the job's getError). `co_yield Steps::turn` only lets whoever runs
        it have a turn, to show what was just said.

        Let go of half way, it destroys what the function held at that moment (its jobs, its
        temporary files) as leaving the function would. */
    class Steps final
    {
    public:
        struct Turn {};
        static constexpr Turn turn {};

        struct promise_type
        {
            ExportJob* awaited = nullptr;

            Steps get_return_object() noexcept;
            std::suspend_always initial_suspend() const noexcept        { return {}; }
            std::suspend_always final_suspend() const noexcept          { return {}; }
            std::suspend_always yield_value (ExportJob& job) noexcept   { awaited = &job; return {}; }
            std::suspend_always yield_value (Turn) noexcept             { awaited = nullptr; return {}; }
            void return_void() const noexcept                           {}
            void unhandled_exception() const noexcept                   { std::terminate(); }
        };

        Steps() = default;
        Steps (Steps&&) noexcept;
        Steps& operator= (Steps&&) noexcept;
        ~Steps();

        /** Works on the job waited for until the clock (juce::Time::getMillisecondCounterHiRes)
            says `untilMs`; once that has finished, goes on to the next job that has work to do.
            True once the function has come to its end. */
        bool run (double untilMs);

        /** The job waited for now, if any. */
        const ExportJob* getAwaited() const noexcept;

        /** Lets go of the function and all it holds. */
        void reset() noexcept;

    private:
        using Handle = std::coroutine_handle<promise_type>;

        explicit Steps (Handle h) noexcept : handle (h) {}

        Handle handle;

        JUCE_DECLARE_NON_COPYABLE (Steps)
    };

    //==============================================================================
    /** A job made of other jobs and of what is done between them: the song is measured, something
        in it is changed, it is measured again... The class derived from it has the function (a
        member returning Steps), hands it over at the end of its constructor (`begin (run())`) and
        lets go of it first thing in its destructor (`stop()`), before its own members go. */
    class RoutineJob : public ExportJob
    {
    public:
        float getProgress() const override;
        juce::String getStage() const override;

    protected:
        RoutineJob() = default;

        /** Takes the function as far as its first job that has work to do: what cannot be done
            is known (isFinished, getError) before any work. */
        void begin (Steps);

        void stop() noexcept                        { steps.reset(); }

        /** What getStage says from here on. The whole job is `numParts` long, `partsDone` of them
            are behind it, and the job waited for next is `span` of them. */
        void say (const juce::String& stage, int partsDone = 0, int numParts = 1, int span = 1);

        /** The job's error (an empty one is none); the function returns after it. */
        void fail (const juce::String& problem)     { error = problem; }

        /** The job waited for now, if any. */
        const ExportJob* getAwaited() const noexcept    { return steps.getAwaited(); }

    private:
        Steps steps;
        juce::String stage;
        int partsDone = 0, numParts = 1, span = 1;

        bool work (double untilMs) final;
    };

    /** A RoutineJob with something to give back once it has finished. */
    template <typename Result>
    class ResultJob : public RoutineJob
    {
    public:
        const Result& getResult() const noexcept    { return result; }

    protected:
        Result result {};
    };

    /** `job`, and then, if it went well, `afterwards`, in the step that finishes it: what a
        command does with the song once its render is there. A problem `afterwards` returns is
        the job's error. */
    std::unique_ptr<ExportJob> followedBy (std::unique_ptr<ExportJob> job, std::function<juce::String()> afterwards);
}
