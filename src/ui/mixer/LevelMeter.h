#pragma once

#include "core/Tracktion.h"

namespace harmonia::ui::mixer
{
    /** A stereo peak meter: two bars from -60 to +6 dB that fall back smoothly, a held peak line
        and a red cap when the signal went over 0 dB (click to clear). */
    class LevelMeter final : public juce::Component,
                             public juce::SettableTooltipClient,
                             private juce::Timer
    {
    public:
        /** `source` gives the measurer to show; it is asked again on every update, because the
            master's measurer comes and goes with the playback context (device restart, export). */
        explicit LevelMeter (std::function<te::LevelMeasurer*()> source);
        ~LevelMeter() override;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

        /** Reads the measurer (the timer does this 30 times a second). */
        void update();

        /** Shown level of channel 0 or 1, in dB. */
        float getLevelDb (int channel) const noexcept   { return levelDb[(size_t) channel]; }

        /** The signal went over 0 dB since the last click. */
        bool hasClipped() const noexcept                { return clipped; }

        static constexpr float minDb = -60.0f, maxDb = 6.0f;

    private:
        std::function<te::LevelMeasurer*()> source;
        juce::WeakReference<te::LevelMeasurer> attached;
        te::LevelMeasurer::Client client;

        std::array<float, 2> levelDb { minDb, minDb }, peakDb { minDb, minDb };
        std::array<double, 2> peakTime { 0.0, 0.0 };
        double lastUpdate = 0.0;
        bool clipped = false;

        void attach();
        void detach();
        void timerCallback() override;
        float dbToY (float db, float height) const;
    };
}
