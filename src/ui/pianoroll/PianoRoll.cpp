#include "ui/pianoroll/PianoRoll.h"
#include "ui/pianoroll/NotePropertiesPanel.h"
#include "ui/ComputerKeyboard.h"
#include "ui/Theme.h"

namespace harmonia::ui::pianoroll
{
    namespace snap = core::snap;
    namespace property = core::note_property;

    namespace
    {
        constexpr int toolbarHeight = 32;
        constexpr int rulerHeight = 24;
        constexpr int keysWidth = 96;
        constexpr int laneHeight = 110;
        constexpr int scrollBarSize = 12;
        constexpr int toolButtonWidth = 66;
        constexpr int toolRadioGroup = 4001;
        constexpr double zoomStep = 1.25;
        constexpr double minContentBars = 32.0, extraContentBars = 8.0;
    }

    PianoRoll::PianoRoll (PianoRollModel& m, audio::Transport& t)
        : model (m), transport (t)
    {
        setWantsKeyboardFocus (true);
        setOpaque (true);

        for (auto tool : getAllTools())
        {
            auto* button = toolButtons.add (new juce::TextButton (pianoroll::getName (tool) + "  " + juce::String::charToString (getShortcutKey (tool))));
            button->setClickingTogglesState (true);
            button->setRadioGroupId (toolRadioGroup);
            button->setWantsKeyboardFocus (false);
            button->setColour (juce::TextButton::buttonOnColourId, theme::colour::accent.withAlpha (0.55f));
            button->onClick = [this, tool] { model.setTool (tool); };
            addAndMakeVisible (button);
        }

        snapLabel.setText ("Snap", juce::dontSendNotification);
        snapLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        addAndMakeVisible (snapLabel);

        const auto& modes = snap::getAllModes();

        for (size_t i = 0; i < modes.size(); ++i)
            snapBox.addItem (snap::getName (modes[i]), (int) i + 1);

        snapBox.setWantsKeyboardFocus (false);
        snapBox.onChange = [this, &modes]
        {
            if (auto index = snapBox.getSelectedId() - 1; index >= 0)
                model.setSnap (modes[(size_t) index]);
        };
        addAndMakeVisible (snapBox);

        const auto& laneProperties = property::getAll();

        for (size_t i = 0; i < laneProperties.size(); ++i)
            laneBox.addItem (property::getName (laneProperties[i]), (int) i + 1);

        laneBox.setWantsKeyboardFocus (false);
        laneBox.setTooltip ("Value shown in the lower panel (F / Shift+F)");
        laneBox.onChange = [this, &laneProperties]
        {
            if (auto index = laneBox.getSelectedId() - 1; index >= 0)
                model.setLaneProperty (laneProperties[(size_t) index]);
        };
        addAndMakeVisible (laneBox);

        ghostsButton.setWantsKeyboardFocus (false);
        ghostsButton.setTooltip ("Show notes of the other tracks (Alt+V)");
        ghostsButton.onClick = [this] { model.setGhostsVisible (ghostsButton.getToggleState()); };
        addAndMakeVisible (ghostsButton);

        infoLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        infoLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (infoLabel);

        for (auto* bar : { &horizontalScroll, &verticalScroll })
        {
            bar->setAutoHide (false);
            bar->addListener (this);
            addAndMakeVisible (bar);
        }

        grid.onViewChanged = [this] { viewChanged(); };
        keys.onViewChanged = [this] { viewChanged(); };
        grid.getGestures().onNoteDoubleClicked = [this] (te::MidiNote& note)
        {
            const auto v = model.read (note);
            const auto bounds = view.getNoteBounds (v.startBeat, v.lengthBeats, v.pitch).toNearestInt();
            NotePropertiesPanel::show (model, note, grid, bounds);
        };

        ruler.onPositionChosen = [this] (double beat)
        {
            const auto time = model.getEdit().tempoSequence.toTime (te::BeatPosition::fromBeats (beat));
            transport.setPositionSeconds (time.inSeconds());
            updatePlayhead();
        };

        for (auto* c : std::initializer_list<juce::Component*> { &ruler, &keys, &grid, &lane })
            addAndMakeVisible (c);

        model.addListener (this);
        model.setPixelsPerBeat (view.pixelsPerBeat);
        updateControls();
    }

    PianoRoll::~PianoRoll()
    {
        model.removeListener (this);
        model.auditionStop();
    }

    //==============================================================================
    void PianoRoll::paint (juce::Graphics& g)
    {
        g.fillAll (theme::colour::background);

        g.setColour (theme::colour::surface);
        g.fillRect (getLocalBounds().removeFromTop (toolbarHeight + rulerHeight));

        g.setColour (theme::colour::border);
        g.fillRect (0, toolbarHeight - 1, getWidth(), 1);
    }

    void PianoRoll::resized()
    {
        auto area = getLocalBounds();

        auto toolbar = area.removeFromTop (toolbarHeight).reduced (theme::size::padding, 5);

        for (auto* button : toolButtons)
        {
            button->setBounds (toolbar.removeFromLeft (toolButtonWidth));
            toolbar.removeFromLeft (2);
        }

        toolbar.removeFromLeft (theme::size::padding * 2);
        snapLabel.setBounds (toolbar.removeFromLeft (40));
        snapBox.setBounds (toolbar.removeFromLeft (160));
        toolbar.removeFromLeft (theme::size::padding * 2);
        ghostsButton.setBounds (toolbar.removeFromLeft (110));
        infoLabel.setBounds (toolbar);

        auto bottom = area.removeFromBottom (scrollBarSize);
        bottom.removeFromLeft (keysWidth);
        bottom.removeFromRight (scrollBarSize);
        horizontalScroll.setBounds (bottom);

        auto laneArea = area.removeFromBottom (laneHeight);
        laneBox.setBounds (laneArea.removeFromLeft (keysWidth).reduced (3, 6).removeFromTop (theme::size::buttonHeight));
        laneArea.removeFromRight (scrollBarSize);
        lane.setBounds (laneArea);

        auto rulerRow = area.removeFromTop (rulerHeight);
        rulerRow.removeFromLeft (keysWidth);
        rulerRow.removeFromRight (scrollBarSize);
        ruler.setBounds (rulerRow);

        verticalScroll.setBounds (area.removeFromRight (scrollBarSize));
        keys.setBounds (area.removeFromLeft (keysWidth));
        grid.setBounds (area);

        view.width = (float) grid.getWidth();
        view.height = (float) grid.getHeight();
        updateContentLength();

        if (showClipWhenSized && view.height > 0.0f)
            showClip();

        viewChanged();
    }

    void PianoRoll::showClip()
    {
        // Centring needs the real size; do it once the piano roll is laid out.
        showClipWhenSized = view.height <= 0.0f;

        if (showClipWhenSized)
            return;

        view.leftBeat = std::max (0.0, model.getClipStartBeat());

        const auto& notes = model.getNotes();

        if (notes.empty())
        {
            view.centreOnPitch (66);
        }
        else
        {
            int lowest = 127, highest = 0;

            for (const auto& d : notes)
            {
                lowest = std::min (lowest, d.values.pitch);
                highest = std::max (highest, d.values.pitch);
            }

            view.centreOnPitch ((lowest + highest) / 2);
        }

        viewChanged();
    }

    //==============================================================================
    bool PianoRoll::keyPressed (const juce::KeyPress& key)
    {
        if (! letterShortcutsEnabled && keys::isTypingKey (key))
            return false;

        const auto mods = key.getModifiers();
        const auto code = key.getKeyCode();
        const auto letter = keys::getLetter (key);   // the same keys with any layout (Russian too)
        const auto isUp = code == juce::KeyPress::upKey, isDown = code == juce::KeyPress::downKey;
        const auto isLeft = code == juce::KeyPress::leftKey, isRight = code == juce::KeyPress::rightKey;

        if (mods.isCommandDown())
        {
            if (mods.isAltDown() && letter == 'Z')       { model.undo(); return true; }
            if (mods.isAltDown())                        return false;

            if (isUp || isDown)                          { model.transpose (isUp ? 12 : -12); return true; }

            switch (letter)
            {
                case 'A': model.selectAll(); return true;
                case 'D': model.deselectAll(); return true;
                case 'C': model.copy(); return true;
                case 'X': model.cut(); return true;
                case 'V': model.paste (model.getPastePosition (view.leftBeat)); return true;
                case 'B': model.duplicate(); return true;
                case 'Q': model.quantize (false); return true;
                case 'Y': model.redo(); return true;
                case 'Z': if (mods.isShiftDown()) model.redo(); else model.undo(); return true;
                default:  return false;
            }
        }

        if (mods.isAltDown())
        {
            if (letter == 'V')
            {
                model.setGhostsVisible (! model.areGhostsVisible());
                return true;
            }

            return false;
        }

        if (mods.isShiftDown())
        {
            if (isLeft || isRight)                       { model.nudge (isRight ? 1 : -1); return true; }
            if (isUp || isDown)                          { model.transpose (isUp ? 1 : -1); return true; }
            if (letter == 'Q')                           { model.quantize (true); return true; }
            if (letter == 'F')                           { model.cycleLaneProperty (-1); return true; }
            return false;
        }

        if (code == juce::KeyPress::deleteKey)           { model.deleteSelectedOrAll(); return true; }
        if (code == juce::KeyPress::pageUpKey)           { zoomAroundMouse (zoomStep); return true; }
        if (code == juce::KeyPress::pageDownKey)         { zoomAroundMouse (1.0 / zoomStep); return true; }
        if (code == juce::KeyPress::escapeKey)           { model.deselectAll(); return true; }
        if (letter == 'F')                               { model.cycleLaneProperty (1); return true; }

        for (auto tool : getAllTools())
        {
            if (letter == getShortcutKey (tool))
            {
                model.setTool (tool);
                return true;
            }
        }

        return false;
    }

    void PianoRoll::zoomAroundMouse (double factor)
    {
        const auto mouse = grid.getMouseXYRelative();
        const auto anchor = grid.getLocalBounds().contains (mouse) ? (float) mouse.x : view.width * 0.5f;
        view.zoomHorizontally (factor, anchor);
        viewChanged();
    }

    //==============================================================================
    void PianoRoll::pianoRollChanged()
    {
        updateContentLength();
        updateControls();
        updateScrollBars();
        repaintAll();
    }

    void PianoRoll::scrollBarMoved (juce::ScrollBar* bar, double newRangeStart)
    {
        if (bar == &horizontalScroll)
            view.leftBeat = newRangeStart;
        else
            view.topRow = (float) newRangeStart;

        viewChanged();
    }

    void PianoRoll::viewChanged()
    {
        view.clamp();
        model.setPixelsPerBeat (view.pixelsPerBeat);
        updateScrollBars();
        repaintAll();
    }

    void PianoRoll::updateContentLength()
    {
        const auto beatsPerBar = model.getBeatsPerBar();
        auto end = model.getClipEndBeat();

        for (const auto& d : model.getNotes())
            end = std::max (end, d.values.getEndBeat());

        view.contentBeats = std::max ({ end + extraContentBars * beatsPerBar,
                                        minContentBars * beatsPerBar,
                                        view.getRightBeat() });
    }

    void PianoRoll::updateScrollBars()
    {
        horizontalScroll.setRangeLimits (0.0, view.contentBeats, juce::dontSendNotification);
        horizontalScroll.setCurrentRange (view.leftBeat, view.width / view.pixelsPerBeat, juce::dontSendNotification);
        horizontalScroll.setSingleStepSize (1.0);

        verticalScroll.setRangeLimits (0.0, View::numPitches, juce::dontSendNotification);
        verticalScroll.setCurrentRange (view.topRow, view.getVisibleRows(), juce::dontSendNotification);
        verticalScroll.setSingleStepSize (1.0);
    }

    void PianoRoll::updateControls()
    {
        const auto& tools = getAllTools();

        for (size_t i = 0; i < tools.size(); ++i)
            toolButtons[(int) i]->setToggleState (tools[i] == model.getTool(), juce::dontSendNotification);

        const auto& modes = snap::getAllModes();
        const auto modeIndex = std::find (modes.begin(), modes.end(), model.getSnap()) - modes.begin();
        snapBox.setSelectedId ((int) modeIndex + 1, juce::dontSendNotification);

        const auto& laneProperties = property::getAll();
        const auto propertyIndex = std::find (laneProperties.begin(), laneProperties.end(), model.getLaneProperty()) - laneProperties.begin();
        laneBox.setSelectedId ((int) propertyIndex + 1, juce::dontSendNotification);

        ghostsButton.setToggleState (model.areGhostsVisible(), juce::dontSendNotification);

        juce::String info;

        if (auto* clip = model.getClip())
        {
            const auto bars = (model.getClipEndBeat() - model.getClipStartBeat()) / model.getBeatsPerBar();
            const auto count = (int) model.getNotes().size();
            const auto selected = model.getNumSelected();

            info << clip->getName() << juce::String::fromUTF8 ("  \xc2\xb7  ")
                 << juce::String (bars, bars == std::floor (bars) ? 0 : 2) << (bars == 1.0 ? " bar" : " bars")
                 << juce::String::fromUTF8 ("  \xc2\xb7  ") << count << (count == 1 ? " note" : " notes");

            if (selected > 0)
                info << " (" << selected << " selected)";
        }

        infoLabel.setText (info, juce::dontSendNotification);
    }

    void PianoRoll::updatePlayhead()
    {
        const auto playing = transport.isPlaying();
        const auto seconds = transport.getPositionSeconds();
        const auto beat = model.getEdit().tempoSequence.toBeats (te::TimePosition::fromSeconds (seconds)).inBeats();

        // While playing, turn the page when the playhead runs off the view (or loops back out of it).
        // If the user has scrolled elsewhere, the view stays put.
        const auto isVisible = [this] (double b) { return b >= view.leftBeat && b <= view.getRightBeat(); };

        if (playing && ! isVisible (beat) && lastPlayheadBeat.has_value()
            && (isVisible (*lastPlayheadBeat) || beat < *lastPlayheadBeat))
        {
            view.leftBeat = beat;
            viewChanged();
        }

        lastPlayheadBeat = beat;
        grid.setPlayheadBeat (beat);
        ruler.setPlayheadBeat (beat);
    }

    void PianoRoll::repaintAll()
    {
        grid.repaint();
        keys.repaint();
        ruler.repaint();
        lane.repaint();
    }
}
