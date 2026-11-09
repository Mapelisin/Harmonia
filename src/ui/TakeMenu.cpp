#include "ui/TakeMenu.h"
#include "core/Clips.h"
#include "core/Takes.h"

namespace harmonia::ui::take_menu
{
    namespace takes = core::takes;

    juce::String describe (const te::Clip& clip)
    {
        const auto count = takes::getCount (clip);

        if (count == 0)
            return {};

        const auto current = takes::getCurrent (clip);
        return current >= 0 ? "Take " + juce::String (current + 1) + " of " + juce::String (count)
                            : juce::String (count) + " takes";
    }

    bool choose (core::Project& project, te::AudioClipBase& clip, int index)
    {
        if (index == takes::getCurrent (clip))
            return true;

        project.beginGesture (takes::getName (index));
        return takes::choose (clip, index);
    }

    juce::PopupMenu make (core::Project& project, te::AudioClipBase& clip)
    {
        juce::PopupMenu menu;
        const auto count = takes::getCount (clip);

        if (count == 0)
            return menu;

        const te::Clip::Ptr target (&clip);
        const auto current = takes::getCurrent (clip);
        const auto withClip = [target, &project] (std::function<void (te::AudioClipBase&)> action)
        {
            return [target, &project, action]
            {
                if (auto* audio = dynamic_cast<te::AudioClipBase*> (target.get()); audio != nullptr && core::clips::isInEdit (*audio))
                    action (*audio);
            };
        };

        for (int i = 0; i < count; ++i)
        {
            const auto file = takes::getFile (clip, i);
            const auto seconds = te::AudioFile (clip.edit.engine, file).getLength();
            menu.addItem (takes::getName (i) + "  (" + juce::String (seconds, 1) + " s)", file.existsAsFile(), i == current,
                          withClip ([&project, i] (te::AudioClipBase& audio) { choose (project, audio, i); }));
        }

        menu.addSeparator();
        menu.addItem ("Keep only this take", current >= 0, false, withClip ([&project] (te::AudioClipBase& audio)
        {
            project.beginGesture ("Keep only this take");
            takes::keepOnlyCurrent (audio);
        }));

        return menu;
    }
}
