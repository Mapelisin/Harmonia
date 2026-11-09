#include "ui/ParameterMenu.h"
#include "audio/MidiLearn.h"

namespace harmonia::ui::parameter_menu
{
    namespace learn = audio::midi_learn;

    namespace
    {
        learn::Controller* findController (te::AutomatableParameter& parameter)
        {
            auto* plugin = parameter.getPlugin();
            return plugin != nullptr ? learn::Controller::find (plugin->edit) : nullptr;
        }
    }

    juce::PopupMenu make (te::AutomatableParameter* p, const juce::String& whyNot)
    {
        juce::PopupMenu menu;

        if (p == nullptr)
        {
            menu.addItem (whyNot, false, false, nullptr);
            return menu;
        }

        const te::AutomatableParameter::Ptr parameter (p);
        auto* controller = findController (*p);
        const auto waiting = controller != nullptr && controller->getWaitingParameter() == p;
        menu.addSectionHeader (learn::describe (*p));

        menu.addItem (waiting ? "MIDI Learn: move a control on your keyboard (click: cancel)" : "MIDI Learn",
                      controller != nullptr && learn::canAssign (*p), waiting, [parameter, waiting]
                      {
                          if (auto* c = findController (*parameter))
                          {
                              if (waiting) c->stopLearning();
                              else         c->learn (*parameter);
                          }
                      });

        if (const auto control = learn::getControl (*p); control.isValid())
            menu.addItem ("Forget " + control.describe(), [parameter] { learn::forget (*parameter); });

        return menu;
    }

    juce::String getBadge (te::AutomatableParameter& parameter)
    {
        if (auto* controller = findController (parameter); controller != nullptr && controller->getWaitingParameter() == &parameter)
            return "Learn...";

        if (const auto control = learn::getControl (parameter); control.isValid())
            return "CC " + juce::String (control.number);

        return {};
    }
}
