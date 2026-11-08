#include "ui/Theme.h"

namespace harmonia::ui::theme
{
    namespace pianoroll
    {
        juce::Colour getNoteColour (int colourGroup)
        {
            static const juce::Colour palette[] = {
                juce::Colour (0xff5aa9e6), juce::Colour (0xff62c79a), juce::Colour (0xffd9c45a), juce::Colour (0xffe0875a),
                juce::Colour (0xffd9677a), juce::Colour (0xffb17ad9), juce::Colour (0xff6f8be0), juce::Colour (0xff4fc2c9),
                juce::Colour (0xff9ccf5c), juce::Colour (0xffe6a85a), juce::Colour (0xffe06f9f), juce::Colour (0xff8d95a8),
                juce::Colour (0xff3d8fc4), juce::Colour (0xff42a77a), juce::Colour (0xffb8a24a), juce::Colour (0xffc46a4a)
            };

            return palette[juce::jlimit (0, 15, colourGroup)];
        }
    }

    namespace arrangement
    {
        juce::Colour getTrackColour (int trackIndex)
        {
            static const juce::Colour palette[] = {
                juce::Colour (0xff5aa9e6), juce::Colour (0xff62c79a), juce::Colour (0xffd9c45a), juce::Colour (0xffe0875a),
                juce::Colour (0xffd9677a), juce::Colour (0xffb17ad9), juce::Colour (0xff4fc2c9), juce::Colour (0xff9ccf5c)
            };

            return palette[(size_t) std::max (0, trackIndex) % std::size (palette)];
        }
    }

    LookAndFeel::LookAndFeel()
    {
        setColourScheme ({ colour::background,      // windowBackground
                           colour::surface,         // widgetBackground
                           colour::surface,         // menuBackground
                           colour::border,          // outline
                           colour::textPrimary,     // defaultText
                           colour::surfaceRaised,   // defaultFill
                           colour::textPrimary,     // highlightedText
                           colour::accent,          // highlightedFill
                           colour::textPrimary });  // menuText

        setColour (juce::ResizableWindow::backgroundColourId, colour::background);
        setColour (juce::TextButton::buttonColourId, colour::surfaceRaised);
        setColour (juce::TextButton::textColourOffId, colour::textPrimary);
        setColour (juce::TextButton::textColourOnId, colour::textPrimary);
        setColour (juce::Label::textColourId, colour::textPrimary);
        setColour (juce::Slider::trackColourId, colour::accent);
        setColour (juce::Slider::rotarySliderFillColourId, colour::accent);
        setColour (juce::Slider::thumbColourId, colour::textPrimary);
        setColour (juce::Slider::backgroundColourId, mixer::meterBackground);
        setColour (juce::Slider::rotarySliderOutlineColourId, mixer::meterBackground);
    }

    void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                            bool isMouseOverButton, bool isButtonDown)
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        auto fill = backgroundColour;

        if (isButtonDown)
            fill = fill.darker (0.2f);
        else if (isMouseOverButton)
            fill = fill.brighter (0.08f);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, size::cornerRadius);
        g.setColour (colour::border);
        g.drawRoundedRectangle (bounds, size::cornerRadius, 1.0f);
    }

    juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
    {
        // Small buttons (a track's M and S) get a smaller letter.
        return juce::Font (juce::FontOptions (std::min (size::fontSize, (float) buttonHeight * 0.68f)));
    }

    void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
    {
        g.setFont (getTextButtonFont (button, button.getHeight()));
        g.setColour (button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                         .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));

        // Narrow margins: one letter fits a small square button.
        const auto margin = std::min (6, button.getWidth() / 8);
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (margin, 1), juce::Justification::centred, 2, 0.8f);
    }

    void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                                        float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider)
    {
        const auto vertical = style == juce::Slider::LinearVertical;

        if (! vertical && style != juce::Slider::LinearHorizontal)
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
            return;
        }

        const juce::Rectangle<float> area ((float) x, (float) y, (float) width, (float) height);
        const auto enabled = slider.isEnabled();
        const auto fill = slider.findColour (juce::Slider::trackColourId).withMultipliedAlpha (enabled ? 1.0f : 0.4f);
        constexpr float groove = 4.0f;

        if (vertical)
        {
            // A groove, lit from the bottom up to the cap.
            const auto track = area.withSizeKeepingCentre (groove, area.getHeight());
            g.setColour (mixer::meterBackground);
            g.fillRoundedRectangle (track, groove / 2.0f);
            g.setColour (fill.withMultipliedAlpha (0.85f));
            g.fillRoundedRectangle (track.withTop (sliderPos), groove / 2.0f);

            // The cap: a flat block with a line across, as on a mixing desk.
            const auto cap = juce::Rectangle<float> (std::min (area.getWidth() - 2.0f, 26.0f), 14.0f)
                                 .withCentre ({ area.getCentreX(), sliderPos });
            g.setColour (enabled ? juce::Colour (0xffc9ced6) : colour::textSecondary);
            g.fillRoundedRectangle (cap, 2.5f);
            g.setColour (mixer::meterBackground);
            g.drawRoundedRectangle (cap, 2.5f, 1.0f);
            g.fillRect (cap.withSizeKeepingCentre (cap.getWidth() - 6.0f, 1.5f));
        }
        else
        {
            const auto track = area.withSizeKeepingCentre (area.getWidth(), groove);
            g.setColour (mixer::meterBackground);
            g.fillRoundedRectangle (track, groove / 2.0f);
            g.setColour (fill);
            g.fillRoundedRectangle (track.withRight (sliderPos), groove / 2.0f);

            const auto thumb = juce::Rectangle<float> (11.0f, 11.0f).withCentre ({ sliderPos, area.getCentreY() });
            g.setColour (enabled ? slider.findColour (juce::Slider::thumbColourId) : colour::textSecondary);
            g.fillEllipse (thumb);
            g.setColour (mixer::meterBackground);
            g.drawEllipse (thumb, 1.0f);
        }
    }

    void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float proportion,
                                        float startAngle, float endAngle, juce::Slider& slider)
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
        const auto radius = std::min (bounds.getWidth(), bounds.getHeight()) / 2.0f;

        if (radius < 4.0f)
            return;

        const auto centre = bounds.getCentre();
        const auto lineWidth = std::max (2.0f, std::min (4.0f, radius * 0.22f));
        const auto arcRadius = radius - lineWidth / 2.0f;
        const auto angle = startAngle + proportion * (endAngle - startAngle);
        const auto enabled = slider.isEnabled();

        const auto arc = [&] (float from, float to, juce::Colour colour)
        {
            juce::Path p;
            p.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, from, to, true);
            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };

        arc (startAngle, endAngle, mixer::meterBackground);

        // A range around zero (pan) lights from the middle; others from the start.
        auto from = startAngle;

        if (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0)
            from = startAngle + (float) slider.valueToProportionOfLength (0.0) * (endAngle - startAngle);

        if (std::abs (angle - from) > 0.01f)
            arc (std::min (from, angle), std::max (from, angle),
                 slider.findColour (juce::Slider::rotarySliderFillColourId).withMultipliedAlpha (enabled ? 1.0f : 0.4f));

        // The knob, with a line pointing at the value.
        const auto knobRadius = arcRadius - lineWidth - 1.5f;
        const auto knob = juce::Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre);
        g.setColour (colour::surfaceRaised.brighter (0.15f));
        g.fillEllipse (knob);
        g.setColour (colour::border);
        g.drawEllipse (knob, 1.0f);

        const auto tip = centre.getPointOnCircumference (knobRadius - 1.0f, angle);
        const auto inner = centre.getPointOnCircumference (knobRadius * 0.35f, angle);
        g.setColour (enabled ? colour::textPrimary : colour::textSecondary);
        g.drawLine ({ inner, tip }, 2.0f);
    }

    int LookAndFeel::getSliderThumbRadius (juce::Slider& slider)
    {
        switch (slider.getSliderStyle())
        {
            case juce::Slider::LinearVertical:      return 7;
            case juce::Slider::LinearHorizontal:    return 6;
            default:                                return juce::LookAndFeel_V4::getSliderThumbRadius (slider);
        }
    }

    juce::Font LookAndFeel::getMenuBarFont (juce::MenuBarComponent&, int, const juce::String&)
    {
        return juce::Font (juce::FontOptions (size::fontSize));
    }

    void LookAndFeel::drawMenuBarBackground (juce::Graphics& g, int, int, bool, juce::MenuBarComponent&)
    {
        g.fillAll (colour::surface);
    }
}
