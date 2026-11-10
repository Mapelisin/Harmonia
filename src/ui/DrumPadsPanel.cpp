#include "ui/DrumPadsPanel.h"
#include "core/Drums.h"
#include "ui/FileDrag.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    namespace drum_kit = audio::drum_kit;

    namespace
    {
        constexpr int margin = 10, buttonSize = 20, nameWidth = 118, volumeWidth = 96, tuneWidth = 84;

        juce::String describeKey (int note)
        {
            const auto name = core::drums::getName (note);
            const auto key = juce::MidiMessage::getMidiNoteName (note, true, true, 4);
            return name.isNotEmpty() ? name : "Key " + key;
        }

        /** The first of the dropped files (a drum plays one sound). */
        juce::File firstFile (const juce::StringArray& paths)
        {
            return paths.isEmpty() ? juce::File() : juce::File (paths[0]);
        }
    }

    //==============================================================================
    /** One drum: play, its name, what it plays, the sample's volume and tuning, back to built-in. */
    class DrumPadsPanel::Pad final : public juce::Component,
                                     public juce::FileDragAndDropTarget,
                                     public juce::DragAndDropTarget
    {
    public:
        Pad (DrumPadsPanel& o, int n)
            : owner (o), note (n)
        {
            play.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xb6"));
            play.setTooltip ("Play this drum");
            play.onClick = [this] { owner.audition (note); };

            name.setText (describeKey (note), juce::dontSendNotification);
            name.setFont (juce::FontOptions (theme::size::smallFontSize));
            name.setColour (juce::Label::textColourId, theme::colour::textPrimary);
            name.setTooltip (describeKey (note) + " (" + juce::MidiMessage::getMidiNoteName (note, true, true, 4) + ")");
            name.setInterceptsMouseClicks (false, false);

            sound.setTooltip ("What this drum plays. Click to choose a sound file, or drop one here "
                              "(from the sound browser or the file explorer).");
            sound.onClick = [this] { owner.chooseFile (note); };

            volume.setSliderStyle (juce::Slider::LinearHorizontal);
            volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            volume.setRange ((double) drum_kit::minSampleGainDb, (double) drum_kit::maxSampleGainDb, 0.1);
            volume.setDoubleClickReturnValue (true, 0.0);
            volume.setScrollWheelEnabled (false);       // the wheel scrolls the list
            volume.onDragStart = [this]
            {
                dragging = true;
                owner.drums.edit.getUndoManager().beginNewTransaction ("Sample volume");
            };
            volume.onDragEnd = [this] { dragging = false; };
            volume.onValueChange = [this]
            {
                // A double-click reset arrives without a drag: still its own undo step.
                if (! dragging)
                    owner.drums.edit.getUndoManager().beginNewTransaction ("Sample volume");

                drum_kit::setSampleGainDb (owner.drums, note, (float) volume.getValue());
                showValues();
            };

            tune.setSliderStyle (juce::Slider::LinearHorizontal);
            tune.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            tune.setRange (-(double) drum_kit::maxSampleTune, (double) drum_kit::maxSampleTune, 1.0);
            tune.setDoubleClickReturnValue (true, 0.0);
            tune.setScrollWheelEnabled (false);
            tune.onDragStart = [this]
            {
                dragging = true;
                owner.drums.edit.getUndoManager().beginNewTransaction ("Sample tuning");
            };
            tune.onDragEnd = [this] { dragging = false; };
            tune.onValueChange = [this]
            {
                if (! dragging)
                    owner.drums.edit.getUndoManager().beginNewTransaction ("Sample tuning");

                drum_kit::setSampleTune (owner.drums, note, (float) tune.getValue());
                showValues();
            };

            clear.setButtonText ("x");
            clear.setTooltip ("Back to the kit's own sound");
            clear.onClick = [this] { owner.clear (note); };

            for (auto* c : std::initializer_list<juce::Component*> { &play, &name, &sound, &volume, &tune, &clear })
            {
                c->setWantsKeyboardFocus (false);
                addAndMakeVisible (c);
            }

            refresh();
        }

        int getNote() const noexcept                { return note; }
        juce::String getSound() const               { return sound.getButtonText(); }
        juce::String getName() const                { return name.getText(); }
        juce::Slider& getVolume() noexcept          { return volume; }
        juce::Slider& getTune() noexcept            { return tune; }
        juce::Button& getClear() noexcept           { return clear; }

        void refresh()
        {
            const auto sample = drum_kit::getSample (owner.drums, note);
            const auto has = sample.has_value();

            sound.setButtonText (! has ? juce::String ("Built-in")
                                 : sample->missing ? "Missing: " + sample->file.getFileName()
                                                   : sample->file.getFileNameWithoutExtension());
            sound.setColour (juce::TextButton::textColourOffId, ! has ? theme::colour::textSecondary
                                                               : sample->missing ? theme::colour::warning
                                                                                 : theme::colour::textPrimary);

            for (auto* c : std::initializer_list<juce::Component*> { &volume, &tune, &clear })
                c->setEnabled (has);

            if (! dragging)
            {
                volume.setValue (has ? (double) sample->gainDb : 0.0, juce::dontSendNotification);
                tune.setValue (has ? (double) sample->tune : 0.0, juce::dontSendNotification);
            }

            showValues();
        }

        void paint (juce::Graphics& g) override
        {
            if (dropHover)
            {
                g.setColour (theme::colour::accent.withAlpha (0.25f));
                g.fillRect (getLocalBounds());
                g.setColour (theme::colour::accent);
                g.drawRect (getLocalBounds(), 1);
            }
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (margin, 3);
            play.setBounds (area.removeFromLeft (buttonSize));
            area.removeFromLeft (6);
            name.setBounds (area.removeFromLeft (nameWidth));
            clear.setBounds (area.removeFromRight (buttonSize));
            area.removeFromRight (6);
            tune.setBounds (area.removeFromRight (tuneWidth));
            area.removeFromRight (4);
            volume.setBounds (area.removeFromRight (volumeWidth));
            area.removeFromRight (6);
            sound.setBounds (area);
        }

        // Files from the file explorer.
        bool isInterestedInFileDrag (const juce::StringArray& files) override    { return ! files.isEmpty(); }
        void fileDragEnter (const juce::StringArray&, int, int) override         { setHover (true); }
        void fileDragExit (const juce::StringArray&) override                    { setHover (false); }

        void filesDropped (const juce::StringArray& files, int, int) override
        {
            setHover (false);
            owner.dropFile (note, firstFile (files));
        }

        // Sounds dragged from the sound browser.
        bool isInterestedInDragSource (const SourceDetails& details) override    { return ! file_drag::read (details.description).isEmpty(); }
        void itemDragEnter (const SourceDetails&) override                       { setHover (true); }
        void itemDragExit (const SourceDetails&) override                        { setHover (false); }

        void itemDropped (const SourceDetails& details) override
        {
            setHover (false);
            owner.dropFile (note, firstFile (file_drag::read (details.description)));
        }

    private:
        DrumPadsPanel& owner;
        const int note;
        juce::TextButton play, sound, clear;
        juce::Label name;
        juce::Slider volume, tune;
        bool dropHover = false, dragging = false;

        void setHover (bool shouldHover)
        {
            if (std::exchange (dropHover, shouldHover) != shouldHover)
                repaint();
        }

        void showValues()
        {
            const auto db = volume.getValue();
            const auto semitones = juce::roundToInt (tune.getValue());
            volume.setTooltip ("The sample's volume: " + juce::String (db > 0.0 ? "+" : "") + juce::String (db, 1) + " dB (double-click: 0)");
            tune.setTooltip ("The sample's tuning: " + juce::String (semitones > 0 ? "+" : "") + juce::String (semitones)
                             + " semitones (double-click: 0)");
        }
    };

    //==============================================================================
    DrumPadsPanel::DrumPadsPanel (te::Plugin& drumMachine)
        : drums (drumMachine), knobs (drumMachine)
    {
        addAndMakeVisible (knobs);

        header.setText ("Drums: drop a sound on one to replace it", juce::dontSendNotification);
        header.setFont (juce::FontOptions (theme::size::smallFontSize, juce::Font::bold));
        header.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        addAndMakeVisible (header);

        message.setFont (juce::FontOptions (theme::size::smallFontSize));
        message.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        message.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (message);

        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (10);
        addAndMakeVisible (viewport);

        refresh();
        setSize (560, std::min (knobs.getIdealHeight() + headerHeight + 14 * rowHeight, 720));
        startTimerHz (4);
    }

    DrumPadsPanel::~DrumPadsPanel()
    {
        stopTimer();
    }

    std::vector<int> DrumPadsPanel::getNotes() const
    {
        std::vector<int> notes;

        // The keys the kit has a sound for, and whatever else has a sample.
        for (int note = 0; note < 128; ++note)
            if (drum_kit::getSound (note) != drum_kit::Sound::none || drum_kit::getSample (drums, note).has_value())
                notes.push_back (note);

        return notes;
    }

    void DrumPadsPanel::refresh()
    {
        const auto notes = getNotes();
        auto same = (int) notes.size() == pads.size();

        for (int i = 0; same && i < pads.size(); ++i)
            same = pads[i]->getNote() == notes[(size_t) i];

        if (! same)
        {
            pads.clear();

            for (const auto note : notes)
                content.addAndMakeVisible (pads.add (std::make_unique<Pad> (*this, note)));

            resized();
            return;
        }

        for (auto* pad : pads)
            pad->refresh();
    }

    int DrumPadsPanel::getPadNote (int index) const
    {
        return juce::isPositiveAndBelow (index, pads.size()) ? pads[index]->getNote() : -1;
    }

    int DrumPadsPanel::indexOfNote (int note) const
    {
        for (int i = 0; i < pads.size(); ++i)
            if (pads[i]->getNote() == note)
                return i;

        return -1;
    }

    juce::String DrumPadsPanel::getPadSound (int index) const
    {
        return juce::isPositiveAndBelow (index, pads.size()) ? pads[index]->getSound() : juce::String();
    }

    juce::String DrumPadsPanel::getPadName (int index) const
    {
        return juce::isPositiveAndBelow (index, pads.size()) ? pads[index]->getName() : juce::String();
    }

    juce::Slider* DrumPadsPanel::getVolumeSlider (int index) const
    {
        return juce::isPositiveAndBelow (index, pads.size()) ? &pads[index]->getVolume() : nullptr;
    }

    juce::Slider* DrumPadsPanel::getTuneSlider (int index) const
    {
        return juce::isPositiveAndBelow (index, pads.size()) ? &pads[index]->getTune() : nullptr;
    }

    juce::Button* DrumPadsPanel::getClearButton (int index) const
    {
        return juce::isPositiveAndBelow (index, pads.size()) ? &pads[index]->getClear() : nullptr;
    }

    void DrumPadsPanel::dropFile (int note, const juce::File& file)
    {
        if (file == juce::File())
            return;

        if (const auto error = drum_kit::setSample (drums, note, file); error.isNotEmpty())
        {
            tell (error, true);
            return;
        }

        // Heard at once, not a moment later.
        if (auto* machine = dynamic_cast<drum_kit::Plugin*> (&drums))
            machine->updateSamplesNow();

        tell (describeKey (note) + " plays " + file.getFileNameWithoutExtension(), false);
        refresh();
        audition (note);
    }

    void DrumPadsPanel::clear (int note)
    {
        drum_kit::clearSample (drums, note);

        if (auto* machine = dynamic_cast<drum_kit::Plugin*> (&drums))
            machine->updateSamplesNow();

        tell (describeKey (note) + ": the kit's own sound", false);
        refresh();
    }

    void DrumPadsPanel::audition (int note)
    {
        // A hit rings out by itself: the key is let go at once.
        if (auto* track = dynamic_cast<te::AudioTrack*> (drums.getOwnerTrack()))
        {
            track->injectLiveMidiMessage (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), auditionSource);
            track->injectLiveMidiMessage (juce::MidiMessage::noteOff (1, note), auditionSource);
        }
    }

    void DrumPadsPanel::chooseFile (int note)
    {
        auto& formats = drums.edit.engine.getAudioFileFormatManager().readFormatManager;
        const auto current = drum_kit::getSample (drums, note);
        const auto folder = current.has_value() ? current->file.getParentDirectory()
                                                : juce::File::getSpecialLocation (juce::File::userMusicDirectory);

        chooser = std::make_unique<juce::FileChooser> ("A sound for " + describeKey (note), folder, formats.getWildcardForAllFormats());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<DrumPadsPanel> (this), note] (const juce::FileChooser& fc)
        {
            if (safe != nullptr)
                safe->dropFile (note, fc.getResult());
        });
    }

    void DrumPadsPanel::tell (const juce::String& text, bool isProblem)
    {
        message.setColour (juce::Label::textColourId, isProblem ? theme::colour::warning : theme::colour::textSecondary);
        message.setText (text, juce::dontSendNotification);
    }

    void DrumPadsPanel::paint (juce::Graphics& g)
    {
        g.fillAll (theme::colour::background);
        g.setColour (theme::colour::border);
        g.fillRect (0, knobs.getBottom(), getWidth(), 1);
    }

    void DrumPadsPanel::resized()
    {
        auto area = getLocalBounds();
        knobs.setBounds (area.removeFromTop (std::min (knobs.getIdealHeight(), getHeight() / 2)));

        auto top = area.removeFromTop (headerHeight).reduced (margin, 0);
        header.setBounds (top.removeFromLeft (top.getWidth() / 2));
        message.setBounds (top);

        viewport.setBounds (area);
        const auto rowsHeight = pads.size() * rowHeight;
        const auto width = getWidth() - (rowsHeight > area.getHeight() ? viewport.getScrollBarThickness() : 0);
        content.setSize (width, rowsHeight);

        for (int i = 0; i < pads.size(); ++i)
            pads[i]->setBounds (0, i * rowHeight, width, rowHeight);
    }

    void DrumPadsPanel::timerCallback()
    {
        refresh();
    }
}
