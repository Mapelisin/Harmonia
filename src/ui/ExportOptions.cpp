#include "ui/ExportOptions.h"
#include "audio/Mp3Encoder.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    using audio::ExportFormat;

    ExportOptions::ExportOptions (const ExportFormat& initial, audio::ExportUse exported)
        : use (exported), choices (ExportFormat::getChoices())
    {
        title.setText (use == audio::ExportUse::loop ? "Export the loop region as" : "Export the song as", juce::dontSendNotification);
        title.setFont (juce::FontOptions (theme::size::fontSize, juce::Font::bold));
        title.setColour (juce::Label::textColourId, theme::colour::textPrimary);

        hint.setFont (juce::FontOptions (theme::size::smallFontSize));
        hint.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        hint.setJustificationType (juce::Justification::topLeft);
        hint.setMinimumHorizontalScale (1.0f);

        for (int i = 0; i < choices.size(); ++i)
        {
            // A line between the kinds of file.
            if (i > 0 && choices[i].type != choices[i - 1].type)
                formatBox.addSeparator();

            formatBox.addItem (choices[i].describe(), i + 1);
        }

        formatBox.onChange = [this] { updateHint(); };
        exportButton.onClick = [this] { if (onExport != nullptr) onExport (getFormat()); };
        cancelButton.onClick = [this] { if (onCancel != nullptr) onCancel(); };

        for (auto* c : std::initializer_list<juce::Component*> { &title, &hint, &formatBox, &exportButton, &cancelButton })
            addAndMakeVisible (c);

        setFormat (initial);
        setSize (440, 170);
    }

    ExportFormat ExportOptions::getFormat() const
    {
        return choices[juce::jmax (0, formatBox.getSelectedId() - 1)];
    }

    void ExportOptions::setFormat (const ExportFormat& format)
    {
        const auto index = choices.indexOf (format);
        formatBox.setSelectedId (juce::jmax (0, index) + 1, juce::dontSendNotification);
        updateHint();
    }

    juce::String ExportOptions::describeUse (const ExportFormat& format, audio::ExportUse use)
    {
        if (use == audio::ExportUse::loop)
        {
            // A loop is a sample for other songs: what matters is whether it repeats cleanly.
            switch (format.type)
            {
                case ExportFormat::Type::mp3:
                    return "To send or to listen to. An MP3 starts with a moment of silence, so it does not repeat "
                           "seamlessly: for a loop to use in a song choose WAV or FLAC.";

                case ExportFormat::Type::flac:
                    return "Exactly the loop, about half the size of a WAV. Not every sampler opens FLAC.";

                case ExportFormat::Type::wav:
                    break;
            }

            if (format.bitDepth == 24)
                return "Exactly the loop, at full quality: a sample that repeats seamlessly in any sampler or program.";
        }

        switch (format.type)
        {
            case ExportFormat::Type::flac:
                return "The same sound as WAV, about half the size. Most players, Bandcamp and SoundCloud take it.";

            case ExportFormat::Type::mp3:
                return "Small and plays everywhere: to send, or to listen to on a phone. Keep the peaks under -1 dB "
                       "(the limiter) so it doesn't crackle.";

            case ExportFormat::Type::wav:
                break;
        }

        if (format.bitDepth == 16)
            return "CD quality: what a CD or an old player needs (a little noise is added so quiet endings fade smoothly).";

        if (format.bitDepth == 32)
            return "Nothing is ever clipped, even over 0 dB: for a mastering engineer or more work in another program.";

        return "Full quality: for mastering, distributors (DistroKid...) and other programs.";
    }

    void ExportOptions::updateHint()
    {
        auto text = describeUse (getFormat(), use);

        if (! audio::mp3::isAvailable (320))
            text << "\nMP3: this computer has no MP3 encoder (Windows' Media Foundation is missing).";

        hint.setText (text, juce::dontSendNotification);
    }

    void ExportOptions::paint (juce::Graphics& g)
    {
        g.fillAll (theme::colour::background);
    }

    void ExportOptions::resized()
    {
        auto area = getLocalBounds().reduced (theme::size::padding * 2);
        title.setBounds (area.removeFromTop (theme::size::buttonHeight));
        formatBox.setBounds (area.removeFromTop (theme::size::buttonHeight));
        area.removeFromTop (theme::size::padding);

        auto buttons = area.removeFromBottom (theme::size::buttonHeight);
        cancelButton.setBounds (buttons.removeFromRight (90));
        buttons.removeFromRight (theme::size::padding);
        exportButton.setBounds (buttons.removeFromRight (100));

        area.removeFromBottom (theme::size::padding);
        hint.setBounds (area);
    }

    void ExportOptions::show (core::Session& session, std::function<void (const ExportFormat&)> onChosen, audio::ExportUse use)
    {
        auto options = std::make_unique<ExportOptions> (audio::getLastExportFormat (session, use), use);
        juce::Component::SafePointer<ExportOptions> safe (options.get());

        const auto close = [safe]
        {
            if (safe != nullptr)
                if (auto* window = safe->findParentComponentOfClass<juce::DialogWindow>())
                    window->exitModalState (0);
        };

        options->onCancel = close;
        options->onExport = [&session, close, onChosen, use] (const ExportFormat& format)
        {
            setLastExportFormat (session, format, use);
            close();

            // After the window has gone: the next step opens a file dialog.
            juce::MessageManager::callAsync ([onChosen, format] { if (onChosen != nullptr) onChosen (format); });
        };

        juce::DialogWindow::LaunchOptions launch;
        launch.dialogTitle = "Export";
        launch.dialogBackgroundColour = theme::colour::background;
        launch.useNativeTitleBar = true;
        launch.resizable = false;
        launch.content.setOwned (options.release());
        launch.launchAsync();
    }
}
