#include "ui/mixer/ChannelStrip.h"
#include "ui/TrackColours.h"
#include "audio/Sends.h"
#include "core/Tracks.h"
#include "ui/Theme.h"

namespace harmonia::ui::mixer
{
    namespace effects = audio::effects;

    namespace
    {
        constexpr int nameHeight = 24, slotHeight = 20, panHeight = 58, valueHeight = 16, buttonRowHeight = 26;
        constexpr int ledWidth = 14, colourStripHeight = 3, meterWidth = 12;
        constexpr int sendHeight = 18, maxVisibleSendRows = 4;

        std::function<te::LevelMeasurer*()> getMeterSource (te::Edit& edit, te::AudioTrack* track)
        {
            if (track != nullptr)
            {
                return [t = juce::ReferenceCountedObjectPtr<te::AudioTrack> (track)]() -> te::LevelMeasurer*
                {
                    auto* plugin = t->getLevelMeterPlugin();
                    return plugin != nullptr ? &plugin->measurer : nullptr;
                };
            }

            // The master is measured after the master volume, in the playback context.
            return [&edit]() -> te::LevelMeasurer*
            {
                auto* context = edit.getTransport().getCurrentPlaybackContext();
                return context != nullptr ? &context->masterLevels : nullptr;
            };
        }

        juce::String panToText (float panValue)
        {
            const auto percent = juce::roundToInt (std::abs (panValue) * 100.0f);

            if (percent == 0)
                return "C";

            return (panValue < 0.0f ? "L" : "R") + juce::String (percent);
        }

        juce::String volumeToText (float db)
        {
            return db <= -96.0f ? juce::String ("-inf dB") : juce::Decibels::toString (db, 1);
        }
    }

    //==============================================================================
    /** One effect in the chain. */
    class ChannelStrip::Slot final : public juce::Component,
                                     public juce::SettableTooltipClient
    {
    public:
        Slot (ChannelStrip& o, te::Plugin& p)
            : owner (o), plugin (&p)
        {
            setTooltip (effects::getDisplayName (p) + ": click to open, the light turns it off and on, right-click for more");
        }

        te::Plugin& getPlugin() const noexcept   { return *plugin; }

        void paint (juce::Graphics& g) override
        {
            const auto bypassed = effects::isBypassed (*plugin);
            auto area = getLocalBounds().reduced (0, 1).toFloat();

            g.setColour (bypassed ? theme::mixer::slotBypassed : theme::mixer::slot);
            g.fillRoundedRectangle (area, theme::size::cornerRadius);

            auto led = area.removeFromLeft ((float) ledWidth).withSizeKeepingCentre (6.0f, 6.0f);
            g.setColour (bypassed ? theme::colour::textSecondary.withAlpha (0.4f) : theme::mixer::slotOn);
            g.fillEllipse (led);

            g.setColour (bypassed ? theme::colour::textSecondary.withAlpha (0.6f) : theme::colour::textPrimary);
            g.setFont (juce::FontOptions (theme::size::smallFontSize));
            g.drawText (effects::getDisplayName (*plugin), area.reduced (2.0f, 0.0f), juce::Justification::centredLeft, true);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
                owner.showSlotMenu (*plugin);
            else if (e.x < ledWidth)
                toggleBypass();
            else
                effects::showEditor (*plugin);
        }

        void toggleBypass()
        {
            effects::setBypassed (*plugin, ! effects::isBypassed (*plugin));
            repaint();
        }

    private:
        ChannelStrip& owner;
        te::Plugin::Ptr plugin;
    };

    //==============================================================================
    /** How much of the track goes to one return track: a bar with the return's name. */
    class ChannelStrip::Send final : public juce::Component
    {
    public:
        Send (ChannelStrip& o, te::AudioTrack& r)
            : owner (o), returnTrack (&r)
        {
            slider.setSliderStyle (juce::Slider::LinearBar);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setRange (0.0, 1.0);
            slider.setDoubleClickReturnValue (true, 0.0);
            slider.setScrollWheelEnabled (false);   // the wheel scrolls the mixer
            slider.setWantsKeyboardFocus (false);
            slider.setColour (juce::Slider::trackColourId, theme::mixer::send.withAlpha (0.55f));
            slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            slider.onDragStart = [this] { owner.model.getProject().beginGesture ("Send to " + returnTrack->getName()); };
            slider.onValueChange = [this]
            {
                if (owner.track != nullptr && ! audio::sends::setLevel (*owner.track, *returnTrack, (float) slider.getValue()))
                    owner.tell ("No room on " + owner.track->getName() + " for another send", true);

                repaint();
            };
            addAndMakeVisible (slider);
            refresh();
        }

        te::AudioTrack& getReturnTrack() const noexcept     { return *returnTrack; }
        juce::Slider& getSlider() noexcept                  { return slider; }

        void refresh()
        {
            slider.setTooltip ("Send to " + returnTrack->getName() + ": how much of this track goes through that return's "
                               "effects (drag; double-click: none)");

            if (! slider.isMouseButtonDown() && owner.track != nullptr)
                slider.setValue (audio::sends::getLevel (*owner.track, *returnTrack), juce::dontSendNotification);

            repaint();
        }

        void resized() override
        {
            slider.setBounds (getLocalBounds().reduced (0, 1));
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (theme::mixer::slot);
            g.fillRoundedRectangle (getLocalBounds().reduced (0, 1).toFloat(), theme::size::cornerRadius);
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            const auto db = owner.track != nullptr ? audio::sends::getLevelDb (*owner.track, *returnTrack) : -100.0f;
            const auto off = slider.getValue() <= 0.0;
            auto area = getLocalBounds().reduced (4, 0);

            g.setFont (juce::FontOptions (theme::size::smallFontSize));
            g.setColour (off ? theme::colour::textSecondary : theme::colour::textPrimary);
            const auto value = off ? juce::String ("-") : juce::String (juce::roundToInt (db));
            g.drawText (value, area.removeFromRight (24), juce::Justification::centredRight, false);
            g.drawText (returnTrack->getName(), area, juce::Justification::centredLeft, true);
        }

    private:
        ChannelStrip& owner;
        juce::ReferenceCountedObjectPtr<te::AudioTrack> returnTrack;
        juce::Slider slider;
    };

    //==============================================================================
    void ChannelStrip::ClickWatcher::mouseDown (const juce::MouseEvent&)
    {
        if (owner.track != nullptr && ! owner.isReturn())
            owner.model.setCurrentTrack (owner.track.get());
    }

    //==============================================================================
    ChannelStrip::ChannelStrip (core::Session& s, arrangement::ArrangementModel& m, te::AudioTrack& t)
        : session (s), model (m), track (&t), meter (getMeterSource (m.getEdit(), &t))
    {
        setUp();
    }

    ChannelStrip::ChannelStrip (core::Session& s, arrangement::ArrangementModel& m)
        : session (s), model (m), meter (getMeterSource (m.getEdit(), nullptr))
    {
        setUp();
    }

    ChannelStrip::~ChannelStrip()
    {
        removeMouseListener (&clickWatcher);
    }

    void ChannelStrip::setUp()
    {
        setOpaque (true);

        name.setFont (juce::FontOptions (theme::size::fontSize, juce::Font::bold));
        name.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        name.setJustificationType (juce::Justification::centred);
        name.setInterceptsMouseClicks (false, false);

        addButton.setTooltip ("Add an effect");
        addButton.onClick = [this] { showAddMenu(); };
        slotList.addAndMakeVisible (addButton);
        slotView.setViewedComponent (&slotList, false);
        slotView.setScrollBarsShown (true, false);
        slotView.setScrollBarThickness (6);

        sendView.setViewedComponent (&sendList, false);
        sendView.setScrollBarsShown (true, false);
        sendView.setScrollBarThickness (6);

        pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pan.setRange (-1.0, 1.0);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.setScrollWheelEnabled (false);
        pan.setColour (juce::Slider::rotarySliderFillColourId, theme::mixer::fader);
        pan.onDragStart = [this] { model.getProject().beginGesture ("Pan"); };
        pan.onValueChange = [this]
        {
            if (auto* plugin = getVolumePlugin())
                plugin->setPan ((float) pan.getValue());

            panText.setText (panToText ((float) pan.getValue()), juce::dontSendNotification);
        };

        volume.setSliderStyle (juce::Slider::LinearVertical);
        volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        volume.setRange (0.0, 1.0);
        volume.setDoubleClickReturnValue (true, (double) te::decibelsToVolumeFaderPosition (0.0f));
        volume.setScrollWheelEnabled (false);   // the wheel scrolls the mixer
        volume.setColour (juce::Slider::trackColourId, theme::mixer::fader);
        volume.onDragStart = [this] { model.getProject().beginGesture (isMaster() ? "Master volume" : "Track volume"); };
        volume.onValueChange = [this]
        {
            if (auto* plugin = getVolumePlugin())
            {
                plugin->setSliderPos ((float) volume.getValue());
                volumeText.setText (volumeToText (plugin->getVolumeDb()), juce::dontSendNotification);
            }
        };

        for (auto* label : { &panText, &volumeText })
        {
            label->setFont (juce::FontOptions (theme::size::smallFontSize));
            label->setColour (juce::Label::textColourId, theme::colour::textSecondary);
            label->setJustificationType (juce::Justification::centred);
            label->setInterceptsMouseClicks (false, false);
        }

        pan.setTooltip ("Pan (double-click: centre)");
        volume.setTooltip ("Volume (double-click: 0 dB)");

        if (! isMaster())
        {
            mute.setTooltip ("Mute");
            mute.onClick = [this]
            {
                model.getProject().beginGesture ("Mute track");
                track->setMute (! track->isMuted (false));
                refreshValues();
            };

            solo.setTooltip ("Solo: hear only this track (and other soloed ones)");
            solo.onClick = [this]
            {
                model.getProject().beginGesture ("Solo track");
                track->setSolo (! track->isSolo (false));
                refreshValues();
            };

            addAndMakeVisible (mute);
            addAndMakeVisible (solo);
        }

        for (auto* c : std::initializer_list<juce::Component*> { &name, &slotView, &sendView, &pan, &panText, &volume, &volumeText, &meter })
            addAndMakeVisible (c);

        // The mixer never takes keyboard focus: Space and the shortcuts keep working.
        for (auto* c : std::initializer_list<juce::Component*> { &addButton, &pan, &volume, &mute, &solo, &slotView, &sendView })
            c->setWantsKeyboardFocus (false);

        if (isReturn())
            name.setTooltip ("Return track: plays what the tracks send to it (the send bars on their strips) "
                             "through its effects. Right-click for its menu");

        addMouseListener (&clickWatcher, true);
        refresh();
    }

    te::PluginList& ChannelStrip::getChain() const
    {
        return track != nullptr ? track->pluginList : model.getEdit().getMasterPluginList();
    }

    te::VolumeAndPanPlugin* ChannelStrip::getVolumePlugin() const
    {
        return track != nullptr ? track->getVolumePlugin() : model.getEdit().getMasterVolumePlugin().get();
    }

    juce::Colour ChannelStrip::getColour() const
    {
        if (isMaster())
            return theme::colour::accent;

        if (isReturn())
            return theme::mixer::returnTrack;

        return track_colours::getTrackColour (*track, model.indexOf (track.get()));
    }

    bool ChannelStrip::isReturn() const
    {
        return track != nullptr && core::tracks::isReturn (*track);
    }

    juce::Slider* ChannelStrip::getSendSlider (int index) const
    {
        auto* send = sends[index];
        return send != nullptr ? &send->getSlider() : nullptr;
    }

    te::AudioTrack* ChannelStrip::getSendTarget (int index) const
    {
        auto* send = sends[index];
        return send != nullptr ? &send->getReturnTrack() : nullptr;
    }

    //==============================================================================
    void ChannelStrip::refresh()
    {
        name.setText (isMaster() ? juce::String ("Master") : track->getName(), juce::dontSendNotification);

        if (! isReturn())
            name.setTooltip (name.getText());

        refreshSlots();
        refreshSends();
        refreshValues();
        repaint();
    }

    void ChannelStrip::refreshSends()
    {
        const auto returns = model.getProject().getReturnTracks();

        if (returns.size() != numSendRows)
        {
            numSendRows = returns.size();
            resized();
        }

        // Only song tracks send (a return sending to a return could feed itself).
        juce::Array<te::AudioTrack*> targets;

        if (track != nullptr && ! isReturn())
            targets = returns;

        auto same = targets.size() == sends.size();

        for (int i = 0; same && i < targets.size(); ++i)
            same = &sends[i]->getReturnTrack() == targets[i];

        if (! same)
        {
            sends.clear();

            for (auto* target : targets)
                sendList.addAndMakeVisible (sends.add (std::make_unique<Send> (*this, *target)));

            layOutSends();
        }
        else
        {
            for (auto* send : sends)
                send->refresh();
        }
    }

    void ChannelStrip::refreshSlots()
    {
        const auto chain = effects::get (getChain());
        auto same = chain.size() == slots.size();

        for (int i = 0; same && i < chain.size(); ++i)
            same = &slots[i]->getPlugin() == chain[i];

        if (! same)
        {
            slots.clear();

            for (auto* plugin : chain)
                slotList.addAndMakeVisible (slots.add (std::make_unique<Slot> (*this, *plugin)));

            layOutSlots();
        }
        else
        {
            for (auto* slot : slots)
                slot->repaint();
        }

        addButton.setEnabled (effects::canAdd (getChain()));
    }

    void ChannelStrip::refreshValues()
    {
        if (auto* plugin = getVolumePlugin())
        {
            if (! volume.isMouseButtonDown())
                volume.setValue (plugin->getSliderPos(), juce::dontSendNotification);

            if (! pan.isMouseButtonDown())
                pan.setValue (plugin->getPan(), juce::dontSendNotification);

            volumeText.setText (volumeToText (plugin->getVolumeDb()), juce::dontSendNotification);
            panText.setText (panToText (plugin->getPan()), juce::dontSendNotification);
        }

        if (track != nullptr)
        {
            const auto muted = track->isMuted (false), soloed = track->isSolo (false);
            mute.setColour (juce::TextButton::buttonColourId, muted ? theme::arrangement::mute.withAlpha (0.8f) : theme::colour::surfaceRaised);
            mute.setColour (juce::TextButton::textColourOffId, muted ? theme::colour::background : theme::colour::textSecondary);
            solo.setColour (juce::TextButton::buttonColourId, soloed ? theme::arrangement::solo.withAlpha (0.8f) : theme::colour::surfaceRaised);
            solo.setColour (juce::TextButton::textColourOffId, soloed ? theme::colour::background : theme::colour::textSecondary);
        }
    }

    //==============================================================================
    void ChannelStrip::paint (juce::Graphics& g)
    {
        const auto isCurrent = track != nullptr && track.get() == model.getCurrentTrack();
        g.fillAll (isCurrent ? theme::mixer::stripCurrent : theme::colour::surface);

        g.setColour (getColour().withAlpha (isCurrent || isMaster() ? 1.0f : 0.7f));
        g.fillRect (getLocalBounds().removeFromTop (colourStripHeight));

        g.setColour (theme::colour::border);
        g.fillRect (getLocalBounds().removeFromRight (1));
    }

    void ChannelStrip::resized()
    {
        auto area = getLocalBounds().withTrimmedRight (1).withTrimmedTop (colourStripHeight).reduced (4, 0);
        name.setBounds (area.removeFromTop (nameHeight));

        auto buttons = area.removeFromBottom (buttonRowHeight).reduced (0, 3);

        if (! isMaster())
        {
            const auto buttonWidth = (buttons.getWidth() - 4) / 2;
            mute.setBounds (buttons.removeFromLeft (buttonWidth));
            solo.setBounds (buttons.removeFromRight (buttonWidth));
        }

        volumeText.setBounds (area.removeFromBottom (valueHeight));

        // Effects get about a third of the height (at least room for a few), the fader the rest.
        const auto slotsHeight = juce::jlimit (3 * slotHeight, 12 * slotHeight, (area.getHeight() - panHeight) / 3);
        slotView.setBounds (area.removeFromTop (slotsHeight));
        layOutSlots();

        // Sends: a row per return track (a few visible, the rest scroll).
        if (numSendRows > 0)
        {
            area.removeFromTop (2);
            sendView.setBounds (area.removeFromTop (std::min (numSendRows, maxVisibleSendRows) * sendHeight + (numSendRows > maxVisibleSendRows ? 2 : 0)));
        }
        else
        {
            sendView.setBounds ({});
        }

        layOutSends();

        // JUCE draws a rotary 10 px inside its bounds: give it room.
        auto panRow = area.removeFromTop (panHeight).reduced (0, 2);
        panText.setBounds (panRow.removeFromRight (26));
        pan.setBounds (panRow.withSizeKeepingCentre (panRow.getHeight(), panRow.getHeight()));

        area.removeFromTop (2);
        auto faderArea = area.reduced (0, 2);
        meter.setBounds (faderArea.removeFromRight (meterWidth));
        faderArea.removeFromRight (4);
        volume.setBounds (faderArea);
    }

    void ChannelStrip::layOutSlots()
    {
        const auto rows = slots.size() + 1;
        const auto needsScroll = rows * slotHeight > slotView.getHeight();
        const auto listWidth = slotView.getWidth() - (needsScroll ? slotView.getScrollBarThickness() : 0);
        slotList.setSize (listWidth, rows * slotHeight);

        for (int i = 0; i < slots.size(); ++i)
            slots[i]->setBounds (0, i * slotHeight, listWidth, slotHeight);

        addButton.setBounds (0, slots.size() * slotHeight + 1, listWidth, slotHeight - 2);
    }

    void ChannelStrip::layOutSends()
    {
        const auto needsScroll = sends.size() * sendHeight > sendView.getHeight();
        const auto listWidth = sendView.getWidth() - (needsScroll ? sendView.getScrollBarThickness() : 0);
        sendList.setSize (std::max (0, listWidth), sends.size() * sendHeight);

        for (int i = 0; i < sends.size(); ++i)
            sends[i]->setBounds (0, i * sendHeight, listWidth, sendHeight);
    }

    //==============================================================================
    void ChannelStrip::mouseDown (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu() && isReturn())
            showReturnMenu();
    }

    void ChannelStrip::showReturnMenu()
    {
        juce::Component::SafePointer<ChannelStrip> safe (this);
        juce::PopupMenu menu;
        menu.addSectionHeader ("Return track " + track->getName());
        menu.addItem ("Rename...", [safe] { if (safe != nullptr) safe->askToRenameReturn(); });
        menu.addItem ("Delete (and the sends to it)", [safe, &project = model.getProject(), t = track]
        {
            audio::sends::removeReturnTrack (project, *t);

            if (safe != nullptr)
                safe->tell ("Return track " + t->getName() + " deleted (Ctrl+Z brings it back)", false);
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&name));
    }

    void ChannelStrip::askToRenameReturn()
    {
        auto* window = new juce::AlertWindow ("Rename return track", {}, juce::MessageBoxIconType::NoIcon, this);
        window->addTextEditor ("name", track->getName());
        window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        juce::Component::SafePointer<juce::AlertWindow> safeWindow (window);
        window->enterModalState (true, juce::ModalCallbackFunction::create (
            [safeWindow, &project = model.getProject(), t = track] (int result)
            {
                if (result == 1 && safeWindow != nullptr && t->state.getParent().isValid())
                    audio::sends::renameReturnTrack (project, *t, safeWindow->getTextEditorContents ("name"));
            }), true);
    }

    //==============================================================================
    void ChannelStrip::addEffect (const effects::EffectType& type)
    {
        juce::String error;
        auto* plugin = effects::add (getChain(), type, error);

        if (plugin == nullptr)
        {
            tell (error, true);
            return;
        }

        refresh();
        tell (type.name + " added to " + name.getText(), false);
        effects::showEditor (*plugin);
    }

    void ChannelStrip::showAddMenu()
    {
        juce::Component::SafePointer<ChannelStrip> safe (this);
        const auto add = [safe] (effects::EffectType type)
        {
            return [safe, type]
            {
                if (safe != nullptr)
                    safe->addEffect (type);
            };
        };

        juce::PopupMenu menu;
        menu.addSectionHeader ("Built-in");

        for (const auto& type : effects::getBuiltIn())
            menu.addItem (type.name, add (type));

        const auto external = effects::getExternal (session);
        menu.addSeparator();

        if (external.isEmpty())
        {
            menu.addItem ("Plugins: none found yet (Scan plugins)", false, false, nullptr);
        }
        else
        {
            juce::PopupMenu plugins;

            for (const auto& type : external)
                plugins.addItem (type.name + "  (" + type.description.manufacturerName + ")", add (type));

            menu.addSubMenu ("Plugins", plugins);
        }

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton));
    }

    void ChannelStrip::showSlotMenu (te::Plugin& plugin)
    {
        const auto chain = effects::get (getChain());
        const auto index = chain.indexOf (&plugin);
        const auto bypassed = effects::isBypassed (plugin);

        juce::Component::SafePointer<ChannelStrip> safe (this);
        const auto run = [safe, p = te::Plugin::Ptr (&plugin)] (std::function<void (te::Plugin&)> action)
        {
            return [safe, p, action]
            {
                action (*p);

                if (safe != nullptr)
                    safe->refresh();
            };
        };

        juce::PopupMenu menu;
        menu.addSectionHeader (effects::getDisplayName (plugin));
        menu.addItem ("Open", run ([] (te::Plugin& p) { effects::showEditor (p); }));
        menu.addItem (bypassed ? "Turn on" : "Turn off (bypass)", run ([bypassed] (te::Plugin& p) { effects::setBypassed (p, ! bypassed); }));
        menu.addSeparator();
        menu.addItem ("Move up", index > 0, false, run ([index] (te::Plugin& p) { effects::move (p, index - 1); }));
        menu.addItem ("Move down", index >= 0 && index < chain.size() - 1, false,
                      run ([index] (te::Plugin& p) { effects::move (p, index + 1); }));
        menu.addSeparator();
        menu.addItem ("Remove", run ([] (te::Plugin& p) { effects::remove (p); }));
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
    }

    void ChannelStrip::tell (const juce::String& text, bool isProblem)
    {
        if (onMessage != nullptr)
            onMessage (text, isProblem);
    }
}
