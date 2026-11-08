#include "ui/mixer/Mixer.h"
#include "ui/Theme.h"
#include "ui/arrangement/TrackHeaders.h"

namespace harmonia::ui::mixer
{
    namespace
    {
        constexpr int addTrackWidth = 70, masterGap = 6, returnGap = 6;
    }

    Mixer::Mixer (core::Session& s, arrangement::ArrangementModel& m)
        : session (s), model (m), editState (m.getEdit().state), master (s, m)
    {
        setOpaque (true);

        viewport.setViewedComponent (&stripRow, false);
        viewport.setScrollBarsShown (false, true);
        viewport.setScrollBarThickness (10);
        viewport.setWantsKeyboardFocus (false);

        addTrackButton.setTooltip ("Add a track, or a return track (one reverb or delay shared by several tracks)");
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
        const auto returnsChanged = syncStrips (returnStrips, model.getProject().getReturnTracks());

        if (tracksChanged || returnsChanged)
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
        // Song tracks, a gap, the return tracks, "+ Track".
        const auto returnsWidth = returnStrips.isEmpty() ? 0 : returnGap + returnStrips.size() * ChannelStrip::width;
        const auto rowWidth = strips.size() * ChannelStrip::width + returnsWidth + addTrackWidth;
        const auto height = viewport.getHeight() - (rowWidth > viewport.getWidth() ? viewport.getScrollBarThickness() : 0);
        auto x = 0;

        for (auto* strip : strips)
        {
            strip->setBounds (x, 0, ChannelStrip::width, height);
            x += ChannelStrip::width;
        }

        if (! returnStrips.isEmpty())
            x += returnGap;

        for (auto* strip : returnStrips)
        {
            strip->setBounds (x, 0, ChannelStrip::width, height);
            x += ChannelStrip::width;
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
