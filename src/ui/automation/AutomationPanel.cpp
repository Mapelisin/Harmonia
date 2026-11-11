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

        trackLabel.setFont (juce::FontOptions (theme::size::fontSize, juce::Font::bold));
        trackLabel.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        infoLabel.setFont (juce::FontOptions (theme::size::smallFontSize));
        infoLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        infoLabel.setJustificationType (juce::Justification::topLeft);

        parameterButton.setTooltip ("Which parameter of the track follows a line: volume, pan, a send, an effect's knob");
        parameterButton.onClick = [this] { showParameterMenu(); };
        clearButton.setTooltip ("Remove this line: the parameter stays where its knob is");
        clearButton.onClick = [this] { clearLine(); };
        lane.onEdited = [this] { updateControls(); };

        for (auto* c : std::initializer_list<juce::Component*> { &parameterButton, &clearButton })
            c->setWantsKeyboardFocus (false);

        for (auto* c : std::initializer_list<juce::Component*> { &trackLabel, &parameterButton, &clearButton, &infoLabel, &lane })
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

        if (track != nullptr)
        {
            g.setColour (track_colours::getTrackColour (*track, model.indexOf (track.get())));
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
        trackLabel.setBounds (left.removeFromTop (20));
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
        track = current;
        te::AutomatableParameter* parameter = nullptr;

        if (current != nullptr)
        {
            const auto parameters = automation::getParameters (*current);

            if (const auto it = chosen.find (current->itemID); it != chosen.end() && parameters.contains (it->second.get()))
                parameter = it->second.get();
            else if (const auto automated = automation::getAutomated (*current); ! automated.isEmpty())
                parameter = automated.getFirst();
            else
                parameter = parameters.getFirst();
        }

        lane.setParameter (parameter);
        lane.setLineColour (current != nullptr ? track_colours::getTrackColour (*current, model.indexOf (current))
                                               : theme::colour::accent);
        updateControls();
        lane.repaint();
        repaint();
    }

    void AutomationPanel::setParameter (te::AutomatableParameter* parameter)
    {
        if (track == nullptr || parameter == nullptr || ! automation::getParameters (*track).contains (parameter))
            return;

        chosen[track->itemID] = parameter;
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
        trackLabel.setText (track != nullptr ? track->getName() : juce::String ("No track"), juce::dontSendNotification);
        parameterButton.setButtonText (parameter != nullptr ? automation::getDisplayName (*parameter) : juce::String ("-"));
        parameterButton.setEnabled (parameter != nullptr);
        clearButton.setEnabled (parameter != nullptr && automation::hasPoints (*parameter));

        if (parameter == nullptr)
        {
            infoLabel.setText ("Click a track in the arrangement", juce::dontSendNotification);
            return;
        }

        const auto numPoints = (int) automation::getPoints (*parameter).size();
        const auto numLines = track != nullptr ? automation::getAutomated (*track).size() : 0;

        auto text = numPoints == 0 ? "No line: stays at " + automation::valueToText (*parameter, automation::getValueAt (*parameter, 0.0))
                                   : juce::String (numPoints) + (numPoints == 1 ? " point" : " points");

        if (const auto numSelected = (int) lane.getGestures().getSelection().size(); numSelected > 0)
            text << ", " << numSelected << " selected";
        else if (numPoints > 1)
            text << "\n" << "Ctrl+drag selects points";

        if (numLines > 0)
            text << "\n" << "Lines on this track: " << numLines;

        infoLabel.setText (text, juce::dontSendNotification);
    }

    void AutomationPanel::showParameterMenu()
    {
        if (track == nullptr)
            return;

        const auto parameters = automation::getParameters (*track);
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
        menu.addSectionHeader ("Automate " + track->getName());

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
