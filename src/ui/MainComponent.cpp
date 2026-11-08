#include "ui/MainComponent.h"
#include "audio/DeviceStatus.h"
#include "audio/Instruments.h"
#include "core/Clips.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    namespace
    {
        constexpr int trackRowHeight = 72;
        constexpr int newClipBars = 4;
    }

    MainComponent::MainComponent (core::Session& s, core::Project& p)
        : session (s), project (p)
    {
        // Clicks on widgets that don't take focus land here, so Space and the editing
        // shortcuts keep working wherever the user clicked.
        setWantsKeyboardFocus (true);

        pianoRollModel.setAuditioner (&auditioner);
        pianoRollModel.addListener (this);
        project.getEdit().state.addListener (this);

        trackLabel.setText ("Track", juce::dontSendNotification);
        trackLabel.setFont (juce::FontOptions (theme::size::fontSize, juce::Font::bold));

        trackBox.setWantsKeyboardFocus (false);
        trackBox.onChange = [this]
        {
            const auto tracks = project.getTracks();
            const auto index = trackBox.getSelectedId() - 1;

            if (index >= 0 && index < tracks.size() && tracks[index] != currentTrack.get())
                selectTrack (tracks[index]);
        };

        addTrackButton.setWantsKeyboardFocus (false);
        addTrackButton.onClick = [this] { addTrack(); };

        audioSettingsButton.setWantsKeyboardFocus (false);
        audioSettingsButton.setEnabled (session.devicesEnabled());
        audioSettingsButton.onClick = [this] { showAudioSettings(); };

        for (auto* c : std::initializer_list<juce::Component*> { &transportBar, &audioSettingsButton, &trackLabel,
                                                                 &trackBox, &addTrackButton, &pianoRoll, &statusBar })
            addAndMakeVisible (c);

        selectTrack (project.getTracks().getFirst());
        setSize (1280, 800);

        // Keyboard focus to the piano roll once the window is on screen.
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (&pianoRoll)]
        {
            if (safe != nullptr && safe->isShowing())
                safe->grabKeyboardFocus();
        });
    }

    MainComponent::~MainComponent()
    {
        cancelPendingUpdate();
        project.getEdit().state.removeListener (this);
        pianoRollModel.removeListener (this);
        pianoRollModel.setAuditioner (nullptr);
        transport.stop();
        notePreview.setTrack (nullptr);
    }

    //==============================================================================
    void MainComponent::selectTrack (te::AudioTrack* track)
    {
        currentTrack = track;
        notePreview.setTrack (track);
        instrumentPanel.reset();

        te::MidiClip* clip = nullptr;

        if (track != nullptr)
        {
            instrumentPanel = std::make_unique<InstrumentPanel> (session, *track);
            addAndMakeVisible (*instrumentPanel);

            clip = core::clips::getMainClip (*track);

            if (clip == nullptr)
            {
                project.beginGesture ("Add clip");
                clip = core::clips::getOrAddMainClip (project, *track, newClipBars * core::clips::getBeatsPerBar (project.getEdit()));
            }
        }

        pianoRollModel.setClip (clip);
        pianoRoll.showClip();
        refreshTrackList();
        updateLoop();
        resized();
    }

    void MainComponent::addTrack()
    {
        // The new track's clip is as long as the current one, so loops line up.
        const auto length = std::max (newClipBars * core::clips::getBeatsPerBar (project.getEdit()),
                                      pianoRollModel.getClip() != nullptr ? pianoRollModel.getClipEndBeat() : 0.0);

        project.beginGesture ("Add track");
        auto* track = project.addTrack ("Track " + juce::String (project.getNumTracks() + 1));

        if (track == nullptr)
            return;

        audio::instruments::useBuiltInSynth (*track);
        core::clips::getOrAddMainClip (project, *track, length);
        selectTrack (track);
    }

    void MainComponent::refreshTrackList()
    {
        const auto tracks = project.getTracks();
        trackBox.clear (juce::dontSendNotification);

        for (int i = 0; i < tracks.size(); ++i)
            trackBox.addItem (tracks[i]->getName(), i + 1);

        trackBox.setSelectedId (tracks.indexOf (currentTrack.get()) + 1, juce::dontSendNotification);
        repaint();
    }

    void MainComponent::updateLoop()
    {
        // Play loops the edited clip, like a pattern.
        if (pianoRollModel.getClip() != nullptr)
            transport.setLoopBeats (pianoRollModel.getClipStartBeat(), pianoRollModel.getClipEndBeat());
        else
            transport.setLoopBeats (0.0, 0.0);
    }

    void MainComponent::pianoRollChanged()
    {
        if (pianoRollModel.getClip() == nullptr && currentTrack != nullptr)
            triggerAsyncUpdate();
        else
            updateLoop();
    }

    void MainComponent::handleAsyncUpdate()
    {
        // Tracks were added or removed (e.g. by undo/redo): keep the track list and the piano roll valid.
        const auto tracks = project.getTracks();

        if (currentTrack == nullptr || ! tracks.contains (currentTrack.get()))
            selectTrack (tracks.getFirst());
        else if (pianoRollModel.getClip() == nullptr)
            selectTrack (currentTrack.get());
        else
            refreshTrackList();
    }

    void MainComponent::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
    {
        if (child.hasType (te::IDs::TRACK))
            triggerAsyncUpdate();
    }

    void MainComponent::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
    {
        if (child.hasType (te::IDs::TRACK))
            triggerAsyncUpdate();
    }

    //==============================================================================
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
        g.drawText (getProjectSummary(), text.removeFromLeft (260), juce::Justification::centredLeft);
    }

    void MainComponent::resized()
    {
        auto area = getLocalBounds();
        auto header = area.removeFromTop (theme::size::headerHeight).reduced (theme::size::padding, 0);
        statusBar.setBounds (area.removeFromBottom (theme::size::statusBarHeight));

        audioSettingsButton.setBounds (header.removeFromRight (120).withSizeKeepingCentre (120, theme::size::buttonHeight));
        header.removeFromLeft (90 + 260);
        transportBar.setBounds (header.removeFromLeft (260));

        auto trackRow = area.removeFromTop (trackRowHeight);

        // Lined up with the first row of the instrument panel.
        auto trackControls = trackRow.removeFromLeft (320).withTrimmedLeft (theme::size::padding)
                                     .withTrimmedTop (4 + theme::size::padding).removeFromTop (theme::size::buttonHeight);
        trackLabel.setBounds (trackControls.removeFromLeft (50));
        trackBox.setBounds (trackControls.removeFromLeft (160));
        trackControls.removeFromLeft (theme::size::padding);
        addTrackButton.setBounds (trackControls.removeFromLeft (90));

        if (instrumentPanel != nullptr)
            instrumentPanel->setBounds (trackRow.reduced (theme::size::padding, 4));

        pianoRoll.setBounds (area);
    }

    bool MainComponent::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::spaceKey)
        {
            transport.togglePlayStop();
            return true;
        }

        // Editing shortcuts work even when the piano roll itself doesn't have focus.
        return pianoRoll.keyPressed (key);
    }

    void MainComponent::showWarning (const juce::String& text)
    {
        statusBar.setWarning (text);
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

        statusBar.setWarning ({});
    }
}
