#include "ui/audioeditor/WaveformView.h"
#include "core/AudioTempo.h"
#include "core/Snap.h"
#include "ui/Theme.h"
#include "ui/TrackColours.h"

namespace harmonia::ui::audioeditor
{
    namespace clips = core::clips;
    namespace colours = theme::pianoroll;

    namespace
    {
        constexpr double minLinePixels = 10.0;
        constexpr float minBarLabelPixels = 36.0f;
    }

    WaveformView::WaveformView (AudioEditorModel& m, audio::Transport& t)
        : model (m), transport (t)
    {
        gestures.onPositionChosen = [this] (double beat)
        {
            if (onPositionChosen != nullptr)
                onPositionChosen (beat);
        };

        gestures.onViewChanged = [this] { repaint(); };
    }

    WaveformView::~WaveformView()
    {
        stopTimer();
    }

    //==============================================================================
    void WaveformView::clipChanged()
    {
        auto* clip = model.getClip();
        const auto another = clip != shownClip.get();
        shownClip = clip;

        if (clip != nullptr)
            updateThumbnail (*clip);
        else
            thumbnail.reset();

        view.fileSeconds = model.getFileSeconds();
        message = findMessage();

        if (another)
            showPlayedPart();
        else
            view.clamp();

        // The engine writes a reversed copy in the background: look again soon.
        if (clip != nullptr && clips::isReversing (*clip) && ! isTimerRunning())
            startTimer (250);

        updatePlayhead();
        repaint();
    }

    void WaveformView::showPlayedPart()
    {
        view.width = (float) getWidth();
        view.fileSeconds = model.getFileSeconds();

        if (model.getClip() != nullptr && view.fileSeconds > 0.0)
            view.show (model.getPlayedSeconds());

        repaint();
    }

    void WaveformView::updateThumbnail (te::AudioClipBase& clip)
    {
        const auto file = clip.getCurrentSourceFile();

        if (clips::isReversing (clip) || ! file.existsAsFile())
        {
            thumbnail.reset();
            shownFile = juce::File();
            return;
        }

        auto& engine = clip.edit.engine;

        if (thumbnail == nullptr)
            thumbnail = std::make_unique<te::SmartThumbnail> (engine, te::AudioFile (engine, file), *this, nullptr);
        else if (file != shownFile)
            thumbnail->setNewFile (te::AudioFile (engine, file));

        shownFile = file;
    }

    bool WaveformView::isWaveformLoaded() const
    {
        if (model.getClip() == nullptr || message.isNotEmpty())
            return true;

        return thumbnail != nullptr && ! thumbnail->isOutOfDate() && thumbnail->getTotalLength() > 0.0 && thumbnail->isFullyLoaded();
    }

    juce::String WaveformView::findMessage() const
    {
        auto* clip = model.getClip();

        if (clip == nullptr)
        {
            return "No audio clip on this track yet. Drop a sound file here from the Sounds panel or Explorer, "
                   "or record one (arm the track, then Record + Play). Click a clip in the arrangement to edit it here.";
        }

        if (clips::isReversing (*clip))
            return "Reversing...";

        if (const auto file = clip->getCurrentSourceFile(); ! file.existsAsFile())
            return "File missing: " + clip->getOriginalFile().getFullPathName();

        if (view.fileSeconds <= 0.0)
            return "This sound file can't be read.";

        return {};
    }

    void WaveformView::timerCallback()
    {
        if (auto* clip = model.getClip(); clip != nullptr && clips::isReversing (*clip))
            return;

        stopTimer();
        clipChanged();
    }

    //==============================================================================
    template <typename Callback>
    void WaveformView::forEachGridLine (Callback&& callback) const
    {
        if (model.getClip() == nullptr)
            return;

        const auto beatsPerBar = model.getArrangement().getBeatsPerBar();
        const auto lineBeats = core::snap::getLineBeats (model.getPixelsPerBeat (view.pixelsPerSecond), beatsPerBar, minLinePixels);
        const auto first = std::max (0.0, model.fileSecondsToBeat (view.leftSeconds));
        const auto last = model.fileSecondsToBeat (view.getRightSeconds());

        if (lineBeats <= 0.0 || last <= first)
            return;

        // Bar numbers far enough apart to read: every bar, or every 2nd, 4th... when zoomed out.
        const auto barPixels = model.getPixelsPerBeat (view.pixelsPerSecond) * beatsPerBar;
        auto barEvery = 1;

        while (barPixels * barEvery < minBarLabelPixels && barEvery < 4096)
            barEvery *= 2;

        const auto firstLine = std::ceil (first / lineBeats - 1.0e-9) * lineBeats;

        for (int i = 0; i < 4000; ++i)
        {
            const auto beat = firstLine + i * lineBeats;

            if (beat > last)
                break;

            const auto barPosition = beat / beatsPerBar;
            const auto isBar = std::abs (barPosition - std::round (barPosition)) < 1.0e-6;
            const auto bar = juce::roundToInt (barPosition);
            callback (view.secondsToX (model.beatToFileSeconds (beat)), isBar, isBar && bar % barEvery == 0 ? bar + 1 : 0);
        }
    }

    void WaveformView::paint (juce::Graphics& g)
    {
        g.fillAll (theme::colour::background);

        const auto bounds = getLocalBounds().toFloat();
        const auto ruler = bounds.withHeight ((float) rulerHeight);
        const auto body = gestures.getBody();
        auto* clip = model.getClip();

        paintRuler (g, ruler);

        // (The clip may have gone since the last clipChanged: undo, delete.)
        if (const auto text = clip != nullptr ? message : findMessage(); text.isNotEmpty())
        {
            const auto quiet = clip == nullptr || text == "Reversing...";
            g.setColour (quiet ? theme::colour::textSecondary : theme::colour::warning);
            g.setFont (juce::FontOptions (theme::size::fontSize));
            g.drawFittedText (text, body.reduced (24.0f, 12.0f).toNearestInt(), juce::Justification::centred, 3);
            return;
        }

        paintWave (g, *clip, body);
        paintEdges (g, body);

        // The beat being moved: its line, bright.
        if (const auto beat = gestures.getMovedBeat())
        {
            g.setColour (theme::arrangement::clipOpen);
            g.fillRect (std::round (view.secondsToX (model.beatToFileSeconds (*beat))), 0.0f, 1.0f, bounds.getHeight());
        }

        if (playheadX.has_value())
        {
            g.setColour (colours::playhead);
            g.fillRect (std::round (*playheadX), 0.0f, 1.0f, bounds.getHeight());
        }

        paintReadout (g);
    }

    void WaveformView::paintRuler (juce::Graphics& g, juce::Rectangle<float> ruler)
    {
        g.setColour (theme::colour::surface);
        g.fillRect (ruler);
        g.setColour (theme::colour::border);
        g.fillRect (ruler.getX(), ruler.getBottom() - 1.0f, ruler.getWidth(), 1.0f);

        g.setFont (juce::FontOptions (theme::size::smallFontSize));

        forEachGridLine ([&] (float x, bool isBar, int label)
        {
            const auto tick = isBar ? ruler.getHeight() * 0.5f : ruler.getHeight() * 0.2f;
            g.setColour (isBar ? colours::barLine : colours::beatLine);
            g.fillRect (x, ruler.getBottom() - tick, 1.0f, tick);

            if (label > 0)
            {
                g.setColour (theme::colour::textSecondary);
                g.drawText (juce::String (label), juce::Rectangle<float> (x + 3.0f, ruler.getY(), 40.0f, ruler.getHeight() - 2.0f),
                            juce::Justification::centredLeft, false);
            }
        });

        // Where the sound's tempo changes (a beat found off the others, or put by hand): a mark.
        g.setColour (theme::arrangement::clipOpen.withAlpha (0.85f));

        for (const auto seconds : model.getTempoChangeSeconds())
        {
            const auto x = view.secondsToX (seconds);

            if (x < ruler.getX() - 4.0f || x > ruler.getRight() + 4.0f)
                continue;

            juce::Path mark;
            mark.addTriangle (x - 3.5f, ruler.getBottom() - 7.0f, x + 3.5f, ruler.getBottom() - 7.0f, x, ruler.getBottom() - 1.0f);
            g.fillPath (mark);
        }
    }

    void WaveformView::paintWave (juce::Graphics& g, te::AudioClipBase& clip, juce::Rectangle<float> body)
    {
        const auto fileStartX = view.secondsToX (0.0), fileEndX = view.secondsToX (view.fileSeconds);
        const auto played = model.getPlayedSeconds();
        const auto playedRect = juce::Rectangle<float>::leftTopRightBottom (view.secondsToX (played.getStart()), body.getY(),
                                                                            view.secondsToX (played.getEnd()), body.getBottom());
        const auto fileRect = juce::Rectangle<float>::leftTopRightBottom (fileStartX, body.getY(), fileEndX, body.getBottom())
                                  .getIntersection (body);

        // Beyond the file: nothing. The file: dark, the part the clip plays a little lighter.
        g.setColour (theme::arrangement::belowTracks);
        g.fillRect (body);

        const auto colour = track_colours::getClipColour (clip, model.getArrangement().indexOf (clip.getClipTrack()));
        g.setColour (theme::colour::surface);
        g.fillRect (fileRect);
        g.setColour (colour.withMultipliedBrightness (0.42f).withMultipliedSaturation (0.8f));
        g.fillRect (playedRect.getIntersection (body));

        forEachGridLine ([&] (float x, bool isBar, int)
        {
            g.setColour ((isBar ? colours::barLine : colours::beatLine).withAlpha (isBar ? 0.55f : 0.35f));
            g.fillRect (x, body.getY(), 1.0f, body.getHeight());
        });

        // The waveform of the part on screen: dim where the clip doesn't play it, bright where it does.
        const auto visible = fileRect.reduced (0.0f, 2.0f).getSmallestIntegerContainer();

        if (thumbnail != nullptr && ! visible.isEmpty())
        {
            const te::TimeRange range { te::TimePosition::fromSeconds (view.xToSeconds ((float) visible.getX())),
                                        te::TimePosition::fromSeconds (view.xToSeconds ((float) visible.getRight())) };

            g.setColour (theme::colour::textSecondary.withAlpha (0.45f));
            thumbnail->drawChannels (g, visible, range, clip.getGain());

            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (playedRect.getSmallestIntegerContainer());
            g.setColour (colour.brighter (0.55f));
            thumbnail->drawChannels (g, visible, range, clip.getGain());
        }

        // The fades and the handles over the played part.
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (playedRect.getSmallestIntegerContainer());
            audio_handles::paintFades (g, clip, playedRect, view.secondsToX (model.getFadeInEndSeconds()),
                                       view.secondsToX (model.getFadeOutStartSeconds()), theme::arrangement::clipOpen.withAlpha (0.8f));
        }

        if (const auto handles = gestures.getHandles())
            audio_handles::paintHandles (g, clip, *handles, theme::arrangement::clipOpen.withAlpha (0.9f),
                                         theme::arrangement::clipOpen.withAlpha (0.5f), colours::noteBorder);
    }

    void WaveformView::paintEdges (juce::Graphics& g, juce::Rectangle<float> body)
    {
        // The start and end lines, each with a flag pointing into the played part.
        const auto played = model.getPlayedSeconds();

        for (const auto start : { true, false })
        {
            const auto x = std::round (view.secondsToX (start ? played.getStart() : played.getEnd()));

            if (x < body.getX() - 8.0f || x > body.getRight() + 8.0f)
                continue;

            g.setColour (theme::arrangement::clipOpen);
            g.fillRect (start ? x : x - 1.0f, body.getY(), 2.0f, body.getHeight());

            juce::Path flag;
            const auto tip = start ? x + 9.0f : x - 9.0f;
            flag.addTriangle (x, body.getBottom() - 12.0f, tip, body.getBottom() - 6.0f, x, body.getBottom());
            g.fillPath (flag);
        }
    }

    void WaveformView::paintReadout (juce::Graphics& g)
    {
        const auto readout = gestures.getReadout();

        if (! readout.has_value())
            return;

        const juce::Font font { juce::FontOptions (theme::size::smallFontSize) };
        const auto width = juce::GlyphArrangement::getStringWidth (font, readout->first) + 10.0f;
        auto box = juce::Rectangle<float> (width, 18.0f).withPosition (readout->second.x + 10.0f, readout->second.y - 22.0f);

        if (box.getRight() > (float) getWidth())
            box.setX (readout->second.x - 10.0f - width);

        box.setX (std::max (0.0f, box.getX()));
        box.setY (std::max ((float) rulerHeight, box.getY()));

        g.setColour (theme::colour::surfaceRaised.withAlpha (0.95f));
        g.fillRoundedRectangle (box, theme::size::cornerRadius);
        g.setColour (theme::colour::textPrimary);
        g.setFont (font);
        g.drawText (readout->first, box, juce::Justification::centred, false);
    }

    void WaveformView::updatePlayhead()
    {
        std::optional<float> x;

        if (model.getClip() != nullptr && message.isEmpty())
        {
            const auto seconds = transport.getPositionSeconds();
            const auto beat = model.getEdit().tempoSequence.toBeats (te::TimePosition::fromSeconds (seconds)).inBeats();
            x = view.secondsToX (model.beatToFileSeconds (beat));

            if (*x < -2.0f || *x > (float) getWidth() + 2.0f)
                x.reset();
        }

        const auto rounded = [] (std::optional<float> v) { return v.has_value() ? std::optional<int> (juce::roundToInt (*v)) : std::optional<int>(); };

        if (rounded (x) == rounded (playheadX))
            return;

        for (const auto& old : { playheadX, x })
            if (old.has_value())
                repaint (juce::roundToInt (*old) - 2, 0, 5, getHeight());

        playheadX = x;
    }

    juce::String WaveformView::getTooltip()
    {
        if (getMouseXYRelative().y >= rulerHeight || ! model.canMoveBeats())
            return {};

        return "Drag a line: that beat of the song goes over another moment of the sound (near a hit, onto it; Alt: anywhere). "
               "Double-click a line: back between its neighbours";
    }

    //==============================================================================
    void WaveformView::resized()
    {
        const auto first = view.width <= 0.0f;
        view.width = (float) getWidth();
        gestures.setBody (getLocalBounds().toFloat().withTrimmedTop ((float) rulerHeight));
        gestures.setRuler (getLocalBounds().toFloat().withHeight ((float) rulerHeight));

        if (first)
            showPlayedPart();
        else
            view.clamp();
    }

    AudioEditorGestures::Pointer WaveformView::toPointer (const juce::MouseEvent& e) const
    {
        return { e.position, e.mods, e.getNumberOfClicks() };
    }

    void WaveformView::mouseMove (const juce::MouseEvent& e)
    {
        setMouseCursor (gestures.getCursorFor (toPointer (e)));
    }

    void WaveformView::mouseDown (const juce::MouseEvent& e)
    {
        gestures.down (toPointer (e));
        setMouseCursor (gestures.getCursorFor (toPointer (e)));
        repaint();
    }

    void WaveformView::mouseDrag (const juce::MouseEvent& e)
    {
        gestures.drag (toPointer (e));
        repaint();
    }

    void WaveformView::mouseUp (const juce::MouseEvent& e)
    {
        gestures.up (toPointer (e));
        setMouseCursor (gestures.getCursorFor (toPointer (e)));
        repaint();
    }

    void WaveformView::mouseDoubleClick (const juce::MouseEvent&)
    {
        // Double-clicks reach mouseDown with two clicks (handles reset there).
    }

    void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        gestures.wheel (toPointer (e), wheel.deltaX, wheel.deltaY);
    }
}
