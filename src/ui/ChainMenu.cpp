#include "ui/ChainMenu.h"
#include "audio/Effects.h"

namespace harmonia::ui::chain_menu
{
    namespace chains = audio::chains;

    namespace
    {
        /** A chain that may be gone (its track deleted) by the time a menu item is chosen: found
            again by its owner. */
        struct ChainRef
        {
            explicit ChainRef (te::PluginList& chain)
                : edit (&chain.getEdit()), track (dynamic_cast<te::AudioTrack*> (chain.getOwnerTrack()))
            {
            }

            te::PluginList* get() const
            {
                if (track == nullptr)
                    return &edit->getMasterPluginList();

                return track->state.getParent().isValid() ? &track->pluginList : nullptr;
            }

            te::Edit* edit;
            juce::ReferenceCountedObjectPtr<te::AudioTrack> track;
        };

        void saveAs (te::PluginList& chain, const juce::String& name, const Tell& tell, const juce::File& folder)
        {
            if (const auto error = chains::save (chain, name, folder); error.isNotEmpty())
                tell (error, true);
            else
                tell ("Chain saved: " + chains::getName (chains::getFileFor (name, folder)), false);
        }
    }

    void load (te::PluginList& chain, const juce::File& file, bool replace, const Tell& tell)
    {
        const auto loaded = chains::load (chain, file, replace);

        if (loaded.error.isNotEmpty())
            tell (loaded.error, true);
        else if (! loaded.problems.isEmpty())
            tell ("Chain " + chains::getName (file) + ": " + juce::String (loaded.added) + " added, left out: "
                  + loaded.problems.joinIntoString ("; "), true);
        else
            tell ("Chain: " + chains::getName (file), false);
    }

    juce::PopupMenu make (te::PluginList& chain, Tell tell, const juce::File& folder)
    {
        const ChainRef ref (chain);
        const auto run = [ref] (std::function<void (te::PluginList&)> action)
        {
            return [ref, action]
            {
                if (auto* target = ref.get())
                    action (*target);
            };
        };

        juce::PopupMenu menu;
        const auto hasEffects = ! audio::effects::get (chain).isEmpty();
        menu.addItem ("Save these effects as a chain...", hasEffects, false,
                      run ([tell, folder] (te::PluginList& target) { askToSave (target, tell, folder); }));
        menu.addSeparator();

        const auto saved = chains::list (folder);

        if (saved.isEmpty())
            menu.addItem ("No chains saved yet", false, false, nullptr);

        juce::PopupMenu replace;

        for (const auto& file : saved)
        {
            menu.addItem ("Add " + chains::getName (file), run ([file, tell] (te::PluginList& target) { load (target, file, false, tell); }));
            replace.addItem (chains::getName (file), run ([file, tell] (te::PluginList& target) { load (target, file, true, tell); }));
        }

        if (! saved.isEmpty())
            menu.addSubMenu ("Replace the effects with", replace);

        menu.addSeparator();
        menu.addItem ("Show chains folder", [folder]
        {
            folder.createDirectory();
            folder.revealToUser();
        });

        return menu;
    }

    void askToSave (te::PluginList& chain, Tell tell, const juce::File& folder)
    {
        const ChainRef ref (chain);
        auto* owner = chain.getOwnerTrack();

        auto* window = new juce::AlertWindow ("Save chain", "The effects of " + (owner != nullptr ? owner->getName() : juce::String ("the master"))
                                                            + ", as a chain called:",
                                              juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("name", owner != nullptr ? owner->getName() : juce::String ("Master"));
        window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        juce::Component::SafePointer<juce::AlertWindow> safeWindow (window);
        window->enterModalState (true, juce::ModalCallbackFunction::create ([safeWindow, ref, tell, folder] (int result)
        {
            auto* target = ref.get();

            if (result != 1 || safeWindow == nullptr || target == nullptr)
                return;

            const auto name = safeWindow->getTextEditorContents ("name");
            const auto file = chains::getFileFor (name, folder);

            if (! file.existsAsFile())
            {
                saveAs (*target, name, tell, folder);
                return;
            }

            const auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                                                          .withTitle ("Replace chain?")
                                                          .withMessage ("There is a chain called \"" + chains::getName (file)
                                                                        + "\" already. Replace it with these effects?")
                                                          .withButton ("Replace", 1)
                                                          .withButton ("Cancel", 0);

            juce::AlertWindow::showAsync (options, [ref, name, tell, folder] (int answer)
            {
                if (auto* again = ref.get(); answer == 1 && again != nullptr)
                    saveAs (*again, name, tell, folder);
            });
        }), true);
    }
}
