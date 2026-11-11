#include "ui/ExportProgress.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    namespace
    {
        constexpr int panelWidth = 440, panelHeight = 132, cancelButtonWidth = 90;

        // The export works this long, then the window gets its turn for a moment (to paint, to
        // hear Cancel): the timer is started anew after each slice, so the turn is never skipped.
        constexpr int sliceMs = 30, turnMs = 3;
    }

    ExportProgress::ExportProgress()
    {
        setWantsKeyboardFocus (true);
        setInterceptsMouseClicks (true, true);
        cancelButton.setTooltip ("Stop the export: nothing is left of it (Esc)");
        cancelButton.onClick = [this] { cancel(); };
        addAndMakeVisible (cancelButton);
        setVisible (false);
    }

    ExportProgress::~ExportProgress()
    {
        stopTimer();
        job.reset();

        if (isCurrentlyModal (false))
            exitModalState (0);
    }

    bool ExportProgress::run (std::unique_ptr<audio::ExportJob> newJob, std::function<void (audio::ExportJob&)> onDone)
    {
        if (job != nullptr || newJob == nullptr)
            return false;

        // Nothing to export, a folder that can't be made...: said at once.
        if (newJob->isFinished())
        {
            if (onDone != nullptr)
                onDone (*newJob);

            return true;
        }

        job = std::move (newJob);
        done = std::move (onDone);
        shownStage.clear();
        shownPercent = -1;

        toFront (false);
        // The other windows too (plugins, settings) wait until it has ended.
        enterModalState (true);
        startTimer (turnMs);
        return true;
    }

    void ExportProgress::close()
    {
        stopTimer();

        if (isCurrentlyModal (false))
            exitModalState (0);

        setVisible (false);
    }

    void ExportProgress::cancel()
    {
        if (job == nullptr)
            return;

        // What it has written goes with it; the song is put back as it was.
        job.reset();
        done = nullptr;
        close();

        if (onCancelled != nullptr)
            onCancelled();
    }

    bool ExportProgress::advance (int milliseconds)
    {
        if (job == nullptr)
            return false;

        if (job->step (milliseconds))
        {
            // Painted only when what it says changes.
            const auto stage = job->getStage();
            const auto percent = juce::roundToInt (job->getProgress() * 100.0f);

            if (stage != shownStage || percent != shownPercent)
            {
                shownStage = stage;
                shownPercent = percent;
                repaint();
            }

            return true;
        }

        const auto finished = std::move (job);
        const auto tell = std::move (done);
        done = nullptr;
        close();

        if (tell != nullptr)
            tell (*finished);

        return false;
    }

    void ExportProgress::timerCallback()
    {
        if (advance (sliceMs))
            startTimer (turnMs);
    }

    juce::String ExportProgress::getStage() const
    {
        return job != nullptr ? job->getStage() : juce::String();
    }

    float ExportProgress::getProgress() const
    {
        return job != nullptr ? juce::jlimit (0.0f, 1.0f, job->getProgress()) : 0.0f;
    }

    juce::Rectangle<int> ExportProgress::getPanelArea() const
    {
        return getLocalBounds().withSizeKeepingCentre (std::min (panelWidth, getWidth()), std::min (panelHeight, getHeight()));
    }

    void ExportProgress::paint (juce::Graphics& g)
    {
        // Dark enough to hide what the export does to the song meanwhile (tracks muted for a stem).
        g.fillAll (theme::colour::background.withAlpha (0.88f));

        const auto panel = getPanelArea();
        g.setColour (theme::colour::surface);
        g.fillRoundedRectangle (panel.toFloat(), 6.0f);
        g.setColour (theme::colour::border);
        g.drawRoundedRectangle (panel.toFloat(), 6.0f, 1.0f);

        auto area = panel.reduced (theme::size::padding * 2);
        area.removeFromBottom (theme::size::buttonHeight + theme::size::padding);

        const auto progress = getProgress();
        g.setColour (theme::colour::textPrimary);
        g.setFont (juce::FontOptions (theme::size::fontSize, juce::Font::bold));
        auto titleRow = area.removeFromTop (theme::size::buttonHeight);
        g.drawText (juce::String (juce::roundToInt (progress * 100.0f)) + "%", titleRow.removeFromRight (56), juce::Justification::centredRight);
        g.drawText (getStage(), titleRow, juce::Justification::centredLeft, true);

        area.removeFromTop (theme::size::padding);
        const auto bar = area.removeFromTop (8).toFloat();
        g.setColour (theme::colour::surfaceRaised);
        g.fillRoundedRectangle (bar, 4.0f);
        g.setColour (theme::colour::accent);
        g.fillRoundedRectangle (bar.withWidth (std::max (bar.getHeight(), bar.getWidth() * progress)), 4.0f);

        // Next to the button.
        auto hint = panel.reduced (theme::size::padding * 2).removeFromBottom (theme::size::buttonHeight);
        hint.removeFromRight (cancelButtonWidth + theme::size::padding);
        g.setColour (theme::colour::textSecondary);
        g.setFont (juce::FontOptions (theme::size::smallFontSize));
        g.drawText ("Esc or Cancel stops it: no file is left half-written.", hint, juce::Justification::centredLeft, true);
    }

    void ExportProgress::resized()
    {
        auto area = getPanelArea().reduced (theme::size::padding * 2);
        cancelButton.setBounds (area.removeFromBottom (theme::size::buttonHeight).removeFromRight (cancelButtonWidth));
    }

    bool ExportProgress::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
            cancel();

        // No key reaches the song while it is exported.
        return true;
    }
}
