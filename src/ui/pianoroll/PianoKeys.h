#pragma once

#include "ui/pianoroll/PianoRollModel.h"
#include "ui/pianoroll/View.h"

namespace harmonia::ui::pianoroll
{
    /** The keyboard left of the notes. Clicking a key plays it (further right = louder);
        the middle button zooms vertically; the wheel scrolls (Ctrl+wheel zooms). With drum
        names on, each key says which drum it plays. Right button: its menu (drum names, the
        key's sample). On a track that plays Harmonia's drum machine, a sound file dropped on a
        key (from the file explorer or the sound browser) becomes that key's sound. */
    class PianoKeys final : public juce::Component,
                            public juce::FileDragAndDropTarget,
                            public juce::DragAndDropTarget
    {
    public:
        PianoKeys (PianoRollModel&, View&);

        void paint (juce::Graphics&) override;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        std::function<void()> onViewChanged;

        /** Something to tell the user (a sample put on a key, or why it couldn't be). */
        std::function<void (const juce::String&)> onMessage;

        /** The right button's menu; for a key (`pitch` 0..127) of the drum machine, with its sample. */
        juce::PopupMenu createMenu (int pitch = -1);

        /** A sound file dropped at this height: the key there plays it. */
        void dropFile (const juce::File&, float y);

        /** The key a dragged sound is over (-1: none). */
        int getDropPitch() const noexcept           { return dropPitch; }

        bool isInterestedInFileDrag (const juce::StringArray&) override;
        void fileDragEnter (const juce::StringArray&, int x, int y) override;
        void fileDragMove (const juce::StringArray&, int x, int y) override;
        void fileDragExit (const juce::StringArray&) override;
        void filesDropped (const juce::StringArray&, int x, int y) override;

        bool isInterestedInDragSource (const SourceDetails&) override;
        void itemDragEnter (const SourceDetails&) override;
        void itemDragMove (const SourceDetails&) override;
        void itemDragExit (const SourceDetails&) override;
        void itemDropped (const SourceDetails&) override;

    private:
        PianoRollModel& model;
        View& view;
        int pressedPitch = -1, dropPitch = -1;
        float lastZoomY = 0.0f;
        std::unique_ptr<juce::FileChooser> chooser;

        void setDropPitch (int);
        void chooseSample (int pitch);

        void press (const juce::MouseEvent&);
        void viewChanged();
    };
}
