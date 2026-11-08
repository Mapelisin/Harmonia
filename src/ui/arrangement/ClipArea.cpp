#include "ui/arrangement/ClipArea.h"
#include "core/Clips.h"
#include "ui/Theme.h"
#include <set>

namespace harmonia::ui::arrangement
{
    namespace colours = theme::arrangement;

    namespace
    {
        constexpr float headerHeight = ArrangementGestures::menuMarkSize;
        constexpr float minBarPixels = 6.0f;
    }

    ClipArea::ClipArea (ArrangementModel& m, ArrangementView& v)
        : model (m), view (v)
    {
        setOpaque (true);
        setWantsKeyboardFocus (true);
        gestures.onViewScrolled = [this]
        {
            if (onViewChanged != nullptr)
                onViewChanged();
        };
    }

    //==============================================================================
    void ClipArea::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds();
        const auto area = g.getClipBounds();
        g.setColour (colours::belowTracks);
        g.fillRect (area);

        const auto numTracks = model.getNumTracks();
        const auto current = model.indexOf (model.getCurrentTrack());
        const auto firstRow = std::max (0, view.yToTrack ((float) area.getY()));
        const auto lastRow = std::min (numTracks - 1, view.yToTrack ((float) area.getBottom()));

        for (int i = firstRow; i <= lastRow; ++i)
        {
            const auto y = view.trackToY (i);
            g.setColour (i == current ? colours::laneCurrent : (i % 2 == 0 ? colours::laneEven : colours::laneOdd));
            g.fillRect (juce::Rectangle<float> ((float) area.getX(), y, (float) area.getWidth(), view.trackHeight));
        }

        paintGrid (g, area);

        for (int i = firstRow; i <= lastRow; ++i)
        {
            g.setColour (theme::colour::border);
            g.fillRect (juce::Rectangle<float> ((float) area.getX(), view.trackToY (i) + view.trackHeight - 1.0f, (float) area.getWidth(), 1.0f));
        }

        if (model.getPlayMode() == PlayMode::song)
        {
            if (const auto region = model.getLoopRegion())
            {
                g.setColour (colours::loopRegion);
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (view.beatToX (region->start), 0.0f,
                                                                        view.beatToX (region->end), (float) bounds.getHeight()));
            }
        }

        const auto leftBeat = view.xToBeat ((float) area.getX()), rightBeat = view.xToBeat ((float) area.getRight());

        for (const auto& d : model.getClips())
            if (d.trackIndex >= firstRow && d.trackIndex <= lastRow && d.endBeat > leftBeat && d.startBeat < rightBeat)
                paintClip (g, d);

        forgetRemovedWaveforms();
        paintRecording (g);
        paintDropMarker (g);

        if (const auto rectangle = gestures.getSelectionRectangle())
        {
            g.setColour (theme::pianoroll::selectionFill);
            g.fillRect (*rectangle);
            g.setColour (theme::colour::accent);
            g.drawRect (*rectangle, 1.0f);
        }

        g.setColour (theme::colour::textSecondary);
        g.setFont (juce::FontOptions (theme::size::fontSize));

        if (numTracks == 0)
            g.drawText ("No tracks yet: + Track adds one", bounds, juce::Justification::centred);
        else if (model.getClips().empty())
            g.drawText ("Double-click in a track's row to add a clip, or drop audio files (WAV, MP3, FLAC...) here",
                        bounds.withTop ((int) view.trackToY (numTracks)).withHeight ((int) view.trackHeight), juce::Justification::centred);

        if (playheadBeat.has_value())
        {
            g.setColour (theme::pianoroll::playhead);
            g.fillRect (juce::Rectangle<float> (std::round (view.beatToX (*playheadBeat)), 0.0f, 1.0f, (float) bounds.getHeight()));
        }
    }

    void ClipArea::paintGrid (juce::Graphics& g, juce::Rectangle<int> area)
    {
        const auto beatsPerBar = model.getBeatsPerBar();
        const auto barPixels = (float) (beatsPerBar * view.pixelsPerBeat);
        const auto bottom = std::min ((float) area.getBottom(), view.trackToY (model.getNumTracks()));

        if (bottom <= (float) area.getY())
            return;

        const auto firstBar = std::max (0, (int) std::floor (view.xToBeat ((float) area.getX()) / beatsPerBar));
        const auto lastBar = (int) std::ceil (view.xToBeat ((float) area.getRight()) / beatsPerBar);
        const auto top = (float) area.getY();

        if (view.pixelsPerBeat >= 8.0)
        {
            g.setColour (theme::pianoroll::stepLine);

            for (int bar = firstBar; bar <= lastBar; ++bar)
                for (int beat = 1; beat < (int) beatsPerBar; ++beat)
                    g.fillRect (std::round (view.beatToX (bar * beatsPerBar + beat)), top, 1.0f, bottom - top);
        }

        int barStep = 1;

        while (barPixels * (float) barStep < minBarPixels)
            barStep *= 2;

        for (int bar = firstBar - firstBar % barStep; bar <= lastBar; bar += barStep)
        {
            g.setColour (bar % (barStep * 4) == 0 ? theme::pianoroll::barLine : theme::pianoroll::beatLine);
            g.fillRect (std::round (view.beatToX (bar * beatsPerBar)), top, 1.0f, bottom - top);
        }
    }

    void ClipArea::paintClip (juce::Graphics& g, const DrawnClip& d)
    {
        const auto top = view.trackToY (d.trackIndex);
        auto r = juce::Rectangle<float>::leftTopRightBottom (view.beatToX (d.startBeat), top + 2.0f,
                                                             view.beatToX (d.endBeat), top + view.trackHeight - 2.0f);
        r.setWidth (std::max (r.getWidth(), 3.0f));

        const auto colour = colours::getTrackColour (d.trackIndex);
        const auto header = r.withHeight (headerHeight);
        const auto body = r.withTrimmedTop (headerHeight);

        g.setColour (colour.withMultipliedBrightness (0.42f).withMultipliedSaturation (0.8f));
        g.fillRect (body);
        g.setColour (d.open ? colour.brighter (0.15f) : colour.withMultipliedBrightness (0.85f));
        g.fillRect (header);

        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (r.toNearestInt());

            if (auto* midi = dynamic_cast<te::MidiClip*> (d.clip))
                paintNotes (g, *midi, r, body, colour);
            else if (auto* audio = dynamic_cast<te::AudioClipBase*> (d.clip))
                paintWaveform (g, *audio, body, colour);

            // Header: the menu mark, the link mark and the name.
            auto text = header.reduced (2.0f, 0.0f);

            if (r.getWidth() >= ArrangementGestures::menuMarkSize * 2.5f)
            {
                const auto mark = text.removeFromLeft (headerHeight - 4.0f).reduced (1.5f, 4.0f);
                juce::Path triangle;
                triangle.addTriangle (mark.getX(), mark.getY(), mark.getRight(), mark.getY(), mark.getCentreX(), mark.getBottom());
                g.setColour (colours::clipText);
                g.fillPath (triangle);
                text.removeFromLeft (2.0f);
            }

            const auto name = (d.linked ? juce::String::fromUTF8 ("\xe2\x88\x9e ") : juce::String()) + d.clip->getName();
            g.setColour (colours::clipText);
            g.setFont (juce::FontOptions (theme::size::smallFontSize));
            g.drawText (name, text, juce::Justification::centredLeft, true);
        }

        if (d.open)
        {
            g.setColour (colours::clipOpen);
            g.drawRect (r, 1.5f);
        }
        else if (d.selected)
        {
            g.setColour (theme::pianoroll::noteSelected);
            g.drawRect (r, 1.5f);
        }
        else
        {
            g.setColour (theme::pianoroll::noteBorder);
            g.drawRect (r, 1.0f);
        }

        if (d.open && d.selected)
        {
            g.setColour (theme::pianoroll::noteSelected);
            g.drawRect (r.reduced (1.5f), 1.0f);
        }
    }

    void ClipArea::paintNotes (juce::Graphics& g, te::MidiClip& clip, juce::Rectangle<float> r,
                               juce::Rectangle<float> body, juce::Colour colour)
    {
        // A small picture of the notes: the clip's own pitch range fills the body.
        const auto& notes = clip.getSequence().getNotes();

        if (notes.isEmpty())
            return;

        int lowest = 127, highest = 0;

        for (auto* n : notes)
        {
            lowest = std::min (lowest, n->getNoteNumber());
            highest = std::max (highest, n->getNoteNumber());
        }

        const auto inner = body.reduced (0.0f, 3.0f);
        const auto span = std::max (1, highest - lowest);
        const auto noteHeight = juce::jlimit (1.5f, 4.0f, inner.getHeight() / (float) (span + 1));
        const auto usable = inner.getHeight() - noteHeight;
        const auto contentStart = core::clips::getContentStartBeat (clip);
        const auto clipLeft = view.xToBeat (r.getX()), clipRight = view.xToBeat (r.getRight());

        g.setColour (colour.brighter (0.5f));

        for (auto* n : notes)
        {
            const auto start = contentStart + n->getStartBeat().inBeats();
            const auto end = start + n->getLengthBeats().inBeats();

            if (end <= clipLeft || start >= clipRight)
                continue;

            const auto position = highest == lowest ? 0.5f : (float) (n->getNoteNumber() - lowest) / (float) span;
            const auto x = view.beatToX (start);
            g.fillRect (juce::Rectangle<float> (x, inner.getY() + usable * (1.0f - position),
                                                std::max (1.0f, view.beatToX (end) - x), noteHeight));
        }
    }

    void ClipArea::paintWaveform (juce::Graphics& g, te::AudioClipBase& clip, juce::Rectangle<float> body, juce::Colour colour)
    {
        const auto file = clip.getCurrentSourceFile();

        if (! file.existsAsFile())
        {
            g.setColour (theme::colour::warning);
            g.setFont (juce::FontOptions (theme::size::smallFontSize));
            g.drawText ("File missing: " + file.getFileName(), body.reduced (4.0f, 0.0f), juce::Justification::centredLeft, true);
            return;
        }

        // Only the part on screen: the clip may be much wider than the window.
        const auto visible = body.reduced (0.0f, 2.0f).getIntersection (g.getClipBounds().toFloat()).getSmallestIntegerContainer();

        if (visible.isEmpty())
            return;

        // Which stretch of the file plays there (clips play their file at its own speed).
        const auto& tempo = model.getEdit().tempoSequence;
        const auto sourceStart = clip.getPosition().getStartOfSource();
        const auto fileTimeAt = [&] (int x)
        {
            const auto songTime = tempo.toTime (te::BeatPosition::fromBeats (view.xToBeat ((float) x)));
            return te::TimePosition::fromSeconds ((songTime - sourceStart).inSeconds());
        };

        auto& thumbnail = getWaveform (clip);
        g.setColour (colour.brighter (0.55f));
        thumbnail.drawChannels (g, visible, { fileTimeAt (visible.getX()), fileTimeAt (visible.getRight()) },
                                clip.getGain());
    }

    te::SmartThumbnail& ClipArea::getWaveform (te::AudioClipBase& clip)
    {
        auto& engine = model.getEdit().engine;
        const auto file = clip.getCurrentSourceFile();
        auto& waveform = waveforms[clip.itemID.getRawID()];

        if (waveform.thumbnail == nullptr)
            waveform.thumbnail = std::make_unique<te::SmartThumbnail> (engine, te::AudioFile (engine, file), *this, nullptr);
        else if (waveform.file != file)
            waveform.thumbnail->setNewFile (te::AudioFile (engine, file));

        waveform.file = file;
        return *waveform.thumbnail;
    }

    void ClipArea::forgetRemovedWaveforms()
    {
        std::set<juce::uint64> present;

        for (const auto& d : model.getClips())
            if (d.audio)
                present.insert (d.clip->itemID.getRawID());

        for (auto it = waveforms.begin(); it != waveforms.end();)
            it = present.count (it->first) == 0 ? waveforms.erase (it) : std::next (it);
    }

    bool ClipArea::areWaveformsLoaded() const
    {
        for (const auto& d : model.getClips())
        {
            if (! d.audio)
                continue;

            auto* clip = dynamic_cast<te::AudioClipBase*> (d.clip);

            if (clip == nullptr || ! clip->getCurrentSourceFile().existsAsFile())
                continue;

            const auto it = waveforms.find (clip->itemID.getRawID());

            if (it == waveforms.end())
                return false;

            // An empty thumbnail counts as "fully loaded" before it starts reading.
            const auto& thumbnail = *it->second.thumbnail;

            if (thumbnail.isOutOfDate() || thumbnail.getTotalLength() <= 0.0 || ! thumbnail.isFullyLoaded())
                return false;
        }

        return true;
    }

    void ClipArea::paintDropMarker (juce::Graphics& g)
    {
        if (! dropMarker.has_value())
            return;

        const auto numTracks = model.getNumTracks();
        const auto x = std::round (view.beatToX (dropMarker->beat));
        juce::Rectangle<float> lane;

        if (dropMarker->newTrack)
        {
            // A new track goes below the one pointed at (or below them all).
            const auto below = dropMarker->trackIndex >= 0 ? dropMarker->trackIndex + 1 : numTracks;
            const auto y = view.trackToY (below);
            lane = { 0.0f, y, (float) getWidth(), view.trackHeight };

            g.setColour (theme::colour::accent);
            g.fillRect (0.0f, y - 1.0f, (float) getWidth(), 2.0f);
            g.setColour (theme::colour::accent.withAlpha (0.08f));
            g.fillRect (lane);
            g.setColour (theme::colour::textSecondary);
            g.setFont (juce::FontOptions (theme::size::smallFontSize));
            g.drawText ("New audio track", lane.withTrimmedLeft (x + 6.0f).toNearestInt(), juce::Justification::centredLeft, true);
        }
        else
        {
            lane = { 0.0f, view.trackToY (dropMarker->trackIndex), (float) getWidth(), view.trackHeight };
            g.setColour (theme::colour::accent.withAlpha (0.12f));
            g.fillRect (lane);
        }

        g.setColour (theme::colour::accent);
        g.fillRect (x, lane.getY(), 2.0f, lane.getHeight());
    }

    void ClipArea::paintRecording (juce::Graphics& g)
    {
        if (! recording.has_value())
            return;

        for (auto index : recording->trackIndices)
        {
            const auto top = view.trackToY (index);
            const auto r = juce::Rectangle<float>::leftTopRightBottom (view.beatToX (recording->startBeat), top + 2.0f,
                                                                       view.beatToX (recording->endBeat), top + view.trackHeight - 2.0f);
            g.setColour (theme::colour::record.withAlpha (0.3f));
            g.fillRect (r);
            g.setColour (theme::colour::record);
            g.drawRect (r, 1.0f);
            g.setColour (theme::colour::textPrimary);
            g.setFont (juce::FontOptions (theme::size::smallFontSize));
            g.drawText ("Recording...", r.reduced (4.0f, 0.0f).withHeight (headerHeight), juce::Justification::centredLeft, true);
        }
    }

    void ClipArea::setRecording (std::optional<RecordingMarker> marker)
    {
        if (marker == recording)
            return;

        recording = marker;
        repaint();
    }

    void ClipArea::setDropMarker (std::optional<DropMarker> marker)
    {
        if (marker == dropMarker)
            return;

        dropMarker = marker;
        repaint();
    }

    //==============================================================================
    void ClipArea::setPlayheadBeat (std::optional<double> beat)
    {
        if (beat == playheadBeat)
            return;

        repaintPlayheadAt (playheadBeat);
        playheadBeat = beat;
        repaintPlayheadAt (playheadBeat);
    }

    void ClipArea::repaintPlayheadAt (std::optional<double> beat)
    {
        if (beat.has_value())
            repaint (juce::Rectangle<int> ((int) std::round (view.beatToX (*beat)) - 2, 0, 5, getHeight()));
    }

    ArrangementGestures::Pointer ClipArea::toPointer (const juce::MouseEvent& e)
    {
        return { e.position, e.mods, e.getNumberOfClicks() };
    }

    void ClipArea::mouseMove (const juce::MouseEvent& e)
    {
        setMouseCursor (gestures.getCursorFor (toPointer (e)));
    }

    void ClipArea::mouseDown (const juce::MouseEvent& e)
    {
        grabKeyboardFocus();
        gestures.down (toPointer (e));
        setMouseCursor (gestures.getCursorFor (toPointer (e)));
        repaint();
    }

    void ClipArea::mouseDrag (const juce::MouseEvent& e)
    {
        gestures.drag (toPointer (e));
        repaint();
    }

    void ClipArea::mouseUp (const juce::MouseEvent& e)
    {
        gestures.up (toPointer (e));
        setMouseCursor (gestures.getCursorFor (toPointer (e)));
        repaint();
    }

    void ClipArea::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        if (e.mods.isCtrlDown())
            view.zoomHorizontally (std::pow (2.0, (double) wheel.deltaY * 2.0), e.position.x);
        else if (e.mods.isShiftDown())
            view.scrollByPixels (-wheel.deltaY * 400.0, 0.0);
        else
            view.scrollByPixels (-wheel.deltaX * 400.0, -wheel.deltaY * 200.0);

        if (onViewChanged != nullptr)
            onViewChanged();
    }
}
