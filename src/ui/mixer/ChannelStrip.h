#pragma once

#include "audio/Effects.h"
#include "ui/arrangement/ArrangementModel.h"
#include "ui/mixer/LevelMeter.h"

namespace harmonia::ui::mixer
{
    /** One mixer channel: a track's, a group track's, a return track's or the master's. Top to
        bottom: name; effect slots (click opens the effect, the light turns it off and on,
        right-click for its menu: also what it listens to, the sidechain) and "+" to add one; on
        song and group tracks a send bar per return track ("how much goes to the Reverb"); pan;
        volume fader with the level meter; mute and solo; on a song track, where it plays (the
        master or a group).
        A click anywhere on a song track's strip makes that track current; a right-click on a group
        or return track's strip opens its menu (rename, delete). */
    class ChannelStrip final : public juce::Component
    {
    public:
        /** A track's strip. */
        ChannelStrip (core::Session&, arrangement::ArrangementModel&, te::AudioTrack&);

        /** The master strip. */
        ChannelStrip (core::Session&, arrangement::ArrangementModel&);

        ~ChannelStrip() override;

        te::AudioTrack* getTrack() const noexcept       { return track.get(); }
        bool isMaster() const noexcept                  { return track == nullptr; }
        bool isReturn() const;
        bool isGroup() const;
        te::PluginList& getChain() const;
        te::VolumeAndPanPlugin* getVolumePlugin() const;

        /** Re-reads the name, the effects and the values. */
        void refresh();

        /** Adds an effect after the others and opens its window; problems go to onMessage. */
        void addEffect (const audio::effects::EffectType&);

        int getNumSlots() const noexcept                { return slots.size(); }

        /** The send bars (one per return track, on song tracks only). */
        int getNumSends() const noexcept                { return sends.size(); }
        juce::Slider* getSendSlider (int index) const;
        te::AudioTrack* getSendTarget (int index) const;
        LevelMeter& getMeter() noexcept                 { return meter; }
        juce::Slider& getFader() noexcept               { return volume; }
        juce::Slider& getPanKnob() noexcept             { return pan; }

        /** Where a song track plays ("-> Master", "-> Drums"); hidden on the other strips. */
        juce::TextButton& getOutputButton() noexcept    { return output; }

        /** What a right-click on an effect's slot offers: open, bypass, move, what it listens to
            (sidechain), remove. */
        juce::PopupMenu makeSlotMenu (te::Plugin&);

        /** Starts a crashed plugin again, and says how that went. */
        void reload (te::Plugin&);

        /** Something to tell the user (e.g. a plugin failed to load). */
        std::function<void (const juce::String& text, bool isProblem)> onMessage;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;

        static constexpr int width = 104;

    private:
        class Slot;
        class Send;
        struct ClickWatcher final : juce::MouseListener
        {
            explicit ClickWatcher (ChannelStrip& o) : owner (o) {}
            void mouseDown (const juce::MouseEvent&) override;
            ChannelStrip& owner;
        };

        core::Session& session;
        arrangement::ArrangementModel& model;
        juce::ReferenceCountedObjectPtr<te::AudioTrack> track;   // kept alive while the strip shows it
        ClickWatcher clickWatcher { *this };

        juce::Label name;
        juce::Viewport slotView;
        juce::Component slotList;
        juce::OwnedArray<Slot> slots;
        juce::TextButton addButton { "+" };
        juce::Viewport sendView;
        juce::Component sendList;
        juce::OwnedArray<Send> sends;
        int numSendRows = 0;   // the same on every strip, so the faders line up
        juce::Slider pan, volume;
        juce::Label panText, volumeText;
        LevelMeter meter;
        juce::TextButton mute { "M" }, solo { "S" };
        juce::TextButton output;

        void setUp();
        void refreshSlots();
        void refreshSends();
        void refreshValues();
        void layOutSlots();
        void layOutSends();
        bool isBus() const;
        void showBusMenu();
        void askToRenameBus();
        void refreshOutput();
        void showAddMenu();
        void tell (const juce::String&, bool isProblem);
        juce::Colour getColour() const;

        JUCE_DECLARE_NON_COPYABLE (ChannelStrip)
    };
}
