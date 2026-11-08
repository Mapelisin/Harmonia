#pragma once

#include "ui/arrangement/ArrangementGestures.h"
#include <map>

namespace harmonia::ui::arrangement
{
    /** The tracks' rows with their clips (a small picture of each MIDI clip's notes, the
        waveform of each audio clip), the grid, the loop region and the playhead. Mouse input goes
        to ArrangementGestures. */
    class ClipArea final : public juce::Component
    {
    public:
        ClipArea (ArrangementModel&, ArrangementView&);

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        ArrangementGestures& getGestures() noexcept     { return gestures; }

        /** Moves the playhead line, repainting only the strips it leaves and enters. */
        void setPlayheadBeat (std::optional<double>);

        /** The view was zoomed or scrolled with the mouse. */
        std::function<void()> onViewChanged;

        /** Where dragged-in audio files would land: on the track's row, or (newTrack) on a new
            track below it (trackIndex -1: below all tracks). */
        struct DropMarker
        {
            int trackIndex = -1;
            double beat = 0.0;
            bool newTrack = false;

            bool operator== (const DropMarker& other) const
            {
                return trackIndex == other.trackIndex && beat == other.beat && newTrack == other.newTrack;
            }
        };

        void setDropMarker (std::optional<DropMarker>);

        /** A recording in progress: what has been recorded so far on these tracks. */
        struct RecordingMarker
        {
            double startBeat = 0.0, endBeat = 0.0;
            std::vector<int> trackIndices;

            bool operator== (const RecordingMarker& other) const
            {
                return startBeat == other.startBeat && endBeat == other.endBeat && trackIndices == other.trackIndices;
            }
        };

        void setRecording (std::optional<RecordingMarker>);
        std::optional<RecordingMarker> getRecording() const noexcept    { return recording; }
        std::optional<DropMarker> getDropMarker() const noexcept    { return dropMarker; }

        /** Every audio clip's waveform has been read (for tests and snapshots). */
        bool areWaveformsLoaded() const;

    private:
        ArrangementModel& model;
        ArrangementView& view;
        ArrangementGestures gestures { model, view };
        std::optional<double> playheadBeat;
        std::optional<DropMarker> dropMarker;
        std::optional<RecordingMarker> recording;

        /** Waveforms of the audio clips, by clip ID; read in the background. */
        struct Waveform
        {
            juce::File file;
            std::unique_ptr<te::SmartThumbnail> thumbnail;
        };

        std::map<juce::uint64, Waveform> waveforms;

        void paintGrid (juce::Graphics&, juce::Rectangle<int> clip);
        void paintClip (juce::Graphics&, const DrawnClip&);
        void paintNotes (juce::Graphics&, te::MidiClip&, juce::Rectangle<float> clip, juce::Rectangle<float> body, juce::Colour);
        void paintWaveform (juce::Graphics&, te::AudioClipBase&, juce::Rectangle<float> body, juce::Colour);
        void paintDropMarker (juce::Graphics&);
        void paintRecording (juce::Graphics&);
        te::SmartThumbnail& getWaveform (te::AudioClipBase&);
        void forgetRemovedWaveforms();
        void repaintPlayheadAt (std::optional<double> beat);

        static ArrangementGestures::Pointer toPointer (const juce::MouseEvent&);
    };
}
