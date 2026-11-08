#include "ui/Icons.h"
#include "ui/Theme.h"

namespace harmonia::ui::icons
{
    namespace
    {
        /** A stroke of `width` along `path`, as a filled shape. */
        juce::Path stroked (const juce::Path& path, float width, juce::PathStrokeType::EndCapStyle caps = juce::PathStrokeType::rounded)
        {
            juce::Path result;
            juce::PathStrokeType (width, juce::PathStrokeType::curved, caps).createStrokedPath (result, path);
            return result;
        }

        /** Keeps a path's 0..1 frame: an invisible point at each corner, so fitting it into an
            area places it as drawn rather than stretching its own bounds. */
        juce::Path framed (juce::Path path)
        {
            path.startNewSubPath (0.0f, 0.0f);
            path.startNewSubPath (1.0f, 1.0f);
            return path;
        }
    }

    juce::Path play()
    {
        juce::Path p;
        p.addTriangle (0.22f, 0.12f, 0.22f, 0.88f, 0.86f, 0.5f);
        return framed (p);
    }

    juce::Path stop()
    {
        juce::Path p;
        p.addRoundedRectangle (0.18f, 0.18f, 0.64f, 0.64f, 0.06f);
        return framed (p);
    }

    juce::Path record()
    {
        juce::Path p;
        p.addEllipse (0.17f, 0.17f, 0.66f, 0.66f);
        return framed (p);
    }

    juce::Path toStart()
    {
        juce::Path p;
        p.addRectangle (0.14f, 0.16f, 0.13f, 0.68f);
        p.addTriangle (0.86f, 0.16f, 0.86f, 0.84f, 0.32f, 0.5f);
        return framed (p);
    }

    juce::Path metronome()
    {
        // A pyramid body with a base, and its arm swung to one side.
        juce::Path body;
        body.startNewSubPath (0.38f, 0.1f);
        body.lineTo (0.62f, 0.1f);
        body.lineTo (0.8f, 0.86f);
        body.lineTo (0.2f, 0.86f);
        body.closeSubPath();

        juce::Path outline = stroked (body, 0.09f);

        juce::Path arm;
        arm.startNewSubPath (0.5f, 0.7f);
        arm.lineTo (0.78f, 0.24f);
        outline.addPath (stroked (arm, 0.09f));
        outline.addEllipse (0.42f, 0.62f, 0.16f, 0.16f);
        return framed (outline);
    }

    juce::Path gear()
    {
        // A ring (a stroked circle keeps its hole) with teeth on its outer edge.
        juce::Path circle;
        circle.addEllipse (0.27f, 0.27f, 0.46f, 0.46f);
        auto p = stroked (circle, 0.16f);
        constexpr int teeth = 8;

        for (int i = 0; i < teeth; ++i)
        {
            const auto angle = juce::MathConstants<float>::twoPi * (float) i / (float) teeth;
            juce::Path tooth;
            tooth.addRoundedRectangle (-0.075f, -0.47f, 0.15f, 0.17f, 0.03f);
            p.addPath (tooth, juce::AffineTransform::rotation (angle).translated (0.5f, 0.5f));
        }

        return framed (p);
    }

    juce::Path logoGlyph()
    {
        // Two upright bars, and between them one period of a sine wave as the crossbar
        // (the same shapes as src/app/resources/icon.svg, there in 256 units).
        juce::Path p;
        p.addRoundedRectangle (0.25f, 0.22f, 0.117f, 0.56f, 0.058f);
        p.addRoundedRectangle (0.633f, 0.22f, 0.117f, 0.56f, 0.058f);

        juce::Path wave;
        // Shallow, so that even small the mark reads as an H with a wavy bar.
        wave.startNewSubPath (0.36f, 0.5f);
        wave.cubicTo (0.4f, 0.43f, 0.46f, 0.43f, 0.5f, 0.5f);
        wave.cubicTo (0.54f, 0.57f, 0.6f, 0.57f, 0.64f, 0.5f);
        p.addPath (stroked (wave, 0.075f));
        return framed (p);
    }

    void draw (juce::Graphics& g, const juce::Path& icon, juce::Rectangle<float> area, juce::Colour colour)
    {
        if (area.isEmpty())
            return;

        const auto side = std::min (area.getWidth(), area.getHeight());
        g.setColour (colour);
        g.fillPath (icon, icon.getTransformToScaleToFit (area.withSizeKeepingCentre (side, side), true));
    }

    void drawLogo (juce::Graphics& g, juce::Rectangle<float> area)
    {
        const auto side = std::min (area.getWidth(), area.getHeight());
        const auto square = area.withSizeKeepingCentre (side, side);
        g.setColour (theme::colour::accent);
        g.fillRoundedRectangle (square, side * 0.22f);
        draw (g, logoGlyph(), square, juce::Colours::white);
    }
}
