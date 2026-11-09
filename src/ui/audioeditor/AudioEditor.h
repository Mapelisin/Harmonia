#pragma once

#include "ui/DragNumber.h"
#include "ui/audioeditor/WaveformView.h"

namespace harmonia::ui::audioeditor
{
    /** The lower panel of an audio track: the clicked audio clip (or the track's first) with its
        whole sound file, and a bar of what can be done to it: gain (and normalize), pitch,
        following the song's tempo (and the loop's own tempo), the fades' shape, reverse, and a
        button that shows the played part again. Every change is one undo step. */
    class AudioEditor final : public juce::Component,
                              private arrangement::ArrangementModel::Listener
    {
    public:
        AudioEditor (arrangement::ArrangementModel&, audio::Transport&);
        ~AudioEditor() override;

        AudioEditorModel& getModel() noexcept               { return model; }
        WaveformView& getWaveform() noexcept                { return waveform; }

        /** Shows the clip as it is now (another clip, or an edit from elsewhere). */
        void refresh();

        /** A click on the waveform or its ruler chose a song position (beats). */
        std::function<void (double beat)> onPositionChosen;

        /** Something to tell in the status bar (true: a problem). */
        std::function<void (const juce::String&, bool)> onMessage;

        //==============================================================================
        // The controls (public for tests).

        DragNumber& getGainNumber() noexcept                { return gain; }
        DragNumber& getPitchNumber() noexcept               { return pitch; }
        DragNumber& getLoopBpmNumber() noexcept             { return loopBpm; }
        juce::TextButton& getNormalizeButton() noexcept     { return normalize; }
        juce::TextButton& getFollowButton() noexcept        { return follow; }
        juce::TextButton& getHalveButton() noexcept         { return halve; }
        juce::TextButton& getDoubleButton() noexcept        { return twice; }
        juce::TextButton& getReverseButton() noexcept       { return reverse; }
        juce::TextButton& getShowClipButton() noexcept      { return showClip; }
        juce::ComboBox& getFadeShapeBox() noexcept          { return fadeShape; }
        juce::Label& getFileLabel() noexcept                { return fileInfo; }

        static constexpr int toolbarHeight = 32;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        arrangement::ArrangementModel& arrangement;
        AudioEditorModel model { arrangement };
        WaveformView waveform;

        juce::Label gainLabel { {}, "Gain" }, pitchLabel { {}, "Pitch" }, tempoLabel { {}, "Loop" }, fadeLabel { {}, "Fades" };
        DragNumber gain, pitch, loopBpm;
        juce::TextButton normalize { "Normalize" }, follow { "Follow tempo" }, halve { juce::String::fromUTF8 ("\xc3\xb7" "2") },
                         twice { juce::String::fromUTF8 ("\xc3\x97" "2") }, reverse { "Reverse" }, showClip { "Show clip" };
        juce::ComboBox fadeShape;
        juce::Label fileInfo;

        void arrangementChanged() override;
        void setUpNumbers();
        void setUpButtons();
        void updateControls();
    };
}
