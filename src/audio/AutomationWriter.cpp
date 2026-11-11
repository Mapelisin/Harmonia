#include "audio/AutomationWriter.h"
#include "audio/Automation.h"

namespace harmonia::audio::automation
{
    namespace
    {
        juce::var modeFor (bool writing)
        {
            return te::toString (writing ? te::AutomationMode::touch : te::AutomationMode::read);
        }

        struct Held
        {
            te::AutomatableParameter::Ptr parameter;
            int hands = 0;
            std::vector<Point> lineBefore;      // as it was when grabbed
        };

        std::vector<Held>& getHeld()
        {
            static std::vector<Held> held;      // message thread only
            return held;
        }

        /** How long the line takes to get back to where it was after a setting is let go. */
        constexpr double returnSeconds = 0.05;

        /** The value of a line (points in beat order, at least one) at `beat`. */
        float valueOn (const std::vector<Point>& line, double beat)
        {
            if (beat <= line.front().beat)
                return line.front().value;

            if (beat >= line.back().beat)
                return line.back().value;

            const auto next = std::upper_bound (line.begin(), line.end(), beat, [] (double b, const Point& p) { return b < p.beat; });
            const auto previous = std::prev (next);
            const auto span = next->beat - previous->beat;
            return span > 0.0 ? previous->value + (next->value - previous->value) * (float) ((beat - previous->beat) / span)
                              : next->value;
        }

        /** Writing stopped at `seconds`: from shortly after, the line goes on as it was (Tracktion
            would instead lead it from the last value written straight to the next point it had). */
        void returnToLine (te::AutomatableParameter& parameter, const std::vector<Point>& lineBefore, double seconds)
        {
            auto& edit = parameter.getEdit();
            const auto& tempo = edit.tempoSequence;
            const auto glide = std::max (0.0, te::AutomationRecordManager::getGlideSeconds (edit.engine).inSeconds());
            const auto from = tempo.toBeats (te::TimePosition::fromSeconds (seconds)).inBeats();
            const auto back = tempo.toBeats (te::TimePosition::fromSeconds (seconds + glide + returnSeconds)).inBeats();

            // A point of its own there brings it back already.
            for (const auto& point : lineBefore)
                if (point.beat > from && point.beat <= back)
                    return;

            addPoint (parameter, back, valueOn (lineBefore, back));
        }
    }

    void beginTouch (te::AutomatableParameter& parameter)
    {
        auto& held = getHeld();
        auto it = std::find_if (held.begin(), held.end(), [&] (const Held& h) { return h.parameter.get() == &parameter; });

        if (it != held.end())
        {
            ++it->hands;
            return;
        }

        held.push_back ({ &parameter, 1, getPoints (parameter) });
        parameter.parameterChangeGestureBegin();
    }

    void endTouch (te::AutomatableParameter& parameter)
    {
        auto& held = getHeld();
        auto it = std::find_if (held.begin(), held.end(), [&] (const Held& h) { return h.parameter.get() == &parameter; });

        if (it == held.end() || --it->hands > 0)
            return;

        const auto letGo = it->parameter;
        const auto lineBefore = std::move (it->lineBefore);
        held.erase (it);

        // Being written (touch): letting go ends that.
        const auto wasWritten = letGo->isCurrentlyRecording();
        auto* context = letGo->getEdit().getTransport().getCurrentPlaybackContext();
        const auto seconds = context != nullptr ? context->getPosition().inSeconds() : 0.0;
        letGo->parameterChangeGestureEnd();

        if (wasWritten && context != nullptr && ! letGo->isCurrentlyRecording() && ! lineBefore.empty())
            returnToLine (*letGo, lineBefore, seconds);
    }

    void Touch::begin (te::AutomatableParameter& p)
    {
        if (parameter.get() == &p)
            return;

        end();
        parameter = &p;
        beginTouch (p);
    }

    void Touch::end()
    {
        if (auto p = std::exchange (parameter, nullptr))
            endTouch (*p);
    }

    Writer::Writer (te::Edit& e)
        : edit (e)
    {
        // Tracktion writes whenever the song plays if its switch is on (as a new song has it).
        edit.getAutomationRecordManager().setWritingAutomation (false);
        setModes();
        edit.state.addListener (this);
    }

    Writer::~Writer()
    {
        edit.state.removeListener (this);
    }

    void Writer::setOn (bool shouldBeOn)
    {
        if (on == shouldBeOn)
            return;

        auto& recorder = edit.getAutomationRecordManager();

        // What's being written goes into its line first.
        if (! shouldBeOn)
            recorder.punchOut (false);

        on = shouldBeOn;
        recorder.setWritingAutomation (on);
        setModes();

        if (onChanged != nullptr)
            onChanged();
    }

    void Writer::setModes()
    {
        // Not undo steps: a way of working, not a change to the song.
        for (auto* track : te::getAudioTracks (edit))
            track->state.setProperty (te::IDs::automationMode, modeFor (on), nullptr);

        // The master's fader and pan go by the master track's.
        if (auto* master = edit.getMasterTrack())
            master->state.setProperty (te::IDs::automationMode, modeFor (on), nullptr);
    }

    void Writer::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
    {
        if (on && child.hasType (te::IDs::TRACK))
            child.setProperty (te::IDs::automationMode, modeFor (true), nullptr);
    }
}
