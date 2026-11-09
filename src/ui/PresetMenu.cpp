#include "ui/PresetMenu.h"

namespace harmonia::ui::preset_menu
{
    namespace presets = audio::presets;

    namespace
    {
        /** The plugin wasn't deleted (or undone away) while a menu or a question was open. */
        bool isInSong (const te::Plugin::Ptr& plugin)
        {
            return plugin != nullptr && plugin->state.getParent().isValid();
        }

        void saveAs (te::Plugin& plugin, const juce::String& name, const Tell& tell, const juce::File& folder)
        {
            if (const auto error = presets::save (plugin, name, folder); error.isNotEmpty())
                tell (error, true);
            else
                tell ("Preset saved: " + presets::getName (presets::getFileFor (plugin, name, folder)), false);
        }

        void load (te::Plugin& plugin, const juce::File& preset, const Tell& tell)
        {
            if (const auto error = presets::load (plugin, preset); error.isNotEmpty())
                tell (error, true);
            else
                tell ("Preset: " + presets::getName (preset), false);
        }
    }

    juce::PopupMenu make (te::Plugin& p, Tell tell, const juce::File& folder)
    {
        juce::PopupMenu menu;
        const te::Plugin::Ptr plugin (&p);

        if (! presets::canHavePresets (p))
        {
            menu.addItem ("No presets for " + presets::getPluginName (p), false, false, nullptr);
            return menu;
        }

        // Every action checks the plugin is still in the song: the menu may outlive it.
        const auto run = [plugin] (std::function<void (te::Plugin&)> action)
        {
            return [plugin, action]
            {
                if (isInSong (plugin))
                    action (*plugin);
            };
        };

        menu.addItem ("Save preset...", run ([tell, folder] (te::Plugin& target) { askToSave (target, tell, folder); }));

        const auto saved = presets::list (p, folder);
        const auto lastUsed = presets::getLastUsed (p);
        menu.addSeparator();

        if (saved.isEmpty())
            menu.addItem ("No presets saved yet", false, false, nullptr);

        for (const auto& file : saved)
            menu.addItem (presets::getName (file), true, presets::getName (file) == lastUsed,
                          run ([file, tell] (te::Plugin& target) { load (target, file, tell); }));

        if (const auto own = presets::getOwnPresets (p); ! own.isEmpty())
        {
            const auto current = presets::getCurrentOwnPreset (p);
            juce::PopupMenu ownMenu;

            for (int i = 0; i < own.size(); ++i)
                ownMenu.addItem (own[i], true, i == current, run ([i, name = own[i], tell] (te::Plugin& target)
                {
                    presets::useOwnPreset (target, i);
                    tell ("Preset: " + name, false);
                }));

            menu.addSeparator();
            menu.addSubMenu ("Presets of " + presets::getPluginName (p), ownMenu);
        }

        menu.addSeparator();

        if (presets::canReset (p))
            menu.addItem ("Reset to first settings", run ([tell] (te::Plugin& target)
            {
                presets::reset (target);
                tell (presets::getPluginName (target) + ": first settings", false);
            }));

        menu.addItem ("Show presets folder", run ([folder] (te::Plugin& target)
        {
            const auto pluginFolder = presets::getFolderFor (target, folder);
            pluginFolder.createDirectory();
            pluginFolder.revealToUser();
        }));

        return menu;
    }

    void askToSave (te::Plugin& p, Tell tell, const juce::File& folder)
    {
        const te::Plugin::Ptr plugin (&p);
        const auto lastUsed = presets::getLastUsed (p);

        auto* window = new juce::AlertWindow ("Save preset", "The settings of " + presets::getPluginName (p) + ", as a preset called:",
                                              juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("name", lastUsed.isNotEmpty() ? lastUsed : juce::String ("New preset"));
        window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        juce::Component::SafePointer<juce::AlertWindow> safeWindow (window);
        window->enterModalState (true, juce::ModalCallbackFunction::create ([safeWindow, plugin, tell, folder] (int result)
        {
            if (result != 1 || safeWindow == nullptr || ! isInSong (plugin))
                return;

            const auto name = safeWindow->getTextEditorContents ("name");
            const auto file = presets::getFileFor (*plugin, name, folder);

            if (! file.existsAsFile())
            {
                saveAs (*plugin, name, tell, folder);
                return;
            }

            const auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                                                          .withTitle ("Replace preset?")
                                                          .withMessage ("There is a preset called \"" + presets::getName (file)
                                                                        + "\" already. Replace it with these settings?")
                                                          .withButton ("Replace", 1)
                                                          .withButton ("Cancel", 0);

            juce::AlertWindow::showAsync (options, [plugin, name, tell, folder] (int answer)
            {
                if (answer == 1 && isInSong (plugin))
                    saveAs (*plugin, name, tell, folder);
            });
        }), true);
    }
}
