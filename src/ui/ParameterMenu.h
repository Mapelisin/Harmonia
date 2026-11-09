#pragma once

#include "core/Tracktion.h"

namespace harmonia::ui
{
    namespace parameter_menu
    {
        /** What a right-click on a knob, fader or slider offers: MIDI Learn (the next control
            moved on a MIDI keyboard moves it) and forgetting the control that moves it.
            Without a parameter, `whyNot` (disabled). */
        juce::PopupMenu make (te::AutomatableParameter*, const juce::String& whyNot = "No MIDI Learn for this setting");

        /** "CC 21" for a setting a MIDI control moves, "Learn..." for one waiting for a
            control, else empty: for a small label next to it. */
        juce::String getBadge (te::AutomatableParameter&);
    }

    /** A slider whose right-click calls onPopupMenu (instead of dragging it). */
    class MenuSlider final : public juce::Slider
    {
    public:
        std::function<void()> onPopupMenu;

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
            {
                if (onPopupMenu != nullptr)
                    onPopupMenu();

                return;
            }

            juce::Slider::mouseDown (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu())
                juce::Slider::mouseDrag (e);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu())
                juce::Slider::mouseUp (e);
        }
    };
}
