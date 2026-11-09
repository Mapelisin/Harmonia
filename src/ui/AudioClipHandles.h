#pragma once

#include "core/Tracktion.h"

namespace harmonia::ui::audio_handles
{
    /** The fade and gain handles of an audio clip, wherever it is drawn (the arrangement, the
        audio clip editor): where they are, what a drag does, how they look. */

    /** Size of a handle; a clip drawn narrower than `minWidth` pixels shows none. */
    inline constexpr float size = 7.0f, minWidth = 40.0f;

    struct Handles
    {
        juce::Rectangle<float> body;                // the waveform
        juce::Point<float> fadeIn, fadeOut, gain;   // handle centres
        float fadeInX = 0.0f, fadeOutX = 0.0f;      // where the fades end and start
    };

    /** The handles of a clip whose waveform is `body`; its fades end and start at `fadeInX` and
        `fadeOutX` (the same pixels). The fade handles sit at the top, the gain handle in the middle
        at the height of the gain. */
    Handles place (const te::AudioClipBase&, juce::Rectangle<float> body, float fadeInX, float fadeOutX);

    /** The gain as a height in the body: the fader's curve, top +6 dB, bottom silence. */
    float gainToY (float db, juce::Rectangle<float> body);
    float yToGain (float y, juce::Rectangle<float> body);

    /** A dragged gain: steps of 0.1 dB and a little catch at 0 dB. */
    float roundDraggedGain (float db);

    /** What a drag shows next to the mouse: "Gain -3.5 dB", "Fade in 1.50 beats (0.75 s)". */
    juce::String describeGain (float db);
    juce::String describeFade (const te::AudioClipBase&, bool fadeIn);

    /** What the fades take away (darkened, under their curves) from `fadeInX` and to `fadeOutX`. */
    void paintFades (juce::Graphics&, const te::AudioClipBase&, juce::Rectangle<float> body, float fadeInX, float fadeOutX,
                     juce::Colour curveColour);

    /** The gain line (once it isn't 0 dB) and the three handles. */
    void paintHandles (juce::Graphics&, const te::AudioClipBase&, const Handles&, juce::Colour handleColour, juce::Colour lineColour,
                       juce::Colour borderColour);
}
