#pragma once

#include "audio/Effects.h"
#include "ui/arrangement/ArrangementModel.h"
#include "ui/mixer/LevelMeter.h"

namespace harmonia::ui::mixer
{
    /** One mixer channel, a track's or the master's. Top to bottom: name; effect slots (click opens
        the effect, the light turns it off and on, right-click for its menu) and "+" to add one;
        pan; volume fader with the level meter; mute and solo.
        A click anywhere on a track's strip makes that track current. */
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
        te::PluginList& getChain() const;
        te::VolumeAndPanPlugin* getVolumePlugin() const;

        /** Re-reads the name, the effects and the values. */
        void refresh();

        /** Adds an effect after the others and opens its window; problems go to onMessage. */
        void addEffect (const audio::effects::EffectType&);

        int getNumSlots() const noexcept                { return slots.size(); }
        LevelMeter& getMeter() noexcept                 { return meter; }
        juce::Slider& getFader() noexcept               { return volume; }
        juce::Slider& getPanKnob() noexcept             { return pan; }

        /** Something to tell the user (e.g. a plugin failed to load). */
        std::function<void (const juce::String& text, bool isProblem)> onMessage;

        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr int width = 104;

    private:
        class Slot;
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
        juce::Slider pan, volume;
        juce::Label panText, volumeText;
        LevelMeter meter;
        juce::TextButton mute { "M" }, solo { "S" };

        void setUp();
        void refreshSlots();
        void refreshValues();
        void layOutSlots();
        void showAddMenu();
        void showSlotMenu (te::Plugin&);
        void tell (const juce::String&, bool isProblem);
        juce::Colour getColour() const;

        JUCE_DECLARE_NON_COPYABLE (ChannelStrip)
    };
}
