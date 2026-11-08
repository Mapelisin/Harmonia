#include "ui/MainComponent.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    MainComponent::MainComponent (core::Session& s, core::Project& p)
        : session (s), project (p)
    {
        addAndMakeVisible (audioSettingsButton);
        addAndMakeVisible (statusBar);

        audioSettingsButton.setEnabled (session.devicesEnabled());
        audioSettingsButton.onClick = [this] { showAudioSettings(); };

        setSize (1280, 800);
    }

    juce::String MainComponent::getProjectSummary() const
    {
        const auto tracks = project.getNumTracks();
        return "Untitled" + juce::String::fromUTF8 ("  \xc2\xb7  ")
             + juce::String (tracks) + (tracks == 1 ? " track" : " tracks") + juce::String::fromUTF8 ("  \xc2\xb7  ")
             + juce::String (project.getTempoBpm(), 0) + " BPM";
    }

    void MainComponent::paint (juce::Graphics& g)
    {
        g.fillAll (theme::colour::background);

        auto header = getLocalBounds().removeFromTop (theme::size::headerHeight);
        g.setColour (theme::colour::surface);
        g.fillRect (header);
        g.setColour (theme::colour::border);
        g.fillRect (header.removeFromBottom (1));

        auto text = header.reduced (theme::size::padding, 0);
        g.setColour (theme::colour::textPrimary);
        g.setFont (juce::FontOptions (theme::size::fontSize, juce::Font::bold));
        g.drawText ("Harmonia", text.removeFromLeft (90), juce::Justification::centredLeft);

        g.setColour (theme::colour::textSecondary);
        g.setFont (juce::FontOptions (theme::size::fontSize));
        g.drawText (getProjectSummary(), text, juce::Justification::centredLeft);
    }

    void MainComponent::resized()
    {
        auto area = getLocalBounds();
        auto header = area.removeFromTop (theme::size::headerHeight);
        statusBar.setBounds (area.removeFromBottom (theme::size::statusBarHeight));

        audioSettingsButton.setBounds (header.reduced (theme::size::padding, 0)
                                             .removeFromRight (120)
                                             .withSizeKeepingCentre (120, theme::size::buttonHeight));
    }

    void MainComponent::showAudioSettings()
    {
        juce::DialogWindow::LaunchOptions options;
        options.dialogTitle = "Audio settings";
        options.dialogBackgroundColour = theme::colour::background;
        options.useNativeTitleBar = true;
        options.resizable = false;

        auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (audio::getDeviceManager (session),
                                                                              0, 2, 1, 2, false, false, true, false);
        selector->setSize (500, 440);
        options.content.setOwned (selector.release());
        options.launchAsync();
    }
}
