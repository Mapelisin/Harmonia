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
                if (const auto folder = getFoldedFolder())
                {
                    model.setFolderMuted (folder->id, ! core::folders::isMuted (model.getEdit(), folder->id));
                    refresh();
                    return;
                }

                model.getProject().beginGesture ("Mute track");
                track->setMute (! track->isMuted (false));
                refresh();
            };

            fold.setTooltip ("Collapse the folder to one row, or show its tracks again");
            fold.onClick = [this]
            {
                if (const auto folder = model.getFolderOf (*track))
                    model.setFolderCollapsed (folder->id, ! folder->collapsed);
            };
            fold.setWantsKeyboardFocus (false);
            addChildComponent (fold);

            arm.setTooltip ("Record arm: when you record (R, then Play), this track records its input");
            arm.onClick = [this]
            {
                audio::AudioInputs::setArmed (*track, ! audio::AudioInputs::isArmed (*track));
                refresh();
            };

            solo.setTooltip ("Solo: hear only this track (and other soloed ones)");
            solo.onClick = [this]
            {
                if (const auto folder = getFoldedFolder())
                {
                    model.setFolderSoloed (folder->id, ! core::folders::isSoloed (model.getEdit(), folder->id));
                    refresh();
                    return;
                }

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
        juce::Button* getFoldButton() noexcept     { return fold.isVisible() ? &fold : nullptr; }
        juce::Button& getMuteButton() noexcept     { return mute; }
        juce::Button& getSoloButton() noexcept     { return solo; }
        juce::String getShownName() const          { return name.getText(); }

        /** The folder this row stands for (collapsed), if it does. */
        std::optional<core::folders::Folder> getFoldedFolder() const
        {
            const auto folder = model.getFolderOf (*track);
            return folder.has_value() && folder->collapsed ? folder : std::nullopt;
        }

        /** The row of a collapsed folder: the folder's name, its tracks counted, mute and solo
            for all of them. */
        void refreshFolded (const core::folders::Folder& folder)
        {
            const auto members = core::folders::getMembers (model.getEdit(), folder.id);
            name.setText (folder.name, juce::dontSendNotification);
            instrument.setText (juce::String (members.size()) + (members.size() == 1 ? " track" : " tracks"), juce::dontSendNotification);
            instrument.setTooltip ("A collapsed folder: the button on the left shows its tracks");

            const auto muted = core::folders::isMuted (model.getEdit(), folder.id);
            const auto soloed = core::folders::isSoloed (model.getEdit(), folder.id);
            mute.setColour (juce::TextButton::buttonColourId, muted ? colours::mute.withAlpha (0.8f) : theme::colour::surfaceRaised);
            mute.setColour (juce::TextButton::textColourOffId, muted ? theme::colour::background : theme::colour::textSecondary);
            solo.setColour (juce::TextButton::buttonColourId, soloed ? colours::solo.withAlpha (0.8f) : theme::colour::surfaceRaised);
            solo.setColour (juce::TextButton::textColourOffId, soloed ? theme::colour::background : theme::colour::textSecondary);
            mute.setTooltip ("Mute every track of the folder");
            solo.setTooltip ("Solo every track of the folder");
            inputMeter.setInput (nullptr);
        }

        void refresh()
        {
            // A folder's first track has the fold button; collapsed, the row shows the folder.
            const auto folder = model.getFolderOf (*track);
            const auto firstOfFolder = folder.has_value()
                                    && core::folders::getMembers (model.getEdit(), folder->id).getFirst() == track.get();
            const auto folded = folder.has_value() && folder->collapsed;
            fold.setButtonText (folded ? ">" : "v");

            if (fold.isVisible() != firstOfFolder || volume.isVisible() == folded || (folded && arm.isVisible())
                 || (folded && inputMeter.isVisible()))
            {
                fold.setVisible (firstOfFolder);
                volume.setVisible (! folded);

                if (folded)
                {
                    arm.setVisible (false);
                    inputMeter.setVisible (false);
                }

                resized();
            }

            folderName = folder.has_value() && ! folded ? folder->name : juce::String();

            if (folded)
            {
                refreshFolded (*folder);
                repaint();
                return;
            }

            mute.setTooltip ("Mute");
            solo.setTooltip ("Solo: hear only this track (and other soloed ones)");

            if (! name.isBeingEdited())
                name.setText (track->getName(), juce::dontSendNotification);

            const auto isAudio = core::tracks::isAudio (*track);
            auto* input = isAudio ? audio::AudioInputs::getInput (*track) : nullptr;
            const auto armed = isAudio && audio::AudioInputs::isArmed (*track);

            if (model.isFrozen (track.get()))
                instrument.setText ("Frozen", juce::dontSendNotification);
            else if (! isAudio)
                instrument.setText (audio::instruments::getName (*track), juce::dontSendNotification);
            else
                instrument.setText (input != nullptr ? "In: " + input->getAliasOrName() : juce::String ("Audio"), juce::dontSendNotification);

            instrument.setTooltip (model.isFrozen (track.get()) ? juce::String ("Frozen: it plays a file of its sound, its instrument and effects are off. "
                                                                                 "Unfreeze it (right-click) to change it.")
                                   : isAudio ? juce::String ("The input this track records from (right-click to change)") : juce::String());

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

            // In a folder: a second strip, and the folder's name small at the bottom right.
            if (model.getFolderOf (*track).has_value())
            {
                g.setColour (theme::colour::accent.withAlpha (0.75f));
                g.fillRect (bounds.removeFromLeft (3));
            }

            if (folderName.isNotEmpty())
            {
                g.setColour (theme::colour::accent);
                g.setFont (juce::FontOptions (theme::size::smallFontSize - 1.0f));
                g.drawText (folderName, getLocalBounds().reduced (8, 3), juce::Justification::bottomRight, true);
            }

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

            if (fold.isVisible())
            {
                fold.setBounds (top.removeFromLeft (smallButtonSize));
                top.removeFromLeft (3);
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
            if (! name.getBounds().contains (e.getPosition()))
                return;

            // The row of a collapsed folder shows the folder's name, not the track's.
            if (const auto folder = getFoldedFolder())
            {
                if (auto* owner = findParentComponentOfClass<TrackHeaders>(); owner != nullptr && owner->onRenameFolder != nullptr)
                    owner->onRenameFolder (*folder);

                return;
            }

            name.showEditor();
        }

    private:
        ArrangementModel& model;
        juce::ReferenceCountedObjectPtr<te::AudioTrack> track;   // kept alive while the header shows it

        juce::Label name, instrument;
        juce::TextButton mute { "M" }, solo { "S" }, arm { juce::String::fromUTF8 ("\xe2\x97\x8f") }, fold;
        juce::String folderName;    // shown small on a track in an open folder
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
            if (! getFoldedFolder().has_value())
                name.showEditor();
        }
    };

    //==============================================================================
    juce::PopupMenu TrackHeaders::createFolderMenu (te::AudioTrack& trackToShow)
    {
        juce::ReferenceCountedObjectPtr<te::AudioTrack> track (&trackToShow);
        juce::Component::SafePointer<TrackHeaders> safe (this);
        const auto folder = model.getFolderOf (*track);
        juce::PopupMenu menu;

        menu.addItem ("New folder with this track", [&m = model, t = track] { m.newFolder (*t); });

        // The other folders, to move into.
        juce::PopupMenu others;

        for (const auto& other : core::folders::getAll (model.getEdit()))
            if (! folder.has_value() || other.id != folder->id)
                others.addItem (other.name, [&m = model, t = track, id = other.id] { m.setTrackFolder (*t, id); });

        menu.addSubMenu ("Move into folder", others, others.getNumItems() > 0);
        menu.addItem ("Take out of the folder", folder.has_value(), false, [&m = model, t = track] { m.setTrackFolder (*t, {}); });

        if (folder.has_value())
        {
            const auto f = *folder;
            menu.addSeparator();
            menu.addSectionHeader (f.name);
            menu.addItem (f.collapsed ? "Show its tracks" : "Collapse to one row", [&m = model, f] { m.setFolderCollapsed (f.id, ! f.collapsed); });
            menu.addItem ("Rename folder...", [safe, f]
            {
                if (safe != nullptr && safe->onRenameFolder != nullptr)
                    safe->onRenameFolder (f);
            });
            menu.addItem ("Remove the folder (its tracks stay)", [&m = model, f] { m.removeFolder (f.id); });
        }

        return menu;
    }

    //==============================================================================
    juce::PopupMenu TrackHeaders::createTrackMenu (te::AudioTrack& trackToShow)
    {
        juce::ReferenceCountedObjectPtr<te::AudioTrack> track (&trackToShow);
        juce::Component::SafePointer<TrackHeaders> safe (this);

        juce::PopupMenu menu;

        // The row of a collapsed folder: the folder's menu.
        if (model.isFoldedRow (track.get()))
        {
            const auto f = *model.getFolderOf (*track);
            const auto row = model.indexOf (track.get());
            menu.addSectionHeader (f.name);
            menu.addItem ("Show its tracks", [&m = model, f] { m.setFolderCollapsed (f.id, false); });
            menu.addItem ("Rename folder...", [safe, f]
            {
                if (safe != nullptr && safe->onRenameFolder != nullptr)
                    safe->onRenameFolder (f);
            });
            menu.addItem ("Move up", row > 0, false, [&m = model, t = track] { m.moveTrack (*t, m.indexOf (t.get()) - 1); });
            menu.addItem ("Move down", row < model.getNumTracks() - 1, false,
                          [&m = model, t = track] { m.moveTrack (*t, m.indexOf (t.get()) + 1); });
            menu.addItem ("Remove the folder (its tracks stay)", [&m = model, f] { m.removeFolder (f.id); });
            menu.addSeparator();
            menu.addSubMenu ("Add track", makeAddTrackMenu (model));
            return menu;
        }

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

        const auto frozen = model.isFrozen (track.get());
        menu.addItem (frozen ? "Unfreeze" : "Freeze (plays as audio, its plugins off)", frozen || hasClips, frozen, [safe, t = track]
        {
            if (safe != nullptr)
                safe->toggleFrozen (*t);
        });
        menu.addSubMenu ("Output (master or a group)", makeOutputMenu (model, *track));
        menu.addSubMenu ("Folder", createFolderMenu (*track));
        menu.addSeparator();
        menu.addSubMenu ("Add track", makeAddTrackMenu (model));
        menu.addSeparator();
        menu.addItem ("Delete track", [&m = model, t = track] { m.deleteTrack (*t); });
        return menu;
    }

    void TrackHeaders::toggleFrozen (te::AudioTrack& track)
    {
        const auto trackName = track.getName();

        if (model.isFrozen (&track))
        {
            model.unfreezeTrack (track);

            if (onMessage != nullptr)
                onMessage (trackName + " unfrozen", false);

            return;
        }

        const auto error = model.freezeTrack (track);

        if (onMessage != nullptr)
            onMessage (error.isNotEmpty() ? error : trackName + " frozen: it plays as audio, its instrument and effects are off", error.isNotEmpty());
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

    juce::Button* TrackHeaders::getFoldButton (int index) const
    {
        auto* header = headers[index];
        return header != nullptr ? header->getFoldButton() : nullptr;
    }

    juce::Button* TrackHeaders::getMuteButton (int index) const
    {
        auto* header = headers[index];
        return header != nullptr ? &header->getMuteButton() : nullptr;
    }

    juce::Button* TrackHeaders::getSoloButton (int index) const
    {
        auto* header = headers[index];
        return header != nullptr ? &header->getSoloButton() : nullptr;
    }

    juce::String TrackHeaders::getShownName (int index) const
    {
        auto* header = headers[index];
        return header != nullptr ? header->getShownName() : juce::String();
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
