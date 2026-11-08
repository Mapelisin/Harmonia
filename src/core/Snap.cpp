#include "core/Snap.h"

namespace harmonia::core::snap
{
    namespace
    {
        constexpr double epsilon = 1.0e-9;
    }

    const std::vector<Mode>& getAllModes()
    {
        static const std::vector<Mode> modes {
            Mode::line, Mode::cell, Mode::none,
            Mode::bar, Mode::beat, Mode::halfBeat, Mode::thirdBeat, Mode::quarterBeat, Mode::sixthBeat,
            Mode::halfStep, Mode::thirdStep, Mode::quarterStep, Mode::sixthStep
        };

        return modes;
    }

    juce::String getName (Mode mode)
    {
        switch (mode)
        {
            case Mode::none:        return "No snap";
            case Mode::line:        return "Line";
            case Mode::cell:        return "Cell";
            case Mode::bar:         return "Bar";
            case Mode::beat:        return "Beat  (1/4)";
            case Mode::halfBeat:    return "1/2 beat  (1/8)";
            case Mode::thirdBeat:   return "1/3 beat  (1/8 T)";
            case Mode::quarterBeat: return "Step  (1/16)";
            case Mode::sixthBeat:   return "1/6 beat  (1/16 T)";
            case Mode::halfStep:    return "1/2 step  (1/32)";
            case Mode::thirdStep:   return "1/3 step  (1/32 T)";
            case Mode::quarterStep: return "1/4 step  (1/64)";
            case Mode::sixthStep:   return "1/6 step  (1/64 T)";
        }

        return {};
    }

    double getGridBeats (Mode mode, const Context& context)
    {
        switch (mode)
        {
            case Mode::none:        return 0.0;
            case Mode::line:
            case Mode::cell:        return context.lineBeats;
            case Mode::bar:         return context.beatsPerBar;
            case Mode::beat:        return 1.0;
            case Mode::halfBeat:    return 1.0 / 2.0;
            case Mode::thirdBeat:   return 1.0 / 3.0;
            case Mode::quarterBeat: return 1.0 / 4.0;
            case Mode::sixthBeat:   return 1.0 / 6.0;
            case Mode::halfStep:    return 1.0 / 8.0;
            case Mode::thirdStep:   return 1.0 / 12.0;
            case Mode::quarterStep: return 1.0 / 16.0;
            case Mode::sixthStep:   return 1.0 / 24.0;
        }

        return 0.0;
    }

    double getLineBeats (double pixelsPerBeat, double beatsPerBar, double minPixels)
    {
        for (double candidate : { 1.0 / 16.0, 1.0 / 8.0, 1.0 / 4.0, 1.0 / 2.0, 1.0 })
            if (candidate * pixelsPerBeat >= minPixels)
                return candidate;

        auto bars = beatsPerBar;

        while (bars * pixelsPerBeat < minPixels && bars < 1.0e6)
            bars *= 2.0;

        return bars;
    }

    double roundToTicks (double beats)
    {
        return std::round (beats * ticksPerBeat) / ticksPerBeat;
    }

    double snapNearest (double beat, Mode mode, const Context& context)
    {
        if (mode == Mode::cell)
            return snapFloor (beat, mode, context);

        const auto grid = getGridBeats (mode, context);

        if (grid <= 0.0)
            return roundToTicks (beat);

        return roundToTicks (std::round (beat / grid) * grid);
    }

    double snapFloor (double beat, Mode mode, const Context& context)
    {
        const auto grid = getGridBeats (mode, context);

        if (grid <= 0.0)
            return roundToTicks (beat);

        return roundToTicks (std::floor (beat / grid + epsilon) * grid);
    }

    double snapDelta (double deltaBeats, Mode mode, const Context& context)
    {
        const auto grid = getGridBeats (mode, context);

        if (grid <= 0.0)
            return roundToTicks (deltaBeats);

        return roundToTicks (std::round (deltaBeats / grid) * grid);
    }

    double getStepBeats (Mode mode, const Context& context)
    {
        const auto grid = getGridBeats (mode, context);
        return grid > 0.0 ? grid : 0.25;
    }
}
