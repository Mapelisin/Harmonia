#pragma once

#include "audio/DrumKit.h"
#include "ui/ParameterPanel.h"

namespace harmonia::ui
{
    /** The drum machine's window: its knobs and presets (ParameterPanel) and, below, its drums,
        each with what it plays (the kit's own sound, or a sample of yours) and the sample's
        volume and tuning.

        Drop a sound file on a drum (from the sound browser or the file explorer) to have it play
        that; click its sound to choose a file; the cross puts the kit's own sound back; the
        triangle plays the drum. Every change is one undo step. */
    class DrumPadsPanel final : public juce::Component,
                                private juce::Timer
    {
    public:
        explicit DrumPadsPanel (te::Plugin& drumMachine);
        ~DrumPadsPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** A drum per key the kit has a sound for, and per other key that has a sample. */
        int getNumPads() const noexcept             { return pads.size(); }
        int getPadNote (int index) const;

        /** The index of a key's drum, or -1. */
        int indexOfNote (int note) const;

        /** What the drum plays, as its row says: "Built-in", the sample's name, "Missing: x.wav". */
        juce::String getPadSound (int index) const;
        juce::String getPadName (int index) const;

        juce::Slider* getVolumeSlider (int index) const;
        juce::Slider* getTuneSlider (int index) const;
        juce::Button* getClearButton (int index) const;

        /** A sound file dropped on a key's drum: the key plays it. Says what happened in the
            message line (also when it couldn't be used). */
        void dropFile (int note, const juce::File&);

        /** The key plays the kit's own sound again. */
        void clear (int note);

        /** Plays the key (as a click on its triangle does). */
        void audition (int note);

        juce::String getMessage() const             { return message.getText(); }

        /** Re-reads the drums (the window does it a few times a second: undo, presets). */
        void refresh();

        ParameterPanel& getKnobs() noexcept         { return knobs; }

        static constexpr int rowHeight = 26, headerHeight = 22;

    private:
        class Pad;

        te::Plugin& drums;
        ParameterPanel knobs;
        juce::Label header, message;
        juce::Viewport viewport;
        juce::Component content;
        juce::OwnedArray<Pad> pads;
        std::unique_ptr<juce::FileChooser> chooser;
        te::MPESourceID auditionSource = te::createUniqueMPESourceID();

        std::vector<int> getNotes() const;
        void chooseFile (int note);
        void tell (const juce::String&, bool isProblem);
        void timerCallback() override;

        JUCE_DECLARE_NON_COPYABLE (DrumPadsPanel)
    };
}
