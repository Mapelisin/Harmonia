#pragma once

#include "EngineSession.h"
#include "PluginScanner.h"

namespace harmonia::spike
{
    /** Minimal test UI: play/stop, audio settings, VST3 scan, instrument choice. */
    class MainComponent : public juce::Component,
                          private juce::Timer
    {
    public:
        explicit MainComponent (te::Engine&);
        ~MainComponent() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        te::Engine& engine;
        EngineSession session { engine };
        PluginScanner scanner { engine };
        juce::Array<juce::PluginDescription> instruments;

        juce::TextButton playButton { "Play" };
        juce::TextButton audioSettingsButton { "Audio settings" };
        juce::TextButton scanButton { "Scan VST3" };
        juce::TextButton editorButton { "Open instrument" };
        juce::ComboBox instrumentBox;
        juce::Label deviceLabel, transportLabel, statusLabel;

        void timerCallback() override;
        void refreshInstrumentList();
        void instrumentChosen();
        void showAudioSettings();
        void scanPlugins();
        void setStatus (const juce::String&);
    };
}
