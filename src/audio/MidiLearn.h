#pragma once

#include "core/Tracktion.h"

/** MIDI Learn: a knob, fader or button of a MIDI controller moves a setting of the song (a track's
    volume, pan or send, any knob of an instrument or effect, those of a plugin's own window too).

    What moves what is kept in the song (assigning or forgetting is one undo step). A control is a
    control change number on a MIDI channel; one control moves one setting, and a setting is moved
    by one control. Each turn of a control, until it rests for a moment, is one undo step and one
    touch for writing automation (audio::automation::Writer). */
namespace harmonia::audio::midi_learn
{
    /** A control of a MIDI controller: a control change number (0..127) on a channel (1..16). */
    struct Control
    {
        int channel = 0, number = -1;

        bool isValid() const noexcept                   { return number >= 0 && channel >= 1; }
        bool operator== (const Control&) const = default;

        /** "CC 21 (channel 1)". */
        juce::String describe() const;

        /** The control a message is from; invalid for anything but a control change that can be
            assigned (not bank select, data entry or the channel mode messages). */
        static Control from (const juce::MidiMessage&);
    };

    struct Mapping
    {
        Control control;
        te::AutomatableParameter::Ptr parameter;
    };

    /** Whether a control can move this setting (one of a track's, the master's or a plugin's). */
    bool canAssign (te::AutomatableParameter&);

    /** "Track 1: Volume", "Bass: EQ: Low gain", "Master: Volume". */
    juce::String describe (te::AutomatableParameter&);

    /** Every assignment of the song, in the order made; those of deleted plugins left out. */
    std::vector<Mapping> getAll (te::Edit&);

    /** The control that moves the setting (invalid if none). */
    Control getControl (te::AutomatableParameter&);

    /** The setting the control moves, or nullptr. */
    te::AutomatableParameter::Ptr getParameter (te::Edit&, Control);

    /** The control moves the setting from now on, instead of what either had before: one undo step. */
    void assign (te::AutomatableParameter&, Control);

    /** No control moves the setting any more: one undo step. */
    void forget (te::AutomatableParameter&);

    /** No control moves anything any more: one undo step. */
    void forgetAll (te::Edit&);

    //==============================================================================
    /** Turns the controls moved on MIDI keyboards into moves of their settings, and learns new
        assignments. One for the song's window; plugin windows find it with find(). */
    class Controller final : private te::ParameterChangeHandler::Listener,
                             private juce::Timer
    {
    public:
        explicit Controller (te::Edit&);
        ~Controller() override;

        /** The song's controller, or nullptr. */
        static Controller* find (te::Edit&);

        /** A message from a MIDI keyboard (message thread). A control moves the setting it is
            assigned to; while learning, it is assigned. Returns true if the message was used so. */
        bool handle (const juce::MidiMessage&);

        /** The next control moved is assigned to this setting. */
        void learn (te::AutomatableParameter&);

        /** Learning for whatever is moved next on screen (a knob, a fader, a plugin's own
            window): then the next control moved is assigned to it. Goes on, one after another,
            until stopped. */
        void startLearning();
        void stopLearning();

        bool isLearning() const noexcept                { return learning; }

        /** The setting waiting for a control, or nullptr. */
        te::AutomatableParameter* getWaitingParameter() const noexcept     { return waiting.get(); }

        /** What to tell the user ("Move a control on your MIDI keyboard for Track 1: Volume"). */
        std::function<void (const juce::String&)> onMessage;

        /** Learning started or stopped, another setting waits, or one was assigned. */
        std::function<void()> onLearningChanged;

        /** How long a control rests before its turn ends (an undo step, a touch); under the 350 ms
            after which Tracktion starts a new undo step by itself. */
        static constexpr double restMs = 300.0;

    private:
        struct Turn
        {
            te::AutomatableParameter::Ptr parameter;
            double lastMs = 0.0;
        };

        te::Edit& edit;
        bool learning = false, learningAny = false, movingNow = false;
        te::AutomatableParameter::Ptr waiting;
        std::vector<Turn> turns;

        void move (te::AutomatableParameter&, float value01);
        void endTurns (double restingSinceMs);
        void tell (const juce::String&);
        void changed();
        void pluginParameterChanged (te::AutomatableParameter&, bool isFollowingAutomation) override;
        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE (Controller)
    };
}
