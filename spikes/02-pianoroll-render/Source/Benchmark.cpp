#include "Benchmark.h"

namespace harmonia::spike
{
    namespace
    {
        double percentile (std::vector<double> values, double p)
        {
            if (values.empty())
                return 0.0;

            std::sort (values.begin(), values.end());
            const auto index = (size_t) std::min ((double) values.size() - 1.0, p * (double) values.size());
            return values[index];
        }
    }

    Benchmark::Benchmark (PianoRollView& v, juce::File file, bool driveFromTimer, bool openGL, std::function<void()> callback)
        : view (v),
          reportFile (std::move (file)),
          onFinished (std::move (callback)),
          useOpenGL (openGL)
    {
        if (driveFromTimer)
            startTimer (1);
        else
            vblank = std::make_unique<juce::VBlankAttachment> (&v, [this] { onVBlank(); });

        scenarios = {
            { "pattern 500 notes, zoomed in",        500,     8,    false },
            { "clip 20k notes, zoomed in",           20000,   256,  false },
            { "clip 20k notes, whole clip visible",  20000,   256,  true  },
            { "stress 200k notes, zoomed in",        200000,  1024, false },
            { "stress 200k notes, all visible",      200000,  1024, true  },
        };

        for (auto& s : scenarios)
            noteSets.push_back (std::make_unique<NoteSet> (NoteSet::generate (s.numNotes, s.numBars, 1234)));

        if (useOpenGL)
            engines = { "OpenGL" };
        else if (auto* peer = view.getPeer())
            engines = peer->getAvailableRenderingEngines();

        reportFile.replaceWithText ({});
        writeLine ("Harmonia spike 02 piano roll render benchmark, " + juce::Time::getCurrentTime().toString (true, true));
        writeLine (juce::SystemStats::getJUCEVersion() + ", " + juce::SystemStats::getCpuModel());

        if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            writeLine ("Display scale " + juce::String (display->scale, 2) + ", view "
                       + juce::String (view.getWidth()) + "x" + juce::String (view.getHeight()) + " logical px");

        writeLine ("Rendering engines: " + engines.joinIntoString (", ")
                   + " | frames driven by " + (driveFromTimer ? "1 ms timer" : "vertical blank"));
        writeLine ("Columns: paint ms median / p95 / max | frames per second | visible notes");
        startScenario();
    }

    void Benchmark::writeLine (const juce::String& line)
    {
        report << line << "\n";
        reportFile.replaceWithText (report);
    }

    void Benchmark::startScenario()
    {
        if (scenarioIndex == 0)
        {
            if (auto* peer = view.getPeer(); peer != nullptr && ! useOpenGL)
                peer->setCurrentRenderingEngine (engineIndex);

            writeLine ("--- " + engines[engineIndex] + " ---");
        }

        const auto& s = scenarios[scenarioIndex];
        const auto& notes = *noteSets[scenarioIndex];
        const auto width = (double) (view.getWidth() - PianoRollView::keyboardWidth);

        view.setNotes (&notes);

        if (s.zoomedOut)
            view.setView (0.0, width / notes.getLengthBeats(), 96.0, 6.0f);
        else
            view.setView (0.0, width / 8.0, 84.0, 14.0f);

        view.paintDurationsMs.clear();
        view.paintStartTimesMs.clear();
        lastPaintCount = (size_t) -1;
        scenarioStartMs = juce::Time::getMillisecondCounterHiRes();
    }

    void Benchmark::finishScenario()
    {
        const auto& s = scenarios[scenarioIndex];

        // Drop warm-up frames.
        auto durations = view.paintDurationsMs;
        auto starts = view.paintStartTimesMs;
        const auto skip = (size_t) std::min ((int) durations.size(), warmUpFrames);
        durations.erase (durations.begin(), durations.begin() + (long) skip);
        starts.erase (starts.begin(), starts.begin() + (long) std::min (skip, starts.size()));

        const double elapsed = starts.size() > 1 ? starts.back() - starts.front() : 0.0;
        const double fps = elapsed > 0.0 ? 1000.0 * (double) (starts.size() - 1) / elapsed : 0.0;

        writeLine (s.name.paddedRight (' ', 38)
                   + juce::String (percentile (durations, 0.5), 2) + " / "
                   + juce::String (percentile (durations, 0.95), 2) + " / "
                   + juce::String (percentile (durations, 1.0), 2) + " ms | "
                   + juce::String (fps, 1) + " fps | "
                   + juce::String ((int) view.lastVisibleNoteCount) + " notes");

        if (++scenarioIndex >= scenarios.size())
        {
            scenarioIndex = 0;

            if (++engineIndex >= engines.size())
            {
                finished = true;
                stopTimer();
                writeLine ("DONE");
                juce::MessageManager::callAsync (onFinished);
                return;
            }
        }

        startScenario();
    }

    void Benchmark::onVBlank()
    {
        if (finished)
            return;

        if ((int) view.paintDurationsMs.size() >= warmUpFrames + measuredFrames)
        {
            finishScenario();
            return;
        }

        // Safety net: if paints stop arriving, don't hang forever.
        if (juce::Time::getMillisecondCounterHiRes() - scenarioStartMs > 30000.0)
        {
            writeLine ("[WARN] paints stalled in " + scenarios[scenarioIndex].name);
            finishScenario();
            return;
        }

        // Only advance once the previous frame has actually been painted.
        if (view.paintDurationsMs.size() == lastPaintCount)
            return;

        lastPaintCount = view.paintDurationsMs.size();

        const auto& s = scenarios[scenarioIndex];
        const double visibleBeats = (view.getWidth() - PianoRollView::keyboardWidth) / view.getPixelsPerBeat();
        const double maxFirstBeat = std::max (0.0, noteSets[scenarioIndex]->getLengthBeats() - visibleBeats);
        double next = s.zoomedOut ? 0.0 : view.getFirstBeat() + 0.25;

        if (next > maxFirstBeat)
            next = 0.0;

        view.setView (next, view.getPixelsPerBeat(), s.zoomedOut ? 96.0 : 84.0, s.zoomedOut ? 6.0f : 14.0f);
    }

    void Benchmark::timerCallback()
    {
        onVBlank();
    }
}
