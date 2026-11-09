#include "ui/AudioClipHandles.h"
#include "core/Clips.h"

namespace harmonia::ui::audio_handles
{
    namespace clips = core::clips;

    Handles place (const te::AudioClipBase& clip, juce::Rectangle<float> body, float fadeInX, float fadeOutX)
    {
        Handles h;
        h.body = body;
        h.fadeInX = fadeInX;
        h.fadeOutX = fadeOutX;

        const auto half = size / 2.0f;
        const auto y = body.getY() + half + 1.0f;
        h.fadeIn = { juce::jlimit (body.getX() + half + 1.0f, body.getRight() - half - 1.0f, fadeInX), y };
        h.fadeOut = { juce::jlimit (body.getX() + half + 1.0f, body.getRight() - half - 1.0f, fadeOutX), y };
        h.gain = { body.getCentreX(), gainToY (clips::getGainDb (clip), body) };
        return h;
    }

    float gainToY (float db, juce::Rectangle<float> body)
    {
        const auto half = size / 2.0f + 1.0f;
        const auto position = juce::jlimit (0.0f, 1.0f, te::decibelsToVolumeFaderPosition (db));
        return body.getBottom() - half - position * std::max (1.0f, body.getHeight() - 2.0f * half);
    }

    float yToGain (float y, juce::Rectangle<float> body)
    {
        const auto half = size / 2.0f + 1.0f;
        const auto position = juce::jlimit (0.0f, 1.0f, (body.getBottom() - half - y) / std::max (1.0f, body.getHeight() - 2.0f * half));

        if (position <= 0.0f)
            return clips::silentGainDb;

        return juce::jlimit (clips::silentGainDb, clips::maxGainDb, te::volumeFaderPositionToDB (position));
    }

    float roundDraggedGain (float db)
    {
        return std::abs (db) < 0.3f ? 0.0f : std::round (db * 10.0f) / 10.0f;
    }

    juce::String describeGain (float db)
    {
        return db <= clips::silentGainDb + 0.5f ? juce::String ("Gain: silent")
                                                : "Gain " + juce::String (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
    }

    juce::String describeFade (const te::AudioClipBase& clip, bool fadeIn)
    {
        auto& c = const_cast<te::AudioClipBase&> (clip);
        const auto beats = fadeIn ? clips::getFadeInBeats (c) : clips::getFadeOutBeats (c);
        const auto seconds = fadeIn ? c.getFadeIn().inSeconds() : c.getFadeOut().inSeconds();
        return juce::String (fadeIn ? "Fade in " : "Fade out ") + juce::String (beats, 2) + " beats (" + juce::String (seconds, 2) + " s)";
    }

    void paintFades (juce::Graphics& g, const te::AudioClipBase& clip, juce::Rectangle<float> body, float fadeInX, float fadeOutX,
                     juce::Colour curveColour)
    {
        const auto paintFade = [&] (float fromX, float toX, bool fadeIn, te::AudioFadeCurve::Type type)
        {
            if (toX - fromX < 1.0f)
                return;

            constexpr int steps = 24;
            juce::Path curve, shaded;
            shaded.startNewSubPath (fromX, body.getY());

            for (int i = 0; i <= steps; ++i)
            {
                const auto alpha = (float) i / (float) steps;
                const auto gain = te::AudioFadeCurve::alphaToGainForType (type, fadeIn ? alpha : 1.0f - alpha);
                const juce::Point<float> point { fromX + (toX - fromX) * alpha, body.getBottom() - gain * body.getHeight() };
                shaded.lineTo (point);

                if (i == 0)
                    curve.startNewSubPath (point);
                else
                    curve.lineTo (point);
            }

            shaded.lineTo (toX, body.getY());
            shaded.closeSubPath();

            g.setColour (juce::Colours::black.withAlpha (0.38f));
            g.fillPath (shaded);
            g.setColour (curveColour);
            g.strokePath (curve, juce::PathStrokeType (1.0f));
        };

        auto& c = const_cast<te::AudioClipBase&> (clip);
        paintFade (body.getX(), fadeInX, true, c.getFadeInType());
        paintFade (fadeOutX, body.getRight(), false, c.getFadeOutType());
    }

    void paintHandles (juce::Graphics& g, const te::AudioClipBase& clip, const Handles& handles, juce::Colour handleColour,
                       juce::Colour lineColour, juce::Colour borderColour)
    {
        // The gain: a line across the clip once it isn't 0 dB, and its handle in the middle.
        if (std::abs (clips::getGainDb (clip)) > 0.05f)
        {
            g.setColour (lineColour);
            g.fillRect (handles.body.getX(), std::round (handles.gain.y), handles.body.getWidth(), 1.0f);
        }

        const auto square = [] (juce::Point<float> centre) { return juce::Rectangle<float> (size, size).withCentre (centre); };

        g.setColour (handleColour);
        g.fillRect (square (handles.fadeIn));
        g.fillRect (square (handles.fadeOut));
        g.fillEllipse (square (handles.gain));
        g.setColour (borderColour);
        g.drawRect (square (handles.fadeIn), 1.0f);
        g.drawRect (square (handles.fadeOut), 1.0f);
        g.drawEllipse (square (handles.gain), 1.0f);
    }
}
