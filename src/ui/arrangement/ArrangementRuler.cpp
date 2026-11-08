#include "ui/arrangement/ArrangementRuler.h"
#include "ui/Theme.h"

namespace harmonia::ui::arrangement
{
    namespace snap = core::snap;

    namespace
    {
        constexpr float minBarLabelSpacing = 32.0f;
        constexpr float rangeStripHeight = 4.0f;
        constexpr float playheadSize = 9.0f;
    }

    ArrangementRuler::ArrangementRuler (ArrangementModel& m, ArrangementView& v)
        : model (m), view (v)
    {
        setOpaque (true);
    }

    void ArrangementRuler::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat();
        g.fillAll (theme::colour::surface);

        // Song mode: the loop region; clip mode: what Play loops (the open clip).
        if (model.getPlayMode() == PlayMode::song)
        {
            if (const auto region = model.getLoopRegion())
            {
                g.setColour (theme::colour::accent.withAlpha (0.35f));
                g.fillRect (bounds.withLeft (view.beatToX (region->start)).withRight (view.beatToX (region->end)));
            }
        }
        else if (const auto range = model.getPlayRange(); range.getLength() > 0.0)
        {
            g.setColour (theme::pianoroll::clipRange);
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (view.beatToX (range.start), bounds.getBottom() - rangeStripHeight,
                                                                    view.beatToX (range.end), bounds.getBottom()));
        }

        const auto beatsPerBar = model.getBeatsPerBar();
        const auto barWidth = (float) (beatsPerBar * view.pixelsPerBeat);
        int labelEvery = 1;

        while (barWidth * (float) labelEvery < minBarLabelSpacing)
            labelEvery *= 2;

        const auto firstBar = std::max (0, (int) std::floor (view.leftBeat / beatsPerBar));
        const auto lastBar = (int) std::ceil (view.getRightBeat() / beatsPerBar);

        g.setFont (juce::FontOptions (theme::size::smallFontSize));

        for (int bar = firstBar - firstBar % labelEvery; bar <= lastBar; bar += labelEvery)
        {
            const auto x = std::round (view.beatToX (bar * beatsPerBar));
            g.setColour (theme::pianoroll::barLine);
            g.fillRect (x, bounds.getHeight() * 0.35f, 1.0f, bounds.getHeight() * 0.65f);
            g.setColour (theme::colour::textSecondary);
            g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 4.0f, 0.0f, 40.0f, bounds.getHeight() - rangeStripHeight),
                        juce::Justification::centredLeft, false);
        }

        if (playheadBeat.has_value())
        {
            const auto x = view.beatToX (*playheadBeat);
            juce::Path triangle;
            triangle.addTriangle (x - playheadSize * 0.5f, bounds.getBottom() - playheadSize,
                                  x + playheadSize * 0.5f, bounds.getBottom() - playheadSize,
                                  x, bounds.getBottom());
            g.setColour (theme::pianoroll::playhead);
            g.fillPath (triangle);
        }

        g.setColour (theme::colour::border);
        g.fillRect (0.0f, bounds.getBottom() - 1.0f, bounds.getWidth(), 1.0f);
    }

    void ArrangementRuler::setPlayheadBeat (std::optional<double> beat)
    {
        if (beat != playheadBeat)
        {
            playheadBeat = beat;
            repaint();
        }
    }

    double ArrangementRuler::snapped (const juce::MouseEvent& e) const
    {
        const auto mode = e.mods.isAltDown() ? snap::Mode::none : model.getSnap();
        return std::max (0.0, snap::snapNearest (view.xToBeat (e.position.x), mode, model.getSnapContext()));
    }

    void ArrangementRuler::mouseDown (const juce::MouseEvent& e)
    {
        settingLoop = e.mods.isCommandDown();
        loopAnchor = snapped (e);

        if (! settingLoop && onPositionChosen != nullptr)
            onPositionChosen (loopAnchor);
    }

    void ArrangementRuler::mouseDrag (const juce::MouseEvent& e)
    {
        if (settingLoop)
        {
            if (e.mouseWasDraggedSinceMouseDown())
            {
                model.setPlayMode (PlayMode::song);
                model.setLoopRegion ({ loopAnchor, snapped (e) });
                repaint();
            }
        }
        else if (onPositionChosen != nullptr)
        {
            onPositionChosen (snapped (e));
        }
    }

    void ArrangementRuler::mouseUp (const juce::MouseEvent& e)
    {
        if (settingLoop && ! e.mouseWasDraggedSinceMouseDown())
            model.clearLoopRegion();

        settingLoop = false;
        repaint();
    }
}
