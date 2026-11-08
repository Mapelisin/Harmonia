#include "ui/mixer/LevelMeter.h"
#include "ui/Theme.h"

namespace harmonia::ui::mixer
{
    namespace
    {
        constexpr float fallDbPerSecond = 30.0f;
        constexpr double peakHoldSeconds = 1.5;
    }

    LevelMeter::LevelMeter (std::function<te::LevelMeasurer*()> s)
        : source (std::move (s))
    {
        setOpaque (true);
        setTooltip ("Level. Red at the top: it went over 0 dB (click to clear)");
        attach();
        startTimerHz (30);
    }

    LevelMeter::~LevelMeter()
    {
        stopTimer();
        detach();
    }

    void LevelMeter::attach()
    {
        auto* measurer = source != nullptr ? source() : nullptr;

        if (measurer == attached.get())
            return;

        detach();

        if (measurer != nullptr)
        {
            client.reset();
            measurer->addClient (client);
            attached = measurer;
        }
    }

    void LevelMeter::detach()
    {
        if (auto* measurer = attached.get())
            measurer->removeClient (client);

        attached = nullptr;
    }

    void LevelMeter::update()
    {
        attach();

        const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        const auto elapsed = lastUpdate > 0.0 ? (float) std::min (0.5, now - lastUpdate) : 0.0f;
        lastUpdate = now;

        const auto connected = attached.get() != nullptr;
        std::array<float, 2> measuredDb { minDb, minDb };

        if (connected)
        {
            // A mono source shows the same level on both bars (reading clears it: read once).
            measuredDb[0] = client.getAndClearAudioLevel (0).dB;
            measuredDb[1] = client.getNumChannelsUsed() > 1 ? client.getAndClearAudioLevel (1).dB : measuredDb[0];
        }

        for (int channel = 0; channel < 2; ++channel)
        {
            auto& level = levelDb[(size_t) channel];
            const auto measured = measuredDb[(size_t) channel];

            level = std::max (measured, level - fallDbPerSecond * elapsed);
            level = juce::jlimit (minDb, maxDb, level);

            auto& peak = peakDb[(size_t) channel];

            if (measured >= peak || now - peakTime[(size_t) channel] > peakHoldSeconds)
            {
                peak = juce::jlimit (minDb, maxDb, std::max (measured, level));
                peakTime[(size_t) channel] = now;
            }

            if (measured > 0.0f)
                clipped = true;
        }

        repaint();
    }

    void LevelMeter::timerCallback()
    {
        if (isShowing())
            update();
    }

    float LevelMeter::dbToY (float db, float height) const
    {
        return height * (1.0f - (juce::jlimit (minDb, maxDb, db) - minDb) / (maxDb - minDb));
    }

    void LevelMeter::paint (juce::Graphics& g)
    {
        g.fillAll (theme::mixer::meterBackground);

        auto area = getLocalBounds().toFloat().reduced (1.0f);
        const auto capHeight = 3.0f;
        auto cap = area.removeFromTop (capHeight);
        area.removeFromTop (1.0f);

        if (clipped)
        {
            g.setColour (theme::colour::record);
            g.fillRect (cap);
        }

        const auto height = area.getHeight();
        const auto barWidth = (area.getWidth() - 1.0f) / 2.0f;
        const auto zeroY = area.getY() + dbToY (0.0f, height);
        const auto warnY = area.getY() + dbToY (-12.0f, height);

        for (int channel = 0; channel < 2; ++channel)
        {
            const auto x = area.getX() + (float) channel * (barWidth + 1.0f);
            const auto top = area.getY() + dbToY (levelDb[(size_t) channel], height);
            const auto bottom = area.getBottom();

            if (top < bottom)
            {
                // Green up to -12 dB, yellow up to 0, red above.
                g.setColour (theme::mixer::meterLow);
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x, std::max (top, warnY), x + barWidth, bottom));

                if (top < warnY)
                {
                    g.setColour (theme::mixer::meterMid);
                    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x, std::max (top, zeroY), x + barWidth, warnY));
                }

                if (top < zeroY)
                {
                    g.setColour (theme::colour::record);
                    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x, top, x + barWidth, zeroY));
                }
            }

            if (peakDb[(size_t) channel] > minDb)
            {
                const auto y = area.getY() + dbToY (peakDb[(size_t) channel], height);
                g.setColour (theme::colour::textPrimary.withAlpha (0.8f));
                g.fillRect (x, y, barWidth, 1.0f);
            }
        }

        // The 0 dB mark.
        g.setColour (theme::colour::textSecondary.withAlpha (0.5f));
        g.fillRect (area.getX(), zeroY, area.getWidth(), 1.0f);
    }

    void LevelMeter::mouseDown (const juce::MouseEvent&)
    {
        clipped = false;
        repaint();
    }
}
