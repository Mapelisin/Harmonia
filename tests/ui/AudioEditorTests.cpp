#include <doctest.h>

#include "TestAudioFiles.h"
#include "core/AudioTempo.h"
#include "core/Clips.h"
#include "core/Project.h"
#include "core/Tracks.h"
#include "ui/MainComponent.h"
#include "ui/audioeditor/AudioEditor.h"

using namespace harmonia;
using namespace harmonia::ui::audioeditor;
namespace audio_tempo = core::audio_tempo;
namespace clips = core::clips;

namespace
{
    constexpr int left = juce::ModifierKeys::leftButtonModifier;

    void dispatch (int ms = 50)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    /** An instrument track and an audio track with a 4-second tone (8 beats at 120 BPM) at the
        start, the audio track current. The editor's view: 100 pixels per second of the file, the
        file starting at x = 100 (so a beat is 50 pixels), the waveform below a 20-pixel ruler. */
    struct EditorFixture
    {
        EditorFixture()
        {
            instrumentTrack = project->getTracks().getFirst();
            track = arrangement.addAudioTrack();
            REQUIRE (track != nullptr);

            file = test::writeTone (temp.getFile ("Tone.wav"), 4.0, 0.25f);
            juce::String error;
            clip = dynamic_cast<te::AudioClipBase*> (project->addAudioClip (*track, file, 0.0, error));
            REQUIRE (clip != nullptr);

            arrangement.setCurrentTrack (track);
            arrangement.setSnap (core::snap::Mode::beat);
            view.width = 1000.0f;
            view.fileSeconds = model.getFileSeconds();
            view.pixelsPerSecond = 100.0;
            view.leftSeconds = -1.0;
            gestures.setBody ({ 0.0f, 20.0f, 1000.0f, 200.0f });
            dispatch();
            project->clearUndoHistory();
        }

        float xAtBeat (double beat) const       { return view.secondsToX (model.beatToFileSeconds (beat)); }

        void drag (juce::Point<float> from, juce::Point<float> to, int mods = 0, int clicks = 1)
        {
            const juce::ModifierKeys keys (left | mods);
            gestures.down ({ from, keys, clicks });

            for (int i = 1; i <= 10; ++i)
                gestures.drag ({ from + (to - from) * ((float) i / 10.0f), keys, clicks });

            gestures.up ({ to, keys, clicks });
        }

        void doubleClick (juce::Point<float> at)
        {
            const juce::ModifierKeys keys (left);
            gestures.down ({ at, keys, 1 });
            gestures.up ({ at, keys, 1 });
            gestures.down ({ at, keys, 2 });
            gestures.up ({ at, keys, 2 });
        }

        test::TempFolder temp;   // first: deleted last
        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        ui::arrangement::ArrangementModel arrangement { *project };
        AudioEditorModel model { arrangement };
        View view;
        AudioEditorGestures gestures { model, view };
        te::AudioTrack* instrumentTrack = nullptr;
        te::AudioTrack* track = nullptr;
        te::AudioClipBase* clip = nullptr;
        juce::File file;
    };
}

TEST_SUITE ("audio clip editor")
{
    TEST_CASE ("the editor shows the clicked audio clip, else the track's first; nothing on an instrument track")
    {
        EditorFixture f;
        CHECK (f.model.getClip() == f.clip);

        juce::String error;
        auto* second = dynamic_cast<te::AudioClipBase*> (f.project->addAudioClip (*f.track, f.file, 16.0, error));
        REQUIRE (second != nullptr);
        dispatch();
        CHECK (f.model.getClip() == f.clip);

        f.arrangement.focusClip (*second);
        CHECK (f.model.getClip() == second);

        // The clip shown is drawn as the open one; clip mode plays it.
        for (const auto& d : f.arrangement.getClips())
            CHECK (d.open == (d.clip == second));

        f.arrangement.setPlayMode (ui::arrangement::PlayMode::clip);
        CHECK (f.arrangement.getPlayRange().start == doctest::Approx (16.0));

        f.arrangement.setCurrentTrack (f.instrumentTrack);
        CHECK (f.model.getClip() == nullptr);
        CHECK (f.model.getFileSeconds() == doctest::Approx (0.0));

        // Deleted: the track's first again.
        f.arrangement.setCurrentTrack (f.track);
        CHECK (f.model.getClip() == second);
        f.project->beginGesture ("Delete");
        clips::remove ({ second });
        dispatch();
        CHECK (f.model.getClip() == f.clip);
    }

    TEST_CASE ("seconds of the file and beats of the song line up, trimmed or following the tempo")
    {
        EditorFixture f;
        CHECK (f.model.getFileSeconds() == doctest::Approx (4.0).epsilon (0.001));
        CHECK (f.model.getPlayedSeconds().getStart() == doctest::Approx (0.0).epsilon (0.001));
        CHECK (f.model.getPlayedSeconds().getEnd() == doctest::Approx (4.0).epsilon (0.001));
        CHECK (f.model.fileSecondsToBeat (1.0) == doctest::Approx (2.0));
        CHECK (f.model.getPixelsPerBeat (100.0) == doctest::Approx (50.0));

        // Moved later and its beginning hidden: the file still begins where it did in the song.
        f.project->beginGesture ("Move");
        f.clip->setStart (te::TimePosition::fromSeconds (2.0), false, true);
        f.model.trimStart (6.0, 0.25);
        CHECK (clips::getStartBeat (*f.clip) == doctest::Approx (6.0));
        CHECK (f.model.getPlayedSeconds().getStart() == doctest::Approx (1.0).epsilon (0.001));
        CHECK (f.model.fileSecondsToBeat (0.0) == doctest::Approx (4.0));
        CHECK (f.model.beatToFileSeconds (12.0) == doctest::Approx (4.0));

        // A loop at 60 BPM following a song at 120: twice as fast, a second of the file per beat.
        auto* wave = dynamic_cast<te::WaveAudioClip*> (f.clip);
        REQUIRE (wave != nullptr);
        audio_tempo::setFollowsTempo (*wave, 60.0);
        dispatch();
        CHECK (f.model.getPixelsPerBeat (100.0) == doctest::Approx (100.0).epsilon (0.01));
        const auto start = clips::getStartBeat (*f.clip);
        CHECK (f.model.beatToFileSeconds (start + 1.0) - f.model.beatToFileSeconds (start) == doctest::Approx (1.0).epsilon (0.01));
        CHECK (f.model.fileSecondsToBeat (f.model.beatToFileSeconds (start + 1.5)) == doctest::Approx (start + 1.5).epsilon (0.001));

        CHECK (f.model.describeFile().startsWith ("Tone.wav"));
        CHECK (f.model.describeFile().contains ("44.1 kHz"));
        CHECK (f.model.describeFile().contains ("mono"));
        CHECK (f.model.describeFile().contains ("4.00 s"));
    }

    TEST_CASE ("dragging the start and end lines trims the clip: on the grid, free with Alt, never past the file")
    {
        EditorFixture f;
        const auto y = 120.0f;
        CHECK (f.xAtBeat (0.0) == doctest::Approx (100.0f));
        CHECK (f.gestures.hitTest ({ 102.0f, y }) == AudioEditorGestures::Zone::startEdge);
        CHECK (f.gestures.hitTest ({ 498.0f, y }) == AudioEditorGestures::Zone::endEdge);
        CHECK (f.gestures.hitTest ({ 300.0f, y }) == AudioEditorGestures::Zone::none);

        // 2.2 beats in: the beat grid makes it 2.
        f.drag ({ 100.0f, y }, { 210.0f, y });
        CHECK (clips::getStartBeat (*f.clip) == doctest::Approx (2.0));
        CHECK (f.model.getPlayedSeconds().getStart() == doctest::Approx (1.0).epsilon (0.001));
        CHECK (f.project->getUndoDescription() == "Resize clip");

        f.project->undo();
        CHECK (clips::getStartBeat (*f.clip) == doctest::Approx (0.0));

        // Alt: free.
        f.drag ({ 500.0f, y }, { 485.0f, y }, juce::ModifierKeys::altModifier);
        CHECK (clips::getEndBeat (*f.clip) == doctest::Approx (7.7).epsilon (0.002));

        // Not past the end of the file, nor before its start.
        f.drag ({ f.xAtBeat (clips::getEndBeat (*f.clip)), y }, { 800.0f, y });
        CHECK (clips::getEndBeat (*f.clip) == doctest::Approx (8.0).epsilon (0.001));
        f.drag ({ 100.0f, y }, { 20.0f, y });
        CHECK (clips::getStartBeat (*f.clip) == doctest::Approx (0.0));
    }

    TEST_CASE ("fade and gain handles: drag them, double-click resets, the readout says how much")
    {
        EditorFixture f;
        auto handles = f.gestures.getHandles();
        REQUIRE (handles.has_value());
        CHECK (handles->body.getX() == doctest::Approx (100.0f));
        CHECK (handles->body.getRight() == doctest::Approx (500.0f));

        // A beat of fade in (50 pixels), free of the grid.
        const juce::ModifierKeys keys (left);
        const auto from = handles->fadeIn;
        const juce::Point<float> to { 150.0f, from.y };
        f.gestures.down ({ from, keys });

        for (int i = 1; i <= 10; ++i)
            f.gestures.drag ({ from + (to - from) * ((float) i / 10.0f), keys });

        const auto readout = f.gestures.getReadout();
        REQUIRE (readout.has_value());
        CHECK (readout->first.startsWith ("Fade in 1.00 beats"));
        f.gestures.up ({ to, keys });
        CHECK (clips::getFadeInBeats (*f.clip) == doctest::Approx (1.0).epsilon (0.01));
        CHECK (f.project->getUndoDescription() == "Fade in");

        handles = f.gestures.getHandles();
        f.drag (handles->fadeOut, handles->fadeOut - juce::Point<float> (100.0f, 0.0f));
        CHECK (clips::getFadeOutBeats (*f.clip) == doctest::Approx (2.0).epsilon (0.05));

        // Gain: down is quieter.
        handles = f.gestures.getHandles();
        f.drag (handles->gain, handles->gain + juce::Point<float> (0.0f, 30.0f));
        CHECK (clips::getGainDb (*f.clip) < -2.0f);
        CHECK (f.project->getUndoDescription() == "Clip gain");

        f.doubleClick (f.gestures.getHandles()->gain);
        CHECK (clips::getGainDb (*f.clip) == doctest::Approx (0.0f));
        f.doubleClick (f.gestures.getHandles()->fadeIn);
        CHECK (clips::getFadeInBeats (*f.clip) == doctest::Approx (0.0));

        f.project->undo();
        CHECK (clips::getFadeInBeats (*f.clip) == doctest::Approx (1.0).epsilon (0.01));

        // Zoomed out so far the played part is tiny: no handles.
        f.view.pixelsPerSecond = 5.0;
        CHECK_FALSE (f.gestures.getHandles().has_value());
    }

    TEST_CASE ("a click chooses the song position under it; the wheel scrolls and Ctrl+wheel zooms within the file")
    {
        EditorFixture f;
        std::optional<double> chosen;
        f.gestures.onPositionChosen = [&chosen] (double beat) { chosen = beat; };

        const juce::ModifierKeys keys (left);
        f.gestures.down ({ { 300.0f, 120.0f }, keys });
        f.gestures.up ({ { 300.0f, 120.0f }, keys });
        REQUIRE (chosen.has_value());
        CHECK (*chosen == doctest::Approx (4.0));

        // On the ruler too; before the song's start it is the start.
        f.gestures.down ({ { 20.0f, 10.0f }, keys });
        f.gestures.up ({ { 20.0f, 10.0f }, keys });
        CHECK (*chosen == doctest::Approx (0.0));

        // Ctrl+wheel up zooms in around the mouse.
        f.view.pixelsPerSecond = 400.0;
        f.view.leftSeconds = 1.0;
        const auto before = f.view.xToSeconds (300.0f);
        f.gestures.wheel ({ { 300.0f, 120.0f }, juce::ModifierKeys (juce::ModifierKeys::ctrlModifier) }, 0.0f, 0.5f);
        CHECK (f.view.pixelsPerSecond == doctest::Approx (800.0));
        CHECK (f.view.xToSeconds (300.0f) == doctest::Approx (before));

        // Scrolling stops a little past the end of the file.
        for (int i = 0; i < 50; ++i)
            f.gestures.wheel ({ { 300.0f, 120.0f }, juce::ModifierKeys() }, 0.0f, -1.0f);

        CHECK (f.view.getRightSeconds() <= 4.0 + 0.51);
        CHECK (f.view.getRightSeconds() > 4.0);

        // Zoomed out past the whole file: it sits in the middle.
        f.view.pixelsPerSecond = 50.0;
        f.view.clamp();
        CHECK (f.view.leftSeconds == doctest::Approx (-8.0));

        // Showing the played part: all of it, with a little room.
        f.view.show (f.model.getPlayedSeconds());
        CHECK (f.view.leftSeconds < 0.0);
        CHECK (f.view.getRightSeconds() > 4.0);
        CHECK (f.view.getRightSeconds() - f.view.leftSeconds < 4.7);
    }

    TEST_CASE ("the editor's bar: gain, normalize, pitch, tempo, fade shape and reverse, one undo step each")
    {
        EditorFixture f;
        audio::Transport transport { f.project->getEdit() };
        AudioEditor editor { f.arrangement, transport };
        editor.setBounds (0, 0, 1200, 300);

        // Typed, the gain can go above the handle's +6 dB.
        editor.getGainNumber().typeValue ("-6");
        CHECK (clips::getGainDb (*f.clip) == doctest::Approx (-6.0f));
        editor.getGainNumber().typeValue ("12");
        CHECK (clips::getGainDb (*f.clip) == doctest::Approx (12.0f));
        f.project->undo();
        CHECK (clips::getGainDb (*f.clip) == doctest::Approx (-6.0f));

        // The tone peaks at a quarter: normalizing brings it up 12 dB.
        editor.getNormalizeButton().onClick();
        CHECK (clips::getGainDb (*f.clip) == doctest::Approx (12.0f).epsilon (0.02));
        CHECK (f.project->getUndoDescription() == "Normalize");
        f.project->undo();

        editor.getPitchNumber().typeValue ("3");
        CHECK (audio_tempo::getPitch (*f.clip) == doctest::Approx (3.0f));
        CHECK (f.project->getUndoDescription() == "Pitch");

        // Following the tempo: the loop's tempo can be set, halved, doubled.
        CHECK_FALSE (editor.getLoopBpmNumber().isEnabled());
        editor.getFollowButton().onClick();
        CHECK (audio_tempo::followsTempo (*f.clip));
        editor.refresh();
        CHECK (editor.getFollowButton().getToggleState());
        CHECK (editor.getLoopBpmNumber().isEnabled());
        CHECK (editor.getLoopBpmNumber().getValue() == doctest::Approx (120.0));

        editor.getLoopBpmNumber().typeValue ("110");
        CHECK (*audio_tempo::getLoopBpm (*f.clip) == doctest::Approx (110.0));
        editor.getHalveButton().onClick();
        CHECK (*audio_tempo::getLoopBpm (*f.clip) == doctest::Approx (55.0));
        editor.getDoubleButton().onClick();
        editor.getDoubleButton().onClick();
        CHECK (*audio_tempo::getLoopBpm (*f.clip) == doctest::Approx (220.0));
        f.project->undo();
        CHECK (*audio_tempo::getLoopBpm (*f.clip) == doctest::Approx (110.0));

        // Not below the slowest loop tempo.
        editor.getHalveButton().onClick();
        editor.getHalveButton().onClick();
        CHECK (*audio_tempo::getLoopBpm (*f.clip) == doctest::Approx (55.0));

        editor.getFollowButton().onClick();
        CHECK_FALSE (audio_tempo::followsTempo (*f.clip));

        // Fade shape.
        auto& shapes = editor.getFadeShapeBox();
        shapes.setSelectedId ((int) clips::FadeShape::sCurve + 1, juce::sendNotificationSync);
        CHECK (clips::getFadeShape (*f.clip) == clips::FadeShape::sCurve);
        CHECK (f.project->getUndoDescription() == "Fade shape");

        // Reverse, and back.
        editor.getReverseButton().onClick();
        CHECK (clips::isReversed (*f.clip));
        editor.refresh();
        CHECK (editor.getReverseButton().getToggleState());
        const auto shown = editor.getWaveform().getMessage();
        CHECK ((shown.isEmpty() || shown == "Reversing..."));

        for (int i = 0; i < 100 && clips::isReversing (*f.clip); ++i)
            dispatch();

        CHECK_FALSE (clips::isReversing (*f.clip));
        f.project->undo();
        CHECK_FALSE (clips::isReversed (*f.clip));

        // On an instrument track there is nothing to edit.
        f.arrangement.setCurrentTrack (f.instrumentTrack);
        editor.refresh();
        CHECK_FALSE (editor.getGainNumber().isEnabled());
        CHECK (editor.getWaveform().getMessage().startsWith ("No audio clip"));
        CHECK (editor.getFileLabel().getText().isEmpty());
    }

    TEST_CASE ("Follow tempo measures the sound: any tempo, the clip starts on its first beat, and says what it found")
    {
        // Clicks at 72.3 BPM after 0.3 s of silence, in a song at 120: nothing but the sound says so.
        test::TempFolder temp;
        const auto file = temp.getFile ("Mystery.wav");
        {
            constexpr double rate = 44100.0, bpm = 72.3;
            juce::AudioBuffer<float> buffer (1, (int) ((0.3 + 12 * 60.0 / bpm + 0.2) * rate));
            buffer.clear();

            for (int beat = 0; beat < 12; ++beat)
                for (int i = 0; i < 441; ++i)
                    buffer.setSample (0, (int) std::round ((0.3 + beat * 60.0 / bpm) * rate) + i,
                                      0.9f * (float) std::sin (i * 0.1425) * (float) std::exp (-i / 132.0));

            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
            auto writer = juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions {}.withSampleRate (rate)
                                                                              .withNumChannels (1).withBitsPerSample (16));
            REQUIRE (writer != nullptr);
            writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        }

        auto project = core::Project::createEmpty (test::session());
        ui::arrangement::ArrangementModel arrangement (*project);
        auto* track = arrangement.addAudioTrack();
        REQUIRE (track != nullptr);
        juce::String error;
        auto* clip = project->addAudioClip (*track, file, 8.0, error);
        REQUIRE_MESSAGE (clip != nullptr, error.toStdString());
        arrangement.setCurrentTrack (track);
        dispatch();
        project->clearUndoHistory();

        audio::Transport transport { project->getEdit() };
        AudioEditor editor { arrangement, transport };
        editor.setBounds (0, 0, 1200, 300);
        juce::String said;
        auto wasProblem = true;
        editor.onMessage = [&said, &wasProblem] (const juce::String& text, bool isProblem) { said = text; wasProblem = isProblem; };
        editor.refresh();
        CHECK_FALSE (editor.getEarlierButton().isEnabled());

        // One click.
        editor.getFollowButton().onClick();
        editor.refresh();
        REQUIRE (audio_tempo::followsTempo (*clip));
        CHECK (*audio_tempo::getLoopBpm (*clip) == doctest::Approx (144.6).epsilon (0.0005));       // nearest the song's 120
        CHECK (editor.getLoopBpmNumber().getValue() == doctest::Approx (144.6).epsilon (0.0005));
        CHECK (said.startsWith ("Its beat was measured: 144.6"));
        CHECK (said.contains ("first beat 0.30 s in"));
        CHECK_FALSE (wasProblem);
        CHECK (project->getUndoDescription() == "Follow the song tempo");

        // The clip stays where it was put and starts on the first hit: the hit is on beat 8.
        CHECK (clips::getStartBeat (*clip) == doctest::Approx (8.0));
        CHECK (editor.getModel().fileSecondsToBeat (0.3) == doctest::Approx (8.0).epsilon (0.0005));
        CHECK (editor.getModel().fileSecondsToBeat (0.3 + 60.0 / 72.3) == doctest::Approx (10.0).epsilon (0.0005));

        // Half: the tempo as another song would hear it. The arrows slide the sound by sixteenths.
        editor.getHalveButton().onClick();
        CHECK (*audio_tempo::getLoopBpm (*clip) == doctest::Approx (72.3).epsilon (0.0005));
        editor.getDoubleButton().onClick();
        editor.refresh();
        CHECK (editor.getEarlierButton().isEnabled());
        editor.getEarlierButton().onClick();
        CHECK (editor.getModel().fileSecondsToBeat (0.3) == doctest::Approx (7.75).epsilon (0.0005));
        CHECK (project->getUndoDescription() == "Slide the sound");
        editor.getLaterButton().onClick();
        CHECK (editor.getModel().fileSecondsToBeat (0.3) == doctest::Approx (8.0).epsilon (0.0005));

        // One Ctrl+Z each; the first click undone leaves the clip as it was dropped.
        project->undo();
        project->undo();
        project->undo();
        project->undo();
        CHECK (project->getUndoDescription() == "Follow the song tempo");
        project->undo();
        dispatch();
        CHECK_FALSE (audio_tempo::followsTempo (*clip));
        CHECK (clip->getPosition().getOffset().inSeconds() == doctest::Approx (0.0));
        CHECK_FALSE (project->canUndo());

        // A sound with no beat in it: said as a problem, and it plays as before.
        const auto pad = test::writeTone (temp.getFile ("Pad.wav"), 3.0, 0.3f);
        auto* padClip = project->addAudioClip (*track, pad, 40.0, error);
        REQUIRE (padClip != nullptr);
        dispatch();
        arrangement.focusClip (*padClip);
        editor.refresh();
        editor.getFollowButton().onClick();
        CHECK (audio_tempo::followsTempo (*padClip));
        CHECK (*audio_tempo::getLoopBpm (*padClip) == doctest::Approx (120.0));
        CHECK (said.startsWith ("No steady beat could be heard"));
        CHECK (wasProblem);
    }

    TEST_CASE ("the main window paints the audio clip editor in place of the piano roll on an audio track")
    {
        test::TempFolder temp;
        auto project = core::Project::createEmpty (test::session());
        auto main = std::make_unique<ui::MainComponent> (test::session(), *project);
        main->setBounds (0, 0, 1280, 800);
        auto& model = main->getArrangementModel();

        CHECK (main->getPianoRoll().isVisible());
        CHECK_FALSE (main->getAudioEditor().isVisible());
        CHECK (main->getEditorName() == "Piano roll");

        const auto drums = test::writeTone (temp.getFile ("Drum loop 120 BPM.wav"), 8.0, 0.9f, 2, true);
        CHECK (model.importAudio ({ drums }, -1, 0.0).isEmpty());
        auto* clip = dynamic_cast<te::AudioClipBase*> (clips::getClips (*model.getTrack (1)).getFirst());
        REQUIRE (clip != nullptr);

        // Its first bar hidden, a fade in and out, 3 dB down.
        project->beginGesture ("Edit");
        clips::setStart (*clip, 4.0, 0.25);
        clips::setFadeShape (*clip, clips::FadeShape::sCurve);
        clips::setFadeInBeats (*clip, 1.0);
        clips::setFadeOutBeats (*clip, 3.0);
        clips::setGainDb (*clip, -3.0f);
        model.setSelection ({ clip });
        model.focusClip (*clip);
        main->getTransport().setPositionSeconds (3.0);
        dispatch();

        CHECK (main->editsAudio());
        CHECK (main->getEditorName() == "Audio clip");
        CHECK (main->getAudioEditor().isVisible());
        CHECK_FALSE (main->getPianoRoll().isVisible());
        CHECK (main->isArrangementTheEditTarget());
        CHECK (main->getAudioEditor().getModel().getClip() == clip);
        CHECK (main->getAudioEditor().getGainNumber().getValue() == doctest::Approx (-3.0));

        bool named = false;
        const auto viewMenu = main->getMainMenu().createViewMenu();

        for (juce::PopupMenu::MenuItemIterator it (viewMenu); it.next();)
            named = named || (it.getItem().text == "Audio clip" && it.getItem().isTicked);

        CHECK (named);

        // Another panel and back: still the audio clip editor.
        main->showLowerPanel (ui::MainComponent::LowerPanel::mixer);
        CHECK_FALSE (main->getAudioEditor().isVisible());
        main->showLowerPanel (ui::MainComponent::LowerPanel::pianoRoll);
        CHECK (main->getAudioEditor().isVisible());
        CHECK_FALSE (main->getPianoRoll().isVisible());

        auto& waveform = main->getAudioEditor().getWaveform();
        juce::Image image;

        for (int i = 0; i < 100; ++i)
        {
            dispatch();
            image = main->createComponentSnapshot (main->getLocalBounds());

            if (waveform.isWaveformLoaded() && main->getArrangement().getClipArea().areWaveformsLoaded())
                break;
        }

        image = main->createComponentSnapshot (main->getLocalBounds());
        CHECK (waveform.isWaveformLoaded());
        CHECK (image.isValid());

        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("main-audio-editor.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }

        // Back on the instrument track: the piano roll.
        model.setCurrentTrack (model.getTrack (0));
        CHECK (main->getPianoRoll().isVisible());
        CHECK_FALSE (main->getAudioEditor().isVisible());

        main.reset();
    }
}
