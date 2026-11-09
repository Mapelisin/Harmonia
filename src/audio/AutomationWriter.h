#pragma once

#include "core/Tracktion.h"

namespace harmonia::audio::automation
{
    /** Writing automation. While it's on and the song plays, a setting of a track moved with the
        mouse or a MIDI control (audio::midi_learn) has its movement written into its line: from
        when it's grabbed until it's let go (a MIDI control: until it rests), the line after that
        stays as it was (Tracktion's touch mode). Tracks added while it's on write too.
        Off for a song just opened. The master isn't written: it has no lines. */
    class Writer final : private juce::ValueTree::Listener
    {
    public:
        explicit Writer (te::Edit&);
        ~Writer() override;

        void setOn (bool);
        bool isOn() const noexcept      { return on; }

        /** Switched on or off. */
        std::function<void()> onChanged;

    private:
        te::Edit& edit;
        bool on = false;

        void setModes();
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;

        JUCE_DECLARE_NON_COPYABLE (Writer)
    };

    /** A setting grabbed (a mouse drag, a MIDI control's turn) and let go again: while automation
        is written and the song plays, what it does in between goes into its line, and from a
        moment after it's let go the line goes on as it was. Grabbed twice at once (the mouse and a
        control) it counts once: let go when both have let go. */
    void beginTouch (te::AutomatableParameter&);
    void endTouch (te::AutomatableParameter&);

    /** One hand on one setting at a time: begin() lets go of the setting held before. */
    class Touch final
    {
    public:
        Touch() = default;
        ~Touch()                            { end(); }

        void begin (te::AutomatableParameter&);
        void end();
        bool isOn() const noexcept          { return parameter != nullptr; }

    private:
        te::AutomatableParameter::Ptr parameter;

        JUCE_DECLARE_NON_COPYABLE (Touch)
    };
}
