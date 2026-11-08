#include "ui/mixer/Mixer.h"
#include "ui/Theme.h"

namespace harmonia::ui::mixer
{
    namespace
    {
        constexpr int addTrackWidth = 70, masterGap = 6;
    }

    Mixer::Mixer (core::Session& s, arrangement::ArrangementModel& m)
        : session (s), model (m), editState (m.getEdit().state), master (s, m)
    {
        setOpaque (true);

        viewport.setViewedComponent (&stripRow, false);
        viewport.setScrollBarsShown (false, true);
        viewport.setScrollBarThickness (10);
        viewport.setWantsKeyboardFocus (false);

        addTrackButton.setTooltip ("Add a track");
        addTrackButton.setWantsKeyboardFocus (false);
        addTrackButton.onClick = [this] { model.addTrack(); };
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

    //==============================================================================
    void Mixer::refresh()
    {
        needsRefresh = false;

        const auto tracks = model.getTracks();
        auto same = tracks.size() == strips.size();

        for (int i = 0; same && i < tracks.size(); ++i)
            same = strips[i]->getTrack() == tracks[i];

        if (! same)
        {
            strips.clear();

            for (auto* track : tracks)
            {
                auto* strip = strips.add (std::make_unique<ChannelStrip> (session, model, *track));
                connect (*strip);
                stripRow.addAndMakeVisible (strip);
            }

            layOutStrips();
        }
        else
        {
            for (auto* strip : strips)
                strip->refresh();
        }

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
        const auto height = viewport.getHeight()
                          - (strips.size() * ChannelStrip::width + addTrackWidth > viewport.getWidth() ? viewport.getScrollBarThickness() : 0);

        for (int i = 0; i < strips.size(); ++i)
            strips[i]->setBounds (i * ChannelStrip::width, 0, ChannelStrip::width, height);

        addTrackButton.setBounds (juce::Rectangle<int> (strips.size() * ChannelStrip::width, 0, addTrackWidth, height)
                                      .reduced (8, 0).withHeight (theme::size::buttonHeight).withY (8));
        stripRow.setSize (strips.size() * ChannelStrip::width + addTrackWidth, height);
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
