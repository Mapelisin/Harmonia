#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** Vector icons drawn by the program itself (no image files): sharp at any size and in any
    colour. Each is a filled path in a 0..1 square. */
namespace harmonia::ui::icons
{
    juce::Path play();
    juce::Path stop();
    juce::Path record();
    juce::Path toStart();       // a bar and a triangle pointing left: back to the start
    juce::Path metronome();
    juce::Path gear();

    /** Harmonia's mark: an "H" whose crossbar is a sound wave (also the program's icon file). */
    juce::Path logoGlyph();

    /** Fills `icon` in `colour`, fitted into `area` keeping its proportions. */
    void draw (juce::Graphics&, const juce::Path& icon, juce::Rectangle<float> area, juce::Colour);

    /** The mark on its rounded square, as on the program's icon. */
    void drawLogo (juce::Graphics&, juce::Rectangle<float> area);
}
