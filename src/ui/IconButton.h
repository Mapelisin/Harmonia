#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace harmonia::ui
{
    /** A button showing an icon (ui::icons) instead of a word: the transport's Play, Stop,
        Record... Its background lights up in the "on" colour while its toggle state is on. */
    class IconButton final : public juce::Button
    {
    public:
        IconButton (const juce::String& name, juce::Path icon);

        void setIcon (juce::Path);
        void setIconColour (juce::Colour);

        /** The background while the button is on. */
        void setOnColour (juce::Colour);

        juce::Colour getIconColour() const noexcept     { return iconColour; }

        void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;

    private:
        juce::Path icon;
        juce::Colour iconColour, onColour;
    };
}
