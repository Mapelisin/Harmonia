#pragma once

#include "audio/Transport.h"
#include "ui/audioeditor/AudioEditorGestures.h"

namespace harmonia::ui::audioeditor
{
    /** The audio clip editor's picture of the clip's whole sound file: a ruler with the song's
        bars, the waveform (the part the clip plays bright, the hidden parts before and after
        dimmed), its start and end lines, fades, gain, and the playhead. Mouse input goes to
        AudioEditorGestures. */
    class WaveformView final : public juce::Component,
                               public juce::TooltipClient,
                               private juce::Timer
    {
    public:
        WaveformView (AudioEditorModel&, audio::Transport&);
        ~WaveformView() override;

        AudioEditorGestures& getGestures() noexcept     { return gestures; }
        View& getView() noexcept                        { return view; }

        static constexpr int rulerHeight = 20;

        /** The clip or its file may have changed: another clip is shown whole, an edited one
            keeps the view. */
        void clipChanged();

        /** Shows the part of the file the clip plays (with a little room either side). */
        void showPlayedPart();

        /** The waveform has been read (for tests: a snapshot taken before shows nothing). */
        bool isWaveformLoaded() const;

        /** What the editor shows instead of a waveform, if anything ("File missing: ..."), as of
            the last clipChanged. */
        const juce::String& getMessage() const noexcept     { return message; }

        /** Over the ruler, while the clip's beats can be moved: how. */
        juce::String getTooltip() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        std::function<void (double beat)> onPositionChosen;

    private:
        AudioEditorModel& model;
        audio::Transport& transport;
        View view;
        AudioEditorGestures gestures { model, view };

        te::Clip::Ptr shownClip;
        juce::File shownFile;
        juce::String message;
        std::unique_ptr<te::SmartThumbnail> thumbnail;

        juce::VBlankAttachment vblank { this, [this] { updatePlayhead(); } };
        std::optional<float> playheadX;

        void paintRuler (juce::Graphics&, juce::Rectangle<float>);
        void paintWave (juce::Graphics&, te::AudioClipBase&, juce::Rectangle<float> body);
        void paintEdges (juce::Graphics&, juce::Rectangle<float> body);
        void paintReadout (juce::Graphics&);
        void updatePlayhead();
        void updateThumbnail (te::AudioClipBase&);
        juce::String findMessage() const;
        void timerCallback() override;

        template <typename Callback>
        void forEachGridLine (Callback&&) const;

        AudioEditorGestures::Pointer toPointer (const juce::MouseEvent&) const;
    };
}
