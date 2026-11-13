#pragma once

#include "audio/SongExport.h"
#include "ui/SampleRateBox.h"

namespace harmonia::ui
{
    /** How to export the song, or its loop region. For both: the file format, and a line on what
        each is for. For the song also its sample rate, a loudness to bring the file to, and a
        second file of the song without some tracks (an instrumental, a "minus one").
        "Export..." hands the choice on (the caller then asks where to save); it is remembered,
        for the song and for a loop apart. */
    class ExportOptions final : public juce::Component
    {
    public:
        /** `tracks`: what the second file can leave out (audio::getStemChoices); `deviceRate`:
            the sample rate the audio device plays at. Both are only for the song. */
        explicit ExportOptions (const audio::SongExport& initial, audio::ExportUse = audio::ExportUse::song,
                                const juce::Array<te::AudioTrack*>& tracks = {}, double deviceRate = 0.0);

        audio::ExportFormat getFormat() const;
        void setFormat (const audio::ExportFormat&);

        /** All that is chosen in the window now. */
        audio::SongExport getOptions() const;

        /** The tracks the second file leaves out, top to bottom; none: there is no second file. */
        const juce::Array<te::AudioTrack*>& getWithout() const noexcept     { return without; }
        void setWithout (te::AudioTrack&, bool leftOut);

        /** What a click on the second file's button opens: a tick for each track and group. */
        juce::PopupMenu createWithoutMenu();

        /** What the hint says about all that is chosen now. */
        juce::String getHint() const    { return hint.getText(); }
        juce::String getTitle() const   { return title.getText(); }

        juce::ComboBox& getFormatBox() noexcept         { return formatBox; }
        SampleRateBox& getSampleRateBox() noexcept      { return sampleRateBox; }
        juce::ComboBox& getLoudnessBox() noexcept       { return loudnessBox; }
        juce::TextButton& getWithoutButton() noexcept   { return withoutButton; }
        juce::TextButton& getExportButton() noexcept    { return exportButton; }

        std::function<void (const audio::SongExport&)> onExport;
        std::function<void()> onCancel;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Opens it in a window of its own, set as the last export was; `onChosen` gets the
            choice (now remembered) after Export... closed the window. */
        static void show (core::Session&, te::Edit&, std::function<void (const audio::SongExport&)> onChosen,
                          audio::ExportUse = audio::ExportUse::song);

        /** What each kind of file is for, in a line. */
        static juce::String describeUse (const audio::ExportFormat&, audio::ExportUse = audio::ExportUse::song);

    private:
        const audio::ExportUse use;
        const juce::Array<te::AudioTrack*> tracks;
        const double deviceRate;
        juce::Array<audio::ExportFormat> choices;
        juce::Array<te::AudioTrack*> without;
        juce::Label title, hint, sampleRateLabel, loudnessLabel, withoutLabel;
        juce::ComboBox formatBox, loudnessBox;
        SampleRateBox sampleRateBox;
        juce::TextButton withoutButton, exportButton { "Export..." }, cancelButton { "Cancel" };

        bool isForSong() const noexcept     { return use == audio::ExportUse::song; }
        void updateHint();
        void updateWithoutButton();

        JUCE_DECLARE_NON_COPYABLE (ExportOptions)
    };
}
