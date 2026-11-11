#include "audio/MidiLearn.h"
#include "audio/Automation.h"
#include "audio/AutomationWriter.h"
#include "core/Controllers.h"
#include "core/SongNodes.h"

namespace harmonia::audio::midi_learn
{
    namespace
    {
        const juce::Identifier controlsTag ("HMIDICONTROLS");      // in the song's state
        const juce::Identifier mappingTag ("MAPPING");
        const juce::Identifier pluginId ("plugin"), parameterId ("parameter"), channelId ("channel"), numberId ("controller");

        std::vector<Controller*>& getControllers()
        {
            static std::vector<Controller*> controllers;
            return controllers;
        }

        Control controlOf (const juce::ValueTree& mapping)
        {
            return { (int) mapping[channelId], (int) mapping[numberId] };
        }

        bool isOf (const juce::ValueTree& mapping, te::AutomatableParameter& parameter)
        {
            auto* plugin = parameter.getPlugin();
            return plugin != nullptr && te::EditItemID::fromVar (mapping[pluginId]) == plugin->itemID
                && mapping[parameterId].toString() == parameter.paramID;
        }

        te::AutomatableParameter::Ptr findParameter (te::Edit& edit, const juce::ValueTree& mapping)
        {
            if (auto plugin = te::findPluginForID (edit, te::EditItemID::fromVar (mapping[pluginId])))
                return plugin->getAutomatableParameterByID (mapping[parameterId].toString());

            return {};
        }

        /** Removes the mappings `matches` picks, into the current undo step. */
        template <typename Matches>
        void removeMappings (te::Edit& edit, Matches&& matches)
        {
            auto node = edit.state.getChildWithName (controlsTag);

            for (int i = node.getNumChildren(); --i >= 0;)
                if (matches (node.getChild (i)))
                    node.removeChild (i, &edit.getUndoManager());
        }

        double now()
        {
            return juce::Time::getMillisecondCounterHiRes();
        }
    }

    juce::String Control::describe() const
    {
        return "CC " + juce::String (number) + " (channel " + juce::String (channel) + ")";
    }

    Control Control::from (const juce::MidiMessage& message)
    {
        if (! message.isController())
            return {};

        const auto number = message.getControllerNumber();

        // Bank select, data entry and the like mean something else.
        if (! core::controllers::isPlainNumber (number))
            return {};

        return { message.getChannel(), number };
    }

    bool canAssign (te::AutomatableParameter& parameter)
    {
        auto* plugin = parameter.getPlugin();
        return plugin != nullptr && plugin->state.getParent().isValid();
    }

    juce::String describe (te::AutomatableParameter& parameter)
    {
        const auto name = automation::getDisplayName (parameter);

        if (auto* track = parameter.getTrack())
            return track->getName() + ": " + name;

        if (auto* plugin = parameter.getPlugin(); plugin != nullptr && plugin->getOwnerTrack() == nullptr)
            return "Master: " + name;

        return name;
    }

    std::vector<Mapping> getAll (te::Edit& edit)
    {
        std::vector<Mapping> mappings;

        for (const auto& mapping : edit.state.getChildWithName (controlsTag))
            if (auto parameter = findParameter (edit, mapping))
                mappings.push_back ({ controlOf (mapping), parameter });

        return mappings;
    }

    Control getControl (te::AutomatableParameter& parameter)
    {
        auto* plugin = parameter.getPlugin();

        if (plugin == nullptr)
            return {};

        for (const auto& mapping : plugin->edit.state.getChildWithName (controlsTag))
            if (isOf (mapping, parameter))
                return controlOf (mapping);

        return {};
    }

    te::AutomatableParameter::Ptr getParameter (te::Edit& edit, Control control)
    {
        for (const auto& mapping : edit.state.getChildWithName (controlsTag))
            if (controlOf (mapping) == control)
                if (auto parameter = findParameter (edit, mapping))
                    return parameter;

        return {};
    }

    void assign (te::AutomatableParameter& parameter, Control control)
    {
        if (! canAssign (parameter) || ! control.isValid())
            return;

        auto& plugin = *parameter.getPlugin();
        auto& edit = plugin.edit;
        auto& undoManager = edit.getUndoManager();
        undoManager.beginNewTransaction ("MIDI control for " + describe (parameter));

        removeMappings (edit, [&] (const juce::ValueTree& mapping) { return controlOf (mapping) == control || isOf (mapping, parameter); });

        core::song_nodes::getOrCreate (edit, controlsTag)
            .appendChild (juce::ValueTree (mappingTag, { { pluginId, plugin.itemID.toVar() },
                                                         { parameterId, parameter.paramID },
                                                         { channelId, control.channel },
                                                         { numberId, control.number } }),
                          &undoManager);
    }

    void forget (te::AutomatableParameter& parameter)
    {
        auto* plugin = parameter.getPlugin();

        if (plugin == nullptr || ! getControl (parameter).isValid())
            return;

        plugin->edit.getUndoManager().beginNewTransaction ("Forget the MIDI control of " + describe (parameter));
        removeMappings (plugin->edit, [&] (const juce::ValueTree& mapping) { return isOf (mapping, parameter); });
    }

    void forgetAll (te::Edit& edit)
    {
        if (edit.state.getChildWithName (controlsTag).getNumChildren() == 0)
            return;

        edit.getUndoManager().beginNewTransaction ("Forget all MIDI controls");
        removeMappings (edit, [] (const juce::ValueTree&) { return true; });
    }

    //==============================================================================
    Controller::Controller (te::Edit& e)
        : edit (e)
    {
        getControllers().push_back (this);
    }

    Controller::~Controller()
    {
        stopTimer();
        endTurns (std::numeric_limits<double>::max());

        if (learningAny)
            edit.getParameterChangeHandler().removeListener (*this);

        std::erase (getControllers(), this);
    }

    Controller* Controller::find (te::Edit& edit)
    {
        for (auto* controller : getControllers())
            if (&controller->edit == &edit)
                return controller;

        return nullptr;
    }

    bool Controller::handle (const juce::MidiMessage& message)
    {
        const auto control = Control::from (message);

        if (! control.isValid())
            return false;

        if (learning)
        {
            // Only the control moved while a setting waits is assigned; the rest of its turn is not a move.
            if (waiting != nullptr && canAssign (*waiting))
            {
                assign (*waiting, control);
                tell (control.describe() + " moves " + describe (*waiting));
                waiting = nullptr;

                if (! learningAny)
                    learning = false;

                changed();
            }

            return true;
        }

        if (auto parameter = getParameter (edit, control))
        {
            move (*parameter, (float) message.getControllerValue() / 127.0f);
            return true;
        }

        return false;
    }

    void Controller::learn (te::AutomatableParameter& parameter)
    {
        if (! canAssign (parameter))
            return;

        if (learningAny)
            edit.getParameterChangeHandler().removeListener (*this);

        learning = true;
        learningAny = false;
        waiting = &parameter;
        tell ("Move a control on your MIDI keyboard for " + describe (parameter) + " (MIDI Learn on its menu again: cancel)");
        changed();
    }

    void Controller::startLearning()
    {
        if (! learningAny)
            edit.getParameterChangeHandler().addListener (*this);

        learning = learningAny = true;
        waiting = nullptr;
        tell ("MIDI Learn: move a knob or fader (here or in a plugin's window), then a control on your MIDI keyboard. Ctrl+Shift+L: stop");
        changed();
    }

    void Controller::stopLearning()
    {
        if (! learning)
            return;

        if (learningAny)
            edit.getParameterChangeHandler().removeListener (*this);

        learning = learningAny = false;
        waiting = nullptr;
        tell ("MIDI Learn off");
        changed();
    }

    void Controller::pluginParameterChanged (te::AutomatableParameter& parameter, bool isFollowingAutomation)
    {
        // Moved by the user (here or in a plugin's window), not by a line or a control.
        if (isFollowingAutomation || movingNow || ! learningAny || ! juce::MessageManager::existsAndIsCurrentThread())
            return;

        if (waiting == &parameter || ! canAssign (parameter))
            return;

        waiting = &parameter;
        tell ("Now move a control on your MIDI keyboard for " + describe (parameter));
        changed();
    }

    void Controller::move (te::AutomatableParameter& parameter, float value01)
    {
        const auto time = now();
        auto turn = std::find_if (turns.begin(), turns.end(), [&] (const Turn& t) { return t.parameter.get() == &parameter; });

        if (turn == turns.end())
        {
            // A new turn: its own undo step, and a touch (written while automation is written).
            edit.getUndoManager().beginNewTransaction ("MIDI: " + describe (parameter));
            automation::beginTouch (parameter);
            turns.push_back ({ &parameter, time });

            if (! isTimerRunning())
                startTimer (50);
        }
        else
        {
            turn->lastMs = time;
        }

        const juce::ScopedValueSetter<bool> moving (movingNow, true);
        parameter.midiControllerMoved (value01);
    }

    void Controller::endTurns (double restingSinceMs)
    {
        for (auto turn = turns.begin(); turn != turns.end();)
        {
            if (turn->lastMs <= restingSinceMs)
            {
                automation::endTouch (*turn->parameter);
                turn = turns.erase (turn);
            }
            else
            {
                ++turn;
            }
        }
    }

    void Controller::timerCallback()
    {
        endTurns (now() - restMs);

        if (turns.empty())
            stopTimer();
    }

    void Controller::tell (const juce::String& text)
    {
        if (onMessage != nullptr)
            onMessage (text);
    }

    void Controller::changed()
    {
        if (onLearningChanged != nullptr)
            onLearningChanged();
    }
}
