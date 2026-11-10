#pragma once

#include "audio/AutomationWriter.h"
#include "audio/EffectChains.h"
#include "audio/Effects.h"
#include "audio/PluginCatalog.h"
#include "ui/ParameterMenu.h"
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

        /** As a choice in the plugin chooser or the "+" menu does: adds it, and it counts as used. */
        void addEffect (const audio::plugin_catalog::Entry&);

        /** The "+" menu: the search, the favourites, the effects used lately, Harmonia's own, all
            the plugins, the chains. */
        juce::PopupMenu makeAddMenu();

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

        /** What a right-click on an effect's slot offers: open, bypass, presets, move, what it
            listens to (sidechain), remove. */
        juce::PopupMenu makeSlotMenu (te::Plugin&);

        /** What "+" offers after the effects: the chains (save these effects, add or put in a
            saved chain). */
        juce::PopupMenu makeChainMenu();

        /** Where the chains are kept (the tests use a folder of their own). */
        juce::File chainsFolder = audio::chains::getDefaultFolder();

        /** Dragging an effect's slot: where among this strip's effects it would go if dropped at
            that point of the screen (0: before the first... the number of slots: after the last);
            -1 if the point isn't on this strip. */
        int getDropPosition (juce::Point<int> screenPosition) const;

        /** Shows where a dragged effect would go (-1: nowhere here). */
        void setDropMarker (int position);
        int getDropMarker() const noexcept              { return dropMarker; }

        /** An effect (of this strip or another) dropped at that place among this strip's effects:
            moved there, or copied. Says what happened; false if it couldn't. */
        bool dropEffect (te::Plugin&, int position, bool copy);

        /** A slot of this strip is being dragged (the mixer finds the strip under the mouse),
            and was dropped. */
        std::function<void (te::Plugin&, juce::Point<int> screenPosition)> onSlotDrag;
        std::function<void (te::Plugin&, juce::Point<int> screenPosition, bool copy)> onSlotDrop;

        void paintOverChildren (juce::Graphics&) override;

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
        int dropMarker = -1;
        MenuSlider pan, volume;
        audio::automation::Touch panTouch, volumeTouch;     // written while automation is written
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
        void showParameterMenu (te::AutomatableParameter*, juce::Component& target);
        juce::String withControl (const juce::String& tooltip, te::AutomatableParameter*) const;
        void tell (const juce::String&, bool isProblem);
        juce::Colour getColour() const;

        JUCE_DECLARE_NON_COPYABLE (ChannelStrip)
    };
}
