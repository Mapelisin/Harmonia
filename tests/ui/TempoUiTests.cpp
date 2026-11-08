#include <doctest.h>

#include "TestSession.h"
#include "audio/Instruments.h"
#include "core/Project.h"
#include "ui/DragNumber.h"
#include "ui/MainComponent.h"

using namespace harmonia;

namespace
{
    void dispatch()
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    }

    /** A tempo-like number: 20..300, whole steps every 3 pixels, tenths with Shift. */
    struct NumberFixture
    {
        NumberFixture()
        {
            number.setRange (20.0, 300.0);
            number.setSteps (1.0, 0.1, 3.0f);
            number.setValue (120.0);
            number.setBounds (0, 0, 80, 22);
            number.onGestureStart = [this] { ++starts; };
            number.onValueChange = [this] (double v) { values.push_back (v); };
            number.onGestureEnd = [this] { ++ends; };
        }

        ui::DragNumber number;
        int starts = 0, ends = 0;
        std::vector<double> values;
    };

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

        std::unique_ptr<core::Project> project = core::Project::createEmpty (test::session());
        std::unique_ptr<ui::MainComponent> main;
    };
}

TEST_SUITE ("ui: tempo")
{
    TEST_CASE ("a drag up raises the number in whole steps, Shift in tenths; one gesture per drag")
    {
        NumberFixture f;
        f.number.beginDrag (100.0f);
        CHECK (f.number.isBeingEdited());
        f.number.dragTo (91.0f, false);        // 9 pixels up: 3 steps
        CHECK (f.number.getValue() == doctest::Approx (123.0));
        f.number.setValue (50.0);              // the model can't fight the hand
        CHECK (f.number.getValue() == doctest::Approx (123.0));
        f.number.dragTo (130.0f, false);       // 30 pixels down from the start
        CHECK (f.number.getValue() == doctest::Approx (110.0));
        f.number.endDrag();
        CHECK_FALSE (f.number.isBeingEdited());
        CHECK (f.starts == 1);
        CHECK (f.ends == 1);
        CHECK (f.values == std::vector<double> { 123.0, 110.0 });

        f.number.beginDrag (0.0f);
        f.number.dragTo (-6.0f, true);         // Shift: two tenths
        f.number.endDrag();
        CHECK (f.number.getValue() == doctest::Approx (110.2));

        // Held to the range.
        f.number.beginDrag (0.0f);
        f.number.dragTo (-5000.0f, false);
        f.number.endDrag();
        CHECK (f.number.getValue() == doctest::Approx (300.0));

        f.number.turnWheel (-3);
        CHECK (f.number.getValue() == doctest::Approx (297.0));
        CHECK (f.starts == 4);
        CHECK (f.ends == 4);
    }

    TEST_CASE ("double-click and type: Enter sets it (a comma works too), Escape or junk changes nothing")
    {
        NumberFixture f;
        f.number.typeValue ("128,5");
        CHECK (f.number.getValue() == doctest::Approx (128.5));
        CHECK (f.number.getEditor() == nullptr);
        CHECK (f.starts == 1);

        f.number.typeValue ("");
        CHECK (f.number.getValue() == doctest::Approx (128.5));

        f.number.typeValue ("1000");
        CHECK (f.number.getValue() == doctest::Approx (300.0));

        f.number.showEditor();
        REQUIRE (f.number.getEditor() != nullptr);
        f.number.getEditor()->setText ("90");
        f.number.getEditor()->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
        dispatch();   // the editor tells its listeners asynchronously
        CHECK (f.number.getEditor() == nullptr);
        CHECK (f.number.getValue() == doctest::Approx (300.0));
    }

    TEST_CASE ("the transport bar sets the tempo by drag and by tapping, and the time signature; one undo step each")
    {
        MainFixture f;
        auto& bar = f.main->getTransportBar();
        auto& tempoBox = bar.getTempoBox();
        CHECK (tempoBox.getValue() == doctest::Approx (120.0));

        tempoBox.beginDrag (50.0f);
        tempoBox.dragTo (20.0f, false);
        tempoBox.endDrag();
        CHECK (f.project->getTempoBpm() == doctest::Approx (130.0));

        f.project->undo();
        CHECK (f.project->getTempoBpm() == doctest::Approx (120.0));
        bar.refresh();
        CHECK (tempoBox.getValue() == doctest::Approx (120.0));

        tempoBox.typeValue ("95");
        CHECK (f.project->getTempoBpm() == doctest::Approx (95.0));

        for (const auto t : { 100.0, 100.6, 101.2, 101.8 })
            bar.tap (t);

        CHECK (f.project->getTempoBpm() == doctest::Approx (100.0));
        CHECK (tempoBox.getValue() == doctest::Approx (100.0));
        f.project->undo();
        CHECK (f.project->getTempoBpm() == doctest::Approx (95.0));

        bar.setBeatsPerBar (3);
        dispatch();
        CHECK (f.project->getBeatsPerBar() == 3);
        CHECK (f.main->getArrangementModel().getBeatsPerBar() == doctest::Approx (3.0));
        f.project->undo();
        CHECK (f.project->getBeatsPerBar() == 4);
    }
}
