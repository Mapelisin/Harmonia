#include "core/PluginState.h"

namespace harmonia::core::plugin_state
{
    void write (te::Plugin& plugin, juce::ValueTree& node)
    {
        juce::MemoryOutputStream stream;

        for (auto* parameter : plugin.getAutomatableParameters())
        {
            if (parameter->getCurrentValue() != parameter->getCurrentExplicitValue())
            {
                stream.writeString (parameter->paramID);
                stream.writeFloat (parameter->getCurrentExplicitValue());
            }
        }

        stream.flush();

        if (stream.getDataSize() > 0)
            node.setProperty (te::IDs::parameters, stream.getMemoryBlock(), nullptr);
        else
            node.removeProperty (te::IDs::parameters, nullptr);

        auto* external = dynamic_cast<te::ExternalPlugin*> (&plugin);
        auto* instance = external != nullptr ? external->getAudioPluginInstance() : nullptr;

        if (instance == nullptr)
            return;

        if (instance->getNumPrograms() > 0)
            node.setProperty (te::IDs::programNum, instance->getCurrentProgram(), nullptr);

        juce::MemoryBlock chunk;
        instance->suspendProcessing (true);
        instance->getStateInformation (chunk);
        instance->suspendProcessing (false);

        if (chunk.getSize() > 0)
            node.setProperty (te::IDs::state, chunk.toBase64Encoding(), nullptr);
        else
            node.removeProperty (te::IDs::state, nullptr);
    }

    juce::ValueTree snapshot (te::Plugin& plugin)
    {
        auto node = plugin.state.createCopy();
        write (plugin, node);
        return node;
    }
}
