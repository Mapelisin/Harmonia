#pragma once

#include "audio/ExportJob.h"

namespace harmonia::ui
{
    /** Over the whole main window while the song is exported, or rendered for anything else that
        takes long (a freeze, Mix Doctor...): what is being done, how far it is, and Cancel (Esc).
        It does the job a bit at a time between the window's other work, so the window stays
        alive, and keeps the user out of the song meanwhile: the mouse and the keys stop here. */
    class ExportProgress final : public juce::Component,
                                 private juce::Timer
    {
    public:
        ExportProgress();
        ~ExportProgress() override;

        /** Starts the job. `onDone` hears of its end, well or not (see the job's getError and
            getFiles), at once if the job had nothing left to do; it does not if the job is
            cancelled: then `onCancelled` does, once nothing is left of the job (without one, the
            panel's own onCancelled). A job already running goes on: false. */
        bool run (std::unique_ptr<audio::ExportJob>, std::function<void (audio::ExportJob&)> onDone,
                  std::function<void()> onCancelled = {});

        bool isRunning() const noexcept                 { return job != nullptr; }

        /** Stops the job: nothing is left of it. (A job that changes the song on its way leaves
            that to whoever ran it: see `run`.) */
        void cancel();

        /** One more bit of the export, about that long (the timer does it; a test can). False
            once there is none running. */
        bool advance (int milliseconds);

        /** "Rendering the song", "Stem 2 of 5: Drums"... and how far the whole export is, 0..1. */
        juce::String getStage() const;
        float getProgress() const;

        juce::TextButton& getCancelButton() noexcept    { return cancelButton; }

        /** A job run without an `onCancelled` of its own was cancelled (by the button, Esc or
            cancel()). */
        std::function<void()> onCancelled;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        bool keyStateChanged (bool) override            { return true; }

    private:
        std::unique_ptr<audio::ExportJob> job;
        std::function<void (audio::ExportJob&)> done;
        std::function<void()> cancelled;
        juce::TextButton cancelButton { "Cancel" };
        juce::String shownStage;
        int shownPercent = -1;

        juce::Rectangle<int> getPanelArea() const;
        void close();
        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE (ExportProgress)
    };
}
