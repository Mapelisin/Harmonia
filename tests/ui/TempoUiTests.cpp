#include <doctest.h>

#include "TestSession.h"
#include "audio/Instruments.h"
#include "core/Project.h"
#include "core/TempoChanges.h"
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

TEST_SUITE ("ui: tempo changes")
{
    namespace changes = core::tempo_changes;

    std::optional<juce::PopupMenu::Item> findItem (const juce::PopupMenu& menu, const juce::String& text)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
            if (it.getItem().text.startsWith (text))
                return it.getItem();

        return std::nullopt;
    }

    juce::MouseEvent mouseAt (juce::Component& component, juce::Point<float> position, bool dragged)
    {
        const juce::ModifierKeys left (juce::ModifierKeys::leftButtonModifier);
        return { juce::Desktop::getInstance().getMainMouseSource(), position, left, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                 &component, &component, juce::Time::getCurrentTime(), position, juce::Time::getCurrentTime(), 1, dragged };
    }

    TEST_CASE ("the ruler: tempo changes are flags to drag; the menu adds, glides and removes them")
    {
        MainFixture f;
        auto& model = f.main->getArrangementModel();
        auto& edit = f.project->getEdit();
        auto& ruler = f.main->getArrangement().getRuler();
        auto& view = f.main->getArrangement().getView();
        f.project->clearUndoHistory();

        // The menu of the ruler offers a change where it was clicked (not at the very start).
        CHECK (findItem (ruler.createMenu (8.0, std::nullopt), "Tempo change here...")->isEnabled);
        CHECK_FALSE (findItem (ruler.createMenu (0.0, std::nullopt), "Tempo change here...")->isEnabled);

        CHECK (model.addTempoChange (8.0, 90.0) == 1);
        CHECK (model.addTempoChange (16.0, 150.0) == 2);
        CHECK (f.project->getUndoDescription() == "Tempo change");
        dispatch();

        // Its flag is in the markers' strip at its beat; the first tempo has none.
        const juce::Point<float> onFlag (view.beatToX (8.0) + 4.0f, 6.0f);
        REQUIRE (ruler.findTempoChangeAt (onFlag).has_value());
        CHECK (*ruler.findTempoChangeAt (onFlag) == 1);
        CHECK_FALSE (ruler.findTempoChangeAt ({ view.beatToX (0.0) + 4.0f, 6.0f }).has_value());
        CHECK_FALSE (ruler.findTempoChangeAt ({ onFlag.x, 30.0f }).has_value());      // on the bar numbers
        CHECK (f.main->createComponentSnapshot (f.main->getLocalBounds()).isValid());

        // Dragged two bars later: one undo step, and not past the next change.
        f.project->clearUndoHistory();
        ruler.mouseDown (mouseAt (ruler, onFlag, false));
        ruler.mouseDrag (mouseAt (ruler, { view.beatToX (10.0) + 4.0f, 6.0f }, true));
        ruler.mouseDrag (mouseAt (ruler, { view.beatToX (12.0) + 4.0f, 6.0f }, true));
        ruler.mouseDrag (mouseAt (ruler, { view.beatToX (40.0) + 4.0f, 6.0f }, true));
        CHECK (model.getTempoChanges()[1].beat == doctest::Approx (16.0 - changes::minGapBeats));
        ruler.mouseDrag (mouseAt (ruler, { view.beatToX (12.0) + 4.0f, 6.0f }, true));
        ruler.mouseUp (mouseAt (ruler, { view.beatToX (12.0) + 4.0f, 6.0f }, true));
        CHECK (model.getTempoChanges()[1].beat == doctest::Approx (12.0));
        CHECK (f.project->getUndoDescription() == "Move tempo change");
        f.project->undo();
        CHECK (model.getTempoChanges()[1].beat == doctest::Approx (8.0));
        CHECK_FALSE (f.project->canUndo());

        // The menu of a flag.
        const auto index = 1;
        auto menu = ruler.createMenu (8.0, std::nullopt, 8.0, index);
        CHECK (findItem (menu, "Set tempo...").has_value());
        CHECK_FALSE (findItem (menu, "Tempo change here...").has_value());
        REQUIRE (findItem (menu, "Glide to it from the tempo before").has_value());
        CHECK_FALSE (findItem (menu, "Glide to it from the tempo before")->isTicked);
        findItem (menu, "Glide to it from the tempo before")->action();
        CHECK (model.getTempoChanges()[1].gradual);
        CHECK (findItem (ruler.createMenu (8.0, std::nullopt, 8.0, index), "Glide to it from the tempo before")->isTicked);
        f.project->undo();
        CHECK_FALSE (model.getTempoChanges()[1].gradual);

        model.setTempoChangeBpm (1, 72.5);
        CHECK (model.getTempoChanges()[1].bpm == doctest::Approx (72.5));

        findItem (ruler.createMenu (8.0, std::nullopt, 8.0, 2), "Remove tempo change")->action();
        CHECK (model.getTempoChanges().size() == 2);
        f.project->undo();
        CHECK (model.getTempoChanges().size() == 3);
        CHECK (changes::getAll (edit)[2].bpm == doctest::Approx (150.0));
    }

    TEST_CASE ("a typed tempo: a number within the limits, with a comma or \"bpm\" too")
    {
        using ui::arrangement::Arrangement;
        CHECK (*Arrangement::parseTempo ("90") == doctest::Approx (90.0));
        CHECK (*Arrangement::parseTempo (" 92,5 bpm") == doctest::Approx (92.5));
        CHECK_FALSE (Arrangement::parseTempo ("fast").has_value());
        CHECK_FALSE (Arrangement::parseTempo ("").has_value());
        CHECK_FALSE (Arrangement::parseTempo ("5").has_value());
        CHECK_FALSE (Arrangement::parseTempo ("900").has_value());
    }

    TEST_CASE ("the tempo box shows and sets the tempo where the song is")
    {
        MainFixture f;
        auto& model = f.main->getArrangementModel();
        auto& edit = f.project->getEdit();
        auto& bar = f.main->getTransportBar();
        auto& transport = f.main->getTransport();
        model.addTempoChange (8.0, 90.0);
        f.project->clearUndoHistory();

        bar.refresh();
        CHECK (bar.getTempoChangeIndex() == 0);
        CHECK (bar.getTempoBox().getValue() == doctest::Approx (120.0));

        // After the change (beat 8 is at 4 seconds).
        transport.setPositionSeconds (6.0);
        bar.refresh();
        CHECK (bar.getTempoChangeIndex() == 1);
        CHECK (bar.getTempoBox().getValue() == doctest::Approx (90.0));

        bar.getTempoBox().typeValue ("100");
        CHECK (changes::getAll (edit)[1].bpm == doctest::Approx (100.0));
        CHECK (changes::getAll (edit)[0].bpm == doctest::Approx (120.0));
        CHECK (f.project->getTempoBpm() == doctest::Approx (120.0));    // the song's first tempo

        f.project->undo();
        CHECK (changes::getAll (edit)[1].bpm == doctest::Approx (90.0));
        transport.setPositionSeconds (0.0);
    }
}
