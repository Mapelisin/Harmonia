#pragma once

#include "audio/Export.h"
#include "ui/SampleRateBox.h"

namespace harmonia::ui
{
    /** Which stems to export and how: a tick for each track and group that can be one, what the
        files are called (a pattern, with an example of what it gives), their format and their
        sample rate. "Export..." hands the choice on (the caller then asks for the folder); the
        pattern, the format and the sample rate are remembered. */
    class StemsOptions final : public juce::Component
    {
    public:
        /** `choices`: what stems can be made of (audio::getStemChoices), all ticked but the
            groups. `songName` and `bpm` are for the example. `sampleRate` 0: as the audio device
            plays, which is at `deviceRate`. */
        StemsOptions (const juce::Array<te::AudioTrack*>& choices, const juce::String& songName, double bpm,
                      const juce::String& pattern, const audio::ExportFormat& format, double sampleRate = 0.0,
                      double deviceRate = 0.0);

        /** What is ticked and written in the window now. */
        audio::StemsOptions getOptions() const;

        int getNumChoices() const noexcept                      { return ticks.size(); }
        juce::ToggleButton& getTick (int index)                 { return *ticks[index]; }
        juce::TextEditor& getPatternEditor() noexcept           { return patternEditor; }
        juce::ComboBox& getFormatBox() noexcept                 { return formatBox; }
        SampleRateBox& getSampleRateBox() noexcept              { return rateBox; }
        juce::TextButton& getExportButton() noexcept            { return exportButton; }
        juce::TextButton& getAllButton() noexcept               { return allButton; }
        juce::TextButton& getNoneButton() noexcept              { return noneButton; }

        /** "My song - 01 Drums.wav": what the pattern gives for the first stem ticked. */
        juce::String getExample() const                         { return example.getText(); }

        std::function<void (const audio::StemsOptions&)> onExport;
        std::function<void()> onCancel;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Opens it in a window of its own for the song as it is; `onChosen` gets the choice
            after Export... closed the window. Says why not through `onNothing` if the song has
            nothing to make stems of. */
        static void show (core::Session&, te::Edit&, const juce::String& songName,
                          std::function<void (const audio::StemsOptions&)> onChosen,
                          std::function<void (const juce::String& reason)> onNothing);

    private:
        juce::Array<te::AudioTrack*> choices;
        juce::Array<audio::ExportFormat> formats;
        juce::String songName;
        double bpm = 120.0;

        juce::Label title, patternLabel, formatLabel, rateLabel, example, hint;
        juce::Viewport list;
        juce::Component rows;
        juce::OwnedArray<juce::ToggleButton> ticks;
        juce::TextEditor patternEditor;
        juce::ComboBox formatBox;
        SampleRateBox rateBox;
        juce::TextButton allButton { "All" }, noneButton { "None" }, exportButton { "Export..." }, cancelButton { "Cancel" };

        void setAll (bool ticked);
        void refresh();

        JUCE_DECLARE_NON_COPYABLE (StemsOptions)
    };
}
