#pragma once

#include "ui/AudioClipHandles.h"
#include "ui/audioeditor/AudioEditorModel.h"
#include "ui/audioeditor/AudioEditorView.h"

namespace harmonia::ui::audioeditor
{
    /** Mouse behaviour of the audio clip editor's waveform, separate from drawing so it can be
        tested with synthetic pointer events. Positions are in waveform pixels; the waveform is
        `body` (set by the component, below its ruler).

        The whole file is shown, the part the clip plays bright. Drag the start or end line:
        show more or less of the sound (the grid snaps it, Alt: free). The handles at the top:
        drag the left / right one across for a fade in / out, the middle one up and down for the
        clip's gain; double-click one to reset it. A click puts the playhead there. Wheel:
        scroll; Ctrl+wheel: zoom; middle button: drag the view.

        In the ruler above the waveform (`ruler`), while the clip follows the tempo: drag a line
        of the song's grid to put that beat over another moment of the sound; near a hit it goes
        onto the hit (Alt: anywhere). The lines either side stay. Double-click a line to let the
        beat go back between its neighbours. */
    class AudioEditorGestures
    {
    public:
        struct Pointer
        {
            juce::Point<float> position;
            juce::ModifierKeys mods;
            int clicks = 1;
        };

        AudioEditorGestures (AudioEditorModel&, View&);

        void setBody (juce::Rectangle<float> b)                 { body = b; }
        juce::Rectangle<float> getBody() const noexcept         { return body; }
        void setRuler (juce::Rectangle<float> r)                { ruler = r; }

        void down (const Pointer&);
        void drag (const Pointer&);
        void up (const Pointer&);
        void wheel (const Pointer&, float deltaX, float deltaY);

        bool isActive() const noexcept                          { return kind != Kind::none; }

        juce::MouseCursor getCursorFor (const Pointer&);

        /** The handles of the clip as drawn (none without a clip or when the played part is too
            narrow for them). */
        std::optional<audio_handles::Handles> getHandles() const;

        /** While something is dragged: what to show by the mouse ("Fade in 1.50 beats"). */
        std::optional<std::pair<juce::String, juce::Point<float>>> getReadout() const;

        /** A click chose a song position (beats): the playhead goes there. */
        std::function<void (double beat)> onPositionChosen;

        /** The view was zoomed or scrolled. */
        std::function<void()> onViewChanged;

        /** How near the start or end line the mouse grabs it. */
        static constexpr float edgeGrabPixels = 5.0f;

        /** How near a hit a dragged beat goes onto it. */
        static constexpr float hitSnapPixels = 8.0f;

        enum class Zone { none, startEdge, endEdge, fadeIn, fadeOut, gain, beat };
        Zone hitTest (juce::Point<float>) const;

        /** The line of the song's grid in the ruler under the mouse (its beat), if the beats of
            this clip can be moved. */
        std::optional<double> getBeatLineAt (juce::Point<float>) const;

        /** The beat being dragged, if one is. */
        std::optional<double> getMovedBeat() const;

    private:
        enum class Kind { none, click, pan, trimStart, trimEnd, fadeIn, fadeOut, gain, beat };

        AudioEditorModel& model;
        View& view;
        juce::Rectangle<float> body, ruler;

        Kind kind = Kind::none;
        Pointer downPointer;
        juce::Point<float> lastPosition;
        bool dragged = false;
        float grabbedGainY = 0.0f;
        double grabbedBeat = 0.0, grabbedSpanBeats = 1.0;
        bool isOnHit = false;

        core::snap::Context getSnapContext() const;
        void dragTrim (const Pointer&);
        void dragHandle (const Pointer&);
        void dragBeat (const Pointer&);
    };
}
