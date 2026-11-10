#include "ui/pianoroll/PianoRoll.h"
#include "ui/pianoroll/NotePropertiesPanel.h"
#include "ui/pianoroll/NoteToolWindow.h"
#include "ui/ComputerKeyboard.h"
#include "ui/Theme.h"

namespace harmonia::ui::pianoroll
{
    namespace snap = core::snap;
    namespace property = core::note_property;

    namespace
    {
        constexpr int toolbarHeight = 32;
        constexpr int rulerHeight = 24 + (int) TimeRuler::markerLaneHeight;
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

        // The note values (ids from 1), then the clip's controllers (ids from firstControllerId).
        const auto& laneProperties = property::getAll();
        const auto& laneControllers = core::controllers::getAll();

        for (size_t i = 0; i < laneProperties.size(); ++i)
            laneBox.addItem (property::getName (laneProperties[i]), (int) i + 1);

        laneBox.addSeparator();

        for (size_t i = 0; i < laneControllers.size(); ++i)
            laneBox.addItem (core::controllers::getName (laneControllers[i]), firstControllerId + (int) i);

        laneBox.setWantsKeyboardFocus (false);
        laneBox.setTooltip ("Shown in the lower panel: a value of each note, or the pitch bend, mod wheel or "
                            "sustain pedal of the clip (F / Shift+F)");
        laneBox.onChange = [this, &laneProperties, &laneControllers]
        {
            const auto id = laneBox.getSelectedId();

            if (id >= firstControllerId)
                model.setLaneController (laneControllers[(size_t) (id - firstControllerId)]);
            else if (id > 0)
                model.setLaneProperty (laneProperties[(size_t) (id - 1)]);
        };
        addAndMakeVisible (laneBox);

        toolsButton.setWantsKeyboardFocus (false);
        toolsButton.setTooltip ("Commands for the selected notes (or all of them)");
        toolsButton.onClick = [this] { showToolsMenu(); };
        addAndMakeVisible (toolsButton);

        scaleButton.setWantsKeyboardFocus (false);
        scaleButton.setTooltip ("The song's scale: its notes are lit, and new or moved notes can snap to it");
        scaleButton.setColour (juce::TextButton::buttonOnColourId, theme::colour::accent.withAlpha (0.35f));
        scaleButton.onClick = [this]
        {
            createScaleMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&scaleButton));
        };
        addAndMakeVisible (scaleButton);

        chordButton.setWantsKeyboardFocus (false);
        chordButton.setTooltip ("A click with Draw puts down this chord (Chord stamp)");
        chordButton.setColour (juce::TextButton::buttonOnColourId, theme::colour::accent.withAlpha (0.35f));
        chordButton.onClick = [this]
        {
            createChordMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&chordButton));
        };
        addAndMakeVisible (chordButton);

        ghostsButton.setWantsKeyboardFocus (false);
        ghostsButton.setTooltip ("Show notes of the other tracks (Alt+V)");
        ghostsButton.onClick = [this] { model.setGhostsVisible (ghostsButton.getToggleState()); };
        addAndMakeVisible (ghostsButton);

        stepsButton.setWantsKeyboardFocus (false);
        stepsButton.setTooltip ("The step sequencer: the same notes as a grid of drums and steps, a click puts a hit (Alt+T)");
        stepsButton.onClick = [this]
        {
            if (onShowSteps != nullptr)
                onShowSteps();
        };
        addAndMakeVisible (stepsButton);

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
        keys.onMessage = [this] (const juce::String& text)
        {
            if (onMessage != nullptr)
                onMessage (text);
        };
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
        snapBox.setBounds (toolbar.removeFromLeft (130));
        toolbar.removeFromLeft (theme::size::padding * 2);
        toolsButton.setBounds (toolbar.removeFromLeft (64));
        toolbar.removeFromLeft (theme::size::padding);
        scaleButton.setBounds (toolbar.removeFromLeft (150));
        toolbar.removeFromLeft (theme::size::padding);
        chordButton.setBounds (toolbar.removeFromLeft (140));
        toolbar.removeFromLeft (theme::size::padding);
        ghostsButton.setBounds (toolbar.removeFromLeft (110));
        stepsButton.setBounds (toolbar.removeFromLeft (56));
        toolbar.removeFromLeft (8);
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
            if (isLeft || isRight)                       { if (mods.isShiftDown()) model.rotate (isRight ? 1 : -1); return mods.isShiftDown(); }
            if (code == juce::KeyPress::insertKey)       { model.insertSpace(); return true; }
            if (code == juce::KeyPress::deleteKey)       { model.deleteSpace(); return true; }

            switch (letter)
            {
                case 'L': if (mods.isShiftDown()) return false; model.legato(); return true;
                case 'U': model.chop(); return true;
                case 'G': model.glue(); return true;
                case 'A': if (mods.isShiftDown()) return false; model.selectAll(); return true;
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

            if (letter == 'M')                           { model.setMuted (! mods.isShiftDown()); return true; }

            for (auto tool : getAllNoteTools())
            {
                if (! mods.isShiftDown() && getShortcutDescription (tool) == juce::String ("Alt+") + juce::String::charToString (letter))
                {
                    openNoteTool (tool);
                    return true;
                }
            }

            if (isLeft || isRight)                       { model.nudgeFine (isRight ? 1 : -1); return true; }
            return false;
        }

        if (mods.isShiftDown())
        {
            if (isLeft || isRight)                       { model.nudge (isRight ? 1 : -1); return true; }
            if (isUp || isDown)                          { model.transpose (isUp ? 1 : -1); return true; }
            if (letter == 'Q')                           { model.quantize (true); return true; }
            if (letter == 'F')                           { model.cycleLaneProperty (-1); return true; }
            if (letter == 'I')                           { model.invertSelection(); return true; }
            if (letter == 'D')                           { model.discardLengths(); return true; }
            return false;
        }

        if (code == juce::KeyPress::deleteKey)           { model.deleteSelectedOrAll(); return true; }
        if (code == juce::KeyPress::pageUpKey)           { zoomAroundMouse (zoomStep); return true; }
        if (code == juce::KeyPress::pageDownKey)         { zoomAroundMouse (1.0 / zoomStep); return true; }
        if (code == juce::KeyPress::escapeKey)           { model.deselectAll(); return true; }
        if (letter == 'F')                               { model.cycleLaneProperty (1); return true; }
        if (letter == 'Z')                               { zoomToFit(); return true; }

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

    juce::PopupMenu PianoRoll::createToolsMenu()
    {
        juce::PopupMenu menu;

        const auto add = [&menu] (const juce::String& name, const juce::String& keys, std::function<void()> action, bool enabled = true)
        {
            juce::PopupMenu::Item item (name);
            item.shortcutKeyDescription = keys;
            item.action = std::move (action);
            item.isEnabled = enabled;
            menu.addItem (std::move (item));
        };

        const auto hasTimeSelection = model.getTimeSelection().has_value();

        menu.addSectionHeader (model.hasSelection() ? "Selected notes" : "All notes (nothing selected)");
        add ("Quantize", "Ctrl+Q", [this] { model.quantize (false); });
        add ("Quantize start times", "Shift+Q", [this] { model.quantize (true); });
        add ("Quick legato", "Ctrl+L", [this] { model.legato(); });
        add ("Quick chop", "Ctrl+U", [this] { model.chop(); });
        add ("Glue", "Ctrl+G", [this] { model.glue(); });
        add ("Discard lengths", "Shift+D", [this] { model.discardLengths(); });
        menu.addSeparator();
        menu.addSeparator();

        for (auto tool : getAllNoteTools())
            add (pianoroll::getName (tool) + "...", getShortcutDescription (tool), [this, tool] { openNoteTool (tool); });

        menu.addSeparator();
        add ("Duplicate", "Ctrl+B", [this] { model.duplicate(); });
        add ("Rotate left", "Ctrl+Shift+Left", [this] { model.rotate (-1); });
        add ("Rotate right", "Ctrl+Shift+Right", [this] { model.rotate (1); });
        add ("Move a little left", "Alt+Left", [this] { model.nudgeFine (-1); });
        add ("Move a little right", "Alt+Right", [this] { model.nudgeFine (1); });
        menu.addSeparator();
        add ("Mute", "Alt+M", [this] { model.setMuted (true); });
        add ("Unmute", "Alt+Shift+M", [this] { model.setMuted (false); });
        add ("Invert selection", "Shift+I", [this] { model.invertSelection(); });
        add ("Fit to scale", {}, [this] { model.fitToScale(); }, ! model.getScale().isChromatic());
        menu.addSeparator();
        menu.addSectionHeader ("Time selection (Ctrl+drag on the ruler)");
        add ("Insert space", "Ctrl+Insert", [this] { model.insertSpace(); }, hasTimeSelection);
        add ("Delete space", "Ctrl+Delete", [this] { model.deleteSpace(); }, hasTimeSelection);
        menu.addSeparator();
        add ("Zoom to fit", "Z", [this] { zoomToFit(); }, model.getClip() != nullptr);
        return menu;
    }

    juce::PopupMenu PianoRoll::createScaleMenu()
    {
        namespace scales = core::scales;
        const auto scale = model.getScale();
        juce::PopupMenu menu, roots;

        for (int root = 0; root < 12; ++root)
            roots.addItem (scales::getRootName (root), true, root == scale.root,
                           [this, root] { auto s = model.getScale(); s.root = root; model.setScale (s); });

        menu.addSubMenu ("Root: " + scales::getRootName (scale.root), roots, ! scale.isChromatic());
        menu.addSeparator();

        for (auto type : scales::getAllTypes())
            menu.addItem (scales::getName (type), true, type == scale.type,
                          [this, type] { auto s = model.getScale(); s.type = type; model.setScale (s); });

        menu.addSeparator();
        menu.addItem ("Snap notes to the scale", ! scale.isChromatic(), model.isSnappingToScale(),
                      [this] { model.setSnapToScale (! model.isSnappingToScale()); });
        menu.addItem ("Fit notes to the scale", ! scale.isChromatic(), false, [this] { model.fitToScale(); });
        menu.addItem ("Detect from the notes", ! model.getNotes().empty(), false, [this] { model.detectScale(); });
        menu.addSeparator();

        const auto drums = model.areDrumNamesShown();
        menu.addItem ("Drum names on the keys (General MIDI)", model.getClip() != nullptr, drums,
                      [this, drums] { model.setDrumNamesShown (! drums); });

        const auto chordNames = model.areChordNamesShown();
        menu.addItem ("Chord names on the ruler", true, chordNames, [this, chordNames] { model.setChordNamesShown (! chordNames); });
        return menu;
    }

    juce::PopupMenu PianoRoll::createChordMenu()
    {
        namespace chords = core::chords;
        const auto stamp = model.getChordStamp();
        juce::PopupMenu menu;

        menu.addItem ("Off (single notes)", true, ! stamp.has_value(), [this] { model.setChordStamp (std::nullopt); });
        menu.addSeparator();
        menu.addSectionHeader (model.getScale().isChromatic() ? "Chords (set a scale for scale chords)" : "Chords");

        for (auto type : chords::getAllTypes())
            menu.addItem (chords::getName (type), true, stamp == type, [this, type]
            {
                model.setChordStamp (type);

                if (model.getTool() != Tool::draw)
                    model.setTool (Tool::draw);
            });

        return menu;
    }

    void PianoRoll::openNoteTool (NoteTool tool)
    {
        if (auto window = NoteToolWindow::create (model, tool))
        {
            if (onShowNoteTool != nullptr)
                onShowNoteTool (std::move (window));
            else
                NoteToolWindow::show (std::move (window), *this, toolsButton.getBounds());
        }
        else if (onMessage != nullptr)
        {
            onMessage (NoteToolWindow::getUnavailableReason (model, tool));
        }
    }

    void PianoRoll::zoomToFit()
    {
        if (model.getClip() == nullptr || view.width <= 0.0f || view.height <= 0.0f)
            return;

        // What to show: the selected notes, or else the clip and every note.
        const auto& notes = model.getNotes();
        const auto onlySelected = model.hasSelection();
        auto start = onlySelected ? std::numeric_limits<double>::max() : model.getClipStartBeat();
        auto end = onlySelected ? std::numeric_limits<double>::lowest() : model.getClipEndBeat();
        int lowest = View::numPitches, highest = -1;

        for (const auto& d : notes)
        {
            if (onlySelected && ! d.selected)
                continue;

            start = std::min (start, d.values.startBeat);
            end = std::max (end, d.values.getEndBeat());
            lowest = std::min (lowest, d.values.pitch);
            highest = std::max (highest, d.values.pitch);
        }

        if (start >= end)
            return;

        // Across: the range and a little room either side.
        const auto length = std::max (end - start, 0.25);
        const auto margin = length * 0.03;
        view.pixelsPerBeat = juce::jlimit (View::minPixelsPerBeat, View::maxPixelsPerBeat, (double) view.width / (length + margin * 2.0));
        view.leftBeat = std::max (0.0, start - margin);

        // Up and down: the notes' pitches with two rows of room (the height stays if there are none).
        if (highest >= 0)
        {
            const auto rows = (float) (highest - lowest + 1 + 4);
            view.rowHeight = juce::jlimit (View::minRowHeight, View::maxRowHeight, view.height / rows);
            view.centreOnPitch ((lowest + highest + 1) / 2);
        }

        updateContentLength();
        viewChanged();
    }

    void PianoRoll::showToolsMenu()
    {
        createToolsMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&toolsButton));
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

        if (const auto controller = model.getLaneController())
        {
            const auto& laneControllers = core::controllers::getAll();
            const auto index = std::find (laneControllers.begin(), laneControllers.end(), *controller) - laneControllers.begin();
            laneBox.setSelectedId (firstControllerId + (int) index, juce::dontSendNotification);
        }
        else
        {
            const auto& laneProperties = property::getAll();
            const auto propertyIndex = std::find (laneProperties.begin(), laneProperties.end(), model.getLaneProperty()) - laneProperties.begin();
            laneBox.setSelectedId ((int) propertyIndex + 1, juce::dontSendNotification);
        }

        ghostsButton.setToggleState (model.areGhostsVisible(), juce::dontSendNotification);

        const auto scale = model.getScale();
        scaleButton.setButtonText ("Scale: " + (scale.isChromatic() ? juce::String ("off") : scale.getName())
                                   + (model.isSnappingToScale() && ! scale.isChromatic() ? " (snap)" : ""));
        scaleButton.setToggleState (! scale.isChromatic(), juce::dontSendNotification);

        const auto stamp = model.getChordStamp();
        chordButton.setButtonText ("Chord: " + (stamp.has_value() ? core::chords::getName (*stamp) : juce::String ("off")));
        chordButton.setToggleState (stamp.has_value(), juce::dontSendNotification);

        juce::String info;

        if (auto* clip = model.getClip())
        {
            const auto bars = (model.getClipEndBeat() - model.getClipStartBeat()) / model.getBeatsPerBar();
            const auto count = (int) model.getNotes().size();
            const auto selected = model.getNumSelected();

            info << clip->getName() << juce::String::fromUTF8 ("  \xc2\xb7  ")
                 << juce::String (bars, bars == std::floor (bars) ? 0 : 2) << (bars == 1.0 ? " bar" : " bars")
                 << juce::String::fromUTF8 ("  \xc2\xb7  ") << count << (count == 1 ? " note" : " notes");

            juce::String chord;

            if (selected > 1)
            {
                std::vector<int> pitches;

                for (auto* note : model.getSelectedNotes())
                    pitches.push_back (note->getNoteNumber());

                chord = core::chords::nameOf (pitches);
            }

            // The chord the selected notes make says more than the clip's size (and the space is short).
            if (chord.isNotEmpty())
                info = "Chord " + chord + juce::String::fromUTF8 ("  \xc2\xb7  ") + juce::String (selected) + " selected";
            else if (selected > 0)
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
