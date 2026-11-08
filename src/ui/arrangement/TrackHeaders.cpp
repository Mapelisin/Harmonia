#include "ui/arrangement/TrackHeaders.h"
#include "audio/Instruments.h"
#include "core/Tracks.h"
#include "ui/Theme.h"

namespace harmonia::ui::arrangement
{
    namespace colours = theme::arrangement;

    namespace
    {
        constexpr int colourStripWidth = 4;
        constexpr int smallButtonSize = 20;
    }

    void showAddTrackMenu (ArrangementModel& model, juce::Component& target)
    {
        juce::PopupMenu menu;
        menu.addItem ("Instrument track (MIDI, built-in synth)", [&model] { model.addTrack(); });
        menu.addItem ("Audio track (sound files)", [&model] { model.addAudioTrack(); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
    }

    //==============================================================================
    class TrackHeaders::Header final : public juce::Component
    {
    public:
        Header (ArrangementModel& m, te::AudioTrack& t)
            : model (m), track (&t)
        {
            name.setFont (juce::FontOptions (theme::size::fontSize, juce::Font::bold));
            name.setColour (juce::Label::textColourId, theme::colour::textPrimary);
            name.setInterceptsMouseClicks (false, false);
            name.onTextChange = [this] { model.renameTrack (*track, name.getText()); };

            instrument.setFont (juce::FontOptions (theme::size::smallFontSize));
            instrument.setColour (juce::Label::textColourId, theme::colour::textSecondary);
            instrument.setInterceptsMouseClicks (false, false);

            mute.setTooltip ("Mute");
            mute.onClick = [this]
            {
                model.getProject().beginGesture ("Mute track");
                track->setMute (! track->isMuted (false));
                refresh();
            };

            solo.setTooltip ("Solo: hear only this track (and other soloed ones)");
            solo.onClick = [this]
            {
                model.getProject().beginGesture ("Solo track");
                track->setSolo (! track->isSolo (false));
                refresh();
            };

            volume.setSliderStyle (juce::Slider::LinearHorizontal);
            volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            volume.setRange (0.0, 1.0);
            volume.setScrollWheelEnabled (false);   // the wheel scrolls the track list
            volume.setDoubleClickReturnValue (true, (double) te::decibelsToVolumeFaderPosition (0.0f));
            volume.onDragStart = [this] { model.getProject().beginGesture ("Track volume"); };
            volume.onValueChange = [this]
            {
                if (auto* plugin = track->getVolumePlugin())
                {
                    plugin->setSliderPos ((float) volume.getValue());
                    volume.setTooltip ("Volume " + juce::Decibels::toString (plugin->getVolumeDb(), 1));
                }
            };

            for (auto* c : std::initializer_list<juce::Component*> { &name, &instrument, &mute, &solo, &volume })
            {
                c->setWantsKeyboardFocus (false);
                addAndMakeVisible (c);
            }

            refresh();
        }

        te::AudioTrack& getTrack() const noexcept  { return *track; }

        void refresh()
        {
            if (! name.isBeingEdited())
                name.setText (track->getName(), juce::dontSendNotification);

            instrument.setText (core::tracks::isAudio (*track) ? juce::String ("Audio") : audio::instruments::getName (*track),
                                juce::dontSendNotification);

            const auto muted = track->isMuted (false), soloed = track->isSolo (false);
            mute.setColour (juce::TextButton::buttonColourId, muted ? colours::mute.withAlpha (0.8f) : theme::colour::surfaceRaised);
            mute.setColour (juce::TextButton::textColourOffId, muted ? theme::colour::background : theme::colour::textSecondary);
            solo.setColour (juce::TextButton::buttonColourId, soloed ? colours::solo.withAlpha (0.8f) : theme::colour::surfaceRaised);
            solo.setColour (juce::TextButton::textColourOffId, soloed ? theme::colour::background : theme::colour::textSecondary);

            if (auto* plugin = track->getVolumePlugin(); plugin != nullptr && ! volume.isMouseButtonDown())
            {
                volume.setValue (plugin->getSliderPos(), juce::dontSendNotification);
                volume.setTooltip ("Volume " + juce::Decibels::toString (plugin->getVolumeDb(), 1));
            }

            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto index = model.indexOf (track.get());
            const auto isCurrent = track.get() == model.getCurrentTrack();
            auto bounds = getLocalBounds();

            g.setColour (isCurrent ? colours::laneCurrent : theme::colour::surface);
            g.fillRect (bounds);
            g.setColour (colours::getTrackColour (index).withAlpha (isCurrent ? 1.0f : 0.7f));
            g.fillRect (bounds.removeFromLeft (colourStripWidth));
            g.setColour (theme::colour::border);
            g.fillRect (getLocalBounds().removeFromBottom (1));
        }

        void resized() override
        {
            auto area = getLocalBounds().withTrimmedLeft (colourStripWidth + 6).withTrimmedRight (6).reduced (0, 5);
            auto top = area.removeFromTop (smallButtonSize);
            solo.setBounds (top.removeFromRight (smallButtonSize));
            top.removeFromRight (3);
            mute.setBounds (top.removeFromRight (smallButtonSize));
            top.removeFromRight (4);
            name.setBounds (top);

            area.removeFromTop (3);
            auto bottom = area.removeFromTop (18);
            volume.setBounds (bottom.removeFromRight (std::min (80, bottom.getWidth() / 2)));
            instrument.setBounds (bottom);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            model.setCurrentTrack (track.get());

            if (e.mods.isPopupMenu())
                showMenu();
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (name.getBounds().contains (e.getPosition()))
                name.showEditor();
        }

    private:
        ArrangementModel& model;
        juce::ReferenceCountedObjectPtr<te::AudioTrack> track;   // kept alive while the header shows it

        juce::Label name, instrument;
        juce::TextButton mute { "M" }, solo { "S" };
        juce::Slider volume;

        void showMenu()
        {
            juce::PopupMenu menu;
            menu.addItem ("Rename", [safe = juce::Component::SafePointer<Header> (this)]
            {
                if (safe != nullptr)
                    safe->name.showEditor();
            });
            menu.addItem ("Add instrument track", [&m = model] { m.addTrack(); });
            menu.addItem ("Add audio track", [&m = model] { m.addAudioTrack(); });
            menu.addSeparator();
            menu.addItem ("Delete track", [&m = model, t = track] { m.deleteTrack (*t); });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
        }
    };

    //==============================================================================
    TrackHeaders::TrackHeaders (ArrangementModel& m, ArrangementView& v)
        : model (m), view (v)
    {
        setOpaque (true);
        refresh();
    }

    TrackHeaders::~TrackHeaders() = default;

    void TrackHeaders::paint (juce::Graphics& g)
    {
        g.fillAll (colours::belowTracks);
        g.setColour (theme::colour::border);
        g.fillRect (getLocalBounds().removeFromRight (1));
    }

    void TrackHeaders::resized()
    {
        for (int i = 0; i < headers.size(); ++i)
            headers[i]->setBounds (0, (int) std::round (view.trackToY (i)), getWidth() - 1, (int) view.trackHeight);
    }

    void TrackHeaders::refresh()
    {
        const auto tracks = model.getTracks();
        auto same = tracks.size() == headers.size();

        for (int i = 0; same && i < tracks.size(); ++i)
            same = &headers[i]->getTrack() == tracks[i];

        if (! same)
        {
            headers.clear();

            for (auto* track : tracks)
                addAndMakeVisible (headers.add (std::make_unique<Header> (model, *track)));
        }
        else
        {
            for (auto* header : headers)
                header->refresh();
        }

        resized();
    }

    void TrackHeaders::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
    {
        view.scrollByPixels (0.0, -wheel.deltaY * 200.0);

        if (onViewChanged != nullptr)
            onViewChanged();
    }
}
