#include "audio/Effects.h"

namespace harmonia::audio::effects
{
    namespace
    {
        /** The fader end of a track's list (and its sends): effects go before it. */
        bool isFaderStage (te::Plugin& plugin)
        {
            return dynamic_cast<te::VolumeAndPanPlugin*> (&plugin) != nullptr
                || dynamic_cast<te::LevelMeterPlugin*> (&plugin) != nullptr
                || dynamic_cast<te::VCAPlugin*> (&plugin) != nullptr
                || dynamic_cast<te::AuxSendPlugin*> (&plugin) != nullptr;
        }

        /** Never an insert effect: the fader end, and a return track's input (at the start). */
        bool isMixerStage (te::Plugin& plugin)
        {
            return isFaderStage (plugin) || dynamic_cast<te::AuxReturnPlugin*> (&plugin) != nullptr;
        }

        bool isEffect (te::Plugin& plugin)
        {
            return ! plugin.isSynth() && ! isMixerStage (plugin);
        }

        /** Index in the whole list for an effect that should end up at `position` among the effects. */
        int getInsertIndex (te::PluginList& chain, int position)
        {
            const auto effects = get (chain);

            if (position >= 0 && position < effects.size())
                return chain.indexOf (effects[position]);

            if (! effects.isEmpty())
                return chain.indexOf (effects.getLast()) + 1;

            // The first effect: right before the fader (after the instrument or the return's input).
            for (int i = 0; i < chain.size(); ++i)
                if (isFaderStage (*chain[i]))
                    return i;

            return -1;
        }

        juce::UndoManager& getUndoManager (te::Plugin& plugin)
        {
            return plugin.edit.getUndoManager();
        }
    }

    juce::Array<EffectType> getBuiltIn()
    {
        juce::Array<EffectType> result;

        for (const auto& [name, type] : { std::pair<const char*, const char*> { "EQ", te::EqualiserPlugin::xmlTypeName },
                                          { "Compressor", te::CompressorPlugin::xmlTypeName },
                                          { "Reverb",     te::ReverbPlugin::xmlTypeName },
                                          { "Delay",      te::DelayPlugin::xmlTypeName },
                                          { "Chorus",     te::ChorusPlugin::xmlTypeName },
                                          { "Phaser",     te::PhaserPlugin::xmlTypeName },
                                          { "Filter",     te::LowPassPlugin::xmlTypeName } })
            result.add ({ name, type, {} });

        return result;
    }

    juce::Array<EffectType> getExternal (core::Session& session)
    {
        juce::Array<EffectType> result;

        for (const auto& description : session.getEngine().getPluginManager().knownPluginList.getTypes())
            if (! description.isInstrument)
                result.add ({ description.name, {}, description });

        std::sort (result.begin(), result.end(), [] (const auto& a, const auto& b)
        {
            return a.name.compareNatural (b.name) < 0;
        });

        return result;
    }

    juce::Array<te::Plugin*> get (te::PluginList& chain)
    {
        juce::Array<te::Plugin*> result;

        for (auto* plugin : chain)
            if (isEffect (*plugin))
                result.add (plugin);

        return result;
    }

    te::PluginList* getChain (te::Plugin& plugin)
    {
        return plugin.getOwnerList();
    }

    bool canAdd (te::PluginList& chain)
    {
        return chain.canInsertPlugin();
    }

    te::Plugin* add (te::PluginList& chain, const EffectType& type, juce::String& error, int position)
    {
        error = {};

        if (! canAdd (chain))
        {
            error = "No room for more effects here";
            return nullptr;
        }

        if (! type.isBuiltIn() && type.description.isInstrument)
        {
            error = type.name + " is an instrument, not an effect";
            return nullptr;
        }

        auto& edit = chain.getEdit();
        const auto plugin = type.isBuiltIn()
                              ? edit.getPluginCache().createNewPlugin (type.builtInType, {})
                              : edit.getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, type.description);

        if (plugin == nullptr)
        {
            error = type.name + " could not be created";
            return nullptr;
        }

        if (auto* external = dynamic_cast<te::ExternalPlugin*> (plugin.get()))
        {
            if (external->getAudioPluginInstance() == nullptr)
            {
                error = "The plugin failed to load: " + external->getLoadError();
                return nullptr;
            }
        }

        if (chain.getOwnerTrack() == nullptr && ! plugin->canBeAddedToMaster())
        {
            error = type.name + " can't be used on the master";
            return nullptr;
        }

        edit.getUndoManager().beginNewTransaction ("Add " + type.name);
        chain.insertPlugin (plugin, getInsertIndex (chain, position), nullptr);

        if (! chain.contains (plugin.get()))
        {
            error = type.name + " could not be added";
            return nullptr;
        }

        return plugin.get();
    }

    bool move (te::Plugin& plugin, int newPosition)
    {
        auto* chain = getChain (plugin);

        if (chain == nullptr)
            return false;

        const auto effects = get (*chain);
        const auto from = effects.indexOf (&plugin);

        if (from < 0)
            return false;

        newPosition = juce::jlimit (0, effects.size() - 1, newPosition);

        if (newPosition == from)
            return false;

        const te::Plugin::Ptr keepAlive (&plugin);
        getUndoManager (plugin).beginNewTransaction ("Move " + getDisplayName (plugin));

        // insertPlugin takes the plugin out first and then puts it where the target was found:
        // that's before the target when moving up and after it when moving down.
        chain->insertPlugin (keepAlive, chain->indexOf (effects[newPosition]), nullptr);
        return true;
    }

    void remove (te::Plugin& plugin)
    {
        const te::Plugin::Ptr keepAlive (&plugin);
        getUndoManager (plugin).beginNewTransaction ("Remove " + getDisplayName (plugin));
        plugin.deleteFromParent();
    }

    void setBypassed (te::Plugin& plugin, bool shouldBeBypassed)
    {
        if (isBypassed (plugin) == shouldBeBypassed)
            return;

        getUndoManager (plugin).beginNewTransaction ((shouldBeBypassed ? "Bypass " : "Enable ") + getDisplayName (plugin));
        plugin.setEnabled (! shouldBeBypassed);
    }

    bool isBypassed (const te::Plugin& plugin)
    {
        return ! plugin.isEnabled();
    }

    juce::String getDisplayName (te::Plugin& plugin)
    {
        if (auto* filter = dynamic_cast<te::LowPassPlugin*> (&plugin))
            return filter->isLowPass() ? "Low-pass" : "High-pass";

        if (dynamic_cast<te::EqualiserPlugin*> (&plugin) != nullptr)
            return "EQ";

        return plugin.getName();
    }

    void showEditor (te::Plugin& plugin)
    {
        plugin.showWindowExplicitly();
    }
}
