#include "ui/ExportOptions.h"
#include "audio/Export.h"
#include "audio/Loudness.h"
#include "audio/Mp3Encoder.h"
#include "core/Tracks.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    using audio::ExportFormat;
    using audio::SongExport;

    namespace
    {
        constexpr int labelWidth = 92;

        juce::String describeTarget (const audio::LoudnessTarget& target)
        {
            return juce::String (target.lufs, 0) + " LUFS (" + target.usedBy + ")";
        }

        /** The item of the loudness list for `lufs`: its first is the song as it is. */
        int findLoudnessId (const std::optional<double>& lufs)
        {
            for (size_t i = 0; i < audio::loudnessTargets.size(); ++i)
                if (lufs == audio::loudnessTargets[i].lufs)
                    return (int) i + 2;

            return 1;
        }
    }

    ExportOptions::ExportOptions (const SongExport& initial, audio::ExportUse exported, const juce::Array<te::AudioTrack*>& leftOut,
                                  double rate)
        : use (exported), tracks (leftOut), deviceRate (rate), choices (ExportFormat::getChoices()), sampleRateBox (rate)
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

        loudnessBox.addItem ("As the song is", 1);

        for (size_t i = 0; i < audio::loudnessTargets.size(); ++i)
            loudnessBox.addItem (describeTarget (audio::loudnessTargets[i]), (int) i + 2);

        sampleRateBox.setRate (initial.sampleRate);
        loudnessBox.setSelectedId (findLoudnessId (initial.loudnessLufs), juce::dontSendNotification);

        for (auto* track : tracks)
            if (initial.without.contains (track))
                without.add (track);

        sampleRateLabel.setText ("Sample rate", juce::dontSendNotification);
        loudnessLabel.setText ("Loudness", juce::dontSendNotification);
        withoutLabel.setText ("Second file", juce::dontSendNotification);

        for (auto* label : { &sampleRateLabel, &loudnessLabel, &withoutLabel })
        {
            label->setFont (juce::FontOptions (theme::size::fontSize));
            label->setColour (juce::Label::textColourId, theme::colour::textPrimary);
        }

        withoutButton.setTooltip ("A second file next to the first: the song without the tracks you tick here (an instrumental, a minus one)");
        withoutButton.setEnabled (! tracks.isEmpty());
        withoutButton.onClick = [this]
        {
            createWithoutMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (withoutButton));
        };

        for (auto* box : std::initializer_list<juce::ComboBox*> { &formatBox, &sampleRateBox, &loudnessBox })
            box->onChange = [this] { updateHint(); };

        exportButton.onClick = [this] { if (onExport != nullptr) onExport (getOptions()); };
        cancelButton.onClick = [this] { if (onCancel != nullptr) onCancel(); };

        for (auto* c : std::initializer_list<juce::Component*> { &title, &hint, &formatBox, &exportButton, &cancelButton })
            addAndMakeVisible (c);

        // A loop is a sample: its format alone.
        if (isForSong())
            for (auto* c : std::initializer_list<juce::Component*> { &sampleRateLabel, &sampleRateBox, &loudnessLabel, &loudnessBox,
                                                                     &withoutLabel, &withoutButton })
                addAndMakeVisible (c);

        updateWithoutButton();
        setFormat (initial.format);
        setSize (isForSong() ? 460 : 440, isForSong() ? 330 : 170);
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

    SongExport ExportOptions::getOptions() const
    {
        SongExport how (getFormat());

        if (! isForSong())
            return how;

        how.sampleRate = sampleRateBox.getRate();

        if (const auto index = loudnessBox.getSelectedId() - 2; index >= 0 && index < (int) audio::loudnessTargets.size())
            how.loudnessLufs = audio::loudnessTargets[(size_t) index].lufs;

        how.without = without;
        return how;
    }

    void ExportOptions::setWithout (te::AudioTrack& track, bool leftOut)
    {
        // Kept top to bottom, as the song has them.
        juce::Array<te::AudioTrack*> now;

        for (auto* t : tracks)
            if (t == &track ? leftOut : without.contains (t))
                now.add (t);

        without = now;
        updateWithoutButton();
        updateHint();
    }

    juce::PopupMenu ExportOptions::createWithoutMenu()
    {
        juce::PopupMenu menu;
        const juce::Component::SafePointer<ExportOptions> safe (this);
        menu.addSectionHeader ("A second file: the song without");

        for (auto* track : tracks)
        {
            const auto isOut = without.contains (track);
            menu.addItem (core::tracks::isGroup (*track) ? track->getName() + "  (group)" : track->getName(), true, isOut,
                          [safe, track, isOut] { if (safe != nullptr) safe->setWithout (*track, ! isOut); });
        }

        menu.addSeparator();
        menu.addItem ("No second file", ! without.isEmpty(), false, [safe]
        {
            if (safe == nullptr)
                return;

            safe->without.clear();
            safe->updateWithoutButton();
            safe->updateHint();
        });

        return menu;
    }

    void ExportOptions::updateWithoutButton()
    {
        if (without.isEmpty())
            withoutButton.setButtonText ("None");
        else
            withoutButton.setButtonText ("The song without " + (without.size() == 1 ? without.getFirst()->getName()
                                                                                   : juce::String (without.size()) + " tracks"));
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

        if (isForSong())
        {
            const auto how = getOptions();

            if (const auto fileRate = audio::getFileSampleRate (how.format, how.sampleRate, deviceRate);
                how.sampleRate > 0.0 && fileRate != how.sampleRate)
                text << "\nAn MP3 can't have " << SampleRateBox::describe (how.sampleRate) << ": the file will be "
                     << SampleRateBox::describe (fileRate) << ".";

            if (how.loudnessLufs.has_value())
                text << "\nLoudness: the whole file is turned up or down to " << juce::String (*how.loudnessLufs, 0)
                     << " LUFS, as far as its peaks allow (they stay under " << juce::String (audio::loudness::peakCeilingDb, 0)
                     << " dB). The song itself stays as it is.";

            if (! how.without.isEmpty())
                text << "\nSecond file: next to the first, its name with \"" << audio::getMinusSuffix (how.without).trim() << "\" added"
                     << (how.loudnessLufs.has_value() ? ", turned up or down as the first is." : ".");
        }

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

        if (isForSong())
        {
            const std::pair<juce::Label*, juce::Component*> rows[] { { &sampleRateLabel, &sampleRateBox },
                                                                     { &loudnessLabel, &loudnessBox },
                                                                     { &withoutLabel, &withoutButton } };

            for (const auto& [label, control] : rows)
            {
                auto row = area.removeFromTop (theme::size::buttonHeight);
                label->setBounds (row.removeFromLeft (labelWidth));
                control->setBounds (row);
                area.removeFromTop (theme::size::padding);
            }
        }

        auto buttons = area.removeFromBottom (theme::size::buttonHeight);
        cancelButton.setBounds (buttons.removeFromRight (90));
        buttons.removeFromRight (theme::size::padding);
        exportButton.setBounds (buttons.removeFromRight (100));

        area.removeFromBottom (theme::size::padding);
        hint.setBounds (area);
    }

    void ExportOptions::show (core::Session& session, te::Edit& edit, std::function<void (const SongExport&)> onChosen, audio::ExportUse use)
    {
        // The song: as its last export was. A loop: its own format, and nothing else to choose.
        auto options = use == audio::ExportUse::song
                           ? std::make_unique<ExportOptions> (audio::getLastSongExport (session), use, audio::getStemChoices (edit),
                                                              edit.engine.getDeviceManager().getSampleRate())
                           : std::make_unique<ExportOptions> (SongExport (audio::getLastExportFormat (session, use)), use);
        juce::Component::SafePointer<ExportOptions> safe (options.get());

        const auto close = [safe]
        {
            if (safe != nullptr)
                if (auto* window = safe->findParentComponentOfClass<juce::DialogWindow>())
                    window->exitModalState (0);
        };

        options->onCancel = close;
        options->onExport = [&session, close, onChosen, use] (const SongExport& how)
        {
            if (use == audio::ExportUse::song)
                audio::setLastSongExport (session, how);
            else
                audio::setLastExportFormat (session, how.format, use);

            close();

            // After the window has gone: the next step opens a file dialog.
            juce::MessageManager::callAsync ([onChosen, how] { if (onChosen != nullptr) onChosen (how); });
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
