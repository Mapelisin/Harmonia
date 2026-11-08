#include "ui/pianoroll/NoteToolWindow.h"
#include "core/Progressions.h"
#include "ui/Theme.h"

namespace harmonia::ui::pianoroll
{
    namespace tools = core::note_tools;

    namespace
    {
        constexpr int rowHeight = 26;
        constexpr int labelWidth = 84;
        constexpr int windowWidth = 380;

        struct Step
        {
            const char* name;
            double beats;
        };

        constexpr std::array<Step, 6> arpSteps { {
            { "1/4", 1.0 }, { "1/8", 0.5 }, { "1/8 triplet", 1.0 / 3.0 },
            { "1/16", 0.25 }, { "1/16 triplet", 1.0 / 6.0 }, { "1/32", 0.125 }
        } };
    }

    const std::vector<NoteTool>& getAllNoteTools()
    {
        static const std::vector<NoteTool> all { NoteTool::strum, NoteTool::arpeggiate, NoteTool::humanize,
                                                 NoteTool::scaleLevels, NoteTool::chordProgression };
        return all;
    }

    juce::String getName (NoteTool tool)
    {
        switch (tool)
        {
            case NoteTool::strum:       return "Strum";
            case NoteTool::arpeggiate:  return "Arpeggiate";
            case NoteTool::humanize:    return "Humanize";
            case NoteTool::scaleLevels: return "Scale levels";
            case NoteTool::chordProgression: return "Chord progression";
        }

        return {};
    }

    juce::String getShortcutDescription (NoteTool tool)
    {
        switch (tool)
        {
            case NoteTool::strum:       return "Alt+S";
            case NoteTool::arpeggiate:  return "Alt+A";
            case NoteTool::humanize:    return "Alt+R";
            case NoteTool::scaleLevels: return "Alt+X";
            case NoteTool::chordProgression: return "Alt+P";
        }

        return {};
    }

    //==============================================================================
    NoteToolWindow::NoteToolWindow (PianoRollModel& m, const juce::String& t, std::vector<Setting> s, Apply a,
                                    const juce::Array<te::MidiNote*>& targets)
        : model (m), toolName (t), preview (m, t, targets), settings (std::move (s)), apply (std::move (a))
    {
        setWantsKeyboardFocus (true);

        const auto count = (int) preview.getOriginals().size();
        titleLabel.setText (count == 0 ? toolName : toolName + "  (" + juce::String (count) + (count == 1 ? " note)" : " notes)"),
                            juce::dontSendNotification);
        titleLabel.setFont (juce::FontOptions (theme::size::fontSize, juce::Font::bold));
        addAndMakeVisible (titleLabel);

        hintLabel.setText (juce::String::fromUTF8 ("Space \xe2\x80\x94 listen.  Enter \xe2\x80\x94 accept.  Esc \xe2\x80\x94 cancel."),
                           juce::dontSendNotification);
        hintLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        hintLabel.setFont (juce::FontOptions (theme::size::smallFontSize));
        addAndMakeVisible (hintLabel);

        for (const auto& setting : settings)
        {
            auto* control = controls.add (new Control());
            control->label.setText (setting.name, juce::dontSendNotification);
            addAndMakeVisible (control->label);

            if (setting.choices.isEmpty())
            {
                control->slider.setSliderStyle (juce::Slider::LinearHorizontal);
                control->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, rowHeight - 4);
                control->slider.setRange (setting.min, setting.max, setting.interval);
                control->slider.setTextValueSuffix (setting.suffix);
                control->slider.setValue (setting.defaultValue, juce::dontSendNotification);
                control->slider.setDoubleClickReturnValue (true, setting.defaultValue);
                control->slider.onValueChange = [this] { update(); };
                addAndMakeVisible (control->slider);
            }
            else
            {
                control->list.addItemList (setting.choices, 1);
                control->list.setSelectedItemIndex ((int) setting.defaultValue, juce::dontSendNotification);
                control->list.onChange = [this] { update(); };
                addAndMakeVisible (control->list);
            }
        }

        resetButton.onClick = [this] { reset(); };
        cancelButton.onClick = [this] { cancel(); };
        acceptButton.onClick = [this] { accept(); };

        for (auto* button : { &resetButton, &cancelButton, &acceptButton })
        {
            button->setWantsKeyboardFocus (false);
            addAndMakeVisible (button);
        }

        setSize (windowWidth, theme::size::padding * 2 + rowHeight * ((int) settings.size() + 3));
        update();
    }

    std::vector<double> NoteToolWindow::getValues() const
    {
        std::vector<double> values;

        for (int i = 0; i < getNumSettings(); ++i)
            values.push_back (getValue (i));

        return values;
    }

    double NoteToolWindow::getValue (int index) const
    {
        const auto* control = controls[index];

        if (settings[(size_t) index].choices.isEmpty())
            return control->slider.getValue();

        return (double) control->list.getSelectedItemIndex();
    }

    void NoteToolWindow::setValue (int index, double value)
    {
        auto* control = controls[index];

        if (settings[(size_t) index].choices.isEmpty())
            control->slider.setValue (value, juce::sendNotificationSync);
        else
            control->list.setSelectedItemIndex (juce::roundToInt (value), juce::sendNotificationSync);
    }

    void NoteToolWindow::update()
    {
        preview.show (apply (preview.getOriginals(), getValues()));
    }

    void NoteToolWindow::reset()
    {
        for (int i = 0; i < getNumSettings(); ++i)
        {
            const auto& setting = settings[(size_t) i];

            if (setting.choices.isEmpty())
                controls[i]->slider.setValue (setting.defaultValue, juce::dontSendNotification);
            else
                controls[i]->list.setSelectedItemIndex ((int) setting.defaultValue, juce::dontSendNotification);
        }

        update();
    }

    void NoteToolWindow::accept()
    {
        close();
    }

    void NoteToolWindow::cancel()
    {
        preview.revert();
        close();
    }

    void NoteToolWindow::close()
    {
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    }

    bool NoteToolWindow::keyPressed (const juce::KeyPress& key)
    {
        // Space goes on to play or stop; nothing else may edit while the preview is open.
        if (key == juce::KeyPress::spaceKey)
            return false;

        if (key == juce::KeyPress::returnKey)
            accept();
        else if (key == juce::KeyPress::escapeKey)
            cancel();

        return true;
    }

    void NoteToolWindow::paint (juce::Graphics& g)
    {
        g.fillAll (theme::colour::surface);
    }

    void NoteToolWindow::resized()
    {
        auto area = getLocalBounds().reduced (theme::size::padding);
        titleLabel.setBounds (area.removeFromTop (rowHeight));

        for (auto* control : controls)
        {
            auto line = area.removeFromTop (rowHeight);
            control->label.setBounds (line.removeFromLeft (labelWidth));
            control->slider.setBounds (line);
            control->list.setBounds (line.reduced (0, 2));
        }

        hintLabel.setBounds (area.removeFromTop (rowHeight));

        auto buttons = area.removeFromTop (rowHeight).reduced (0, 1);
        acceptButton.setBounds (buttons.removeFromRight (80));
        buttons.removeFromRight (theme::size::padding);
        cancelButton.setBounds (buttons.removeFromRight (80));
        resetButton.setBounds (buttons.removeFromLeft (80));
    }

    void NoteToolWindow::show (std::unique_ptr<NoteToolWindow> window, juce::Component& parent, juce::Rectangle<int> area)
    {
        juce::Component::SafePointer<NoteToolWindow> shown (window.get());
        juce::CallOutBox::launchAsynchronously (std::move (window), area, &parent);

        // Keys come here first, so the piano roll's shortcuts can't edit under the preview.
        if (shown != nullptr && shown->isShowing())
            shown->grabKeyboardFocus();
    }

    //==============================================================================
    std::unique_ptr<NoteToolWindow> NoteToolWindow::create (PianoRollModel& model, NoteTool tool)
    {
        if (tool == NoteTool::chordProgression)
            return createChordProgression (model);

        const auto targets = model.getSelectedOrAll();

        if (targets.isEmpty())
            return nullptr;

        // Times are set in milliseconds at the clip's tempo.
        auto& tempo = model.getEdit().tempoSequence;
        const auto beatsPerMs = tempo.getBeatsPerSecondAt (tempo.toTime (te::BeatPosition::fromBeats (model.getClipStartBeat()))) / 1000.0;
        const auto minBeat = model.getMinBeat();
        const auto name = pianoroll::getName (tool);

        switch (tool)
        {
            case NoteTool::strum:
                return std::make_unique<NoteToolWindow> (model, name, std::vector<Setting> {
                    { "Time", 0.0, 300.0, 1.0, 40.0, " ms", {} },
                    { "Tension", -100.0, 100.0, 1.0, 0.0, "%", {} },
                    { "Direction", 0.0, 0.0, 0.0, 0.0, {}, { "Up (low note first)", "Down (high note first)", "Alternate" } },
                    { "Velocity", -100.0, 100.0, 1.0, 0.0, "%", {} },
                    { "Ends", 0.0, 0.0, 0.0, 0.0, {}, { "Stay where they were", "Move with the starts" } } },
                    [beatsPerMs] (const std::vector<core::Note>& notes, const std::vector<double>& v)
                    {
                        tools::StrumSettings s;
                        s.timeBeats = v[0] * beatsPerMs;
                        s.tension = v[1] / 100.0;
                        s.direction = (tools::StrumDirection) juce::roundToInt (v[2]);
                        s.velocity = v[3] / 100.0;
                        s.keepEnds = v[4] < 0.5;
                        return tools::Result { tools::strum (notes, s), {}, {} };
                    }, targets);

            case NoteTool::arpeggiate:
            {
                juce::StringArray steps;

                for (const auto& step : arpSteps)
                    steps.add (step.name);

                return std::make_unique<NoteToolWindow> (model, name, std::vector<Setting> {
                    { "Pattern", 0.0, 0.0, 0.0, 0.0, {}, { "Up", "Down", "Up and down", "Down and up" } },
                    { "Step", 0.0, 0.0, 0.0, 3.0, {}, steps },
                    { "Gate", 5.0, 100.0, 1.0, 90.0, "%", {} },
                    { "Octaves", 1.0, 4.0, 1.0, 1.0, {}, {} } },
                    [] (const std::vector<core::Note>& notes, const std::vector<double>& v)
                    {
                        tools::ArpSettings s;
                        s.pattern = (tools::ArpPattern) juce::roundToInt (v[0]);
                        s.stepBeats = arpSteps[(size_t) juce::jlimit (0, (int) arpSteps.size() - 1, juce::roundToInt (v[1]))].beats;
                        s.gate = v[2] / 100.0;
                        s.octaves = juce::roundToInt (v[3]);
                        return tools::arpeggiate (notes, s);
                    }, targets);
            }

            case NoteTool::humanize:
                return std::make_unique<NoteToolWindow> (model, name, std::vector<Setting> {
                    { "Timing", 0.0, 50.0, 1.0, 10.0, " ms", {} },
                    { "Velocity", 0.0, 64.0, 1.0, 12.0, {}, {} },
                    { "Length", 0.0, 50.0, 1.0, 0.0, "%", {} },
                    { "Variation", 1.0, 100.0, 1.0, 1.0, {}, {} } },
                    [beatsPerMs, minBeat] (const std::vector<core::Note>& notes, const std::vector<double>& v)
                    {
                        tools::HumanizeSettings s;
                        s.timingBeats = v[0] * beatsPerMs;
                        s.velocity = v[1];
                        s.length = v[2] / 100.0;
                        s.seed = juce::roundToInt (v[3]);
                        return tools::Result { tools::humanize (notes, s, minBeat), {}, {} };
                    }, targets);

            case NoteTool::scaleLevels:
            {
                const auto& properties = core::note_property::getAll();
                juce::StringArray names;

                for (auto p : properties)
                    names.add (core::note_property::getName (p));

                const auto shown = (double) (std::find (properties.begin(), properties.end(), model.getLaneProperty()) - properties.begin());

                return std::make_unique<NoteToolWindow> (model, name, std::vector<Setting> {
                    { "Value", 0.0, 0.0, 0.0, shown, {}, names },
                    { "Multiply", 0.0, 200.0, 1.0, 100.0, "%", {} },
                    { "Offset", -100.0, 100.0, 1.0, 0.0, "%", {} } },
                    [&properties] (const std::vector<core::Note>& notes, const std::vector<double>& v)
                    {
                        tools::ScaleLevelsSettings s;
                        s.property = properties[(size_t) juce::jlimit (0, (int) properties.size() - 1, juce::roundToInt (v[0]))];
                        s.multiply = v[1] / 100.0;
                        s.offset = v[2] / 100.0;
                        return tools::Result { tools::scaleLevels (notes, s), {}, {} };
                    }, targets);
            }

            case NoteTool::chordProgression:
                break;
        }

        return nullptr;
    }

    std::unique_ptr<NoteToolWindow> NoteToolWindow::createChordProgression (PianoRollModel& model)
    {
        namespace progressions = core::progressions;
        namespace scales = core::scales;

        if (model.getClip() == nullptr)
            return nullptr;

        juce::StringArray roots, names, rhythms;

        for (int root = 0; root < 12; ++root)
            roots.add (scales::getRootName (root));

        for (const auto& p : progressions::getAll())
            names.add (p.name);

        for (auto r : progressions::getRhythms())
            rhythms.add (progressions::getName (r));

        // The song's key, if it has one.
        const auto scale = model.getScale();
        const auto minorish = scale.type == scales::ScaleType::minor || scale.type == scales::ScaleType::harmonicMinor
                           || scale.type == scales::ScaleType::melodicMinor || scale.type == scales::ScaleType::dorian
                           || scale.type == scales::ScaleType::phrygian || scale.type == scales::ScaleType::minorPentatonic
                           || scale.type == scales::ScaleType::blues;

        const auto beatsPerBar = model.getBeatsPerBar();
        const auto start = model.getTimeSelection().has_value() ? std::max (model.getMinBeat(), model.getTimeSelection()->start)
                                                                 : model.getClipStartBeat();
        const std::array<double, 3> chordBars { 0.5, 1.0, 2.0 };

        return std::make_unique<NoteToolWindow> (model, pianoroll::getName (NoteTool::chordProgression), std::vector<Setting> {
            { "Key", 0.0, 0.0, 0.0, (double) scale.root, {}, roots },
            { "Mode", 0.0, 0.0, 0.0, minorish ? 1.0 : 0.0, {}, { "Major", "Minor" } },
            { "Chords", 0.0, 0.0, 0.0, minorish ? 6.0 : 0.0, {}, names },
            { "Notes", 0.0, 0.0, 0.0, 0.0, {}, { "Three (triads)", "Four (sevenths)" } },
            { "Each chord", 0.0, 0.0, 0.0, 1.0, {}, { "Half a bar", "1 bar", "2 bars" } },
            { "Rhythm", 0.0, 0.0, 0.0, 0.0, {}, rhythms },
            { "Octave", 2.0, 6.0, 1.0, 4.0, {}, {} },
            { "Voicing", 0.0, 0.0, 0.0, 0.0, {}, { "Smooth (close together)", "Root position" } },
            { "Bass", 0.0, 0.0, 0.0, 0.0, {}, { "No", "Root an octave below" } },
            { "Repeat", 1.0, 8.0, 1.0, 1.0, " x", {} } },
            [start, beatsPerBar, chordBars] (const std::vector<core::Note>&, const std::vector<double>& v)
            {
                progressions::Settings s;
                s.key = { juce::roundToInt (v[0]), juce::roundToInt (v[1]) == 1 ? scales::ScaleType::minor : scales::ScaleType::major };
                s.progression = juce::roundToInt (v[2]);
                s.sevenths = juce::roundToInt (v[3]) == 1;
                s.chordBeats = chordBars[(size_t) juce::jlimit (0, 2, juce::roundToInt (v[4]))] * beatsPerBar;
                s.rhythm = progressions::getRhythms()[(size_t) juce::jlimit (0, 3, juce::roundToInt (v[5]))];
                s.octave = juce::roundToInt (v[6]);
                s.smooth = juce::roundToInt (v[7]) == 0;
                s.bass = juce::roundToInt (v[8]) == 1;
                s.repeats = juce::roundToInt (v[9]);
                s.startBeat = start;
                return tools::Result { {}, {}, progressions::generate (s) };
            }, juce::Array<te::MidiNote*>());
    }
}
