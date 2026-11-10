#pragma once

#include "audio/Sampler.h"
#include "ui/DragNumber.h"
#include "ui/ParameterPanel.h"

namespace harmonia::ui
{
    /** The sampler's window: its knobs and presets (ParameterPanel) and, below, its sound: the
        file's name, the key that plays it as it is (the root key), and its picture.

        Drop a sound file anywhere on the window (from the sound browser or the file explorer)
        to have the sampler play it, or click the name or the picture to choose a file; the
        triangle plays the root key while it is held. Every change is one undo step. */
    class SamplerPanel final : public juce::Component,
                               public juce::FileDragAndDropTarget,
                               public juce::DragAndDropTarget,
                               private juce::Timer
    {
    public:
        explicit SamplerPanel (te::Plugin& sampler);
        ~SamplerPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseUp (const juce::MouseEvent&) override;

        /** A sound file dropped on the window: the sampler plays it, and it is heard for a
            moment. Says what happened in the message line (also when it couldn't be used). */
        void dropFile (const juce::File&);

        /** Plays the root key, or lets it go (the triangle, while it is pressed). */
        void setAuditioning (bool);
        bool isAuditioning() const noexcept         { return auditionNote >= 0; }

        /** What the sound's button says: "Choose a sound...", the file's name, "Missing: x.wav". */
        juce::String getSoundText() const           { return soundButton.getButtonText(); }

        /** About the sound, under its picture: "2.00 s, mono, 44.1 kHz" (none: empty). */
        juce::String getInfoText() const;

        DragNumber& getRootKey() noexcept           { return rootKey; }

        /** What the window last said (in the bar at the top, next to Presets). */
        juce::String getMessage() const             { return knobs.getMessage().getText(); }

        /** Re-reads the sound and the root key (the window does it a few times a second: undo,
            presets). */
        void refresh();

        ParameterPanel& getKnobs() noexcept         { return knobs; }

        bool isInterestedInFileDrag (const juce::StringArray&) override;
        void fileDragEnter (const juce::StringArray&, int, int) override;
        void fileDragExit (const juce::StringArray&) override;
        void filesDropped (const juce::StringArray&, int, int) override;

        bool isInterestedInDragSource (const SourceDetails&) override;
        void itemDragEnter (const SourceDetails&) override;
        void itemDragExit (const SourceDetails&) override;
        void itemDropped (const SourceDetails&) override;

        static constexpr int soundRowHeight = 34, pictureHeight = 150;

    private:
        te::Plugin& sampler;
        ParameterPanel knobs;
        juce::TextButton playButton, soundButton;
        juce::Label rootCaption { {}, "Root key" };
        DragNumber rootKey;
        juce::Rectangle<int> picture;

        // The picture: the loudest of the sound in each column, for the sound and width shown.
        audio::SampleData::Ptr drawn;
        std::vector<juce::Range<float>> columns;

        std::unique_ptr<juce::FileChooser> chooser;
        te::MPESourceID auditionSource = te::createUniqueMPESourceID();
        int auditionNote = -1;
        juce::uint32 auditionUntil = 0;         // let go by itself then (0: when the button is)
        bool dropHover = false;

        audio::SampleData::Ptr getSample() const;
        void updateColumns (int width);
        void chooseFile();
        void setHover (bool);
        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE (SamplerPanel)
    };
}
