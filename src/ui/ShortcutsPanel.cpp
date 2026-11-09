#include "ui/ShortcutsPanel.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    namespace
    {
        constexpr int rowHeight = 22, sectionHeight = 34, keysWidth = 230, panelWidth = 860, panelHeight = 640;

        bool matches (const ShortcutsPanel::Row& row, const juce::String& filter)
        {
            return filter.isEmpty() || row.keys.containsIgnoreCase (filter) || row.what.containsIgnoreCase (filter);
        }
    }

    const std::vector<ShortcutsPanel::Section>& ShortcutsPanel::getSections()
    {
        static const std::vector<Section> sections {
            { "Everywhere", {
                { "Space", "Play / stop" },
                { "Home", "Back to the start (of the song, or of the clip in Clip mode)" },
                { "L", "Clip mode (loop the open clip) / Song mode" },
                { "R", "Arm recording (when the computer keyboard isn't a piano)" },
                { "Record with a loop region (Song mode)", "Audio is recorded round the loop: a take each time; choose one in the clip's menu (Takes)" },
                { "Ctrl+M", "Metronome on / off" },
                { "Ctrl+Shift+A", "Write automation on / off: knobs and faders moved while the song plays go into their lines" },
                { "Ctrl+Shift+L", "MIDI Learn on / off: move a knob or fader, then a control on your MIDI keyboard" },
                { "Knob, fader, slider: right button", "MIDI Learn for it alone, or forget its MIDI control" },
                { "Ctrl+,", "Settings: the sound card, MIDI keyboards, plugin folders" },
                { "Ctrl+T", "Computer keyboard as a piano on / off (Up / Down: its octave)" },
                { "Ctrl+Z / Ctrl+Y (Ctrl+Shift+Z)", "Undo / redo" },
                { "Ctrl+N / Ctrl+O", "New project / open a project" },
                { "Ctrl+S / Ctrl+Shift+S", "Save / save as" },
                { "Ctrl+R", "Export the song: WAV, FLAC or MP3 (the File menu also exports the loop region)" },
                { "F6 / F7 / F8 / F9", "Sounds / piano roll (audio clip on an audio track) / automation / mixer below" },
                { "Ctrl+Left / Ctrl+Right", "Play from the previous / next marker" },
                { "Bar numbers: right button > Tempo change here", "The song plays at another tempo from there (an orange flag: drag it, double-click to set it)" },
                { "F1", "This list" },
                { "Menu bar", "Every command, with its key written next to it: File, Edit, View, Track, Transport, Help" } } },

            { "Arrangement (after a click in it)", {
                { "Click a clip", "Open it in the piano roll and select it" },
                { "Drag a clip", "Move it (to another track too); Alt: without the grid" },
                { "Shift+drag / Ctrl+drag", "A linked copy / an independent copy" },
                { "Drag a clip's edge", "Hide or show its beginning or end (a looping clip repeats)" },
                { "Right button", "Delete the clips under the mouse" },
                { "Double-click empty space", "A new one-bar clip" },
                { "Clip's corner mark", "Its menu: rename, colour, loop, copies, split; sounds: reverse, normalize..." },
                { "Del", "Delete the selected clips" },
                { "Ctrl+B / Ctrl+Shift+B", "Repeat the selection right after it: linked / independent copies" },
                { "Ctrl+A / Ctrl+D, Esc", "Select all / deselect" },
                { "Ctrl+C / Ctrl+X / Ctrl+V", "Copy / cut / paste at the playhead" },
                { "Ctrl+E", "Split the clips at the playhead" },
                { "Z", "Zoom to fit: the whole song, or the selected clips" },
                { "Click the ruler", "Play from there (Song mode)" },
                { "Ctrl+drag on the ruler", "Loop region for Song mode (a click without dragging clears it)" },
                { "Ctrl+Insert / Ctrl+Delete", "Empty bars before the loop region / delete its bars, on every track" },
                { "Right button on the ruler", "Markers; duplicate, insert or delete bars on every track" },
                { "Markers strip", "+ Marker, double-click: add; drag: move; double-click a marker: rename" },
                { "Track header: right button", "Rename, colour, duplicate, move, render to audio, delete" },
                { "Drag a track header", "Move the track up or down" } } },

            { "Piano roll: tools", {
                { "P", "Draw: click for a note, drag to move, its right edge for the length" },
                { "B", "Paint: drag to paint a row of notes" },
                { "D", "Delete: click or drag over notes" },
                { "T", "Mute: click or drag to mute / unmute notes" },
                { "C", "Slice: draw a line through notes to cut them" },
                { "E", "Select: a rectangle selects, dragging moves the selection" } } },

            { "Piano roll: mouse", {
                { "Shift+drag a note", "A copy" },
                { "Shift+drag on empty space", "A note as long as the drag (Draw)" },
                { "Ctrl+rectangle", "Select in any tool (Ctrl+Shift: add)" },
                { "Alt while dragging", "Without the grid" },
                { "Double-click a note", "Its properties (all selected ones)" },
                { "Wheel / Shift+wheel / Ctrl+wheel", "Scroll up-down / left-right / zoom" },
                { "Middle button", "Move the view" },
                { "Keyboard: right button", "Drum names on the keys (General MIDI) on / off" },
                { "Select notes", "The chord they make shows top right (Am7, C/E...)" },
                { "Ruler, top line", "The chords the notes make (Scale menu: on / off)" },
                { "Ctrl+drag on the ruler", "Select time (for rotate, insert / delete space)" },
                { "Click a marker on the ruler", "Play from it (markers are added in the arrangement)" },
                { "Drag the bracket on the ruler", "A looping clip: make its loop longer or shorter" },
                { "Lower panel: drag", "Set velocity (or the property chosen); Alt: reset" },
                { "Lower panel, pitch bend / mod wheel / pedal", "Drag: draw the line; right button: erase; Alt: back to rest" } } },

            { "Piano roll: keys (on the selected notes, or all)", {
                { "Del", "Delete" },
                { "Ctrl+A / Ctrl+D", "Select all / deselect" },
                { "Ctrl+C / Ctrl+X / Ctrl+V", "Copy / cut / paste" },
                { "Ctrl+B", "Repeat right after" },
                { "Ctrl+Q / Shift+Q", "Quantize / quantize the starts only" },
                { "Shift+Up / Shift+Down", "A semitone up / down (a scale step with Snap to the scale)" },
                { "Ctrl+Up / Ctrl+Down", "An octave up / down" },
                { "Shift+Left / Shift+Right", "A grid step left / right" },
                { "Alt+Left / Alt+Right", "A little left / right, off the grid" },
                { "Ctrl+Shift+Left / Right", "Rotate inside the bar (or the selected time)" },
                { "Ctrl+L", "Legato: each note up to the next" },
                { "Ctrl+U", "Chop into grid steps" },
                { "Ctrl+G", "Glue touching notes of one pitch" },
                { "Shift+D", "Every note one grid step long" },
                { "Alt+M / Alt+Shift+M", "Mute / unmute" },
                { "Shift+I", "Invert the selection" },
                { "Ctrl+Insert / Ctrl+Delete", "Insert / delete the selected time" },
                { "F / Shift+F", "Lower panel: next / previous note value, then pitch bend, mod wheel, pedal" },
                { "PgUp / PgDn", "Zoom in / out" },
                { "Z", "Zoom to fit: the selected notes, or the whole clip" },
                { "Alt+V", "Show / hide the other tracks' notes" } } },

            { "Piano roll: tools with a window", {
                { "Alt+S", "Strum: chord notes one after another" },
                { "Alt+A", "Arpeggiate chords" },
                { "Alt+R", "Humanize: a little random timing, velocity, length" },
                { "Alt+X", "Scale levels: velocity (or another property) all at once" },
                { "Alt+P", "Chord progression: writes chords, even into an empty clip" },
                { "Alt+D", "Drum pattern: writes a groove (rock, house, trap...); a new track gets the built-in drums" },
                { "Alt+B", "Bass line: a bass under the chords the other tracks play" },
                { "Alt+L", "Melody: a tune over the chords the other tracks play (Variation: another one)" },
                { "In the window", "Space: listen; Enter: accept; Esc: cancel" } } },

            { "Sounds (F6)", {
                { "Click a sound", "Listen (click it again: stop); MIDI files too" },
                { "Search box", "Finds sounds here and in the subfolders" },
                { "Up / Down", "The next / previous sound, and listen to it" },
                { "Double-click, Enter", "Put it into the song: on the current track, at the playhead" },
                { "Drag onto a track", "Put it there (Ctrl+click or Shift+click first: several)" },
                { "Click a folder / Backspace", "Open it / the folder above" },
                { "+ Folder", "Add a folder with your sounds to the places (right button: remove)" },
                { "Esc", "Stop listening" } } },

            { "Mixer (F9)", {
                { "+ on a strip", "Add an effect: built-in or a plugin" },
                { "Click an effect", "Open its window" },
                { "Green dot on an effect", "Bypass it / turn it back on" },
                { "Effect: right button", "Open, bypass, move up / down, remove" },
                { "Drag an effect", "Move it in its chain or to another track, group, return or the master (Ctrl: a copy)" },
                { "+ on a strip > Chains", "Save all the strip's effects as a chain; add a saved chain or put it in their place" },
                { "Fader: double-click", "0 dB" },
                { "Pan knob: double-click", "Centre" },
                { "Meter's red line: click", "Forget the overload" } } }
        };

        return sections;
    }

    //==============================================================================
    class ShortcutsPanel::List final : public juce::Component
    {
    public:
        void setFilter (const juce::String& f)
        {
            filter = f.trim();
            setSize (getWidth(), getContentHeight());
            repaint();
        }

        int getNumShownRows() const
        {
            int count = 0;

            for (const auto& section : getSections())
                for (const auto& row : section.rows)
                    count += matches (row, filter) ? 1 : 0;

            return count;
        }

        int getContentHeight() const
        {
            int height = 0;

            for (const auto& section : getSections())
            {
                const auto shown = (int) std::count_if (section.rows.begin(), section.rows.end(),
                                                        [this] (const Row& r) { return matches (r, filter); });

                if (shown > 0)
                    height += sectionHeight + shown * rowHeight;
            }

            return std::max (height, rowHeight * 2);
        }

        void paint (juce::Graphics& g) override
        {
            auto y = 0;
            const auto width = getWidth();

            if (getNumShownRows() == 0)
            {
                g.setColour (theme::colour::textSecondary);
                g.setFont (juce::FontOptions (theme::size::fontSize));
                g.drawText ("Nothing found", 0, 0, width, rowHeight * 2, juce::Justification::centred);
                return;
            }

            for (const auto& section : getSections())
            {
                std::vector<const Row*> shown;

                for (const auto& row : section.rows)
                    if (matches (row, filter))
                        shown.push_back (&row);

                if (shown.empty())
                    continue;

                g.setColour (theme::colour::accent);
                g.setFont (juce::FontOptions (theme::size::fontSize + 1.0f, juce::Font::bold));
                g.drawText (section.title, 0, y + 10, width, sectionHeight - 12, juce::Justification::bottomLeft);
                y += sectionHeight;

                for (size_t i = 0; i < shown.size(); ++i)
                {
                    if (i % 2 == 0)
                    {
                        g.setColour (theme::colour::surfaceRaised.withAlpha (0.5f));
                        g.fillRect (0, y, width, rowHeight);
                    }

                    g.setFont (juce::FontOptions (theme::size::fontSize, juce::Font::bold));
                    g.setColour (theme::colour::textPrimary);
                    g.drawText (shown[i]->keys, 8, y, keysWidth - 12, rowHeight, juce::Justification::centredLeft, true);
                    g.setFont (juce::FontOptions (theme::size::fontSize));
                    g.setColour (theme::colour::textSecondary);
                    g.drawText (shown[i]->what, keysWidth, y, width - keysWidth - 8, rowHeight, juce::Justification::centredLeft, true);
                    y += rowHeight;
                }
            }
        }

    private:
        juce::String filter;
    };

    //==============================================================================
    ShortcutsPanel::ShortcutsPanel()
        : list (std::make_unique<List>())
    {
        setWantsKeyboardFocus (true);

        title.setText ("Keyboard shortcuts and mouse", juce::dontSendNotification);
        title.setFont (juce::FontOptions (theme::size::fontSize + 4.0f, juce::Font::bold));
        addAndMakeVisible (title);

        search.setTextToShowWhenEmpty ("Search (e.g. copy, loop, Ctrl+B)", theme::colour::textSecondary);
        search.onTextChange = [this] { setFilter (search.getText()); };
        search.onEscapeKey = [this] { if (onClose != nullptr) onClose(); };
        addAndMakeVisible (search);

        closeButton.onClick = [this] { if (onClose != nullptr) onClose(); };
        addAndMakeVisible (closeButton);

        viewport.setViewedComponent (list.get(), false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
    }

    ShortcutsPanel::~ShortcutsPanel() = default;

    void ShortcutsPanel::setFilter (const juce::String& text)
    {
        if (search.getText() != text)
            search.setText (text, juce::dontSendNotification);

        list->setFilter (text);
        viewport.setViewPosition (0, 0);
    }

    int ShortcutsPanel::getNumShownRows() const
    {
        return list->getNumShownRows();
    }

    juce::Rectangle<int> ShortcutsPanel::getPanelArea() const
    {
        return getLocalBounds().withSizeKeepingCentre (std::min (panelWidth, getWidth() - 40),
                                                       std::min (panelHeight, getHeight() - 40));
    }

    void ShortcutsPanel::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colours::black.withAlpha (0.55f));

        const auto panel = getPanelArea().toFloat();
        g.setColour (theme::colour::surface);
        g.fillRoundedRectangle (panel, 6.0f);
        g.setColour (theme::colour::border);
        g.drawRoundedRectangle (panel, 6.0f, 1.0f);
    }

    void ShortcutsPanel::resized()
    {
        auto area = getPanelArea().reduced (16);
        auto top = area.removeFromTop (30);
        closeButton.setBounds (top.removeFromRight (80).withSizeKeepingCentre (80, theme::size::buttonHeight));
        top.removeFromRight (8);
        search.setBounds (top.removeFromRight (std::min (320, top.getWidth() / 2)).withSizeKeepingCentre (std::min (320, top.getWidth() / 2), 26));
        title.setBounds (top);

        area.removeFromTop (8);
        viewport.setBounds (area);
        list->setSize (area.getWidth() - viewport.getScrollBarThickness(), list->getContentHeight());
    }

    bool ShortcutsPanel::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey || key == juce::KeyPress::F1Key)
        {
            if (onClose != nullptr)
                onClose();

            return true;
        }

        // Typing goes to the search box.
        if (key.getTextCharacter() >= ' ' && ! key.getModifiers().isCtrlDown() && ! key.getModifiers().isAltDown())
        {
            search.grabKeyboardFocus();
            search.insertTextAtCaret (juce::String::charToString (key.getTextCharacter()));
            return true;
        }

        return true;    // nothing else reaches the song while the list shows
    }

    void ShortcutsPanel::mouseDown (const juce::MouseEvent& e)
    {
        if (! getPanelArea().contains (e.getPosition()) && onClose != nullptr)
            onClose();
    }

    void ShortcutsPanel::visibilityChanged()
    {
        if (isShowing())
            search.grabKeyboardFocus();
    }
}
