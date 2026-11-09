#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>
#include <optional>

namespace harmonia::sandbox
{
    /** The plugin's own process: loads one plugin and runs it for Harmonia, which talks to it
        through a RemotePlugin. If the plugin crashes, only this process ends. */
    class Host
    {
    public:
        struct Options
        {
            juce::String pipeName, memoryName;
            uint32_t harmoniaProcessId = 0;

            /** The plugin formats it can load (VST3...). */
            std::function<void (juce::AudioPluginFormatManager&)> addFormats;

            /** Called (on the message thread) when it's time for the process to end: Harmonia
                is done with the plugin, or Harmonia itself ended. */
            std::function<void()> onFinished;
        };

        /** The options if this program was started as a plugin's process (empty otherwise). */
        static std::optional<Options> fromCommandLine (const juce::StringArray& arguments);

        explicit Host (Options);
        ~Host();

    private:
        class Impl;
        std::unique_ptr<Impl> impl;

        JUCE_DECLARE_NON_COPYABLE (Host)
    };
}
