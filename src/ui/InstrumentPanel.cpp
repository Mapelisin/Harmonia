#include "ui/InstrumentPanel.h"
#include "audio/Instruments.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    namespace
    {
        constexpr int builtInSynthId = 1;
        constexpr int firstPluginId = 100;
        const juce::String builtInSynthName = "4OSC (built-in)";
    }

    InstrumentPanel::InstrumentPanel (core::Session& s, te::AudioTrack& t)
        : session (s), track (t)
    {
        titleLabel.setText (track.getName() + "  instrument", juce::dontSendNotification);
        titleLabel.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        titleLabel.setFont (juce::FontOptions (theme::size::fontSize, juce::Font::bold));

        messageLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        messageLabel.setFont (juce::FontOptions (theme::size::smallFontSize));

        instrumentBox.onChange = [this] { instrumentChosen(); };
        editorButton.onClick = [this] { audio::instruments::showEditor (track); };
        scanButton.onClick = [this] { startScan(); };

        for (auto* c : std::initializer_list<juce::Component*> { &titleLabel, &instrumentBox, &editorButton,
                                                                 &scanButton, &messageLabel })
            addAndMakeVisible (c);

        refreshList();
    }

    void InstrumentPanel::paint (juce::Graphics& g)
    {
        g.setColour (theme::colour::surface);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), theme::size::cornerRadius);
        g.setColour (theme::colour::border);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), theme::size::cornerRadius, 1.0f);
    }

    void InstrumentPanel::resized()
    {
        auto area = getLocalBounds().reduced (theme::size::padding);
        auto row = area.removeFromTop (theme::size::buttonHeight);

        titleLabel.setBounds (row.removeFromLeft (180));
        instrumentBox.setBounds (row.removeFromLeft (320));
        row.removeFromLeft (theme::size::padding);
        editorButton.setBounds (row.removeFromLeft (110));
        row.removeFromLeft (theme::size::padding);
        scanButton.setBounds (row.removeFromLeft (110));

        area.removeFromTop (theme::size::padding / 2);
        messageLabel.setBounds (area.removeFromTop (theme::size::buttonHeight));
    }

    void InstrumentPanel::refreshList()
    {
        available = audio::instruments::getAvailable (session);

        instrumentBox.clear (juce::dontSendNotification);
        instrumentBox.addItem (builtInSynthName, builtInSynthId);

        if (! available.isEmpty())
            instrumentBox.addSeparator();

        for (int i = 0; i < available.size(); ++i)
            instrumentBox.addItem (available[i].name + "  (" + available[i].manufacturerName + ")", firstPluginId + i);

        // Show the current instrument.
        auto* current = audio::instruments::get (track);
        int selectedId = 0;

        if (dynamic_cast<te::FourOscPlugin*> (current) != nullptr)
            selectedId = builtInSynthId;
        else if (auto* external = dynamic_cast<te::ExternalPlugin*> (current))
            for (int i = 0; i < available.size(); ++i)
                if (external->desc.isDuplicateOf (available[i]))
                    selectedId = firstPluginId + i;

        instrumentBox.setSelectedId (selectedId, juce::dontSendNotification);
        editorButton.setEnabled (current != nullptr);

        if (available.isEmpty())
            showMessage ("No plugin instruments known yet. Press \"Scan plugins\" to look for VST3 instruments.", false);
    }

    void InstrumentPanel::instrumentChosen()
    {
        const auto id = instrumentBox.getSelectedId();
        juce::String error;
        juce::String name;

        if (id == builtInSynthId)
        {
            name = builtInSynthName;
            error = audio::instruments::useBuiltInSynth (track);
        }
        else if (id >= firstPluginId && id - firstPluginId < available.size())
        {
            const auto& description = available.getReference (id - firstPluginId);
            name = description.name;
            error = audio::instruments::useExternal (track, description);
        }
        else
        {
            return;
        }

        if (error.isNotEmpty())
            showMessage (error, true);
        else
            showMessage ("Instrument: " + name, false);

        refreshList();
    }

    void InstrumentPanel::startScan()
    {
        if (scanner.isScanning())
        {
            scanner.cancelScan();
            scanButton.setButtonText ("Scan plugins");
            showMessage ("Scan cancelled", false);
            return;
        }

        scanButton.setButtonText ("Stop scan");
        showMessage ("Scanning plugins...", false);

        audio::PluginScanner::Callbacks callbacks;

        callbacks.onProgress = [this] (const juce::String& file)
        {
            showMessage ("Scanning: " + juce::File (file).getFileName(), false);
        };

        callbacks.onFinished = [this] (int)
        {
            scanButton.setButtonText ("Scan plugins");
            refreshList();
            showMessage ("Scan finished: " + juce::String (available.size()) + " instrument(s) found", false);
        };

        scanner.startScan (std::move (callbacks));
    }

    void InstrumentPanel::showMessage (const juce::String& text, bool isProblem)
    {
        messageLabel.setColour (juce::Label::textColourId,
                                isProblem ? theme::colour::warning : theme::colour::textSecondary);
        messageLabel.setText (text, juce::dontSendNotification);
    }
}
