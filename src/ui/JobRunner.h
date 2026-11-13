#pragma once

#include "audio/ExportJob.h"

namespace harmonia::ui
{
    /** How a part of the window has a long job done (a render: freezing a track, making one clip
        of several): the main window does it a bit at a time under its progress panel, where it
        can be stopped (MainComponent::runExport). `onDone` hears of the job's end, well or not;
        `onCancelled` that it was stopped, nothing left of it. */
    using JobRunner = std::function<void (std::unique_ptr<audio::ExportJob>, std::function<void (audio::ExportJob&)> onDone,
                                          std::function<void()> onCancelled)>;

    /** Has `job` done by the runner. With no runner (a part of the window on its own, as tests
        have it) the job is done right here, to its end. */
    inline void runJob (const JobRunner& runner, std::unique_ptr<audio::ExportJob> job,
                        std::function<void (audio::ExportJob&)> onDone, std::function<void()> onCancelled = {})
    {
        if (job == nullptr)
            return;

        if (runner != nullptr)
        {
            runner (std::move (job), std::move (onDone), std::move (onCancelled));
            return;
        }

        job->finish();

        if (onDone != nullptr)
            onDone (*job);
    }
}
