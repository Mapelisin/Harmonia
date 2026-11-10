#include "ui/audioeditor/AudioEditorGestures.h"
#include "core/Snap.h"

namespace harmonia::ui::audioeditor
{
    namespace clips = core::clips;
    namespace snap = core::snap;

    namespace
    {
        constexpr float dragThreshold = 3.0f;
        constexpr double minClipBeats = 1.0 / 16.0;
        constexpr float minLinePixels = 10.0f;

        /** "bar 5, beat 2" (or "beat 2.50" between beats); bars and beats counted from 1. */
        juce::String describePosition (double beat, double beatsPerBar)
        {
            const auto bar = std::floor (beat / beatsPerBar + 1.0e-9);
            const auto inBar = beat - bar * beatsPerBar + 1.0;
            const auto whole = std::abs (inBar - std::round (inBar)) < 1.0e-6;
            return "bar " + juce::String ((int) bar + 1) + ", beat " + (whole ? juce::String (juce::roundToInt (inBar)) : juce::String (inBar, 2));
        }
    }

    AudioEditorGestures::AudioEditorGestures (AudioEditorModel& m, View& v)
        : model (m), view (v)
    {
    }

    std::optional<audio_handles::Handles> AudioEditorGestures::getHandles() const
    {
        auto* clip = model.getClip();

        if (clip == nullptr || view.fileSeconds <= 0.0)
            return {};

        const auto played = model.getPlayedSeconds();
        const auto region = juce::Rectangle<float>::leftTopRightBottom (view.secondsToX (played.getStart()), body.getY(),
                                                                        view.secondsToX (played.getEnd()), body.getBottom());

        if (region.getWidth() < audio_handles::minWidth)
            return {};

        return audio_handles::place (*clip, region, view.secondsToX (model.getFadeInEndSeconds()),
                                     view.secondsToX (model.getFadeOutStartSeconds()));
    }

    std::optional<double> AudioEditorGestures::getBeatLineAt (juce::Point<float> position) const
    {
        if (! ruler.contains (position) || ! model.canMoveBeats())
            return {};

        // The lines the ruler draws: the nearest of them, if the mouse is on it.
        const auto lineBeats = getSnapContext().lineBeats;

        if (lineBeats <= 0.0)
            return {};

        const auto beat = std::round (model.fileSecondsToBeat (view.xToSeconds (position.x)) / lineBeats) * lineBeats;

        if (std::abs (view.secondsToX (model.beatToFileSeconds (beat)) - position.x) > edgeGrabPixels)
            return {};

        return beat;
    }

    std::optional<double> AudioEditorGestures::getMovedBeat() const
    {
        return kind == Kind::beat ? std::optional<double> (grabbedBeat) : std::nullopt;
    }

    AudioEditorGestures::Zone AudioEditorGestures::hitTest (juce::Point<float> position) const
    {
        if (getBeatLineAt (position).has_value())
            return Zone::beat;

        if (model.getClip() == nullptr || ! body.contains (position))
            return Zone::none;

        if (const auto handles = getHandles())
        {
            const auto near = [position] (juce::Point<float> centre)
            {
                const auto reach = audio_handles::size / 2.0f + 2.0f;
                return std::abs (position.x - centre.x) <= reach && std::abs (position.y - centre.y) <= reach;
            };

            if (near (handles->gain))       return Zone::gain;
            if (near (handles->fadeIn))     return Zone::fadeIn;
            if (near (handles->fadeOut))    return Zone::fadeOut;
        }

        const auto played = model.getPlayedSeconds();
        const auto startX = view.secondsToX (played.getStart()), endX = view.secondsToX (played.getEnd());

        // When the lines are close together, the nearer one wins.
        const auto toStart = std::abs (position.x - startX), toEnd = std::abs (position.x - endX);

        if (toStart <= edgeGrabPixels && toStart <= toEnd)
            return Zone::startEdge;

        if (toEnd <= edgeGrabPixels)
            return Zone::endEdge;

        return Zone::none;
    }

    //==============================================================================
    void AudioEditorGestures::down (const Pointer& p)
    {
        downPointer = p;
        lastPosition = p.position;
        dragged = false;
        kind = Kind::none;

        if (p.mods.isMiddleButtonDown())
        {
            kind = Kind::pan;
            return;
        }

        if (! p.mods.isLeftButtonDown())
            return;

        auto* clip = model.getClip();

        if (clip == nullptr)
            return;

        const auto zone = hitTest (p.position);

        if (const auto beat = getBeatLineAt (p.position))
        {
            // Double-click: the beat back between its neighbours.
            if (p.clicks >= 2)
            {
                model.resetBeat (*beat);
                return;
            }

            // The lines either side of it are what stays.
            model.beginChange ("Move beat");
            kind = Kind::beat;
            grabbedBeat = *beat;
            grabbedSpanBeats = getSnapContext().lineBeats;
            isOnHit = false;
            return;
        }

        if (zone == Zone::fadeIn || zone == Zone::fadeOut || zone == Zone::gain)
        {
            // Double-click: back to none / 0 dB.
            if (p.clicks >= 2)
            {
                model.beginChange (zone == Zone::gain ? "Reset clip gain" : "Remove fade");

                if (zone == Zone::gain)             model.setGainDb (0.0f);
                else if (zone == Zone::fadeIn)      model.setFadeInBeats (0.0);
                else                                model.setFadeOutBeats (0.0);

                return;
            }

            model.beginChange (zone == Zone::gain ? "Clip gain" : zone == Zone::fadeIn ? "Fade in" : "Fade out");
            kind = zone == Zone::gain ? Kind::gain : zone == Zone::fadeIn ? Kind::fadeIn : Kind::fadeOut;

            if (const auto handles = getHandles())
                grabbedGainY = handles->gain.y;

            return;
        }

        if (zone == Zone::startEdge || zone == Zone::endEdge)
        {
            model.beginChange ("Resize clip");
            kind = zone == Zone::startEdge ? Kind::trimStart : Kind::trimEnd;
            return;
        }

        kind = Kind::click;
    }

    void AudioEditorGestures::drag (const Pointer& p)
    {
        if (! dragged && p.position.getDistanceFrom (downPointer.position) >= dragThreshold)
            dragged = true;

        switch (kind)
        {
            case Kind::trimStart:
            case Kind::trimEnd:     if (dragged) dragTrim (p); break;
            case Kind::fadeIn:
            case Kind::fadeOut:
            case Kind::gain:        if (dragged) dragHandle (p); break;
            case Kind::beat:        if (dragged) dragBeat (p); break;

            case Kind::pan:
                view.scrollByPixels (-(double) (p.position.x - lastPosition.x));

                if (onViewChanged != nullptr)
                    onViewChanged();
                break;

            case Kind::click:
            case Kind::none:
            default:
                break;
        }

        lastPosition = p.position;
    }

    void AudioEditorGestures::up (const Pointer& p)
    {
        // (A beat line clicked and not dragged is a click like any other.)
        if ((kind == Kind::click || kind == Kind::beat) && ! dragged && onPositionChosen != nullptr && model.getClip() != nullptr)
            onPositionChosen (std::max (0.0, model.fileSecondsToBeat (view.xToSeconds (p.position.x))));

        kind = Kind::none;
    }

    void AudioEditorGestures::wheel (const Pointer& p, float deltaX, float deltaY)
    {
        if (p.mods.isCommandDown())
            view.zoom (std::pow (2.0, (double) deltaY * 2.0), p.position.x);
        else
        {
            // Up / left scrolls back, as everywhere; a sideways wheel too.
            const auto amount = std::abs (deltaX) > std::abs (deltaY) ? deltaX : deltaY;
            view.scrollByPixels (-(double) amount * 300.0);
        }

        if (onViewChanged != nullptr)
            onViewChanged();
    }

    juce::MouseCursor AudioEditorGestures::getCursorFor (const Pointer& p)
    {
        switch (kind)
        {
            case Kind::trimStart:
            case Kind::trimEnd:
            case Kind::fadeIn:
            case Kind::fadeOut:
            case Kind::beat:        return juce::MouseCursor::LeftRightResizeCursor;
            case Kind::gain:        return juce::MouseCursor::UpDownResizeCursor;
            case Kind::pan:         return juce::MouseCursor::DraggingHandCursor;
            case Kind::click:
            case Kind::none:
            default:                break;
        }

        switch (hitTest (p.position))
        {
            case Zone::startEdge:
            case Zone::endEdge:
            case Zone::fadeIn:
            case Zone::fadeOut:
            case Zone::beat:        return juce::MouseCursor::LeftRightResizeCursor;
            case Zone::gain:        return juce::MouseCursor::UpDownResizeCursor;
            case Zone::none:
            default:                return juce::MouseCursor::NormalCursor;
        }
    }

    //==============================================================================
    snap::Context AudioEditorGestures::getSnapContext() const
    {
        // The grid of the ruler: its lines follow this view's zoom, not the arrangement's.
        auto context = model.getArrangement().getSnapContext();
        context.lineBeats = snap::getLineBeats (model.getPixelsPerBeat (view.pixelsPerSecond), context.beatsPerBar, minLinePixels);
        return context;
    }

    void AudioEditorGestures::dragTrim (const Pointer& p)
    {
        const auto mode = p.mods.isAltDown() ? snap::Mode::none : model.getArrangement().getSnap();
        const auto context = getSnapContext();
        const auto beat = snap::snapNearest (model.fileSecondsToBeat (view.xToSeconds (p.position.x)), mode, context);
        const auto grid = snap::getGridBeats (mode, context);
        const auto minLength = grid > 0.0 ? std::min (grid, context.beatsPerBar) : minClipBeats;

        if (kind == Kind::trimStart)
            model.trimStart (beat, minLength);
        else
            model.trimEnd (beat, minLength);
    }

    void AudioEditorGestures::dragHandle (const Pointer& p)
    {
        auto* clip = model.getClip();

        if (clip == nullptr)
            return;

        if (kind == Kind::gain)
        {
            const auto db = audio_handles::yToGain (grabbedGainY + (p.position.y - downPointer.position.y), body);
            model.setGainDb (audio_handles::roundDraggedGain (db));
            return;
        }

        // Fades are free (no grid): they follow the sound, not the bars.
        const auto beat = snap::roundToTicks (model.fileSecondsToBeat (view.xToSeconds (p.position.x)));

        if (kind == Kind::fadeIn)
            model.setFadeInBeats (beat - clips::getStartBeat (*clip));
        else
            model.setFadeOutBeats (clips::getEndBeat (*clip) - beat);
    }

    void AudioEditorGestures::dragBeat (const Pointer& p)
    {
        auto seconds = view.xToSeconds (p.position.x);
        isOnHit = false;

        // Onto the nearest hit of the sound, if the mouse is near one.
        if (! p.mods.isAltDown())
        {
            const auto& hits = model.getHitSeconds();
            const auto after = std::lower_bound (hits.begin(), hits.end(), seconds);
            auto nearest = after;

            if (after == hits.end() || (after != hits.begin() && seconds - *(after - 1) < *after - seconds))
                nearest = after == hits.begin() ? hits.end() : after - 1;

            if (nearest != hits.end() && std::abs (*nearest - seconds) * view.pixelsPerSecond <= hitSnapPixels)
            {
                seconds = *nearest;
                isOnHit = true;
            }
        }

        model.moveBeat (grabbedBeat, seconds, grabbedSpanBeats);
    }

    std::optional<std::pair<juce::String, juce::Point<float>>> AudioEditorGestures::getReadout() const
    {
        auto* clip = model.getClip();

        if (clip == nullptr || ! dragged)
            return {};

        if (kind == Kind::beat)
        {
            const auto text = describePosition (grabbedBeat, model.getArrangement().getBeatsPerBar())
                            + (isOnHit ? juce::String (": on a hit") : ": at " + juce::String (model.beatToFileSeconds (grabbedBeat), 3) + " s");
            return std::make_pair (text, juce::Point<float> (view.secondsToX (model.beatToFileSeconds (grabbedBeat)), body.getY() + 30.0f));
        }

        if (kind == Kind::trimStart || kind == Kind::trimEnd)
        {
            const auto start = kind == Kind::trimStart;
            const auto beat = start ? clips::getStartBeat (*clip) : clips::getEndBeat (*clip);
            const auto seconds = start ? model.getPlayedSeconds().getStart() : model.getPlayedSeconds().getEnd();
            const auto text = juce::String (start ? "Start: " : "End: ") + describePosition (beat, model.getArrangement().getBeatsPerBar())
                            + "  (" + juce::String (seconds, 2) + " s of the file)";
            return std::pair { text, juce::Point<float> (view.secondsToX (seconds), body.getY() + 10.0f) };
        }

        const auto handles = getHandles();

        if (! handles.has_value())
            return {};

        if (kind == Kind::gain)
            return std::pair { audio_handles::describeGain (clips::getGainDb (*clip)), handles->gain };

        if (kind == Kind::fadeIn || kind == Kind::fadeOut)
            return std::pair { audio_handles::describeFade (*clip, kind == Kind::fadeIn), kind == Kind::fadeIn ? handles->fadeIn : handles->fadeOut };

        return {};
    }
}
