#include "ui/automation/AutomationPanel.h"
#include "ui/ComputerKeyboard.h"
#include "ui/TrackColours.h"
#include "ui/Theme.h"

namespace harmonia::ui::automation
{
    namespace automation = audio::automation;

    namespace
    {
        // Marks parameters (and effects) that have a line, in the menu.
        const juce::String lineMark = juce::String::fromUTF8 ("  \xe2\x80\xa2");

        bool isSend (te::AutomatableParameter& parameter)
        {
            return dynamic_cast<te::AuxSendPlugin*> (parameter.getPlugin()) != nullptr;
        }
    }

    AutomationPanel::AutomationPanel (arrangement::ArrangementModel& m, const arrangement::ArrangementView& v, audio::Transport& t)
        : model (m), view (v), transport (t), edit (m.getEdit())
    {
        model.addListener (this);
        edit.state.addListener (this);

        targetButton.setTooltip ("Whose lines: the track clicked in the arrangement, the master, a return track or a group");
        targetButton.onClick = [this] { showTargetMenu(); };
        infoLabel.setFont (juce::FontOptions (theme::size::smallFontSize));
        infoLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        infoLabel.setJustificationType (juce::Justification::topLeft);

        parameterButton.setTooltip ("Which parameter of the track follows a line: volume, pan, a send, an effect's knob");
        parameterButton.onClick = [this] { showParameterMenu(); };
        clearButton.setTooltip ("Remove this line: the parameter stays where its knob is");
        clearButton.onClick = [this] { clearLine(); };
        lane.onEdited = [this] { updateControls(); };

        for (auto* c : std::initializer_list<juce::Component*> { &targetButton, &parameterButton, &clearButton })
            c->setWantsKeyboardFocus (false);

        for (auto* c : std::initializer_list<juce::Component*> { &targetButton, &parameterButton, &clearButton, &infoLabel, &lane })
            addAndMakeVisible (c);

        refresh();
    }

    AutomationPanel::~AutomationPanel()
    {
        edit.state.removeListener (this);
        model.removeListener (this);
    }

    //==============================================================================
    void AutomationPanel::paint (juce::Graphics& g)
    {
        g.fillAll (theme::colour::surface);
        g.setColour (theme::colour::border);
        g.fillRect (leftMargin - 1, 0, 1, getHeight());

        if (track != nullptr || master)
        {
            g.setColour (getTargetColour());
            g.fillRect (0, 0, 4, getHeight());
        }
    }

    void AutomationPanel::resized()
    {
        auto area = getLocalBounds();
        auto left = area.removeFromLeft (leftMargin).withTrimmedRight (1).reduced (theme::size::padding, 6);
        area.removeFromRight (rightMargin);
        lane.setBounds (area);

        left.removeFromLeft (2);
        targetButton.setBounds (left.removeFromTop (theme::size::buttonHeight));
        left.removeFromTop (4);
        parameterButton.setBounds (left.removeFromTop (theme::size::buttonHeight));
        left.removeFromTop (4);
        clearButton.setBounds (left.removeFromTop (theme::size::buttonHeight).removeFromLeft (64));
        left.removeFromTop (4);
        infoLabel.setBounds (left);
    }

    void AutomationPanel::setMargins (int left, int right)
    {
        leftMargin = left;
        rightMargin = right;
        resized();
        repaint();
    }

    //==============================================================================
    void AutomationPanel::refresh()
    {
        auto* current = model.getCurrentTrack();
        const auto currentID = current != nullptr ? current->itemID : te::EditItemID();

        // Another track clicked in the arrangement: the panel is that track's again.
        if (currentID != lastCurrentTrack)
        {
            lastCurrentTrack = currentID;
            master = false;
            bus = nullptr;
        }

        // The return track or group shown was deleted.
        if (bus != nullptr && ! model.getProject().getReturnTracks().contains (bus.get())
            && ! model.getProject().getGroupTracks().contains (bus.get()))
            bus = nullptr;

        track = master ? nullptr : bus != nullptr ? bus.get() : current;
        const auto parameters = getParameters();
        te::AutomatableParameter* parameter = nullptr;

        if (! parameters.isEmpty())
        {
            const auto it = chosen.find (track != nullptr ? track->itemID : te::EditItemID());
            const auto withLine = std::find_if (parameters.begin(), parameters.end(),
                                                [] (te::AutomatableParameter* p) { return automation::hasPoints (*p); });

            if (it != chosen.end() && parameters.contains (it->second.get()))
                parameter = it->second.get();
            else if (withLine != parameters.end())
                parameter = *withLine;
            else
                parameter = parameters.getFirst();
        }

        lane.setParameter (parameter);
        lane.setLineColour (getTargetColour());
        updateControls();
        lane.repaint();
        repaint();
    }

    juce::Array<te::AutomatableParameter*> AutomationPanel::getParameters() const
    {
        if (master)
            return automation::getMasterParameters (edit);

        return track != nullptr ? automation::getParameters (*track) : juce::Array<te::AutomatableParameter*>();
    }

    juce::String AutomationPanel::getTargetName() const
    {
        return master ? juce::String ("Master") : track != nullptr ? track->getName() : juce::String ("No track");
    }

    juce::Colour AutomationPanel::getTargetColour() const
    {
        return track != nullptr ? track_colours::getTrackColour (*track, model.indexOf (track.get())) : theme::colour::accent;
    }

    void AutomationPanel::showMaster()
    {
        master = true;
        bus = nullptr;
        refresh();
    }

    void AutomationPanel::showBus (te::AudioTrack& returnOrGroup)
    {
        if (! core::tracks::isBus (returnOrGroup))
            return;

        master = false;
        bus = &returnOrGroup;
        refresh();
    }

    void AutomationPanel::followCurrentTrack()
    {
        master = false;
        bus = nullptr;
        refresh();
    }

    void AutomationPanel::setParameter (te::AutomatableParameter* parameter)
    {
        if (parameter == nullptr || ! getParameters().contains (parameter))
            return;

        chosen[track != nullptr ? track->itemID : te::EditItemID()] = parameter;
        lane.setParameter (parameter);
        updateControls();
    }

    void AutomationPanel::clearLine()
    {
        if (auto* parameter = lane.getParameter(); parameter != nullptr && automation::hasPoints (*parameter))
        {
            model.getProject().beginGesture ("Clear automation");
            automation::clear (*parameter);
            updateControls();
            lane.repaint();
        }
    }

    double AutomationPanel::getPasteBeat() const
    {
        const auto playhead = edit.tempoSequence.toBeats (te::TimePosition::fromSeconds (transport.getPositionSeconds())).inBeats();
        return std::max (0.0, core::snap::snapFloor (playhead, model.getSnap(), model.getSnapContext()));
    }

    bool AutomationPanel::keyPressed (const juce::KeyPress& key)
    {
        auto& gestures = lane.getGestures();
        const auto mods = key.getModifiers();
        const auto code = key.getKeyCode();
        auto used = false;

        if (! mods.isAnyModifierKeyDown())
        {
            if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
            {
                used = gestures.deleteSelected();
            }
            else if (code == juce::KeyPress::escapeKey && ! gestures.getSelection().empty())
            {
                gestures.clearSelection();
                used = true;
            }
        }
        else if (mods.isCommandDown() && ! mods.isAltDown() && ! mods.isShiftDown())
        {
            switch (keys::getLetter (key))
            {
                case 'C': used = gestures.copySelected(); break;
                case 'X': used = gestures.cutSelected(); break;
                case 'V': used = gestures.paste (getPasteBeat()); break;
                case 'A': gestures.selectAll(); used = ! gestures.getSelection().empty(); break;
                default:  break;
            }
        }

        if (used)
        {
            updateControls();
            lane.repaint();
        }

        return used;
    }

    void AutomationPanel::updateControls()
    {
        auto* parameter = lane.getParameter();
        targetButton.setButtonText (getTargetName());
        parameterButton.setButtonText (parameter != nullptr ? automation::getDisplayName (*parameter) : juce::String ("-"));
        parameterButton.setEnabled (parameter != nullptr);
        clearButton.setEnabled (parameter != nullptr && automation::hasPoints (*parameter));

        if (parameter == nullptr)
        {
            infoLabel.setText ("Click a track in the arrangement", juce::dontSendNotification);
            return;
        }

        const auto numPoints = (int) automation::getPoints (*parameter).size();
        const auto parameters = getParameters();
        const auto numLines = (int) std::count_if (parameters.begin(), parameters.end(),
                                                   [] (te::AutomatableParameter* p) { return automation::hasPoints (*p); });

        auto text = numPoints == 0 ? "No line: stays at " + automation::valueToText (*parameter, automation::getValueAt (*parameter, 0.0))
                                   : juce::String (numPoints) + (numPoints == 1 ? " point" : " points");

        if (const auto numSelected = (int) lane.getGestures().getSelection().size(); numSelected > 0)
            text << ", " << numSelected << " selected";
        else if (numPoints > 1)
            text << "\n" << "Ctrl+drag selects points";

        if (numLines > 0)
            text << "\n" << (master ? "Lines on the master: " : "Lines on this track: ") << numLines;

        infoLabel.setText (text, juce::dontSendNotification);
    }

    void AutomationPanel::showTargetMenu()
    {
        juce::Component::SafePointer<AutomationPanel> safe (this);

        const auto hasLines = [] (const juce::Array<te::AutomatableParameter*>& parameters)
        {
            return std::any_of (parameters.begin(), parameters.end(), [] (te::AutomatableParameter* p) { return automation::hasPoints (*p); });
        };

        const auto busItem = [&] (te::AudioTrack* busTrack, const juce::String& kind, juce::PopupMenu& menu)
        {
            const auto name = kind + busTrack->getName() + (hasLines (automation::getParameters (*busTrack)) ? lineMark : juce::String());

            menu.addItem (name, true, busTrack == bus.get(), [safe, ptr = te::AudioTrack::Ptr (busTrack)]
            {
                if (safe != nullptr)
                    safe->showBus (*ptr);
            });
        };

        juce::PopupMenu menu;
        menu.addSectionHeader ("Whose lines");

        if (auto* current = model.getCurrentTrack())
            menu.addItem ("Track: " + current->getName() + (hasLines (automation::getParameters (*current)) ? lineMark : juce::String()),
                          true, ! master && bus == nullptr, [safe] { if (safe != nullptr) safe->followCurrentTrack(); });

        menu.addItem ("Master" + (hasLines (automation::getMasterParameters (edit)) ? lineMark : juce::String()), true, master,
                      [safe] { if (safe != nullptr) safe->showMaster(); });

        const auto returns = model.getProject().getReturnTracks();
        const auto groups = model.getProject().getGroupTracks();

        if (! returns.isEmpty() || ! groups.isEmpty())
            menu.addSeparator();

        for (auto* returnTrack : returns)
            busItem (returnTrack, "Return: ", menu);

        for (auto* group : groups)
            busItem (group, "Group: ", menu);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&targetButton));
    }

    void AutomationPanel::showParameterMenu()
    {
        const auto parameters = getParameters();

        if (parameters.isEmpty())
            return;

        auto* shown = lane.getParameter();
        juce::Component::SafePointer<AutomationPanel> safe (this);

        const auto choose = [safe] (te::AutomatableParameter* parameter)
        {
            return [safe, ptr = te::AutomatableParameter::Ptr (parameter)]
            {
                if (safe != nullptr)
                    safe->setParameter (ptr.get());
            };
        };

        const auto withMark = [] (te::AutomatableParameter& parameter, const juce::String& text)
        {
            return automation::hasPoints (parameter) ? text + lineMark : text;
        };

        juce::PopupMenu menu;
        menu.addSectionHeader ("Automate " + getTargetName());

        // Volume and pan, then the sends, then each effect (and the instrument) in a submenu.
        struct Group { te::Plugin* plugin; juce::String name; juce::PopupMenu menu; bool automated = false, ticked = false; };
        std::vector<Group> groups;
        auto sendsStarted = false;

        for (auto* parameter : parameters)
        {
            if (automation::getGroupName (*parameter).isEmpty())
            {
                if (isSend (*parameter) && ! std::exchange (sendsStarted, true))
                    menu.addSeparator();

                menu.addItem (withMark (*parameter, automation::getDisplayName (*parameter)), true, parameter == shown, choose (parameter));
                continue;
            }

            auto* plugin = parameter->getPlugin();
            auto group = std::find_if (groups.begin(), groups.end(), [plugin] (const Group& g) { return g.plugin == plugin; });

            if (group == groups.end())
            {
                groups.push_back ({ plugin, automation::getGroupName (*parameter), {} });
                group = std::prev (groups.end());
            }

            group->menu.addItem (withMark (*parameter, parameter->getParameterName()), true, parameter == shown, choose (parameter));
            group->automated = group->automated || automation::hasPoints (*parameter);
            group->ticked = group->ticked || parameter == shown;
        }

        if (! groups.empty())
            menu.addSeparator();

        for (auto& group : groups)
            menu.addSubMenu (group.automated ? group.name + lineMark : group.name, group.menu, true, nullptr, group.ticked);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&parameterButton));
    }

    //==============================================================================
    void AutomationPanel::updatePlayhead()
    {
        if (! isShowing())
            return;

        std::optional<double> beat;

        if (transport.isPlaying() || transport.getPositionSeconds() > 0.0)
            beat = edit.tempoSequence.toBeats (te::TimePosition::fromSeconds (transport.getPositionSeconds())).inBeats();

        lane.setPlayheadBeat (beat);
    }

    void AutomationPanel::arrangementChanged()
    {
        refresh();
    }

    void AutomationPanel::handleAsyncUpdate()
    {
        refresh();
    }

    void AutomationPanel::treeChanged (const juce::ValueTree& tree)
    {
        // Not the plugins' values: those change all the time while a line plays them.
        if (tree.hasType (te::IDs::AUTOMATIONCURVE) || tree.hasType (te::IDs::POINT))
            triggerAsyncUpdate();
    }

    void AutomationPanel::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
    {
        treeChanged (tree);
    }

    void AutomationPanel::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
    {
        treeChanged (parent);
        treeChanged (child);

        if (child.hasType (te::IDs::PLUGIN))
            triggerAsyncUpdate();
    }

    void AutomationPanel::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
    {
        treeChanged (parent);
        treeChanged (child);

        if (child.hasType (te::IDs::PLUGIN))
            triggerAsyncUpdate();
    }
}
