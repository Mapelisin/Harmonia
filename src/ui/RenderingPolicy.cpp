#include "ui/RenderingPolicy.h"

namespace harmonia::ui
{
    void applyPreferredRenderer (juce::Component& topLevelWindow)
    {
       #if JUCE_WINDOWS
        if (auto* peer = topLevelWindow.getPeer())
        {
            const auto index = peer->getAvailableRenderingEngines().indexOf ("Software Renderer");

            if (index >= 0)
                peer->setCurrentRenderingEngine (index);
        }
       #else
        juce::ignoreUnused (topLevelWindow);
       #endif
    }

    juce::String getCurrentRendererName (juce::Component& topLevelWindow)
    {
        if (auto* peer = topLevelWindow.getPeer())
            return peer->getAvailableRenderingEngines()[peer->getCurrentRenderingEngine()];

        return {};
    }
}
