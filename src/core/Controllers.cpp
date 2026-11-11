#include "core/Controllers.h"
#include "core/ClipLinks.h"
#include "core/Clips.h"

namespace harmonia::core::controllers
{
    namespace
    {
        constexpr int maxBend = 16383;

        /** The on/off controllers: the pedals, portamento, legato. */
        bool isSwitchNumber (int number)    { return number >= 64 && number <= 69; }

        /** Tracktion keeps controller values as 14 bits: the wheel's own, the others' 7 bits shifted up. */
        int toStored (Controller c, float value)
        {
            value = juce::jlimit (0.0f, 1.0f, value);

            if (c == Controller::pitchBend)
                return juce::roundToInt (value * (float) maxBend);

            if (isSwitch (c))
                return value >= 0.5f ? 127 << 7 : 0;

            return juce::roundToInt (value * 127.0f) << 7;
        }

        float fromStored (Controller c, int stored)
        {
            if (c == Controller::pitchBend)
                return juce::jlimit (0.0f, 1.0f, (float) stored / (float) maxBend);

            return juce::jlimit (0.0f, 1.0f, (float) (stored >> 7) / 127.0f);
        }

        /** The names musicians know the other numbers by. */
        juce::String getNumberName (int number)
        {
            switch (number)
            {
                case 2:     return "Breath";
                case 4:     return "Foot pedal";
                case 5:     return "Portamento time";
                case 8:     return "Balance";
                case 65:    return "Portamento";
                case 66:    return "Sostenuto pedal";
                case 67:    return "Soft pedal";
                case 68:    return "Legato";
                case 69:    return "Hold 2 pedal";
                case 71:    return "Resonance";
                case 72:    return "Release";
                case 73:    return "Attack";
                case 74:    return "Brightness";
                case 91:    return "Reverb";
                case 93:    return "Chorus";
                default:    return {};
            }
        }

        juce::UndoManager* undoManagerFor (te::MidiClip& clip)
        {
            return &clip.edit.getUndoManager();
        }

        void removeBetween (te::MidiClip& clip, Controller c, double fromBeat, double toBeat)
        {
            clip.getSequence().removeControllersBetween (c.type, te::BeatPosition::fromBeats (fromBeat),
                                                         te::BeatPosition::fromBeats (toBeat), undoManagerFor (clip));
        }

        void addEvent (te::MidiClip& clip, Controller c, double beat, int stored)
        {
            clip.getSequence().addControllerEvent (te::BeatPosition::fromBeats (std::max (0.0, beat)), c.type, stored,
                                                   undoManagerFor (clip));
        }
    }

    const std::vector<Controller>& getCommon()
    {
        static const std::vector<Controller> common { Controller::pitchBend, Controller::modWheel, Controller::sustain,
                                                      Controller::expression, Controller::volume, Controller::pan,
                                                      Controller::aftertouch };
        return common;
    }

    const std::vector<Controller>& getAll()
    {
        static const std::vector<Controller> all = []
        {
            auto result = getCommon();

            for (int number = 0; number < 128; ++number)
                if (isPlainNumber (number) && std::find (result.begin(), result.end(), Controller::cc (number)) == result.end())
                    result.push_back (Controller::cc (number));

            return result;
        }();

        return all;
    }

    bool isPlainNumber (int number)
    {
        // Bank select, data entry, (N)RPN numbers and the channel-mode messages mean something else.
        return number > 0 && number < 120 && number != 32 && number != 6 && number != 38 && ! (number >= 98 && number <= 101);
    }

    std::vector<Controller> getUsed (const te::MidiClip& clip)
    {
        std::set<int> types;

        for (auto* e : clip.getSequence().getControllerEvents())
            types.insert (e->getType());

        std::vector<Controller> used;

        for (auto c : getAll())
            if (types.count (c.type) > 0)
                used.push_back (c);

        return used;
    }

    juce::String getName (Controller c)
    {
        if (c == Controller::pitchBend)     return "Pitch bend";
        if (c == Controller::aftertouch)    return "Aftertouch";
        if (c == Controller::modWheel)      return "Mod wheel";
        if (c == Controller::sustain)       return "Sustain pedal";
        if (c == Controller::expression)    return "Expression";
        if (c == Controller::volume)        return "Volume";
        if (c == Controller::pan)           return "Pan";

        const auto name = getNumberName (c.type);
        return "CC " + juce::String (c.type) + (name.isNotEmpty() ? " " + name : juce::String());
    }

    juce::String getLongName (Controller c)
    {
        const auto& common = getCommon();

        if (c.isCc() && std::find (common.begin(), common.end(), c) != common.end())
            return getName (c) + " (CC " + juce::String (c.type) + ")";

        return getName (c);
    }

    float getDefault (Controller c)
    {
        if (c == Controller::pitchBend)                         return fromStored (c, 8192);
        if (c == Controller::pan || c == Controller::cc (8))    return 64.0f / 127.0f;
        if (c == Controller::volume)                            return 100.0f / 127.0f;
        if (c == Controller::expression)                        return 1.0f;

        return 0.0f;
    }

    bool isBipolar (Controller c)   { return c == Controller::pitchBend || c == Controller::pan || c == Controller::cc (8); }
    bool isSwitch (Controller c)    { return c.isCc() && isSwitchNumber (c.type); }

    juce::String describe (Controller c, float value)
    {
        if (c == Controller::pitchBend)
        {
            const auto percent = juce::roundToInt ((toStored (c, value) - 8192) * 100.0 / 8192.0);
            return percent == 0 ? juce::String ("0% (no bend)") : (percent > 0 ? "+" : "") + juce::String (percent) + "%";
        }

        const auto number = toStored (c, value) >> 7;

        if (isSwitch (c))
        {
            const auto isPedal = c.type != 65 && c.type != 68;
            return number >= 64 ? (isPedal ? "Down" : "On") : (isPedal ? "Up" : "Off");
        }

        if (isBipolar (c))
            return number == 64 ? juce::String ("Centre") : (number < 64 ? "L" + juce::String (64 - number) : "R" + juce::String (number - 64));

        return juce::String (number);
    }

    std::optional<std::pair<Controller, float>> fromMidi (const juce::MidiMessage& message)
    {
        if (message.isPitchWheel())
            return std::pair { Controller::pitchBend, fromStored (Controller::pitchBend, message.getPitchWheelValue()) };

        if (message.isChannelPressure())
            return std::pair { Controller::aftertouch, (float) message.getChannelPressureValue() / 127.0f };

        if (message.isController() && isPlainNumber (message.getControllerNumber()))
        {
            const auto c = Controller::cc (message.getControllerNumber());
            const auto value = message.getControllerValue();
            return std::pair { c, isSwitch (c) ? (value >= 64 ? 1.0f : 0.0f) : (float) value / 127.0f };
        }

        return std::nullopt;
    }

    juce::MidiMessage toMidi (Controller c, float value, int channel)
    {
        channel = juce::jlimit (1, 16, channel);

        if (c == Controller::pitchBend)
            return juce::MidiMessage::pitchWheel (channel, toStored (c, value));

        if (c == Controller::aftertouch)
            return juce::MidiMessage::channelPressureChange (channel, toStored (c, value) >> 7);

        return juce::MidiMessage::controllerEvent (channel, c.type, toStored (c, value) >> 7);
    }

    std::vector<Point> getPoints (const te::MidiClip& clip, Controller c)
    {
        std::vector<Point> points;
        const auto type = c.type;

        for (auto* e : clip.getSequence().getControllerEvents())
            if (e->getType() == type)
                points.push_back ({ e->getBeatPosition().inBeats(), fromStored (c, e->getControllerValue()) });

        std::stable_sort (points.begin(), points.end(), [] (const Point& a, const Point& b) { return a.beat < b.beat; });
        return points;
    }

    bool hasPoints (const te::MidiClip& clip, Controller c)
    {
        return clip.getSequence().containsController (c.type);
    }

    float getValueAt (const te::MidiClip& clip, Controller c, double beat)
    {
        auto value = getDefault (c);

        for (const auto& p : getPoints (clip, c))
        {
            if (p.beat > beat + 1.0e-9)
                break;

            value = p.value;
        }

        return value;
    }

    void drawLine (te::MidiClip& clip, Controller c, double beatA, float valueA, double beatB, float valueB)
    {
        if (beatB < beatA)
        {
            std::swap (beatA, beatB);
            std::swap (valueA, valueB);
        }

        beatA = std::max (0.0, beatA);
        beatB = std::max (beatA, beatB);

        // What plays just before the line, so a step that changes nothing isn't written.
        auto previous = toStored (c, getValueAt (clip, c, beatA - 1.0e-6));
        removeBetween (clip, c, beatA, beatB + 1.0e-9);

        const auto steps = std::max (0, (int) std::ceil ((beatB - beatA) / drawStepBeats - 1.0e-9));

        for (int i = 0; i <= steps; ++i)
        {
            const auto beat = i == steps ? beatB : beatA + i * drawStepBeats;
            const auto t = beatB > beatA ? (float) ((beat - beatA) / (beatB - beatA)) : 1.0f;
            const auto stored = toStored (c, valueA + (valueB - valueA) * t);

            if (stored != previous)
                addEvent (clip, c, beat, stored);

            previous = stored;
        }

        clip_links::syncFrom (clip);
    }

    void erase (te::MidiClip& clip, Controller c, double fromBeat, double toBeat)
    {
        removeBetween (clip, c, std::min (fromBeat, toBeat), std::max (fromBeat, toBeat));
        clip_links::syncFrom (clip);
    }

    void add (te::MidiClip& clip, Controller c, double beat, float value)
    {
        beat = std::max (0.0, beat);
        removeBetween (clip, c, beat - 1.0e-6, beat + 1.0e-6);
        addEvent (clip, c, beat, toStored (c, value));
        clip_links::syncFrom (clip);
    }

    void set (te::MidiClip& clip, Controller c, const std::vector<Point>& points)
    {
        auto sorted = points;
        std::stable_sort (sorted.begin(), sorted.end(), [] (const Point& a, const Point& b) { return a.beat < b.beat; });

        removeBetween (clip, c, -1.0e9, 1.0e9);
        std::optional<int> previous;

        for (const auto& p : sorted)
        {
            const auto stored = toStored (c, p.value);

            if (stored != previous)
                addEvent (clip, c, p.beat, stored);

            previous = stored;
        }

        clip_links::syncFrom (clip);
    }

    std::vector<Point> getPlayed (const te::MidiClip& clip, Controller c)
    {
        std::vector<Point> played;
        const auto points = getPoints (clip, c);

        if (points.empty())
            return played;

        const auto clipStart = clips::getStartBeat (clip), clipEnd = clips::getEndBeat (clip);
        constexpr double tiny = 1.0e-9;

        // Content beats [from, to) played from song beat `songFrom`: the part inside the clip.
        const auto addStretch = [&] (double from, double to, double songFrom)
        {
            const auto shift = songFrom - from;
            from = std::max (from, clipStart - shift);
            to = std::min (to, clipEnd - shift);

            if (to <= from + tiny)
                return;

            std::optional<float> before;     // set by the last event before the stretch

            for (const auto& p : points)
            {
                if (p.beat < from - tiny)
                {
                    before = p.value;
                    continue;
                }

                if (p.beat >= to - tiny)
                    break;

                if (before.has_value() && p.beat > from + tiny)
                    played.push_back ({ from + shift, *before });

                before.reset();
                played.push_back ({ p.beat + shift, p.value });
            }

            if (before.has_value())
                played.push_back ({ from + shift, *before });
        };

        const auto contentStart = clips::getContentStartBeat (clip);

        if (! clips::isLooping (clip))
        {
            addStretch (clipStart - contentStart, clipEnd - contentStart, clipStart);
            return played;
        }

        // Every time round, as getPlayedNotes counts them.
        const auto loop = clips::getLoopRange (clip);
        const auto length = loop.getLength();
        const auto first = contentStart + loop.getStart();
        const auto firstRepeat = (int) std::floor ((clipStart - first) / length + tiny);
        const auto lastRepeat = (int) std::ceil ((clipEnd - first) / length - tiny);

        for (int repeat = firstRepeat; repeat < lastRepeat; ++repeat)
            addStretch (loop.getStart(), loop.getEnd(), first + repeat * length);

        std::stable_sort (played.begin(), played.end(), [] (const Point& a, const Point& b) { return a.beat < b.beat; });
        return played;
    }
}
