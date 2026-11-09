#pragma once

#include "audio/PluginSafety.h"
#include "audio/PluginScanner.h"

namespace harmonia::ui
{
    /** Chooses the instrument of one track: built-in synth or a scanned plugin,
        opens its editor and scans for new plugins (the folders: Settings > Plugins). */
    class InstrumentPanel final : public juce::Component,
                                  private juce::ValueTree::Listener,
                                  private juce::ChangeListener,
                                  private juce::AsyncUpdater
    {
    public:
        InstrumentPanel (core::Session&, te::AudioTrack&);
        ~InstrumentPanel() override;

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
        juce::TextButton foldersButton { "Folders..." };
        juce::Label messageLabel;

        // The instrument's process crashed or came back: the editor button becomes "Reload" and back.
        audio::plugin_safety::Watcher crashWatcher;

        void refreshList();
        void showCurrent();
        void editorClicked();
        void instrumentChosen();
        void startScan();
        void showMessage (const juce::String&, bool isProblem);

        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
        void handleAsyncUpdate() override;

        // The plugin list changed (a scan here or in the settings).
        void changeListenerCallback (juce::ChangeBroadcaster*) override;

        JUCE_DECLARE_NON_COPYABLE (InstrumentPanel)
    };
}
