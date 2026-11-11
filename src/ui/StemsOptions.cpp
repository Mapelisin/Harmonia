#include "ui/StemsOptions.h"
#include "core/Tracks.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    using audio::ExportFormat;

    namespace
    {
        constexpr int rowHeight = 24, mostRowsShown = 10;
    }

    StemsOptions::StemsOptions (const juce::Array<te::AudioTrack*>& c, const juce::String& song, double tempo,
                                const juce::String& pattern, const ExportFormat& format)
        : choices (c), formats (ExportFormat::getChoices()), songName (song), bpm (tempo)
    {
        title.setText ("Export stems: a file for each track ticked", juce::dontSendNotification);
        title.setFont (juce::FontOptions (theme::size::fontSize, juce::Font::bold));
        title.setColour (juce::Label::textColourId, theme::colour::textPrimary);

        for (auto* label : { &patternLabel, &formatLabel, &example, &hint })
        {
            label->setFont (juce::FontOptions (theme::size::smallFontSize));
            label->setColour (juce::Label::textColourId, theme::colour::textSecondary);
            label->setMinimumHorizontalScale (1.0f);
        }

        patternLabel.setText ("File names", juce::dontSendNotification);
        formatLabel.setText ("Format", juce::dontSendNotification);
        hint.setText ("{song}  {n}  {track}  {bpm}  {date} are filled in.\nA group's stem is all its tracks in one file.",
                      juce::dontSendNotification);
        hint.setJustificationType (juce::Justification::topLeft);

        // Every song track ticked; the groups are there to be ticked.
        for (auto* track : choices)
        {
            const auto isGroup = core::tracks::isGroup (*track);
            auto* tick = ticks.add (new juce::ToggleButton (isGroup ? track->getName() + "  (group)" : track->getName()));
            tick->setToggleState (! isGroup, juce::dontSendNotification);
            tick->onClick = [this] { refresh(); };
            rows.addAndMakeVisible (tick);
        }

        list.setViewedComponent (&rows, false);
        list.setScrollBarsShown (true, false);

        patternEditor.setText (pattern.trim().isNotEmpty() ? pattern.trim() : juce::String (audio::defaultStemPattern), false);
        patternEditor.setTextToShowWhenEmpty (audio::defaultStemPattern, theme::colour::textSecondary);
        patternEditor.onTextChange = [this] { refresh(); };

        for (int i = 0; i < formats.size(); ++i)
        {
            if (i > 0 && formats[i].type != formats[i - 1].type)
                formatBox.addSeparator();

            formatBox.addItem (formats[i].describe(), i + 1);
        }

        formatBox.setSelectedId (juce::jmax (0, formats.indexOf (format)) + 1, juce::dontSendNotification);
        formatBox.onChange = [this] { refresh(); };

        allButton.onClick = [this] { setAll (true); };
        noneButton.onClick = [this] { setAll (false); };
        exportButton.onClick = [this] { if (onExport != nullptr) onExport (getOptions()); };
        cancelButton.onClick = [this] { if (onCancel != nullptr) onCancel(); };

        for (auto* component : std::initializer_list<juce::Component*> { &title, &list, &allButton, &noneButton, &patternLabel, &patternEditor,
                                                                          &example, &hint, &formatLabel, &formatBox, &exportButton, &cancelButton })
            addAndMakeVisible (component);

        refresh();
        setSize (480, 232 + rowHeight * juce::jlimit (2, mostRowsShown, choices.size()));
    }

    audio::StemsOptions StemsOptions::getOptions() const
    {
        audio::StemsOptions options;

        for (int i = 0; i < ticks.size(); ++i)
            if (ticks[i]->getToggleState())
                options.tracks.add (choices[i]);

        options.songName = songName;
        options.namePattern = patternEditor.getText().trim();
        options.format = formats[juce::jmax (0, formatBox.getSelectedId() - 1)];
        return options;
    }

    void StemsOptions::setAll (bool ticked)
    {
        for (auto* tick : ticks)
            tick->setToggleState (ticked, juce::dontSendNotification);

        refresh();
    }

    void StemsOptions::refresh()
    {
        const auto options = getOptions();
        exportButton.setEnabled (! options.tracks.isEmpty());

        // What the first stem ticked will be called (its number as the export gives it: its place
        // among the tracks ticked is near enough for an example).
        const auto firstTicked = options.tracks.isEmpty() ? 0 : choices.indexOf (options.tracks.getFirst());
        const auto trackName = choices.isEmpty() ? juce::String ("Track") : choices[firstTicked]->getName();
        example.setText (options.tracks.isEmpty() ? juce::String ("Tick at least one track")
                                                  : "For example: " + audio::getStemFileName (options.namePattern, songName, firstTicked + 1,
                                                                                               trackName, bpm, options.format),
                         juce::dontSendNotification);
    }

    void StemsOptions::paint (juce::Graphics& g)
    {
        g.fillAll (theme::colour::background);
    }

    void StemsOptions::resized()
    {
        auto area = getLocalBounds().reduced (theme::size::padding * 2);
        title.setBounds (area.removeFromTop (theme::size::buttonHeight));

        auto buttons = area.removeFromBottom (theme::size::buttonHeight);
        cancelButton.setBounds (buttons.removeFromRight (90));
        buttons.removeFromRight (theme::size::padding);
        exportButton.setBounds (buttons.removeFromRight (100));
        area.removeFromBottom (theme::size::padding);

        auto formatRow = area.removeFromBottom (theme::size::buttonHeight);
        formatLabel.setBounds (formatRow.removeFromLeft (80));
        formatBox.setBounds (formatRow.removeFromLeft (220));
        area.removeFromBottom (theme::size::padding);

        hint.setBounds (area.removeFromBottom (34));
        example.setBounds (area.removeFromBottom (18));
        auto patternRow = area.removeFromBottom (theme::size::buttonHeight);
        patternLabel.setBounds (patternRow.removeFromLeft (80));
        patternEditor.setBounds (patternRow);
        area.removeFromBottom (theme::size::padding);

        auto pick = area.removeFromBottom (theme::size::buttonHeight);
        allButton.setBounds (pick.removeFromLeft (60));
        pick.removeFromLeft (theme::size::padding);
        noneButton.setBounds (pick.removeFromLeft (60));
        area.removeFromBottom (theme::size::padding);

        list.setBounds (area);
        rows.setSize (area.getWidth() - list.getScrollBarThickness(), rowHeight * ticks.size());

        for (int i = 0; i < ticks.size(); ++i)
            ticks[i]->setBounds (0, i * rowHeight, rows.getWidth(), rowHeight);
    }

    void StemsOptions::show (core::Session& session, te::Edit& edit, const juce::String& songName,
                             std::function<void (const audio::StemsOptions&)> onChosen,
                             std::function<void (const juce::String&)> onNothing)
    {
        const auto choices = audio::getStemChoices (edit);

        if (choices.isEmpty())
        {
            if (onNothing != nullptr)
                onNothing (edit.getLength() <= te::TimeDuration() ? "Nothing to export: the song has no clips"
                                                                  : "Nothing to export: no track that is heard has clips");
            return;
        }

        auto options = std::make_unique<StemsOptions> (choices, songName, edit.tempoSequence.getBpmAt (te::TimePosition()),
                                                       audio::getLastStemPattern (session), audio::getLastExportFormat (session));
        juce::Component::SafePointer<StemsOptions> safe (options.get());

        const auto close = [safe]
        {
            if (safe != nullptr)
                if (auto* window = safe->findParentComponentOfClass<juce::DialogWindow>())
                    window->exitModalState (0);
        };

        options->onCancel = close;
        options->onExport = [&session, close, onChosen] (const audio::StemsOptions& chosen)
        {
            audio::setLastStemPattern (session, chosen.namePattern);
            audio::setLastExportFormat (session, chosen.format);
            close();

            // After the window has gone: the next step opens a folder dialog.
            juce::MessageManager::callAsync ([onChosen, chosen] { if (onChosen != nullptr) onChosen (chosen); });
        };

        juce::DialogWindow::LaunchOptions launch;
        launch.dialogTitle = "Export stems";
        launch.dialogBackgroundColour = theme::colour::background;
        launch.useNativeTitleBar = true;
        launch.resizable = false;
        launch.content.setOwned (options.release());
        launch.launchAsync();
    }
}
