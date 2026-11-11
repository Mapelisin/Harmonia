#pragma once

#include "audio/ExportFormat.h"
#include "core/Session.h"

namespace harmonia::ui
{
    /** How to export the song, or its loop region: the file format, and a line on what each is
        for. "Export..." hands the format on (the caller then asks where to save); the choice is
        remembered, for the song and for a loop apart. */
    class ExportOptions final : public juce::Component
    {
    public:
        explicit ExportOptions (const audio::ExportFormat& initial, audio::ExportUse = audio::ExportUse::song);

        audio::ExportFormat getFormat() const;
        void setFormat (const audio::ExportFormat&);

        /** What the hint line says about the format chosen now. */
        juce::String getHint() const    { return hint.getText(); }
        juce::String getTitle() const   { return title.getText(); }

        juce::ComboBox& getFormatBox() noexcept         { return formatBox; }
        juce::TextButton& getExportButton() noexcept    { return exportButton; }

        std::function<void (const audio::ExportFormat&)> onExport;
        std::function<void()> onCancel;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Opens it in a window of its own, set to the format used last time; `onChosen` gets the
            format (now remembered) after Export... closed the window. */
        static void show (core::Session&, std::function<void (const audio::ExportFormat&)> onChosen,
                          audio::ExportUse = audio::ExportUse::song);

        /** What each kind of file is for, in a line. */
        static juce::String describeUse (const audio::ExportFormat&, audio::ExportUse = audio::ExportUse::song);

    private:
        const audio::ExportUse use;
        juce::Array<audio::ExportFormat> choices;
        juce::Label title, hint;
        juce::ComboBox formatBox;
        juce::TextButton exportButton { "Export..." }, cancelButton { "Cancel" };

        void updateHint();

        JUCE_DECLARE_NON_COPYABLE (ExportOptions)
    };
}
