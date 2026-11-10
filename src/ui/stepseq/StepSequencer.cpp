#include "ui/stepseq/StepSequencer.h"
#include "ui/Theme.h"

namespace harmonia::ui::stepseq
{
    namespace
    {
        constexpr int minCellWidth = 18, maxCellWidth = 44, velocityStep = 10;
    }

    //==============================================================================
    /** The cells (and the bar numbers above them): scrolled by the viewport. */
    class StepSequencer::Grid final : public juce::Component
    {
    public:
        explicit Grid (StepSequencer& o) : owner (o)    { setOpaque (true); }

        void paint (juce::Graphics& g) override
        {
            const auto area = g.getClipBounds();
            g.fillAll (theme::colour::background);

            const auto& shown = owner.rows;
            const auto perBeat = owner.model.getStepsPerBeat();
            const auto perBar = std::max (1, owner.model.getStepsPerBar());
            const auto width = owner.cellWidth;
            const auto firstStep = std::max (0, area.getX() / width);
            const auto lastStep = std::min (owner.numSteps - 1, area.getRight() / width);

            // Bar numbers; every other beat a little lighter, so the eye finds its place.
            g.setFont (juce::FontOptions (theme::size::smallFontSize));

            for (int step = firstStep; step <= lastStep; ++step)
            {
                const auto x = step * width;

                if ((step / perBeat) % 2 == 0)
                {
                    g.setColour (theme::colour::surface);
                    g.fillRect (x, headerHeight, width, (int) shown.size() * rowHeight);
                }

                if (step % perBar == 0)
                {
                    g.setColour (theme::colour::textSecondary);
                    g.drawText (juce::String (step / perBar + 1), x + 3, 0, width * perBeat, headerHeight, juce::Justification::centredLeft, false);
                }
            }

            // The column being played.
            if (owner.playingStep.has_value())
            {
                g.setColour (theme::colour::accent.withAlpha (0.22f));
                g.fillRect (*owner.playingStep * width, headerHeight, width, (int) shown.size() * rowHeight);
            }

            for (int row = 0; row < (int) shown.size(); ++row)
            {
                const auto y = headerHeight + row * rowHeight;

                if (y > area.getBottom() || y + rowHeight < area.getY())
                    continue;

                for (int step = firstStep; step <= lastStep; ++step)
                {
                    const auto cell = juce::Rectangle<int> (step * width, y, width, rowHeight).reduced (2, 3).toFloat();
                    const auto velocity = owner.model.getVelocity (shown[(size_t) row].pitch, step);

                    if (velocity > 0)
                    {
                        // Harder hits are brighter.
                        g.setColour (theme::colour::accent.withAlpha (0.35f + 0.65f * (float) velocity / 127.0f));
                        g.fillRoundedRectangle (cell, 2.0f);
                    }
                    else
                    {
                        g.setColour (theme::colour::surfaceRaised);
                        g.fillRoundedRectangle (cell, 2.0f);
                    }
                }
            }

            // Lines at the beats, stronger at the bars.
            for (int step = firstStep; step <= lastStep + 1; ++step)
            {
                if (step % perBeat != 0)
                    continue;

                g.setColour (step % perBar == 0 ? theme::pianoroll::barLine : theme::pianoroll::beatLine);
                g.fillRect (step * width, step % perBar == 0 ? 0 : headerHeight, 1, getHeight());
            }
        }

        void mouseDown (const juce::MouseEvent& e) override      { owner.gridDown (e.getPosition(), e.mods.isPopupMenu()); }
        void mouseDrag (const juce::MouseEvent& e) override      { owner.gridDrag (e.getPosition()); }
        void mouseUp (const juce::MouseEvent&) override          { owner.gridUp(); }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            if (e.mods.isAltDown())
                owner.gridWheel (e.getPosition(), wheel.deltaY);
            else
                juce::Component::mouseWheelMove (e, wheel);      // the viewport scrolls
        }

    private:
        StepSequencer& owner;
    };

    //==============================================================================
    /** The rows' names, left of the cells: they stay put while the cells scroll sideways. */
    class StepSequencer::Names final : public juce::Component
    {
    public:
        explicit Names (StepSequencer& o) : owner (o)   { setOpaque (true); }

        int scrollY = 0;

        void paint (juce::Graphics& g) override
        {
            g.fillAll (theme::colour::surface);
            g.setFont (juce::FontOptions (theme::size::smallFontSize));

            for (int row = 0; row < (int) owner.rows.size(); ++row)
            {
                const auto y = headerHeight + row * rowHeight - scrollY;

                if (y + rowHeight < headerHeight || y > getHeight())
                    continue;

                g.setColour (row == pressedRow ? theme::colour::accent : theme::colour::textPrimary);
                g.drawText (owner.rows[(size_t) row].name, 8, y, getWidth() - 12, rowHeight, juce::Justification::centredLeft, true);
                g.setColour (theme::colour::border);
                g.fillRect (0, y + rowHeight - 1, getWidth(), 1);
            }

            g.setColour (theme::colour::surface);
            g.fillRect (0, 0, getWidth(), headerHeight);
            g.setColour (theme::colour::border);
            g.fillRect (getWidth() - 1, 0, 1, getHeight());
        }

        int rowAt (int y) const
        {
            const auto row = (y - headerHeight + scrollY) / rowHeight;
            return y >= headerHeight && juce::isPositiveAndBelow (row, (int) owner.rows.size()) ? row : -1;
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto row = rowAt (e.y);

            if (row < 0)
                return;

            const auto pitch = owner.rows[(size_t) row].pitch;

            if (e.mods.isPopupMenu())
            {
                owner.createRowMenu (pitch).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
                return;
            }

            pressedRow = row;
            owner.notes.auditionStart (pitch, StepSequencerModel::defaultVelocity);
            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (std::exchange (pressedRow, -1) >= 0)
            {
                owner.notes.auditionStop();
                repaint();
            }
        }

    private:
        StepSequencer& owner;
        int pressedRow = -1;
    };

    //==============================================================================
    /** The viewport of the cells: keeps the names level with them. */
    struct StepSequencer::View final : juce::Viewport
    {
        explicit View (StepSequencer& o) : owner (o) {}

        void visibleAreaChanged (const juce::Rectangle<int>& area) override
        {
            if (owner.names != nullptr && std::exchange (owner.names->scrollY, area.getY()) != area.getY())
                owner.names->repaint();
        }

        StepSequencer& owner;
    };

    //==============================================================================
    StepSequencer::StepSequencer (pianoroll::PianoRollModel& n, audio::Transport& t)
        : notes (n), transport (t)
    {
        setOpaque (true);

        notesButton.setTooltip ("Back to the piano roll: the same notes as keys and bars (Alt+T)");
        notesButton.onClick = [this]
        {
            if (onShowNotes != nullptr)
                onShowNotes();
        };

        for (const auto steps : StepSequencerModel::getStepChoices())
            stepBox.addItem (StepSequencerModel::getStepName (steps), steps);

        stepBox.setSelectedId (model.getStepsPerBeat(), juce::dontSendNotification);
        stepBox.setTooltip ("How long a step is");
        stepBox.onChange = [this]
        {
            model.setStepsPerBeat (stepBox.getSelectedId());
            refresh();
        };

        clearButton.setTooltip ("Takes every hit of the clip away (Ctrl+Z brings them back)");
        clearButton.onClick = [this]
        {
            model.clearAll();
            refresh();
        };

        stepLabel.setFont (juce::FontOptions (theme::size::smallFontSize));
        stepLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        infoLabel.setFont (juce::FontOptions (theme::size::smallFontSize));
        infoLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);

        names = std::make_unique<Names> (*this);
        grid = std::make_unique<Grid> (*this);
        view = std::make_unique<View> (*this);
        view->setViewedComponent (grid.get(), false);
        view->setScrollBarsShown (true, true);
        view->setScrollBarThickness (10);

        for (auto* c : std::initializer_list<juce::Component*> { &notesButton, &stepLabel, &stepBox, &clearButton, &infoLabel, names.get(), view.get() })
        {
            c->setWantsKeyboardFocus (false);
            addAndMakeVisible (c);
        }

        notes.addListener (this);
        refresh();
        startTimerHz (30);
    }

    StepSequencer::~StepSequencer()
    {
        stopTimer();
        notes.removeListener (this);
    }

    void StepSequencer::refresh()
    {
        const auto newRows = model.getRows();
        const auto newSteps = model.getNumSteps();

        if (newRows != rows || newSteps != numSteps)
        {
            rows = newRows;
            numSteps = newSteps;
            layOut();
        }

        const auto hasClip = model.hasClip();
        stepBox.setEnabled (hasClip);
        clearButton.setEnabled (hasClip);
        infoLabel.setText (hasClip ? "Click: a hit. Drag: along the way. Right button: take away. Alt+wheel: harder / softer."
                                   : juce::String(), juce::dontSendNotification);
        names->repaint();
        grid->repaint();
        repaint();
    }

    void StepSequencer::layOut()
    {
        // As wide as the clip's steps need; a short clip fills the width.
        const auto available = std::max (1, view->getWidth() - view->getScrollBarThickness());
        cellWidth = numSteps > 0 ? juce::jlimit (minCellWidth, maxCellWidth, available / numSteps) : minCellWidth;
        grid->setSize (std::max (1, numSteps * cellWidth + 1), headerHeight + (int) rows.size() * rowHeight + 1);
    }

    juce::Point<int> StepSequencer::getGridSize() const
    {
        return { grid->getWidth(), grid->getHeight() };
    }

    juce::Rectangle<int> StepSequencer::getCellBounds (int row, int step) const
    {
        return { step * cellWidth, headerHeight + row * rowHeight, cellWidth, rowHeight };
    }

    std::optional<std::pair<int, int>> StepSequencer::getCellAt (juce::Point<int> p) const
    {
        if (p.x < 0 || p.y < headerHeight)
            return std::nullopt;

        const auto step = p.x / cellWidth, row = (p.y - headerHeight) / rowHeight;

        if (step >= numSteps || row >= (int) rows.size())
            return std::nullopt;

        return std::pair { row, step };
    }

    //==============================================================================
    void StepSequencer::gridDown (juce::Point<int> p, bool rightButton)
    {
        painting = erasing = false;
        lastCell = getCellAt (p);

        if (! lastCell.has_value())
            return;

        const auto pitch = rows[(size_t) lastCell->first].pitch;

        if (rightButton)
        {
            // The right button takes hits away, here and along the drag.
            notes.beginGesture ("Remove hits");
            model.beginPaint (false);
            model.paint (pitch, lastCell->second);
            erasing = true;
        }
        else
        {
            model.beginPaint (model.toggle (pitch, lastCell->second));
            painting = true;
        }

        grid->repaint();
    }

    void StepSequencer::gridDrag (juce::Point<int> p)
    {
        const auto cell = getCellAt (p);

        if ((! painting && ! erasing) || ! cell.has_value() || cell == lastCell)
            return;

        // Every step passed on the way, however fast the mouse went.
        const auto pitch = rows[(size_t) cell->first].pitch;
        const auto from = lastCell.has_value() ? lastCell->second : cell->second;

        for (auto step = from; step != cell->second; step += cell->second > from ? 1 : -1)
            if (step != from || cell->first != lastCell->first)
                model.paint (pitch, step);

        model.paint (pitch, cell->second);
        lastCell = cell;
        grid->repaint();
    }

    void StepSequencer::gridUp()
    {
        painting = erasing = false;
        lastCell.reset();
    }

    void StepSequencer::gridWheel (juce::Point<int> p, float deltaY)
    {
        if (const auto cell = getCellAt (p); cell.has_value() && deltaY != 0.0f)
        {
            model.nudgeVelocity (rows[(size_t) cell->first].pitch, cell->second, deltaY > 0.0f ? velocityStep : -velocityStep);
            grid->repaint();
        }
    }

    juce::PopupMenu StepSequencer::createRowMenu (int pitch)
    {
        juce::PopupMenu menu;
        juce::Component::SafePointer<StepSequencer> safe (this);

        menu.addItem ("Clear this row", [safe, pitch]
        {
            if (safe != nullptr)
            {
                safe->model.clearRow (pitch);
                safe->refresh();
            }
        });

        return menu;
    }

    //==============================================================================
    void StepSequencer::pianoRollChanged()
    {
        refresh();
    }

    void StepSequencer::timerCallback()
    {
        // The step the song is in, while it plays inside the clip.
        std::optional<int> step;

        if (model.hasClip() && (transport.isPlaying() || transport.isPlayRequested()))
        {
            const auto beat = notes.getEdit().tempoSequence.toBeats (te::TimePosition::fromSeconds (transport.getPositionSeconds())).inBeats();

            if (const auto at = model.getStepAt (beat); at >= 0 && at < numSteps)
                step = at;
        }

        if (step != playingStep)
        {
            playingStep = step;
            grid->repaint();
        }
    }

    void StepSequencer::paint (juce::Graphics& g)
    {
        g.fillAll (theme::colour::surface);
        g.setColour (theme::colour::border);
        g.fillRect (0, toolbarHeight - 1, getWidth(), 1);

        if (! model.hasClip())
        {
            g.setColour (theme::colour::textSecondary);
            g.setFont (juce::FontOptions (theme::size::fontSize));
            g.drawText ("Open a clip (click it in the arrangement) to see its steps", getLocalBounds().withTrimmedTop (toolbarHeight),
                        juce::Justification::centred);
        }
    }

    void StepSequencer::resized()
    {
        auto area = getLocalBounds();
        auto toolbar = area.removeFromTop (toolbarHeight).reduced (theme::size::padding, 5);
        notesButton.setBounds (toolbar.removeFromLeft (90));
        toolbar.removeFromLeft (12);
        stepLabel.setBounds (toolbar.removeFromLeft (36));
        stepBox.setBounds (toolbar.removeFromLeft (120));
        toolbar.removeFromLeft (12);
        clearButton.setBounds (toolbar.removeFromLeft (60));
        toolbar.removeFromLeft (12);
        infoLabel.setBounds (toolbar);

        names->setBounds (area.removeFromLeft (namesWidth));
        view->setBounds (area);
        layOut();
    }
}
