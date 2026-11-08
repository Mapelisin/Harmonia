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
        constexpr float flagPadding = 5.0f;

        juce::FontOptions markerFont()
        {
            return juce::FontOptions (theme::size::smallFontSize, juce::Font::bold);
        }
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

        paintMarkers (g);

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
            const auto barsTop = markerLaneHeight;
            const auto barsHeight = bounds.getHeight() - barsTop;
            g.setColour (theme::pianoroll::barLine);
            g.fillRect (x, barsTop + barsHeight * 0.35f, 1.0f, barsHeight * 0.65f);
            g.setColour (theme::colour::textSecondary);
            g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 4.0f, barsTop, 40.0f, barsHeight - rangeStripHeight),
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

    juce::Rectangle<float> ArrangementRuler::getFlag (const core::markers::Marker& marker, juce::Graphics*) const
    {
        const auto textWidth = juce::GlyphArrangement::getStringWidth (juce::Font (markerFont()), marker.name);
        return { view.beatToX (marker.beat), 1.0f, textWidth + flagPadding * 2.0f, markerLaneHeight - 2.0f };
    }

    void ArrangementRuler::paintMarkers (juce::Graphics& g)
    {
        const auto markers = model.getMarkers();
        g.setFont (markerFont());

        for (size_t i = 0; i < markers.size(); ++i)
        {
            auto flag = getFlag (markers[i], &g);

            // Not over the next marker's flag.
            if (i + 1 < markers.size())
                flag.setRight (std::max (flag.getX() + 6.0f, std::min (flag.getRight(), view.beatToX (markers[i + 1].beat) - 2.0f)));

            if (flag.getRight() < 0.0f || flag.getX() > (float) getWidth())
                continue;

            const auto dragged = draggedMarker.has_value() && draggedMarker->state == markers[i].state;
            g.setColour ((dragged ? theme::colour::accent.brighter (0.2f) : theme::colour::accent).withAlpha (0.85f));
            g.fillRoundedRectangle (flag, 3.0f);
            g.fillRect (flag.getX(), 1.0f, 2.0f, (float) getHeight() - 1.0f);    // its line down to the bars
            g.setColour (theme::colour::background);
            g.drawText (markers[i].name, flag.withTrimmedLeft (flagPadding), juce::Justification::centredLeft, true);
        }
    }

    std::optional<core::markers::Marker> ArrangementRuler::findMarkerAt (juce::Point<float> position) const
    {
        if (position.y >= markerLaneHeight)
            return std::nullopt;

        std::optional<core::markers::Marker> found;

        // The later flag is drawn on top.
        for (const auto& marker : model.getMarkers())
            if (getFlag (marker, nullptr).expanded (2.0f, 0.0f).contains (position.x, 2.0f))
                found = marker;

        return found;
    }

    juce::PopupMenu ArrangementRuler::createMenu (double beat, std::optional<core::markers::Marker> marker)
    {
        juce::PopupMenu menu;

        if (marker.has_value())
        {
            const auto m = *marker;
            menu.addSectionHeader (m.name);
            menu.addItem ("Rename...", [this, m] { if (onRenameMarker != nullptr) onRenameMarker (m); });
            menu.addItem ("Loop this section", [this, m] { model.loopSection (m); });
            menu.addItem ("Delete marker", [this, m] { model.removeMarker (m); });
            menu.addSeparator();
        }

        juce::PopupMenu names;

        for (const auto& name : core::markers::getSuggestedNames())
            names.addItem (name, [this, beat, name] { model.addMarker (beat, name); });

        names.addSeparator();
        names.addItem ("Other name...", [this, beat]
        {
            const auto added = model.addMarker (beat, model.getNewMarkerName());

            if (onRenameMarker != nullptr)
                onRenameMarker (added);
        });

        menu.addSubMenu ("Add marker here", names);
        return menu;
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
        draggedMarker.reset();
        markerMoved = false;
        const auto marker = findMarkerAt (e.position);

        if (e.mods.isPopupMenu())
        {
            createMenu (snapped (e), marker).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                                                      .withMousePosition());
            return;
        }

        // A marker: drag it, or click to play from it.
        if (marker.has_value())
        {
            draggedMarker = marker;
            dragOffsetBeats = marker->beat - view.xToBeat (e.position.x);
            return;
        }

        settingLoop = e.mods.isCommandDown();
        loopAnchor = snapped (e);

        if (! settingLoop && onPositionChosen != nullptr)
            onPositionChosen (loopAnchor);
    }

    void ArrangementRuler::mouseDrag (const juce::MouseEvent& e)
    {
        if (draggedMarker.has_value())
        {
            if (! e.mouseWasDraggedSinceMouseDown())
                return;

            const auto mode = e.mods.isAltDown() ? snap::Mode::none : model.getSnap();
            const auto beat = std::max (0.0, snap::snapNearest (view.xToBeat (e.position.x) + dragOffsetBeats, mode,
                                                                model.getSnapContext()));

            if (std::abs (beat - draggedMarker->beat) > 1.0e-9)
            {
                // One undo step for the whole drag.
                if (! markerMoved)
                    model.getProject().beginGesture ("Move marker");

                markerMoved = true;
                core::markers::move (model.getEdit(), *draggedMarker, beat);
                draggedMarker->beat = beat;
                repaint();
            }

            return;
        }

        if (e.mods.isPopupMenu())
            return;

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
        if (draggedMarker.has_value())
        {
            // A click on a marker plays from there.
            if (! markerMoved && onPositionChosen != nullptr)
                onPositionChosen (draggedMarker->beat);

            draggedMarker.reset();
            repaint();
            return;
        }

        if (e.mods.isPopupMenu())
            return;

        if (settingLoop && ! e.mouseWasDraggedSinceMouseDown())
            model.clearLoopRegion();

        settingLoop = false;
        repaint();
    }

    void ArrangementRuler::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (e.position.y >= markerLaneHeight || e.mods.isPopupMenu())
            return;

        // On a marker: rename it. Elsewhere in the lane: a new one there.
        if (const auto marker = findMarkerAt (e.position))
        {
            if (onRenameMarker != nullptr)
                onRenameMarker (*marker);

            return;
        }

        model.addMarker (snapped (e), model.getNewMarkerName());
        repaint();
    }
}
