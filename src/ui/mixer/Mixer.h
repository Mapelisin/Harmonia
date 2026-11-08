#pragma once

#include "ui/mixer/ChannelStrip.h"

namespace harmonia::ui::mixer
{
    /** The mixer: a strip per track (scrolls sideways) and the master strip on the right.
        Follows the edit: tracks coming and going, effects, volumes, undo. */
    class Mixer final : public juce::Component,
                        private arrangement::ArrangementModel::Listener,
                        private juce::ValueTree::Listener,
                        private juce::AsyncUpdater
    {
    public:
        Mixer (core::Session&, arrangement::ArrangementModel&);
        ~Mixer() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        /** Re-reads the tracks (rebuilding strips if tracks came or went) and every strip. */
        void refresh();

        int getNumTrackStrips() const noexcept          { return strips.size(); }
        ChannelStrip* getTrackStrip (int index) const   { return strips[index]; }
        ChannelStrip& getMasterStrip() noexcept         { return master; }

        /** Something to tell the user (e.g. a plugin failed to load). */
        std::function<void (const juce::String& text, bool isProblem)> onMessage;

    private:
        core::Session& session;
        arrangement::ArrangementModel& model;
        juce::ValueTree editState;

        juce::Viewport viewport;
        juce::Component stripRow;
        juce::OwnedArray<ChannelStrip> strips;
        juce::TextButton addTrackButton { "+ Track" };
        ChannelStrip master;
        bool needsRefresh = false;

        void connect (ChannelStrip&);
        void layOutStrips();

        void arrangementChanged() override;
        void handleAsyncUpdate() override;
        void treeChanged (const juce::ValueTree&);

        void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
        void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;

        JUCE_DECLARE_NON_COPYABLE (Mixer)
    };
}
