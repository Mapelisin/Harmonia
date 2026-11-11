#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace harmonia::ui
{
    /** How busy the sound is: the share of the time between two audio buffers that working out
        the sound takes. Near 100% the sound starts to crackle. A bar that turns yellow, then red. */
    class CpuMeter final : public juce::Component,
                           public juce::SettableTooltipClient,
                           private juce::Timer
    {
    public:
        /** `readLoad` gives 0..1, or nothing while no audio device is running. */
        explicit CpuMeter (std::function<std::optional<float>()> readLoad);
        ~CpuMeter() override;

        /** Reads the load now (the meter also does so a few times a second). */
        void refresh();

        std::optional<float> getLoad() const noexcept        { return load; }

        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;

        /** A click on the meter (the owner shows which plugin takes how much). */
        std::function<void()> onClick;

        /** The bar's colour for a load: calm, then a warning, then a problem. */
        static juce::Colour getColour (float load);

    private:
        std::function<std::optional<float>()> readLoad;
        std::optional<float> load;

        void timerCallback() override;
    };
}
