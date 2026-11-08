#pragma once

#include "PianoRollView.h"

namespace harmonia::spike
{
    /** Scrolls the piano roll every vertical blank through a list of scenarios,
        for every rendering engine the window offers, and writes a report. */
    class Benchmark : private juce::Timer
    {
    public:
        Benchmark (PianoRollView&, juce::File reportFile, bool driveFromTimer, bool useOpenGL, std::function<void()> onFinished);

    private:
        struct Scenario
        {
            juce::String name;
            int numNotes;
            int numBars;
            bool zoomedOut;
        };

        PianoRollView& view;
        juce::File reportFile;
        std::function<void()> onFinished;
        std::unique_ptr<juce::VBlankAttachment> vblank;
        size_t lastPaintCount = (size_t) -1;

        std::vector<Scenario> scenarios;
        std::vector<std::unique_ptr<NoteSet>> noteSets;
        juce::StringArray engines;
        int engineIndex = 0;
        size_t scenarioIndex = 0;
        double scenarioStartMs = 0.0;
        bool finished = false;
        bool useOpenGL = false;
        juce::String report;

        static constexpr int warmUpFrames = 30;
        static constexpr int measuredFrames = 240;

        void onVBlank();
        void timerCallback() override;
        void startScenario();
        void finishScenario();
        void writeLine (const juce::String&);
    };
}
