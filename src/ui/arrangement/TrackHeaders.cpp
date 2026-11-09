#include "ui/arrangement/TrackHeaders.h"
#include "ui/TrackColours.h"
#include "audio/AudioInputs.h"
#include "audio/Groups.h"
#include "audio/Instruments.h"
#include "core/Clips.h"
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

    juce::PopupMenu makeAddTrackMenu (ArrangementModel& model)
    {
        juce::PopupMenu menu;
        menu.addItem ("Instrument track (MIDI, built-in synth)", [&model] { model.addTrack(); });
        menu.addItem ("Drum track (built-in drums)", [&model] { model.addDrumTrack(); });
        menu.addItem ("Audio track (sound files)", [&model] { model.addAudioTrack(); });
        return menu;
    }

    void showAddTrackMenu (ArrangementModel& model, juce::Component& target)
    {
        makeAddTrackMenu (model).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target));
    }

    juce::PopupMenu makeOutputMenu (ArrangementModel& model, te::AudioTrack& track)
    {
        auto& project = model.getProject();
        const auto* current = audio::groups::getGroup (track);
        const te::Track::Ptr keepAlive (&track);
        const auto setGroup = [&project, keepAlive] (te::AudioTrack* group)
        {
            return [&project, keepAlive, g = te::Track::Ptr (group)]
            {
                auto* t = dynamic_cast<te::AudioTrack*> (keepAlive.get());
                auto* target = dynamic_cast<te::AudioTrack*> (g.get());

                if (t == nullptr || ! t->state.getParent().isValid() || (target != nullptr && ! target->state.getParent().isValid()))
                    return;

                project.beginGesture (target != nullptr ? "Play through " + target->getName() : juce::String ("Play to the master"));
                audio::groups::setGroup (*t, target);
            };
        };

        juce::PopupMenu menu;
        menu.addItem ("Master", true, current == nullptr, setGroup (nullptr));

        for (auto* group : project.getGroupTracks())
            menu.addItem (group->getName(), true, group == current, setGroup (group));

        menu.addSeparator();
        menu.addItem ("New group with this track", [&project, keepAlive]
        {
            if (auto* t = dynamic_cast<te::AudioTrack*> (keepAlive.get()); t != nullptr && t->state.getParent().isValid())
                audio::groups::addGroup (project, { t });
        });
        return menu;
    }

    //==============================================================================
    /** A thin horizontal meter of an input's level (the louder channel). */
    class InputMeter final : public juce::Component,
                             private juce::Timer
    {
    public:
        InputMeter()
        {
            setInterceptsMouseClicks (false, false);
        }

        ~InputMeter() override
        {
            detach();
        }

        void setInput (te::InputDevice* input)
        {
            if (input == shownInput && (input == nullptr || attached != nullptr))
                return;

            detach();
            shownInput = input;

            if (input != nullptr)
            {
                attached = &input->levelMeasurer;
                attached->addClient (client);
                startTimerHz (30);
            }

            levelDb = minDb;
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            auto area = getLocalBounds().toFloat();
            g.setColour (theme::mixer::meterBackground);
            g.fillRect (area);

            const auto proportion = juce::jlimit (0.0f, 1.0f, (levelDb - minDb) / (maxDb - minDb));
            const auto level = area.withWidth (area.getWidth() * proportion);
            g.setColour (levelDb > 0.0f ? theme::colour::record : levelDb > -12.0f ? theme::mixer::meterMid : theme::mixer::meterLow);
            g.fillRect (level);
        }

    private:
        static constexpr float minDb = -60.0f, maxDb = 6.0f;

        juce::WeakReference<te::LevelMeasurer> attached;
        const te::InputDevice* shownInput = nullptr;   // only compared, never used
        te::LevelMeasurer::Client client;
        float levelDb = minDb;

        void detach()
        {
            stopTimer();

            if (auto* measurer = attached.get())
                measurer->removeClient (client);

            attached = nullptr;
        }

        void timerCallback() override
        {
            if (attached == nullptr)
                return;

            auto loudest = minDb;

            for (int channel = 0; channel < 2; ++channel)
                loudest = std::max (loudest, client.getAndClearAudioLevel (channel).dB);

            // Falls back smoothly, jumps up at once.
            const auto fallen = levelDb - 1.0f;
            const auto newLevel = std::max (loudest, std::max (minDb, fallen));

            if (std::abs (newLevel - levelDb) > 0.05f)
            {
                levelDb = newLevel;
                repaint();
            }
        }
    };

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

            arm.setTooltip ("Record arm: when you record (R, then Play), this track records its input");
            arm.onClick = [this]
            {
                audio::AudioInputs::setArmed (*track, ! audio::AudioInputs::isArmed (*track));
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

            arm.setWantsKeyboardFocus (false);
            addChildComponent (arm);
            addChildComponent (inputMeter);

            refresh();
        }

        te::AudioTrack& getTrack() const noexcept  { return *track; }
        juce::Button* getArmButton() noexcept      { return arm.isVisible() ? &arm : nullptr; }

        void refresh()
        {
            if (! name.isBeingEdited())
                name.setText (track->getName(), juce::dontSendNotification);

            const auto isAudio = core::tracks::isAudio (*track);
            auto* input = isAudio ? audio::AudioInputs::getInput (*track) : nullptr;
            const auto armed = isAudio && audio::AudioInputs::isArmed (*track);

            if (! isAudio)
                instrument.setText (audio::instruments::getName (*track), juce::dontSendNotification);
            else
                instrument.setText (input != nullptr ? "In: " + input->getAliasOrName() : juce::String ("Audio"), juce::dontSendNotification);

            instrument.setTooltip (isAudio ? juce::String ("The input this track records from (right-click to change)") : juce::String());

            if (arm.isVisible() != isAudio)
            {
                arm.setVisible (isAudio);
                resized();
            }

            arm.setColour (juce::TextButton::buttonColourId, armed ? theme::colour::record : theme::colour::surfaceRaised);
            arm.setColour (juce::TextButton::textColourOffId, armed ? theme::colour::textPrimary : theme::colour::record);
            inputMeter.setVisible (armed && input != nullptr);
            inputMeter.setInput (armed ? input : nullptr);

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
            g.setColour (track_colours::getTrackColour (*track, index).withAlpha (isCurrent ? 1.0f : 0.7f));
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
            top.removeFromRight (3);

            if (arm.isVisible())
            {
                arm.setBounds (top.removeFromRight (smallButtonSize));
                top.removeFromRight (4);
            }

            name.setBounds (top);

            area.removeFromTop (3);
            auto bottom = area.removeFromTop (18);
            volume.setBounds (bottom.removeFromRight (std::min (80, bottom.getWidth() / 2)));
            instrument.setBounds (bottom);
            inputMeter.setBounds (bottom.withTrimmedLeft (2).withTrimmedRight (6).removeFromBottom (3).translated (0, 3));
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            model.setCurrentTrack (track.get());
            dragging = false;

            if (e.mods.isPopupMenu())
                showMenu();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
                return;

            auto* owner = findParentComponentOfClass<TrackHeaders>();
            dragging = dragging || std::abs (e.getDistanceFromDragStartY()) > 4;

            if (owner != nullptr && dragging)
                owner->dragHeader (model.indexOf (track.get()), e.getEventRelativeTo (owner).position.y);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! std::exchange (dragging, false))
                return;

            if (auto* owner = findParentComponentOfClass<TrackHeaders>())
                owner->endHeaderDrag (model.indexOf (track.get()), e.getEventRelativeTo (owner).position.y);
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
        juce::TextButton mute { "M" }, solo { "S" }, arm { juce::String::fromUTF8 ("\xe2\x97\x8f") };
        juce::Slider volume;
        InputMeter inputMeter;
        bool dragging = false;

        void showMenu()
        {
            if (auto* owner = findParentComponentOfClass<TrackHeaders>())
                owner->createTrackMenu (*track).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
        }

    public:
        void startRenaming()
        {
            name.showEditor();
        }
    };

    //==============================================================================
    juce::PopupMenu TrackHeaders::createTrackMenu (te::AudioTrack& trackToShow)
    {
        juce::ReferenceCountedObjectPtr<te::AudioTrack> track (&trackToShow);
        juce::Component::SafePointer<TrackHeaders> safe (this);

        juce::PopupMenu menu;
        menu.addItem ("Rename", [safe, track]
        {
            if (safe != nullptr)
                safe->startRenaming (*track);
        });
        menu.addSubMenu ("Colour", track_colours::createMenu (core::tracks::getColour (*track), "Automatic",
                                                              [&m = model, t = track] (std::optional<juce::Colour> colour)
                                                              {
                                                                  m.setTrackColour (*t, colour);
                                                              }));
        if (core::tracks::isAudio (*track))
        {
            auto& engine = track->edit.engine;
            const auto inputs = audio::AudioInputs::getInputs (engine);
            auto* current = audio::AudioInputs::getInput (*track);
            juce::PopupMenu inputMenu;

            for (auto* input : inputs)
                inputMenu.addItem (input->getAliasOrName(), true, input == current, [t = track, inputName = input->getName()]
                {
                    audio::AudioInputs::setInput (*t, inputName);
                });

            if (inputs.empty())
                inputMenu.addItem ("No inputs (see File > Settings > Audio)", false, false, [] {});

            menu.addSeparator();
            menu.addItem ("Record arm", true, audio::AudioInputs::isArmed (*track), [t = track]
            {
                audio::AudioInputs::setArmed (*t, ! audio::AudioInputs::isArmed (*t));
            });
            menu.addSubMenu ("Record from", inputMenu);
            menu.addItem ("Hear the input while armed", true, audio::AudioInputs::isMonitoring (engine), [&engine]
            {
                audio::AudioInputs::setMonitoring (engine, ! audio::AudioInputs::isMonitoring (engine));
            });
            menu.addSeparator();
        }

        const auto index = model.indexOf (track.get());
        const auto hasClips = ! core::clips::getClips (*track).isEmpty();

        menu.addItem ("Duplicate track", [&m = model, t = track] { m.duplicateTrack (*t); });
        menu.addItem ("Move up", index > 0, false, [&m = model, t = track] { m.moveTrack (*t, m.indexOf (t.get()) - 1); });
        menu.addItem ("Move down", index < model.getNumTracks() - 1, false,
                      [&m = model, t = track] { m.moveTrack (*t, m.indexOf (t.get()) + 1); });
        menu.addItem ("Render to audio track", hasClips, false, [safe, t = track]
        {
            if (safe != nullptr)
                safe->renderToAudio (*t);
        });
        menu.addSubMenu ("Output (master or a group)", makeOutputMenu (model, *track));
        menu.addSeparator();
        menu.addSubMenu ("Add track", makeAddTrackMenu (model));
        menu.addSeparator();
        menu.addItem ("Delete track", [&m = model, t = track] { m.deleteTrack (*t); });
        return menu;
    }

    void TrackHeaders::startRenaming (te::AudioTrack& track)
    {
        for (auto* header : headers)
            if (&header->getTrack() == &track)
                header->startRenaming();
    }

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

    juce::Button* TrackHeaders::getArmButton (int index) const
    {
        auto* header = headers[index];
        return header != nullptr ? header->getArmButton() : nullptr;
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

    int TrackHeaders::gapAt (float y) const
    {
        return juce::jlimit (0, headers.size(), (int) std::round ((y + view.scrollY) / view.trackHeight));
    }

    void TrackHeaders::dragHeader (int trackIndex, float y)
    {
        if (trackIndex < 0)
            return;

        // The gaps right above and below the track itself would leave it where it is.
        const auto gap = gapAt (y);
        const std::optional<int> newGap = gap == trackIndex || gap == trackIndex + 1 ? std::nullopt : std::optional<int> (gap);

        if (newGap != dropGap)
        {
            dropGap = newGap;
            repaint();
        }
    }

    void TrackHeaders::endHeaderDrag (int trackIndex, float y)
    {
        dragHeader (trackIndex, y);
        const auto gap = std::exchange (dropGap, std::nullopt);
        repaint();

        if (auto* track = model.getTrack (trackIndex); track != nullptr && gap.has_value())
            model.moveTrack (*track, *gap > trackIndex ? *gap - 1 : *gap);
    }

    void TrackHeaders::paintOverChildren (juce::Graphics& g)
    {
        if (! dropGap.has_value())
            return;

        const auto y = view.trackToY (*dropGap);
        g.setColour (theme::colour::accent);
        g.fillRect (juce::Rectangle<float> (0.0f, y - 1.5f, (float) getWidth(), 3.0f));
    }

    void TrackHeaders::renderToAudio (te::AudioTrack& track)
    {
        juce::MouseCursor::showWaitCursor();
        const auto name = track.getName();
        const auto error = model.renderToAudio (track);
        juce::MouseCursor::hideWaitCursor();

        if (onMessage != nullptr)
            onMessage (error.isNotEmpty() ? error : "Rendered " + name + " to audio (the track is muted)", error.isNotEmpty());
    }

    void TrackHeaders::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
    {
        view.scrollByPixels (0.0, -wheel.deltaY * 200.0);

        if (onViewChanged != nullptr)
            onViewChanged();
    }
}
