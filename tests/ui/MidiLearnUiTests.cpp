#include <doctest.h>

#include "TestSession.h"
#include "audio/Effects.h"
#include "audio/Instruments.h"
#include "audio/MidiLearn.h"
#include "audio/PluginControls.h"
#include "audio/Sends.h"
#include "core/Project.h"
#include "ui/MainComponent.h"
#include "ui/ParameterMenu.h"
#include "ui/ParameterPanel.h"
#include "ui/mixer/Mixer.h"

using namespace harmonia;
namespace learn = audio::midi_learn;

namespace
{
    /** The item whose text starts with `text`, in `menu` or its submenus. */
    std::optional<juce::PopupMenu::Item> find (const juce::PopupMenu& menu, const juce::String& text)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
            if (it.getItem().text.startsWith (text))
                return it.getItem();

        return std::nullopt;
    }

    void choose (const juce::PopupMenu& menu, const juce::String& text)
    {
        const auto item = find (menu, text);
        REQUIRE_MESSAGE (item.has_value(), text.toStdString());
        REQUIRE_MESSAGE (item->isEnabled, text.toStdString());
        REQUIRE (item->action != nullptr);
        item->action();
    }

    void drag (juce::Slider& slider, double value)
    {
        if (slider.onDragStart != nullptr)
            slider.onDragStart();

        slider.setValue (value, juce::sendNotificationSync);

        if (slider.onDragEnd != nullptr)
            slider.onDragEnd();
    }

    juce::KeyPress ctrlShift (char letter)
    {
        return juce::KeyPress (letter, juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0);
    }

    /** Counts the grabs and let-gos of a setting (what writing automation listens to). */
    struct Gestures final : te::AutomatableParameter::Listener
    {
        explicit Gestures (te::AutomatableParameter& p) : parameter (&p)  { parameter->addListener (this); }
        ~Gestures() override                                            { parameter->removeListener (this); }

        void curveHasChanged (te::AutomatableParameter&) override {}
        void parameterChangeGestureBegin (te::AutomatableParameter&) override   { ++begun; }
        void parameterChangeGestureEnd (te::AutomatableParameter&) override     { ++ended; }

        te::AutomatableParameter::Ptr parameter;
        int begun = 0, ended = 0;
    };

    struct MainFixture
    {
        MainFixture()
        {
            auto& track = *project->getTracks().getFirst();
            track.setName ("Keys");
            audio::instruments::useBuiltInSynth (track);
            project->addMidiClip (track, 0.0, 16.0, "Main");
            project->clearUndoHistory();
            main = std::make_unique<ui::MainComponent> (test::session(), *project);
            main->setBounds (0, 0, 1280, 800);
        }

        ~MainFixture()
        {
            main.reset();
        }

        te::AudioTrack& track()     { return *project->getTracks().getFirst(); }

        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        std::unique_ptr<ui::MainComponent> main;
    };
}

TEST_SUITE ("ui: midi learn")
{
    TEST_CASE ("a setting's right-click menu: MIDI Learn (again: cancel), forget its control; its mark")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();
        auto& volume = *track.getVolumePlugin()->volParam;

        const auto none = ui::parameter_menu::make (nullptr, "Turn it up first");
        REQUIRE (find (none, "Turn it up first").has_value());
        CHECK_FALSE (find (none, "Turn it up first")->isEnabled);

        // Without a controller (no main window) there's nothing to learn with.
        CHECK_FALSE (find (ui::parameter_menu::make (&volume), "MIDI Learn")->isEnabled);

        learn::Controller controller (project->getEdit());
        CHECK (ui::parameter_menu::getBadge (volume).isEmpty());
        choose (ui::parameter_menu::make (&volume), "MIDI Learn");
        CHECK (controller.getWaitingParameter() == &volume);
        CHECK (ui::parameter_menu::getBadge (volume) == "Learn...");

        const auto waiting = ui::parameter_menu::make (&volume);
        CHECK (find (waiting, "MIDI Learn")->isTicked);
        choose (waiting, "MIDI Learn");
        CHECK_FALSE (controller.isLearning());

        choose (ui::parameter_menu::make (&volume), "MIDI Learn");
        controller.handle (juce::MidiMessage::controllerEvent (1, 21, 64));
        CHECK (ui::parameter_menu::getBadge (volume) == "CC 21");

        const auto assigned = ui::parameter_menu::make (&volume);
        CHECK (find (assigned, track.getName() + ": Volume").has_value());     // which setting it is
        choose (assigned, "Forget CC 21 (channel 1)");
        CHECK_FALSE (learn::getControl (volume).isValid());
        CHECK_FALSE (find (ui::parameter_menu::make (&volume), "Forget").has_value());
    }

    TEST_CASE ("a knobs window row shows the control that moves it")
    {
        auto project = core::Project::createEmpty (test::session());
        auto& track = *project->getTracks().getFirst();
        juce::String error;
        auto* compressor = audio::effects::add (track.pluginList, audio::effects::getBuiltIn().getFirst(), error);
        REQUIRE (compressor != nullptr);

        ui::ParameterPanel panel (*compressor);
        REQUIRE (panel.getNumRows() > 1);
        CHECK (panel.getBadge (0).isEmpty());

        auto* parameter = audio::PluginControl::getAll (*compressor).front().getParameter();
        REQUIRE (parameter != nullptr);
        learn::assign (*parameter, { 2, 74 });
        panel.refresh();
        CHECK (panel.getBadge (0) == "CC 74");
        CHECK (panel.getBadge (1).isEmpty());
    }

    TEST_CASE ("the mixer's fader, pan knob and send bars hold their setting while dragged, and have the menu")
    {
        MainFixture f;
        auto& mixer = f.main->getMixer();
        auto* reverb = mixer.addReturnTrack (audio::sends::Preset::reverb);
        REQUIRE (reverb != nullptr);
        mixer.refresh();

        auto& strip = *mixer.getTrackStrip (0);
        auto* volumePlugin = f.track().getVolumePlugin();
        Gestures volume (*volumePlugin->volParam), pan (*volumePlugin->panParam);

        drag (strip.getFader(), 0.3);
        CHECK (volume.begun == 1);
        CHECK (volume.ended == 1);
        drag (strip.getPanKnob(), 0.4);
        CHECK (pan.begun == 1);
        CHECK (pan.ended == 1);

        // The first drag of a send makes it, and holds it from then on: let go at the end.
        REQUIRE (strip.getNumSends() == 1);
        auto& sendBar = *strip.getSendSlider (0);
        REQUIRE (audio::sends::getSend (f.track(), *reverb) == nullptr);
        REQUIRE (sendBar.onDragStart != nullptr);
        REQUIRE (sendBar.onDragEnd != nullptr);

        sendBar.onDragStart();
        sendBar.setValue (0.5, juce::sendNotificationSync);
        auto* send = audio::sends::getSend (f.track(), *reverb);
        REQUIRE (send != nullptr);

        {
            Gestures sendGestures (*send->gain);
            sendBar.onDragEnd();
            CHECK (sendGestures.ended == 1);
        }

        for (auto* slider : { &strip.getFader(), &strip.getPanKnob(), &sendBar, &mixer.getMasterStrip().getFader() })
        {
            auto* menuSlider = dynamic_cast<ui::MenuSlider*> (slider);
            REQUIRE (menuSlider != nullptr);
            CHECK (menuSlider->onPopupMenu != nullptr);
        }

        learn::assign (*volumePlugin->volParam, { 1, 7 });
        mixer.refresh();
        CHECK (strip.getFader().getTooltip().contains ("moved by CC 7"));
    }

    TEST_CASE ("what a MIDI keyboard sends reaches MIDI Learn: its knob is learned, then moves the setting")
    {
        MainFixture f;
        auto& inputs = f.main->getMidiInputs();
        auto& controls = f.main->getMidiControls();
        auto& volume = *f.track().getVolumePlugin()->volParam;

        const auto letMessagesThrough = []
        {
            for (int i = 0; i < 10; ++i)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        };

        // Not a control: the setting goes on waiting.
        controls.learn (volume);
        inputs.post (juce::MidiMessage::midiClock());
        inputs.post (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
        inputs.post (juce::MidiMessage::noteOff (1, 60));
        letMessagesThrough();
        CHECK (controls.getWaitingParameter() == &volume);

        // The knob, as the keyboard sends it.
        inputs.post (juce::MidiMessage::controllerEvent (2, 21, 64));
        letMessagesThrough();
        CHECK (learn::getControl (volume).number == 21);
        CHECK (learn::getControl (volume).channel == 2);
        CHECK (controls.getWaitingParameter() == nullptr);

        // It moves the fader now.
        inputs.post (juce::MidiMessage::controllerEvent (2, 21, 0));
        letMessagesThrough();
        CHECK (volume.getCurrentNormalisedValue() < 0.02f);
        inputs.post (juce::MidiMessage::controllerEvent (2, 21, 127));
        letMessagesThrough();
        CHECK (volume.getCurrentNormalisedValue() > 0.98f);

        // Let the knob's turn end before the window goes.
        for (int i = 0; i < 50; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }

    TEST_CASE ("main window: Write and Ctrl+Shift+A write automation, Ctrl+Shift+L and Edit learn, controls listed to forget")
    {
        MainFixture f;
        auto& writer = f.main->getAutomationWriter();
        auto& controls = f.main->getMidiControls();
        auto& menu = f.main->getMainMenu();
        CHECK (learn::Controller::find (f.project->getEdit()) == &controls);

        // Writing automation.
        CHECK_FALSE (writer.isOn());
        CHECK_FALSE (find (menu.createTransportMenu(), "Write automation")->isTicked);
        CHECK (find (menu.createTransportMenu(), "Write automation")->shortcutKeyDescription == "Ctrl+Shift+A");

        auto& write = f.main->getTransportBar().getWriteButton();
        REQUIRE (write.onClick != nullptr);
        write.onClick();
        CHECK (writer.isOn());
        CHECK (write.findColour (juce::TextButton::buttonColourId) != f.main->getTransportBar().getClickButton().findColour (juce::TextButton::buttonColourId));
        CHECK (find (menu.createTransportMenu(), "Write automation")->isTicked);
        CHECK (f.main->keyPressed (ctrlShift ('a')));
        CHECK_FALSE (writer.isOn());
        choose (menu.createTransportMenu(), "Write automation");
        CHECK (writer.isOn());
        writer.setOn (false);

        // The piano roll leaves Ctrl+Shift+L and Ctrl+Shift+A to the window (Ctrl+L: legato there).
        CHECK_FALSE (f.main->getPianoRoll().keyPressed (ctrlShift ('l')));
        CHECK_FALSE (f.main->getPianoRoll().keyPressed (ctrlShift ('a')));

        // MIDI Learn for whatever is moved next.
        CHECK (f.main->keyPressed (ctrlShift ('l')));
        CHECK (controls.isLearning());
        CHECK (find (menu.createEditMenu(), "MIDI Learn")->isTicked);
        choose (menu.createEditMenu(), "MIDI Learn");
        CHECK_FALSE (controls.isLearning());

        // The song's controls, each to forget.
        CHECK_FALSE (find (menu.createEditMenu(), "None yet")->isEnabled);
        auto& volume = *f.track().getVolumePlugin()->volParam;
        auto& pan = *f.track().getVolumePlugin()->panParam;
        learn::assign (volume, { 1, 21 });
        learn::assign (pan, { 1, 22 });

        const auto edit = menu.createEditMenu();
        CHECK (find (edit, "Forget: CC 21 (channel 1) -> Keys: Volume").has_value());
        choose (edit, "Forget: CC 22 (channel 1) -> Keys: Pan");
        CHECK_FALSE (learn::getControl (pan).isValid());
        CHECK (learn::getControl (volume).isValid());
        choose (menu.createEditMenu(), "Forget them all");
        CHECK (learn::getAll (f.project->getEdit()).empty());
    }
}
