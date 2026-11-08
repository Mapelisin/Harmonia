#pragma once

#include "audio/PluginScanner.h"

namespace harmonia::ui
{
    /** Chooses the instrument of one track: built-in synth or a scanned plugin,
        opens its editor and scans for new plugins. */
    class InstrumentPanel final : public juce::Component
    {
    public:
        InstrumentPanel (core::Session&, te::AudioTrack&);

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        core::Session& session;
        te::AudioTrack& track;
        audio::PluginScanner scanner { session };

        juce::Array<juce::PluginDescription> available;

        juce::Label titleLabel;
        juce::ComboBox instrumentBox;
        juce::TextButton editorButton { "Show editor" };
        juce::TextButton scanButton { "Scan plugins" };
        juce::Label messageLabel;

        void refreshList();
        void instrumentChosen();
        void startScan();
        void showMessage (const juce::String&, bool isProblem);

        JUCE_DECLARE_NON_COPYABLE (InstrumentPanel)
    };
}
