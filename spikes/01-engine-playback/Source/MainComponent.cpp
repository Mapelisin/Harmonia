#include "MainComponent.h"
#include "AudioDevice.h"

namespace harmonia::spike
{
    namespace
    {
        constexpr int builtInSynthId = 1;
        constexpr int firstExternalId = 100;
    }

    MainComponent::MainComponent (te::Engine& e)
        : engine (e)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &playButton, &audioSettingsButton, &scanButton,
                                                                 &editorButton, &instrumentBox, &deviceLabel,
                                                                 &transportLabel, &statusLabel })
            addAndMakeVisible (c);

        playButton.onClick = [this]
        {
            if (session.isPlaying())
                session.stop();
            else
                session.play();
        };

        audioSettingsButton.onClick = [this] { showAudioSettings(); };
        scanButton.onClick = [this] { scanPlugins(); };
        editorButton.onClick = [this]
        {
            if (auto p = session.getInstrument())
                p->showWindowExplicitly();
        };
        instrumentBox.onChange = [this] { instrumentChosen(); };

        // Device choice is persisted by the engine; pick ASIO once in "Audio settings".
        setStatus ("Tip: choose ASIO in Audio settings for low latency");

        refreshInstrumentList();
        startTimerHz (10);
        setSize (760, 220);
    }

    MainComponent::~MainComponent()
    {
        stopTimer();
    }

    void MainComponent::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colour (0xff1e1f22));
    }

    void MainComponent::resized()
    {
        auto r = getLocalBounds().reduced (8);
        auto row = r.removeFromTop (28);

        playButton.setBounds (row.removeFromLeft (80));
        row.removeFromLeft (6);
        audioSettingsButton.setBounds (row.removeFromLeft (120));
        row.removeFromLeft (6);
        scanButton.setBounds (row.removeFromLeft (100));
        row.removeFromLeft (6);
        editorButton.setBounds (row.removeFromRight (130));
        row.removeFromRight (6);
        instrumentBox.setBounds (row);

        r.removeFromTop (8);
        deviceLabel.setBounds (r.removeFromTop (24));
        transportLabel.setBounds (r.removeFromTop (24));
        statusLabel.setBounds (r.removeFromTop (24));
    }

    void MainComponent::timerCallback()
    {
        playButton.setButtonText (session.isPlaying() ? "Stop" : "Play");
        deviceLabel.setText (audio_device::describeCurrentDevice (engine), juce::dontSendNotification);
        transportLabel.setText ("Position " + juce::String (session.getPositionSeconds(), 2) + " s"
                                    + " | CPU " + juce::String (engine.getDeviceManager().getCpuUsage() * 100.0f, 1) + " %"
                                    + " | instrument: " + session.getInstrumentName(),
                                juce::dontSendNotification);
    }

    void MainComponent::refreshInstrumentList()
    {
        instruments = scanner.getInstruments();
        instrumentBox.clear (juce::dontSendNotification);
        instrumentBox.addItem ("Built-in synth (4OSC)", builtInSynthId);

        for (int i = 0; i < instruments.size(); ++i)
            instrumentBox.addItem (instruments[i].name + " (" + instruments[i].manufacturerName + ")", firstExternalId + i);

        instrumentBox.setSelectedId (builtInSynthId, juce::dontSendNotification);
    }

    void MainComponent::instrumentChosen()
    {
        const auto id = instrumentBox.getSelectedId();
        juce::String error;

        if (id == builtInSynthId)
            error = session.useBuiltInSynth();
        else if (juce::isPositiveAndBelow (id - firstExternalId, instruments.size()))
            error = session.useExternalInstrument (instruments[id - firstExternalId]);

        setStatus (error.isEmpty() ? "Instrument: " + session.getInstrumentName() : error);
    }

    void MainComponent::showAudioSettings()
    {
        juce::DialogWindow::LaunchOptions o;
        o.dialogTitle = "Audio settings";
        o.dialogBackgroundColour = juce::Colour (0xff1e1f22);
        o.content.setOwned (new juce::AudioDeviceSelectorComponent (engine.getDeviceManager().deviceManager,
                                                                    0, 2, 1, 2, false, false, true, false));
        o.content->setSize (480, 420);
        o.launchAsync();
    }

    void MainComponent::scanPlugins()
    {
        setStatus ("Scanning VST3 plugins...");
        const auto count = scanner.scanDefaultVst3Folders ({});
        refreshInstrumentList();
        setStatus ("Scan finished: " + juce::String (count) + " plugin(s), "
                   + juce::String (instruments.size()) + " instrument(s)");
    }

    void MainComponent::setStatus (const juce::String& text)
    {
        statusLabel.setText (text, juce::dontSendNotification);
    }
}
