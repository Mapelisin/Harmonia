#include "ui/audioeditor/AudioEditor.h"
#include "core/AudioTempo.h"
#include "ui/TakeMenu.h"
#include "ui/Theme.h"

namespace harmonia::ui::audioeditor
{
    namespace clips = core::clips;
    namespace audio_tempo = core::audio_tempo;

    namespace
    {
        constexpr float minTypedGainDb = -60.0f;

        juce::String formatGain (double db)
        {
            if (db <= minTypedGainDb + 0.05)
                return "silent";

            return (db > 0.05 ? "+" : "") + juce::String (db, 1) + " dB";
        }

        juce::String formatPitch (double semitones)
        {
            const auto rounded = juce::roundToInt (semitones * 10.0) / 10.0;

            if (std::abs (rounded) < 0.05)
                return "0 st";

            const auto whole = std::abs (rounded - std::round (rounded)) < 0.05;
            return (rounded > 0.0 ? "+" : "") + (whole ? juce::String (juce::roundToInt (rounded)) : juce::String (rounded, 1)) + " st";
        }
    }

    AudioEditor::AudioEditor (arrangement::ArrangementModel& a, audio::Transport& transport)
        : arrangement (a), waveform (model, transport)
    {
        waveform.onPositionChosen = [this] (double beat)
        {
            if (onPositionChosen != nullptr)
                onPositionChosen (beat);
        };

        for (auto* label : { &gainLabel, &pitchLabel, &tempoLabel, &fadeLabel })
        {
            label->setFont (juce::FontOptions (theme::size::smallFontSize));
            label->setColour (juce::Label::textColourId, theme::colour::textSecondary);
            label->setJustificationType (juce::Justification::centredRight);
            label->setBorderSize ({ 0, 0, 0, 4 });
            addAndMakeVisible (label);
        }

        fileInfo.setFont (juce::FontOptions (theme::size::smallFontSize));
        fileInfo.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        fileInfo.setJustificationType (juce::Justification::centredRight);
        fileInfo.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (fileInfo);

        setUpNumbers();
        setUpButtons();

        for (auto shape : clips::getFadeShapes())
            fadeShape.addItem (clips::getFadeShapeName (shape), (int) shape + 1);

        fadeShape.setTooltip ("How the clip's fades go (drag the small squares at the top of the waveform to make them)");
        fadeShape.onChange = [this]
        {
            if (const auto id = fadeShape.getSelectedId(); id > 0)
                model.setFadeShape ((clips::FadeShape) (id - 1));
        };

        for (auto* c : std::initializer_list<juce::Component*> { &gain, &pitch, &loopBpm, &normalize, &follow, &halve, &twice, &earlier, &later, &reverse,
                                                                 &showClip, &takes, &fadeShape })
        {
            c->setWantsKeyboardFocus (false);
            addAndMakeVisible (c);
        }

        addAndMakeVisible (waveform);
        arrangement.addListener (this);
        refresh();
    }

    AudioEditor::~AudioEditor()
    {
        arrangement.removeListener (this);
    }

    void AudioEditor::setUpNumbers()
    {
        gain.setRange (minTypedGainDb, clips::maxNormalizeGainDb);
        gain.setSteps (0.5, 0.1);
        gain.textFromValue = formatGain;
        gain.setTooltip ("The clip's volume before the track's effects and fader: drag up or down, or double-click and type "
                         "(also the round handle on the waveform)");
        gain.onGestureStart = [this] { model.beginChange ("Clip gain"); };
        gain.onValueChange = [this] (double db)
        {
            model.setGainDb (db <= minTypedGainDb + 0.05 ? clips::silentGainDb : (float) db, clips::maxNormalizeGainDb);
        };

        const auto maxPitch = (double) audio_tempo::maxPitchSemitones;
        pitch.setRange (-maxPitch, maxPitch);
        pitch.setSteps (1.0, 0.1, 6.0f);
        pitch.textFromValue = formatPitch;
        pitch.setTooltip ("Higher or lower in semitones, the speed staying the same (Shift: finer)");
        pitch.onGestureStart = [this] { model.beginChange ("Pitch"); };
        pitch.onValueChange = [this] (double semitones) { model.setPitch ((float) semitones); };

        loopBpm.setRange (audio_tempo::minLoopBpm, audio_tempo::maxLoopBpm);
        loopBpm.setSteps (1.0, 0.1);
        loopBpm.textFromValue = [this] (double bpm)
        {
            return (model.followsBeatByBeat() ? "~" : "") + audio_tempo::formatBpm (bpm) + " BPM";
        };
        loopBpm.onGestureStart = [this] { model.beginChange ("Loop tempo"); };
        loopBpm.onValueChange = [this] (double bpm) { model.setLoopBpm (bpm); };
    }

    void AudioEditor::setUpButtons()
    {
        for (auto* toggle : { &follow, &reverse })
        {
            toggle->setClickingTogglesState (false);
            toggle->setColour (juce::TextButton::buttonOnColourId, theme::colour::accent.withAlpha (0.45f));
        }

        normalize.setTooltip ("Sets the gain so that the loudest point of the clip is at 0 dB");
        normalize.onClick = [this]
        {
            if (! model.normalize() && onMessage != nullptr)
                onMessage ("Nothing to normalize: the clip is silent, or its file can't be read", true);
        };

        follow.setTooltip ("Follow the song's tempo: the sound's own tempo and first beat are measured, and it is played "
                           "faster or slower so that its hits are on the song's grid");
        follow.onClick = [this]
        {
            const auto found = model.setFollowsTempo (! follow.getToggleState());

            if (found.note.isNotEmpty() && onMessage != nullptr)
                onMessage (found.note, found.isProblem);
        };

        // A first beat taken a step off: the sound slides under the grid.
        const auto slideBy = [this] (double direction)
        {
            const auto step = juce::ModifierKeys::currentModifiers.isShiftDown() ? 1.0 / 64.0 : 0.25;

            if (! model.slide (direction * step) && onMessage != nullptr)
                onMessage ("The sound can't go further that way: its file ends there", true);
        };

        earlier.setTooltip ("Slide the sound a sixteenth earlier under the grid (Shift: a hair)");
        earlier.onClick = [slideBy] { slideBy (-1.0); };
        later.setTooltip ("Slide the sound a sixteenth later under the grid (Shift: a hair)");
        later.onClick = [slideBy] { slideBy (1.0); };

        halve.setTooltip ("The loop's tempo halved: it plays twice as fast");
        halve.onClick = [this] { model.halveLoopBpm(); };
        twice.setTooltip ("The loop's tempo doubled: it plays half as fast");
        twice.onClick = [this] { model.doubleLoopBpm(); };

        reverse.setTooltip ("Play the sound backwards");
        reverse.onClick = [this] { model.setReversed (! reverse.getToggleState()); };

        showClip.setTooltip ("Show the part of the file the clip plays (Ctrl+wheel zooms, the wheel scrolls)");
        showClip.onClick = [this] { waveform.showPlayedPart(); };

        takes.setTooltip ("This clip was recorded round a loop: choose which take it plays.\n"
                          "Split the clip (Ctrl+E) and each piece can play its own take.");
        takes.onClick = [this]
        {
            if (auto* clip = model.getClip())
                take_menu::make (arrangement.getProject(), *clip).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&takes));
        };
    }

    //==============================================================================
    void AudioEditor::arrangementChanged()
    {
        refresh();
    }

    void AudioEditor::refresh()
    {
        updateControls();
        waveform.clipChanged();
    }

    void AudioEditor::updateControls()
    {
        auto* clip = model.getClip();
        const auto hasClip = clip != nullptr;

        for (auto* c : std::initializer_list<juce::Component*> { &gain, &pitch, &normalize, &reverse, &showClip, &fadeShape })
            c->setEnabled (hasClip);

        fileInfo.setText (model.describeFile(), juce::dontSendNotification);

        const auto takesText = hasClip ? take_menu::describe (*clip) : juce::String();

        if (takes.isVisible() != takesText.isNotEmpty() || takes.getButtonText() != takesText)
        {
            takes.setVisible (takesText.isNotEmpty());
            takes.setButtonText (takesText);
            resized();
        }

        if (! hasClip)
        {
            follow.setEnabled (false);
            loopBpm.setEnabled (false);
            halve.setEnabled (false);
            twice.setEnabled (false);
            earlier.setEnabled (false);
            later.setEnabled (false);
            follow.setToggleState (false, juce::dontSendNotification);
            reverse.setToggleState (false, juce::dontSendNotification);
            return;
        }

        const auto db = clips::getGainDb (*clip);
        gain.setValue (db <= minTypedGainDb ? minTypedGainDb : db);
        pitch.setValue (audio_tempo::getPitch (*clip));

        const auto loop = audio_tempo::getLoopBpm (*clip);
        follow.setEnabled (model.canFollowTempo());
        follow.setToggleState (loop.has_value(), juce::dontSendNotification);
        loopBpm.setEnabled (loop.has_value());
        loopBpm.setValue (loop.value_or (arrangement.getProject().getTempoBpm()));
        loopBpm.setTooltip (model.followsBeatByBeat()
                                ? "The sound's tempo on the whole. It doesn't keep one tempo: each of its beats is played on a beat of the song (the lines over the waveform)"
                                : "The tempo the loop was recorded at: it is played faster or slower to fit the song");
        loopBpm.repaint();
        halve.setEnabled (loop.has_value() && *loop / 2.0 >= audio_tempo::minLoopBpm - 0.05);
        twice.setEnabled (loop.has_value() && *loop * 2.0 <= audio_tempo::maxLoopBpm + 0.05);
        earlier.setEnabled (model.canSlide());
        later.setEnabled (model.canSlide());

        reverse.setToggleState (clips::isReversed (*clip), juce::dontSendNotification);
        fadeShape.setSelectedId ((int) clips::getFadeShape (*clip) + 1, juce::dontSendNotification);
    }

    //==============================================================================
    void AudioEditor::paint (juce::Graphics& g)
    {
        g.fillAll (theme::colour::surface);
        g.setColour (theme::colour::border);
        g.fillRect (0, toolbarHeight - 1, getWidth(), 1);
    }

    void AudioEditor::resized()
    {
        auto area = getLocalBounds();
        auto bar = area.removeFromTop (toolbarHeight).reduced (theme::size::padding, 5);
        waveform.setBounds (area);

        const auto gap = [&bar] (int pixels) { bar.removeFromLeft (pixels); };
        const auto place = [&bar] (juce::Component& c, int width) { c.setBounds (bar.removeFromLeft (width)); };

        place (gainLabel, 40);
        place (gain, 66);
        gap (4);
        place (normalize, 76);
        gap (12);
        place (pitchLabel, 40);
        place (pitch, 56);
        gap (12);
        place (follow, 96);
        place (tempoLabel, 38);
        place (loopBpm, 76);
        gap (2);
        place (halve, 28);
        gap (2);
        place (twice, 28);
        gap (4);
        place (earlier, 24);
        gap (2);
        place (later, 24);
        gap (12);
        place (fadeLabel, 42);
        place (fadeShape, 86);
        gap (12);
        place (reverse, 66);
        gap (4);
        place (showClip, 70);
        gap (12);

        if (takes.isVisible())
        {
            place (takes, 96);
            gap (12);
        }
        fileInfo.setBounds (bar);
    }
}
