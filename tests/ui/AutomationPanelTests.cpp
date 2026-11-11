#include <doctest.h>

#include "TestSession.h"
#include "audio/Automation.h"
#include "audio/Effects.h"
#include "audio/Instruments.h"
#include "core/Project.h"
#include "ui/MainComponent.h"
#include "ui/automation/AutomationPanel.h"

using namespace harmonia;
namespace automation = audio::automation;
using ui::automation::AutomationGestures;

namespace
{
    void dispatch()
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    }

    /** A project, the arrangement's model and view (20 pixels per beat) and the gestures of a
        120-pixel lane editing the first track's volume. */
    struct GestureFixture
    {
        GestureFixture()
        {
            project->clearUndoHistory();
            view.pixelsPerBeat = 20.0;
            view.width = 800.0f;
            gestures.setHeight (120.0f);
            gestures.setParameter (&volume());
            model.setSnap (core::snap::Mode::beat);
        }

        te::AudioTrack& track() const           { return *project->getTracks().getFirst(); }
        te::AutomatableParameter& volume() const { return *track().getVolumePlugin()->volParam; }
        std::vector<automation::Point> points() const { return automation::getPoints (volume()); }

        juce::Point<float> at (double beat, float value) const
        {
            return { view.beatToX (beat), gestures.valueToY (value) };
        }

        AutomationGestures::Pointer pointer (juce::Point<float> p, juce::ModifierKeys mods = {}, int clicks = 1) const
        {
            return { p, mods, clicks };
        }

        void click (juce::Point<float> p, juce::ModifierKeys mods = {}, int clicks = 1)
        {
            gestures.down (pointer (p, mods, clicks));
            gestures.up (pointer (p, mods, clicks));
        }

        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        ui::arrangement::ArrangementModel model { *project };
        ui::arrangement::ArrangementView view;
        AutomationGestures gestures { model, view };
    };

    const juce::ModifierKeys alt { juce::ModifierKeys::altModifier | juce::ModifierKeys::leftButtonModifier };
    const juce::ModifierKeys shift { juce::ModifierKeys::shiftModifier | juce::ModifierKeys::leftButtonModifier };
    const juce::ModifierKeys rightButton { juce::ModifierKeys::rightButtonModifier };

    struct MainFixture
    {
        MainFixture()
        {
            auto& track = *project->getTracks().getFirst();
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

        ui::automation::AutomationPanel& panel()    { return main->getAutomationPanel(); }

        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        std::unique_ptr<ui::MainComponent> main;
    };
}

TEST_SUITE ("automation: gestures")
{
    TEST_CASE ("values run from the bottom of the lane to the top, inside its margins")
    {
        GestureFixture f;
        CHECK (f.gestures.valueToY (1.0f) == doctest::Approx (AutomationGestures::margin));
        CHECK (f.gestures.valueToY (0.0f) == doctest::Approx (120.0f - AutomationGestures::margin));
        CHECK (f.gestures.yToValue (f.gestures.valueToY (0.3f)) == doctest::Approx (0.3f));
        CHECK (f.gestures.yToValue (-50.0f) == 1.0f);
        CHECK (f.gestures.yToValue (500.0f) == 0.0f);
    }

    TEST_CASE ("a click adds a point on the grid and dragging places it; one undo step")
    {
        GestureFixture f;

        // Beat 2.3 snaps to beat 2 (snap: beat).
        f.gestures.down (f.pointer (f.at (2.3, 0.5f)));
        CHECK (f.gestures.getDraggedPoint() == 0);
        REQUIRE (f.points().size() == 1);
        CHECK (f.points()[0].beat == doctest::Approx (2.0));
        CHECK (f.points()[0].value == doctest::Approx (0.5f).epsilon (0.01));

        f.gestures.drag (f.pointer (f.at (5.2, 0.75f)));
        f.gestures.up (f.pointer (f.at (5.2, 0.75f)));
        CHECK (f.gestures.getDraggedPoint() == -1);
        REQUIRE (f.points().size() == 1);
        CHECK (f.points()[0].beat == doctest::Approx (5.0));
        CHECK (f.points()[0].value == doctest::Approx (0.75f).epsilon (0.01));

        // Alt: off the grid.
        f.click (f.at (7.3, 0.2f), alt);
        REQUIRE (f.points().size() == 2);
        CHECK (f.points()[1].beat == doctest::Approx (7.3).epsilon (0.01));

        f.project->undo();
        CHECK (f.points().size() == 1);
        f.project->undo();
        CHECK (f.points().empty());
    }

    TEST_CASE ("dragging a point moves it, not past its neighbours; Shift keeps its beat")
    {
        GestureFixture f;
        f.click (f.at (2.0, 0.5f));
        f.click (f.at (6.0, 0.5f));
        f.click (f.at (10.0, 0.5f));
        REQUIRE (f.points().size() == 3);

        // Grab the middle point a little off its centre.
        CHECK (f.gestures.pointAt (f.at (6.0, 0.5f) + juce::Point<float> (3.0f, -2.0f)) == 1);
        CHECK (f.gestures.pointAt (f.at (6.0, 0.5f) + juce::Point<float> (12.0f, 0.0f)) == -1);

        f.gestures.down (f.pointer (f.at (6.0, 0.5f)));
        f.gestures.drag (f.pointer (f.at (14.0, 0.9f)));
        f.gestures.up (f.pointer (f.at (14.0, 0.9f)));
        REQUIRE (f.points().size() == 3);
        CHECK (f.points()[1].beat == doctest::Approx (10.0));   // stopped at the next point
        CHECK (f.points()[1].value == doctest::Approx (0.9f).epsilon (0.01));

        f.gestures.down (f.pointer (f.at (2.0, 0.5f), shift));
        f.gestures.drag (f.pointer (f.at (4.0, 0.1f), shift));
        f.gestures.up (f.pointer (f.at (4.0, 0.1f), shift));
        CHECK (f.points()[0].beat == doctest::Approx (2.0));
        CHECK (f.points()[0].value == doctest::Approx (0.1f).epsilon (0.01));
    }

    TEST_CASE ("right-click and double-click delete points; a right-drag deletes what it passes")
    {
        GestureFixture f;

        for (const auto beat : { 2.0, 4.0, 6.0, 8.0 })
            f.click (f.at (beat, 0.5f));

        REQUIRE (f.points().size() == 4);

        f.click (f.at (2.0, 0.5f), rightButton);
        CHECK (f.points().size() == 3);

        f.click (f.at (4.0, 0.5f), {}, 2);
        CHECK (f.points().size() == 2);

        // A right-click on empty space adds nothing.
        f.click (f.at (12.0, 0.9f), rightButton);
        CHECK (f.points().size() == 2);

        f.gestures.down (f.pointer (f.at (5.0, 0.5f), rightButton));
        f.gestures.drag (f.pointer (f.at (6.0, 0.5f), rightButton));
        f.gestures.drag (f.pointer (f.at (8.0, 0.5f), rightButton));
        f.gestures.up (f.pointer (f.at (8.0, 0.5f), rightButton));
        CHECK (f.points().empty());

        // The whole drag was one step.
        f.project->undo();
        CHECK (f.points().size() == 2);
    }

    TEST_CASE ("the handle between two points bends the line and stays under the mouse; double-click and right-click straighten")
    {
        GestureFixture f;
        f.click (f.at (2.0, 0.2f));
        f.click (f.at (10.0, 0.8f));
        f.click (f.at (12.0, 0.8f));
        f.click (f.at (13.0, 0.3f));
        REQUIRE (f.points().size() == 4);
        f.project->clearUndoHistory();

        // The rise has its handle in the middle; the level stretch has none, nor has a stretch too
        // narrow to take hold of (20 pixels here).
        const auto handle = f.gestures.getBendHandle (0);
        REQUIRE (handle.has_value());
        CHECK (handle->x == doctest::Approx (f.at (6.0, 0.5f).x));
        CHECK (handle->y == doctest::Approx (f.at (6.0, 0.5f).y).epsilon (0.01));
        CHECK_FALSE (f.gestures.getBendHandle (1).has_value());
        CHECK_FALSE (f.gestures.getBendHandle (2).has_value());
        CHECK_FALSE (f.gestures.getBendHandle (3).has_value());

        CHECK (f.gestures.bendAt (*handle + juce::Point<float> (3.0f, -2.0f)) == 0);
        CHECK (f.gestures.bendAt (*handle + juce::Point<float> (9.0f, 0.0f)) == -1);
        CHECK (f.gestures.bendAt (f.at (2.0, 0.2f)) == -1);

        // Taken a little off its centre and dragged down: the line bends late, and the handle has
        // moved by what the mouse did.
        const auto grab = *handle + juce::Point<float> (2.0f, -2.0f);
        f.gestures.down (f.pointer (grab));
        CHECK (f.gestures.getBentStretch() == 0);
        CHECK (f.gestures.getDraggedPoint() == -1);
        CHECK (f.points().size() == 4);                 // no point was added
        CHECK (f.points()[0].curve == 0.0f);            // nor the line bent by the click alone

        f.gestures.drag (f.pointer (grab + juce::Point<float> (0.0f, 15.0f)));
        CHECK (f.points()[0].curve > 0.0f);
        CHECK (f.gestures.getBendHandle (0)->y == doctest::Approx (handle->y + 15.0f).epsilon (0.01));
        CHECK (f.gestures.getBendHandle (0)->x > handle->x);

        // Far above the stretch: as soon as it can be, and no further.
        f.gestures.drag (f.pointer (grab + juce::Point<float> (0.0f, -200.0f)));
        CHECK (f.points()[0].curve == -1.0f);
        f.gestures.drag (f.pointer (grab + juce::Point<float> (0.0f, 10.0f)));
        f.gestures.up (f.pointer (grab + juce::Point<float> (0.0f, 10.0f)));
        CHECK (f.gestures.getBentStretch() == -1);
        const auto bent = f.points()[0].curve;
        CHECK (bent > 0.0f);
        CHECK (bent < 1.0f);
        CHECK (f.project->getUndoDescription() == "Bend automation line");

        // The whole drag was one step.
        f.project->undo();
        CHECK (f.points()[0].curve == 0.0f);
        f.project->redo();
        CHECK (f.points()[0].curve == bent);

        // A double-click on the handle: straight again. So does a right-click.
        f.click (*f.gestures.getBendHandle (0), {}, 2);
        CHECK (f.points()[0].curve == 0.0f);
        CHECK (f.project->getUndoDescription() == "Straighten automation line");
        CHECK (f.points().size() == 4);

        f.project->undo();
        CHECK (f.points()[0].curve == bent);
        f.click (*f.gestures.getBendHandle (0), rightButton);
        CHECK (f.points()[0].curve == 0.0f);
        CHECK (f.points().size() == 4);

        // Zoomed out until the stretch is too narrow to take hold of (under 24 pixels): no handle.
        f.view.pixelsPerBeat = 2.9;
        CHECK_FALSE (f.gestures.getBendHandle (0).has_value());
        CHECK (f.gestures.bendAt (f.at (6.0, 0.5f)) == -1);
        f.view.pixelsPerBeat = 20.0;

        // The mouse over a handle says so.
        CHECK (f.gestures.getCursorFor (*f.gestures.getBendHandle (0)) == juce::MouseCursor::UpDownResizeCursor);
        CHECK (f.gestures.getCursorFor (f.at (2.0, 0.2f)) == juce::MouseCursor::DraggingHandCursor);
        CHECK (f.gestures.getCursorFor (f.at (20.0, 0.9f)) == juce::MouseCursor::CrosshairCursor);
    }

    TEST_CASE ("without a parameter the gestures do nothing")
    {
        GestureFixture f;
        f.gestures.setParameter (nullptr);
        f.click (f.at (2.0, 0.5f));
        CHECK (f.points().empty());
        CHECK (f.gestures.pointAt (f.at (2.0, 0.5f)) == -1);
    }
}

TEST_SUITE ("automation: panel")
{
    TEST_CASE ("F8 shows the automation of the current track; it starts on Volume")
    {
        using Panel = ui::MainComponent::LowerPanel;
        MainFixture f;
        CHECK_FALSE (f.panel().isVisible());

        CHECK (f.main->keyPressed (juce::KeyPress (juce::KeyPress::F8Key)));
        CHECK (f.main->getLowerPanel() == Panel::automation);
        CHECK (f.panel().isVisible());
        CHECK_FALSE (f.main->getPianoRoll().isVisible());
        CHECK_FALSE (f.main->getMixer().isVisible());

        auto& track = *f.project->getTracks().getFirst();
        CHECK (f.panel().getParameter() == track.getVolumePlugin()->volParam.get());
        CHECK (f.panel().getParameterButton().getButtonText() == "Volume");

        // The lane lines up with the arrangement's clips above it.
        const auto lane = f.main->getLocalArea (&f.panel().getLane(), f.panel().getLane().getLocalBounds());
        const auto clips = f.main->getLocalArea (&f.main->getArrangement().getClipArea(),
                                                 f.main->getArrangement().getClipArea().getLocalBounds());
        CHECK (lane.getX() == clips.getX());
        CHECK (lane.getWidth() == clips.getWidth());

        CHECK (f.main->keyPressed (juce::KeyPress (juce::KeyPress::F7Key)));
        CHECK_FALSE (f.panel().isVisible());
    }

    TEST_CASE ("each track remembers its parameter; a track with a line starts on it")
    {
        MainFixture f;
        auto& arrangement = f.main->getArrangementModel();
        auto& first = *f.project->getTracks().getFirst();
        auto* second = arrangement.addTrack();
        auto* third = arrangement.addTrack();
        REQUIRE (second != nullptr);
        REQUIRE (third != nullptr);
        dispatch();

        // A line on the third track's pan.
        automation::addPoint (*third->getVolumePlugin()->panParam, 0.0, 0.2f);

        arrangement.setCurrentTrack (&first);
        f.main->showLowerPanel (ui::MainComponent::LowerPanel::automation);
        dispatch();
        f.panel().setParameter (first.getVolumePlugin()->panParam.get());
        CHECK (f.panel().getParameterButton().getButtonText() == "Pan");

        arrangement.setCurrentTrack (second);
        dispatch();
        CHECK (f.panel().getParameter() == second->getVolumePlugin()->volParam.get());

        arrangement.setCurrentTrack (third);
        dispatch();
        CHECK (f.panel().getParameter() == third->getVolumePlugin()->panParam.get());

        arrangement.setCurrentTrack (&first);
        dispatch();
        CHECK (f.panel().getParameter() == first.getVolumePlugin()->panParam.get());

        // Another track's parameter isn't taken.
        f.panel().setParameter (second->getVolumePlugin()->volParam.get());
        CHECK (f.panel().getParameter() == first.getVolumePlugin()->panParam.get());
    }

    TEST_CASE ("an effect's knob can be automated; when the effect goes the panel falls back")
    {
        MainFixture f;
        auto& track = *f.project->getTracks().getFirst();
        f.main->showLowerPanel (ui::MainComponent::LowerPanel::automation);

        audio::effects::EffectType filter;

        for (const auto& type : audio::effects::getBuiltIn())
            if (type.name == "Filter")
                filter = type;

        juce::String error;
        auto* plugin = audio::effects::add (track.pluginList, filter, error);
        REQUIRE (plugin != nullptr);
        dispatch();

        te::AutomatableParameter* knob = plugin->getAutomatableParameter (0).get();
        REQUIRE (knob != nullptr);
        f.panel().setParameter (knob);
        CHECK (f.panel().getParameter() == knob);
        CHECK (f.panel().getParameterButton().getButtonText().startsWith ("Low-pass: "));

        // Clear removes the line in one step.
        automation::addPoint (*knob, 0.0, 0.1f);
        automation::addPoint (*knob, 4.0, 0.9f);
        f.project->clearUndoHistory();
        f.panel().clearLine();
        CHECK_FALSE (automation::hasPoints (*knob));
        f.project->undo();
        CHECK (automation::getPoints (*knob).size() == 2);

        audio::effects::remove (*plugin);
        dispatch();
        CHECK (f.panel().getParameter() == track.getVolumePlugin()->volParam.get());
    }

    TEST_CASE ("the main window paints the automation")
    {
        MainFixture f;
        auto& arrangement = f.main->getArrangementModel();

        for (const auto* name : { "Bass", "Lead" })
            arrangement.renameTrack (*arrangement.addTrack(), name);

        auto tracks = arrangement.getTracks();
        arrangement.setCurrentTrack (tracks[1]);

        // A fade in, a dip and a fade out on the bass.
        auto& volume = *tracks[1]->getVolumePlugin()->volParam;
        const auto full = te::decibelsToVolumeFaderPosition (0.0f);

        for (const auto& [beat, value] : std::initializer_list<std::pair<double, float>> {
                 { 0.0, 0.0f }, { 4.0, full }, { 16.0, full }, { 18.0, 0.45f }, { 22.0, 0.45f }, { 24.0, full },
                 { 40.0, full }, { 48.0, 0.0f } })
            automation::addPoint (volume, beat, value);

        f.main->showLowerPanel (ui::MainComponent::LowerPanel::automation);
        dispatch();
        CHECK (f.panel().getParameter() == &volume);

        const auto image = f.main->createComponentSnapshot (f.main->getLocalBounds());
        CHECK (image.isValid());

        // Set HARMONIA_SNAPSHOT_DIR to look at it without opening a window.
        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("HARMONIA_SNAPSHOT_DIR", {}); dir.isNotEmpty())
        {
            juce::FileOutputStream out (juce::File (dir).getChildFile ("main-automation.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
    }
}
