#include "ui/SamplerPanel.h"
#include "ui/FileDrag.h"
#include "ui/Theme.h"

namespace harmonia::ui
{
    namespace sampler = audio::sampler;

    namespace
    {
        constexpr int margin = 10, buttonSize = 22, rootWidth = 64, captionWidth = 62;
        constexpr juce::uint32 dropAuditionMs = 800;

        juce::String keyName (int note)
        {
            return juce::MidiMessage::getMidiNoteName (note, true, true, 4);
        }

        juce::File firstFile (const juce::StringArray& paths)
        {
            return paths.isEmpty() ? juce::File() : juce::File (paths[0]);
        }
    }

    SamplerPanel::SamplerPanel (te::Plugin& p)
        : sampler (p), knobs (p)
    {
        addAndMakeVisible (knobs);

        playButton.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xb6"));
        playButton.setTooltip ("Hold to hear the sound as it is (the root key)");
        playButton.onStateChange = [this]
        {
            // Pressed: plays; let go: stops (a moment of sound after a drop stops by itself).
            if (playButton.isDown())
            {
                setAuditioning (true);
                auditionUntil = 0;
            }
            else if (auditionUntil == 0)
            {
                setAuditioning (false);
            }
        };

        soundButton.setTooltip ("The sound the sampler plays. Click to choose a sound file, or drop one on this window "
                                "(from the sound browser or the file explorer).");
        soundButton.onClick = [this] { chooseFile(); };

        rootCaption.setFont (juce::FontOptions (theme::size::smallFontSize));
        rootCaption.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        rootCaption.setJustificationType (juce::Justification::centredRight);

        rootKey.setRange (0.0, 127.0);
        rootKey.setSteps (1.0, 1.0, 6.0f);
        rootKey.textFromValue = [] (double value) { return keyName (juce::roundToInt (value)); };
        rootKey.setTooltip ("The key that plays the sound as it is: the keys above it play it higher, those below lower. "
                            "Drag up or down, or turn the wheel (in the piano roll: right-click a key).");
        rootKey.onGestureStart = [this] { sampler.edit.getUndoManager().beginNewTransaction ("Root key"); };
        rootKey.onValueChange = [this] (double value) { sampler::setRoot (sampler, juce::roundToInt (value)); };

        for (auto* c : std::initializer_list<juce::Component*> { &playButton, &soundButton, &rootCaption, &rootKey })
        {
            c->setWantsKeyboardFocus (false);
            addAndMakeVisible (c);
        }

        refresh();
        setSize (560, knobs.getIdealHeight() + soundRowHeight + pictureHeight);
        startTimerHz (5);
    }

    SamplerPanel::~SamplerPanel()
    {
        stopTimer();
        setAuditioning (false);
    }

    audio::SampleData::Ptr SamplerPanel::getSample() const
    {
        auto* plugin = dynamic_cast<sampler::Plugin*> (&sampler);
        return plugin != nullptr ? plugin->getSample() : audio::SampleData::Ptr();
    }

    juce::String SamplerPanel::getInfoText() const
    {
        const auto sample = getSample();

        if (sample == nullptr)
            return {};

        const auto seconds = sample->getLengthSeconds();
        const auto kHz = sample->sampleRate / 1000.0;
        const auto whole = std::abs (kHz - std::round (kHz)) < 0.05;

        return juce::String (seconds, seconds < 10.0 ? 2 : 1) + " s, "
                 + (sample->audio.getNumChannels() > 1 ? "stereo" : "mono") + ", "
                 + (whole ? juce::String (juce::roundToInt (kHz)) : juce::String (kHz, 1)) + " kHz"
                 + (sample->cut ? " (a long file: its first " + juce::String (juce::roundToInt (audio::SampleData::maxSeconds)) + " seconds)"
                                : juce::String());
    }

    void SamplerPanel::refresh()
    {
        const auto file = sampler::getFile (sampler);
        const auto none = file == juce::File();
        const auto missing = sampler::isFileMissing (sampler);

        soundButton.setButtonText (none ? juce::String ("Choose a sound...")
                                   : missing ? "Missing: " + file.getFileName()
                                             : file.getFileNameWithoutExtension());
        soundButton.setColour (juce::TextButton::textColourOffId, none ? theme::colour::textSecondary
                                                                  : missing ? theme::colour::warning
                                                                            : theme::colour::textPrimary);
        rootKey.setValue ((double) sampler::getRoot (sampler));

        if (getSample() != drawn)
            repaint();
    }

    void SamplerPanel::dropFile (const juce::File& file)
    {
        if (file == juce::File())
            return;

        if (const auto error = sampler::setFile (sampler, file); error.isNotEmpty())
        {
            knobs.tell (error, true);
            return;
        }

        // Heard at once, not a moment later.
        if (auto* plugin = dynamic_cast<sampler::Plugin*> (&sampler))
            plugin->updateSampleNow();

        knobs.tell ("Plays " + file.getFileNameWithoutExtension() + ": " + keyName (sampler::getRoot (sampler)) + " as it is", false);
        refresh();

        setAuditioning (false);
        setAuditioning (true);
        auditionUntil = juce::Time::getMillisecondCounter() + dropAuditionMs;
    }

    void SamplerPanel::setAuditioning (bool shouldPlay)
    {
        auto* track = dynamic_cast<te::AudioTrack*> (sampler.getOwnerTrack());

        if (auditionNote >= 0)
        {
            if (track != nullptr)
                track->injectLiveMidiMessage (juce::MidiMessage::noteOff (1, auditionNote), auditionSource);

            auditionNote = -1;
        }

        auditionUntil = 0;

        if (shouldPlay && track != nullptr)
        {
            auditionNote = sampler::getRoot (sampler);
            track->injectLiveMidiMessage (juce::MidiMessage::noteOn (1, auditionNote, (juce::uint8) 100), auditionSource);
        }
    }

    void SamplerPanel::chooseFile()
    {
        auto& formats = sampler.edit.engine.getAudioFileFormatManager().readFormatManager;
        const auto current = sampler::getFile (sampler);
        const auto folder = current != juce::File() ? current.getParentDirectory()
                                                    : juce::File::getSpecialLocation (juce::File::userMusicDirectory);

        chooser = std::make_unique<juce::FileChooser> ("A sound for the sampler", folder, formats.getWildcardForAllFormats());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<SamplerPanel> (this)] (const juce::FileChooser& fc)
        {
            if (safe != nullptr)
                safe->dropFile (fc.getResult());
        });
    }

    void SamplerPanel::updateColumns (int width)
    {
        const auto sample = getSample();

        if (sample == drawn && (int) columns.size() == width)
            return;

        drawn = sample;
        columns.assign ((size_t) std::max (0, width), {});

        if (sample == nullptr || width <= 0)
            return;

        const auto length = sample->audio.getNumSamples();

        for (int x = 0; x < width; ++x)
        {
            const auto from = (int) ((juce::int64) x * length / width);
            const auto to = std::max (from + 1, (int) ((juce::int64) (x + 1) * length / width));
            auto range = sample->audio.findMinMax (0, from, std::min (to, length) - from);

            if (sample->audio.getNumChannels() > 1)
                range = range.getUnionWith (sample->audio.findMinMax (1, from, std::min (to, length) - from));

            columns[(size_t) x] = range;
        }
    }

    void SamplerPanel::paint (juce::Graphics& g)
    {
        g.fillAll (theme::colour::background);
        g.setColour (theme::colour::border);
        g.fillRect (0, knobs.getBottom(), getWidth(), 1);

        const auto area = picture.toFloat();
        g.setColour (theme::colour::surface);
        g.fillRoundedRectangle (area, theme::size::cornerRadius);

        const auto file = sampler::getFile (sampler);
        const auto wave = picture.reduced (6, 6).withTrimmedBottom (16);
        updateColumns (wave.getWidth());

        g.setFont (juce::FontOptions (theme::size::smallFontSize));

        if (drawn != nullptr)
        {
            const auto middle = (float) wave.getCentreY();
            const auto half = (float) wave.getHeight() * 0.5f;
            g.setColour (theme::colour::border);
            g.fillRect ((float) wave.getX(), middle, (float) wave.getWidth(), 1.0f);
            g.setColour (theme::colour::accent);

            for (int x = 0; x < (int) columns.size(); ++x)
            {
                const auto top = middle - juce::jlimit (-1.0f, 1.0f, columns[(size_t) x].getEnd()) * half;
                const auto bottom = middle - juce::jlimit (-1.0f, 1.0f, columns[(size_t) x].getStart()) * half;
                g.fillRect ((float) (wave.getX() + x), top, 1.0f, std::max (1.0f, bottom - top));
            }

            g.setColour (theme::colour::textSecondary);
            g.drawText (getInfoText(), picture.reduced (8, 2).removeFromBottom (18), juce::Justification::centredLeft, true);
        }
        else
        {
            const auto missing = sampler::isFileMissing (sampler);
            g.setColour (file != juce::File() ? theme::colour::warning : theme::colour::textSecondary);
            g.drawFittedText (file == juce::File() ? juce::String ("Drop a sound here (from the sound browser or the file explorer), or click to choose a file.\n"
                                                                    "The keys then play it: higher keys higher, lower keys lower.")
                              : missing ? "The file isn't there any more: " + file.getFullPathName() + "\nClick to choose it again."
                                        : "Can't read " + file.getFileName() + " as a sound. Click to choose another.",
                              picture.reduced (16, 8), juce::Justification::centred, 4);
        }

        if (dropHover)
        {
            g.setColour (theme::colour::accent.withAlpha (0.2f));
            g.fillRoundedRectangle (area, theme::size::cornerRadius);
            g.setColour (theme::colour::accent);
            g.drawRoundedRectangle (area.reduced (0.5f), theme::size::cornerRadius, 1.0f);
        }
    }

    void SamplerPanel::resized()
    {
        auto area = getLocalBounds();
        knobs.setBounds (area.removeFromTop (std::min (knobs.getIdealHeight(), getHeight() / 2)));

        auto row = area.removeFromTop (soundRowHeight).reduced (margin, 6);
        playButton.setBounds (row.removeFromLeft (buttonSize));
        row.removeFromLeft (6);
        rootKey.setBounds (row.removeFromRight (rootWidth));
        row.removeFromRight (4);
        rootCaption.setBounds (row.removeFromRight (captionWidth));
        row.removeFromRight (6);
        soundButton.setBounds (row);

        picture = area.reduced (margin, 0).withTrimmedBottom (margin);
    }

    void SamplerPanel::mouseUp (const juce::MouseEvent& e)
    {
        if (picture.contains (e.getPosition()) && e.mouseWasClicked() && ! e.mods.isPopupMenu())
            chooseFile();
    }

    void SamplerPanel::setHover (bool shouldHover)
    {
        if (std::exchange (dropHover, shouldHover) != shouldHover)
            repaint();
    }

    // Files from the file explorer.
    bool SamplerPanel::isInterestedInFileDrag (const juce::StringArray& files)      { return ! files.isEmpty(); }
    void SamplerPanel::fileDragEnter (const juce::StringArray&, int, int)           { setHover (true); }
    void SamplerPanel::fileDragExit (const juce::StringArray&)                      { setHover (false); }

    void SamplerPanel::filesDropped (const juce::StringArray& files, int, int)
    {
        setHover (false);
        dropFile (firstFile (files));
    }

    // Sounds dragged from the sound browser.
    bool SamplerPanel::isInterestedInDragSource (const SourceDetails& details)      { return ! file_drag::read (details.description).isEmpty(); }
    void SamplerPanel::itemDragEnter (const SourceDetails&)                         { setHover (true); }
    void SamplerPanel::itemDragExit (const SourceDetails&)                          { setHover (false); }

    void SamplerPanel::itemDropped (const SourceDetails& details)
    {
        setHover (false);
        dropFile (firstFile (file_drag::read (details.description)));
    }

    void SamplerPanel::timerCallback()
    {
        if (auditionNote >= 0 && auditionUntil != 0 && juce::Time::getMillisecondCounter() >= auditionUntil)
            setAuditioning (false);

        refresh();
    }
}
