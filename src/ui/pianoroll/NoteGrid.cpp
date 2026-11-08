#include "ui/pianoroll/NoteGrid.h"
#include "ui/Theme.h"

namespace harmonia::ui::pianoroll
{
    namespace colours = theme::pianoroll;
    namespace snap = core::snap;

    namespace
    {
        constexpr double minGridPixels = 6.0;
        constexpr float minLabelWidth = 26.0f, minLabelHeight = 10.0f;
        constexpr size_t maxLabelledNotes = 3000;

        bool isBlackKey (int pitch)
        {
            return juce::MidiMessage::isMidiNoteBlack (pitch);
        }
    }

    double getVisibleGridBeats (PianoRollModel& model, const View& view)
    {
        model.setPixelsPerBeat (view.pixelsPerBeat);
        const auto context = model.getSnapContext();
        const auto mode = model.getSnap();

        if (mode != snap::Mode::none && mode != snap::Mode::line && mode != snap::Mode::cell)
        {
            const auto grid = snap::getGridBeats (mode, context);

            if (grid * view.pixelsPerBeat >= minGridPixels)
                return std::min (grid, context.lineBeats);
        }

        return context.lineBeats;
    }

    NoteGrid::NoteGrid (PianoRollModel& m, View& v)
        : model (m), view (v)
    {
        setOpaque (true);
        gestures.onViewScrolled = [this]
        {
            if (onViewChanged != nullptr)
                onViewChanged();
        };
    }

    //==============================================================================
    void NoteGrid::paint (juce::Graphics& g)
    {
        const auto area = g.getClipBounds();

        paintRows (g, area);
        paintGridLines (g, area);
        paintClipAndTimeSelection (g, area);

        if (model.areGhostsVisible())
            paintGhostNotes (g, area);

        paintNotes (g, area);
        paintOverlays (g);

        if (model.getClip() == nullptr)
        {
            g.setColour (theme::colour::textSecondary);
            g.setFont (juce::FontOptions (theme::size::fontSize));
            g.drawText (emptyText, getLocalBounds(), juce::Justification::centred);
        }
    }

    juce::String NoteGrid::getDefaultEmptyText()
    {
        return "No clip open: click a clip in the arrangement, or double-click in a track's row to make one";
    }

    void NoteGrid::setEmptyText (const juce::String& text)
    {
        if (text != emptyText)
        {
            emptyText = text;
            repaint();
        }
    }

    void NoteGrid::paintRows (juce::Graphics& g, juce::Rectangle<int> area)
    {
        g.setColour (colours::whiteRow);
        g.fillRect (area);

        const auto topPitch = view.yToPitch ((float) area.getY());
        const auto bottomPitch = view.yToPitch ((float) area.getBottom());
        const auto x = (float) area.getX(), w = (float) area.getWidth();

        juce::RectangleList<float> blackRows, octaveLines;

        for (int pitch = bottomPitch; pitch <= topPitch; ++pitch)
        {
            const auto y = view.pitchToY (pitch);

            if (isBlackKey (pitch))
                blackRows.addWithoutMerging ({ x, y, w, view.rowHeight });

            if (pitch % 12 == 0)    // line under every C
                octaveLines.addWithoutMerging ({ x, y + view.rowHeight - 1.0f, w, 1.0f });
        }

        g.setColour (colours::blackRow);
        g.fillRectList (blackRows);
        g.setColour (colours::octaveLine);
        g.fillRectList (octaveLines);
    }

    void NoteGrid::paintGridLines (juce::Graphics& g, juce::Rectangle<int> area)
    {
        const auto fine = getVisibleGridBeats (model, view);
        const auto beatsPerBar = model.getBeatsPerBar();
        const auto first = std::floor (view.xToBeat ((float) area.getX()) / fine);
        const auto last = std::ceil (view.xToBeat ((float) area.getRight()) / fine);
        const auto y = (float) area.getY(), h = (float) area.getHeight();

        juce::RectangleList<float> bars, beats, steps;

        for (auto i = first; i <= last; i += 1.0)
        {
            const auto beat = i * fine;
            const auto x = std::round (view.beatToX (beat));
            const juce::Rectangle<float> line { x, y, 1.0f, h };

            const auto isWhole = [beat] (double unit) { return std::abs (beat / unit - std::round (beat / unit)) < 1.0e-6; };

            if (isWhole (beatsPerBar))
                bars.addWithoutMerging (line);
            else if (isWhole (1.0))
                beats.addWithoutMerging (line);
            else
                steps.addWithoutMerging (line);
        }

        g.setColour (colours::stepLine);
        g.fillRectList (steps);
        g.setColour (colours::beatLine);
        g.fillRectList (beats);
        g.setColour (colours::barLine);
        g.fillRectList (bars);
    }

    void NoteGrid::paintClipAndTimeSelection (juce::Graphics& g, juce::Rectangle<int> area)
    {
        const auto bounds = area.toFloat();

        if (auto range = model.getTimeSelection())
        {
            g.setColour (colours::timeSelection);
            g.fillRect (bounds.withLeft (std::max (bounds.getX(), view.beatToX (range->start)))
                              .withRight (std::min (bounds.getRight(), view.beatToX (range->end))));
        }

        // Darken what's outside the clip: notes there don't play.
        g.setColour (colours::outsideClip);

        if (model.getClip() == nullptr)
        {
            g.fillRect (bounds);
            return;
        }

        const auto clipStartX = view.beatToX (model.getClipStartBeat());
        const auto clipEndX = view.beatToX (model.getClipEndBeat());

        if (clipStartX > bounds.getX())
            g.fillRect (bounds.withRight (std::min (bounds.getRight(), clipStartX)));

        if (clipEndX < bounds.getRight())
            g.fillRect (bounds.withLeft (std::max (bounds.getX(), clipEndX)));
    }

    void NoteGrid::paintGhostNotes (juce::Graphics& g, juce::Rectangle<int> area)
    {
        const auto& ghosts = model.getGhostNotes();
        const auto [first, last] = model.getGhostNotesInRange (view.xToBeat ((float) area.getX()),
                                                               view.xToBeat ((float) area.getRight()));
        const auto topPitch = view.yToPitch ((float) area.getY());
        const auto bottomPitch = view.yToPitch ((float) area.getBottom());

        juce::RectangleList<float> rects;

        for (auto i = first; i < last; ++i)
        {
            const auto& n = ghosts[i];

            if (n.pitch >= bottomPitch && n.pitch <= topPitch)
                rects.addWithoutMerging (view.getNoteBounds (n.startBeat, n.lengthBeats, n.pitch).reduced (0.0f, 1.0f));
        }

        g.setColour (colours::ghostNote);
        g.fillRectList (rects);
    }

    void NoteGrid::paintNotes (juce::Graphics& g, juce::Rectangle<int> area)
    {
        const auto& notes = model.getNotes();
        const auto [first, last] = model.getNotesInRange (view.xToBeat ((float) area.getX()),
                                                          view.xToBeat ((float) area.getRight()));
        const auto topPitch = view.yToPitch ((float) area.getY());
        const auto bottomPitch = view.yToPitch ((float) area.getBottom());
        const auto areaLeft = (float) area.getX();

        // One list per fill colour: 16 colour groups, selected, muted.
        constexpr int selectedBucket = core::note::numColours, mutedBucket = selectedBucket + 1;
        std::array<juce::RectangleList<float>, (size_t) mutedBucket + 1> fills;
        juce::RectangleList<float> borders;
        std::vector<size_t> labelled;

        for (auto i = first; i < last; ++i)
        {
            const auto& d = notes[i];
            const auto& v = d.values;

            if (v.pitch < bottomPitch || v.pitch > topPitch || v.getEndBeat() < view.xToBeat (areaLeft))
                continue;

            const auto bounds = view.getNoteBounds (v.startBeat, v.lengthBeats, v.pitch);
            borders.addWithoutMerging (bounds);

            const auto bucket = d.selected ? selectedBucket : (v.muted ? mutedBucket : v.colour);

            if (bounds.getWidth() > 3.0f && bounds.getHeight() > 3.0f)
                fills[(size_t) bucket].addWithoutMerging (bounds.reduced (1.0f));

            if (bounds.getWidth() >= minLabelWidth && bounds.getHeight() >= minLabelHeight && labelled.size() < maxLabelledNotes)
                labelled.push_back (i);
        }

        g.setColour (colours::noteBorder);
        g.fillRectList (borders);

        for (size_t b = 0; b < fills.size(); ++b)
        {
            if (fills[b].isEmpty())
                continue;

            const auto bucket = (int) b;
            g.setColour (bucket == selectedBucket ? colours::noteSelected
                                                  : bucket == mutedBucket ? colours::noteMuted
                                                                          : colours::getNoteColour (bucket));
            g.fillRectList (fills[b]);
        }

        if (labelled.empty())
            return;

        g.setColour (colours::noteLabel);
        g.setFont (juce::FontOptions (std::min (theme::size::smallFontSize, view.rowHeight - 2.0f)));

        for (auto i : labelled)
        {
            const auto& v = notes[i].values;
            auto bounds = view.getNoteBounds (v.startBeat, v.lengthBeats, v.pitch).reduced (4.0f, 0.0f);

            // Keep the name readable when the note starts left of the view.
            if (bounds.getX() < areaLeft + 4.0f && bounds.getRight() > areaLeft + minLabelWidth)
                bounds.setLeft (std::max (bounds.getX(), 4.0f));

            g.drawText (juce::MidiMessage::getMidiNoteName (v.pitch, true, true, 4), bounds,
                        juce::Justification::centredLeft, false);
        }
    }

    void NoteGrid::paintOverlays (juce::Graphics& g)
    {
        // Notes being recorded, keys still down.
        for (const auto& n : model.getLiveNotes())
        {
            const auto bounds = view.getNoteBounds (n.startBeat, n.lengthBeats, n.pitch).reduced (0.0f, 1.0f);
            g.setColour (colours::recordingNote);
            g.fillRect (bounds);
            g.setColour (colours::noteBorder);
            g.drawRect (bounds, 1.0f);
        }

        if (auto rect = gestures.getSelectionRectangle())
        {
            g.setColour (colours::selectionFill);
            g.fillRect (*rect);
            g.setColour (theme::colour::accent);
            g.drawRect (*rect, 1.0f);
        }

        if (auto line = gestures.getSliceLine())
        {
            g.setColour (colours::sliceLine);
            g.drawLine (*line, 1.5f);
        }

        if (playheadBeat.has_value())
        {
            g.setColour (colours::playhead);
            g.fillRect (std::round (view.beatToX (*playheadBeat)), 0.0f, 1.0f, (float) getHeight());
        }
    }

    //==============================================================================
    void NoteGrid::setPlayheadBeat (std::optional<double> beat)
    {
        if (beat.has_value() && playheadBeat.has_value()
            && std::abs (view.beatToX (*beat) - view.beatToX (*playheadBeat)) < 0.5f)
            return;

        repaint (getPlayheadArea());
        playheadBeat = beat;
        repaint (getPlayheadArea());
    }

    juce::Rectangle<int> NoteGrid::getPlayheadArea() const
    {
        if (! playheadBeat.has_value())
            return {};

        const auto x = (int) std::round (view.beatToX (*playheadBeat));
        return { x - 1, 0, 3, getHeight() };
    }

    //==============================================================================
    GridGestures::Pointer NoteGrid::toPointer (const juce::MouseEvent& e) const
    {
        return { e.position, e.mods, e.getNumberOfClicks() };
    }

    void NoteGrid::mouseDown (const juce::MouseEvent& e)
    {
        gestures.down (toPointer (e));
        repaint();
    }

    void NoteGrid::mouseDrag (const juce::MouseEvent& e)
    {
        gestures.drag (toPointer (e));

        if (gestures.getSelectionRectangle().has_value() || gestures.getSliceLine().has_value())
            repaint();
    }

    void NoteGrid::mouseUp (const juce::MouseEvent& e)
    {
        gestures.up (toPointer (e));
        setMouseCursor (gestures.getCursorFor (toPointer (e)));
        repaint();
    }

    void NoteGrid::mouseMove (const juce::MouseEvent& e)
    {
        setMouseCursor (gestures.getCursorFor (toPointer (e)));
    }

    void NoteGrid::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        if (e.mods.isAltDown())
        {
            // Alt+wheel changes the lower panel's value of the note under the mouse (Ctrl+Alt: finely).
            if (auto* d = model.findNoteAt (view.xToBeat (e.position.x), view.yToPitch (e.position.y)))
            {
                const auto direction = (wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX) > 0.0f ? 1.0f : -1.0f;
                model.nudgeProperty (*d->note, direction * (e.mods.isCtrlDown() ? 1.0f : 5.0f) / 127.0f);
            }

            return;
        }

        if (e.mods.isCtrlDown())
            view.zoomHorizontally (std::pow (2.0, (double) wheel.deltaY * 2.0), e.position.x);
        else if (e.mods.isShiftDown())
            view.scrollByPixels (-wheel.deltaY * 400.0, 0.0);
        else
            view.scrollByPixels (-wheel.deltaX * 400.0, -wheel.deltaY * 250.0);

        if (onViewChanged != nullptr)
            onViewChanged();
    }
}
