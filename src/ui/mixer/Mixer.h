#pragma once

#include "audio/Sends.h"
#include "ui/mixer/ChannelStrip.h"

namespace harmonia::ui::mixer
{
    /** The mixer: a strip per track, then the group tracks' and the return tracks' strips
        (scrolls sideways), and the master strip on the right. "+ Track" adds tracks, group tracks
        and return tracks (Reverb, Delay...). Follows the edit: tracks coming and going, effects,
        sends, volumes, undo. */
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
        int getNumGroupStrips() const noexcept          { return groupStrips.size(); }
        ChannelStrip* getGroupStrip (int index) const   { return groupStrips[index]; }
        int getNumReturnStrips() const noexcept         { return returnStrips.size(); }
        ChannelStrip* getReturnStrip (int index) const  { return returnStrips[index]; }

        /** Adds an empty group track (what "+ Track" offers). */
        te::AudioTrack* addGroupTrack();

        /** Adds a return track with that effect (what "+ Track" offers); problems go to onMessage. */
        te::AudioTrack* addReturnTrack (audio::sends::Preset);
        ChannelStrip& getMasterStrip() noexcept         { return master; }

        /** Something to tell the user (e.g. a plugin failed to load). */
        std::function<void (const juce::String& text, bool isProblem)> onMessage;

    private:
        core::Session& session;
        arrangement::ArrangementModel& model;
        juce::ValueTree editState;

        juce::Viewport viewport;
        juce::Component stripRow;
        juce::OwnedArray<ChannelStrip> strips, groupStrips, returnStrips;
        juce::TextButton addTrackButton { "+ Track" };
        ChannelStrip master;
        bool needsRefresh = false;

        void connect (ChannelStrip&);
        void layOutStrips();
        void showAddMenu();
        bool syncStrips (juce::OwnedArray<ChannelStrip>&, const juce::Array<te::AudioTrack*>&);

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
