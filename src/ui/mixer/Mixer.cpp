#include "ui/mixer/Mixer.h"
#include "audio/Groups.h"
#include "ui/Theme.h"
#include "ui/arrangement/TrackHeaders.h"

namespace harmonia::ui::mixer
{
    namespace
    {
        constexpr int addTrackWidth = 70, masterGap = 6, busGap = 6;
    }

    Mixer::Mixer (core::Session& s, arrangement::ArrangementModel& m)
        : session (s), model (m), editState (m.getEdit().state), master (s, m)
    {
        setOpaque (true);

        viewport.setViewedComponent (&stripRow, false);
        viewport.setScrollBarsShown (false, true);
        viewport.setScrollBarThickness (10);
        viewport.setWantsKeyboardFocus (false);

        addTrackButton.setTooltip ("Add a track, a group track (several tracks through one fader) or a return track "
                                   "(one reverb or delay shared by several tracks)");
        addTrackButton.setWantsKeyboardFocus (false);
        addTrackButton.onClick = [this] { showAddMenu(); };
        stripRow.addAndMakeVisible (addTrackButton);

        connect (master);
        addAndMakeVisible (viewport);
        addAndMakeVisible (master);

        model.addListener (this);
        editState.addListener (this);
        refresh();
    }

    Mixer::~Mixer()
    {
        cancelPendingUpdate();
        editState.removeListener (this);
        model.removeListener (this);
    }

    void Mixer::connect (ChannelStrip& strip)
    {
        strip.onMessage = [this] (const juce::String& text, bool isProblem)
        {
            if (onMessage != nullptr)
                onMessage (text, isProblem);
        };
        strip.onSlotDrag = [this] (te::Plugin&, juce::Point<int> position) { slotDragged (position); };
        strip.onSlotDrop = [this] (te::Plugin& plugin, juce::Point<int> position, bool copy) { slotDropped (plugin, position, copy); };
    }

    std::vector<ChannelStrip*> Mixer::getAllStrips()
    {
        std::vector<ChannelStrip*> all;

        for (auto* list : { &strips, &groupStrips, &returnStrips })
            for (auto* strip : *list)
                all.push_back (strip);

        all.push_back (&master);
        return all;
    }

    ChannelStrip* Mixer::getStripAt (juce::Point<int> screenPosition)
    {
        // The master is drawn over the row of the others.
        if (master.getScreenBounds().contains (screenPosition))
            return &master;

        if (! viewport.getScreenBounds().contains (screenPosition))
            return nullptr;

        for (auto* strip : getAllStrips())
            if (strip != &master && strip->getScreenBounds().contains (screenPosition))
                return strip;

        return nullptr;
    }

    void Mixer::slotDragged (juce::Point<int> screenPosition)
    {
        auto* target = getStripAt (screenPosition);

        for (auto* strip : getAllStrips())
            strip->setDropMarker (strip == target ? strip->getDropPosition (screenPosition) : -1);
    }

    void Mixer::slotDropped (te::Plugin& plugin, juce::Point<int> screenPosition, bool copy)
    {
        const te::Plugin::Ptr keepAlive (&plugin);
        auto* target = getStripAt (screenPosition);
        const auto position = target != nullptr ? target->getDropPosition (screenPosition) : -1;

        for (auto* strip : getAllStrips())
            strip->setDropMarker (-1);

        if (target != nullptr && position >= 0 && target->dropEffect (plugin, position, copy))
            refresh();
    }

    void Mixer::showAddMenu()
    {
        using audio::sends::Preset;
        juce::Component::SafePointer<Mixer> safe (this);
        const auto addReturn = [safe] (Preset preset)
        {
            return [safe, preset]
            {
                if (safe != nullptr)
                    safe->addReturnTrack (preset);
            };
        };

        auto menu = arrangement::makeAddTrackMenu (model);
        menu.addSeparator();
        menu.addItem ("Group track: several tracks through one fader", [safe]
        {
            if (safe != nullptr)
                safe->addGroupTrack();
        });
        menu.addSeparator();
        menu.addSectionHeader ("Return track: one effect for several tracks");
        menu.addItem ("Reverb return", addReturn (Preset::reverb));
        menu.addItem ("Delay return", addReturn (Preset::delay));
        menu.addItem ("Empty return (add effects yourself)", addReturn (Preset::empty));
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addTrackButton));
    }

    te::AudioTrack* Mixer::addReturnTrack (audio::sends::Preset preset)
    {
        juce::String error;
        auto* track = audio::sends::addReturnTrack (model.getProject(), preset, error);

        if (onMessage != nullptr)
        {
            if (track == nullptr)
                onMessage (error, true);
            else
                onMessage ("Return track " + track->getName() + " added: drag the \"" + track->getName()
                               + "\" bar on a track's strip to send that track to it", false);
        }

        refresh();
        return track;
    }

    te::AudioTrack* Mixer::addGroupTrack()
    {
        auto* group = audio::groups::addGroup (model.getProject(), {});

        if (onMessage != nullptr && group != nullptr)
            onMessage ("Group track " + group->getName() + " added: choose it under the Output button of a track's strip "
                           "to play that track through it", false);

        refresh();
        return group;
    }

    //==============================================================================
    bool Mixer::syncStrips (juce::OwnedArray<ChannelStrip>& group, const juce::Array<te::AudioTrack*>& tracks)
    {
        auto same = tracks.size() == group.size();

        for (int i = 0; same && i < tracks.size(); ++i)
            same = group[i]->getTrack() == tracks[i];

        if (same)
        {
            for (auto* strip : group)
                strip->refresh();

            return false;
        }

        group.clear();

        for (auto* track : tracks)
        {
            auto* strip = group.add (std::make_unique<ChannelStrip> (session, model, *track));
            connect (*strip);
            stripRow.addAndMakeVisible (strip);
        }

        return true;
    }

    void Mixer::refresh()
    {
        needsRefresh = false;

        const auto tracksChanged = syncStrips (strips, model.getTracks());
        const auto groupsChanged = syncStrips (groupStrips, model.getProject().getGroupTracks());
        const auto returnsChanged = syncStrips (returnStrips, model.getProject().getReturnTracks());

        if (tracksChanged || groupsChanged || returnsChanged)
            layOutStrips();

        master.refresh();
    }

    void Mixer::paint (juce::Graphics& g)
    {
        g.fillAll (theme::colour::background);

        // A gap between the tracks and the master.
        g.setColour (theme::colour::border);
        g.fillRect (master.getX() - masterGap / 2, 0, 1, getHeight());
    }

    void Mixer::resized()
    {
        auto area = getLocalBounds();
        master.setBounds (area.removeFromRight (ChannelStrip::width));
        area.removeFromRight (masterGap);
        viewport.setBounds (area);
        layOutStrips();
    }

    void Mixer::layOutStrips()
    {
        // Song tracks; a gap, the group tracks; a gap, the return tracks; "+ Track".
        const auto busesWidth = [] (const juce::OwnedArray<ChannelStrip>& buses)
        {
            return buses.isEmpty() ? 0 : busGap + buses.size() * ChannelStrip::width;
        };
        const auto rowWidth = strips.size() * ChannelStrip::width + busesWidth (groupStrips) + busesWidth (returnStrips) + addTrackWidth;
        const auto height = viewport.getHeight() - (rowWidth > viewport.getWidth() ? viewport.getScrollBarThickness() : 0);
        auto x = 0;

        for (const auto* row : { &strips, &groupStrips, &returnStrips })
        {
            if (row != &strips && ! row->isEmpty())
                x += busGap;

            for (auto* strip : *row)
            {
                strip->setBounds (x, 0, ChannelStrip::width, height);
                x += ChannelStrip::width;
            }
        }

        addTrackButton.setBounds (juce::Rectangle<int> (x, 0, addTrackWidth, height)
                                      .reduced (8, 0).withHeight (theme::size::buttonHeight).withY (8));
        stripRow.setSize (rowWidth, height);
    }

    void Mixer::visibilityChanged()
    {
        if (isVisible() && needsRefresh)
            refresh();
    }

    void Mixer::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
    {
        // Either wheel direction scrolls the strips sideways.
        const auto delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        viewport.setViewPosition (viewport.getViewPositionX() - juce::roundToInt (delta * 300.0f), 0);
    }

    //==============================================================================
    void Mixer::arrangementChanged()
    {
        // The current track changed, or tracks did.
        triggerAsyncUpdate();
    }

    void Mixer::handleAsyncUpdate()
    {
        // Hidden behind the piano roll: catch up when shown.
        if (isVisible())
            refresh();
        else
            needsRefresh = true;
    }

    void Mixer::treeChanged (const juce::ValueTree& tree)
    {
        // The playhead position is saved in the edit too: that's no reason to redraw.
        if (tree.hasType (te::IDs::TRANSPORT))
            return;

        triggerAsyncUpdate();
    }

    void Mixer::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)    { treeChanged (tree); }
    void Mixer::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&)              { treeChanged (parent); }
    void Mixer::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)       { treeChanged (parent); }
    void Mixer::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int)               { treeChanged (parent); }
}
