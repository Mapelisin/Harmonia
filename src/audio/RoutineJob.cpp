#include "audio/RoutineJob.h"

namespace harmonia::audio
{
    Steps Steps::promise_type::get_return_object() noexcept
    {
        return Steps (Handle::from_promise (*this));
    }

    Steps::Steps (Steps&& other) noexcept
        : handle (std::exchange (other.handle, {}))
    {
    }

    Steps& Steps::operator= (Steps&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            handle = std::exchange (other.handle, {});
        }

        return *this;
    }

    Steps::~Steps()
    {
        reset();
    }

    void Steps::reset() noexcept
    {
        if (handle)
            std::exchange (handle, {}).destroy();
    }

    bool Steps::run (double untilMs)
    {
        if (! handle || handle.done())
            return true;

        auto& promise = handle.promise();

        if (promise.awaited != nullptr && promise.awaited->stepUntil (untilMs))
            return false;

        // The job waited for has finished (or there was none): on to the next one that has work
        // to do. One that had finished before it began (nothing to render) isn't waited for.
        do
        {
            promise.awaited = nullptr;
            handle.resume();

            if (handle.done())
                return true;
        }
        while (promise.awaited != nullptr && promise.awaited->isFinished());

        return false;
    }

    const ExportJob* Steps::getAwaited() const noexcept
    {
        return handle && ! handle.done() ? handle.promise().awaited : nullptr;
    }

    //==============================================================================
    void RoutineJob::begin (Steps function)
    {
        steps = std::move (function);
        finished = steps.run (0.0);
    }

    bool RoutineJob::work (double untilMs)
    {
        return steps.run (untilMs);
    }

    void RoutineJob::say (const juce::String& text, int done, int total, int next)
    {
        stage = text;
        partsDone = juce::jmax (0, done);
        numParts = juce::jmax (1, total);
        span = juce::jmax (1, next);
    }

    float RoutineJob::getProgress() const
    {
        if (finished)
            return 1.0f;

        const auto* awaited = steps.getAwaited();
        const auto current = awaited != nullptr ? juce::jlimit (0.0f, 1.0f, awaited->getProgress()) : 0.0f;
        return juce::jlimit (0.0f, 1.0f, ((float) partsDone + (float) span * current) / (float) numParts);
    }

    juce::String RoutineJob::getStage() const
    {
        if (finished)
            return error.isEmpty() ? "Done" : "Failed";

        return stage;
    }

    //==============================================================================
    namespace
    {
        class Followed final : public RoutineJob
        {
        public:
            Followed (std::unique_ptr<ExportJob> first, std::function<juce::String()> then)
                : job (std::move (first)), afterwards (std::move (then))
            {
                begin (run());
            }

            ~Followed() override
            {
                stop();
            }

            juce::String getStage() const override
            {
                return isFinished() ? RoutineJob::getStage() : job->getStage();
            }

        private:
            const std::unique_ptr<ExportJob> job;
            const std::function<juce::String()> afterwards;

            Steps run()
            {
                co_yield *job;

                files = job->getFiles();
                fail (job->getError());

                if (error.isEmpty() && afterwards != nullptr)
                    fail (afterwards());
            }
        };
    }

    std::unique_ptr<ExportJob> followedBy (std::unique_ptr<ExportJob> job, std::function<juce::String()> afterwards)
    {
        jassert (job != nullptr);
        return std::make_unique<Followed> (std::move (job), std::move (afterwards));
    }
}
