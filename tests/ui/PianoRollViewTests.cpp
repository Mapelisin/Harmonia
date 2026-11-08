#include <doctest.h>

#include "core/TestProject.h"
#include "ui/pianoroll/PianoRoll.h"

#include <iostream>

using namespace harmonia;
using namespace harmonia::ui::pianoroll;

namespace
{
    /** Adds notes straight to the clip, bypassing undo (fast setup for big test sets). */
    void fillClip (te::MidiClip& clip, int count, double spanBeats)
    {
        auto& sequence = clip.getSequence().state;

        for (int i = 0; i < count; ++i)
        {
            core::Note n;
            n.pitch = 24 + (i * 37) % 72;
            n.startBeat = spanBeats * i / count;
            n.lengthBeats = 0.5;
            n.colour = i % 3;
            sequence.addChild (core::note::createState (n), -1, nullptr);
        }
    }

    /** Median time to paint the whole component, in milliseconds. */
    double measurePaint (juce::Component& c)
    {
        juce::Image image (juce::Image::RGB, c.getWidth(), c.getHeight(), true);
        std::vector<double> times;

        for (int i = 0; i < 7; ++i)
        {
            juce::Graphics g (image);
            const auto start = juce::Time::getMillisecondCounterHiRes();
            c.paintEntireComponent (g, true);
            times.push_back (juce::Time::getMillisecondCounterHiRes() - start);
        }

        std::sort (times.begin(), times.end());
        return times[times.size() / 2];
    }
}

TEST_SUITE ("piano roll: view")
{
    TEST_CASE ("pixels and beats/pitches convert both ways")
    {
        View view;
        view.width = 1000.0f;
        view.height = 500.0f;
        view.pixelsPerBeat = 50.0;
        view.rowHeight = 10.0f;
        view.leftBeat = 2.0;
        view.topRow = 60.0f;

        CHECK (view.beatToX (2.0) == doctest::Approx (0.0f));
        CHECK (view.xToBeat (view.beatToX (7.25)) == doctest::Approx (7.25));
        CHECK (view.yToPitch (view.pitchToY (60) + 1.0f) == 60);
        CHECK (view.yToPitch (view.pitchToY (60) + 9.0f) == 60);
        CHECK (view.yToPitch (-100000.0f) == 127);
        CHECK (view.yToPitch (100000.0f) == 0);
    }

    TEST_CASE ("zoom keeps the beat or pitch under the mouse in place")
    {
        View view;
        view.width = 1000.0f;
        view.height = 500.0f;
        view.contentBeats = 1000.0;
        view.leftBeat = 10.0;

        const auto beat = view.xToBeat (400.0f);
        view.zoomHorizontally (2.0, 400.0f);
        CHECK (view.xToBeat (400.0f) == doctest::Approx (beat));

        const auto row = 300.0f / view.rowHeight + view.topRow;
        view.zoomVertically (1.5f, 300.0f);
        CHECK (300.0f / view.rowHeight + view.topRow == doctest::Approx (row));

        // Never scrolls before the start or past the pitch range.
        view.scrollByPixels (-1.0e6, -1.0e6);
        CHECK (view.leftBeat == doctest::Approx (0.0));
        CHECK (view.topRow == doctest::Approx (0.0f));

        view.centreOnPitch (60);
        CHECK (view.yToPitch (view.height * 0.5f) == 60);
    }
}

TEST_SUITE ("piano roll: component")
{
    TEST_CASE ("the whole piano roll lays out and paints, and its shortcuts reach the model")
    {
        test::ProjectWithClip p;
        PianoRollModel model (*p.project);
        model.setClip (p.clip);
        model.setSnap (core::snap::Mode::quarterBeat);
        audio::Transport transport (p.project->getEdit());

        PianoRoll pianoRoll (model, transport);
        pianoRoll.setBounds (0, 0, 1280, 700);
        pianoRoll.showClip();

        model.beginGesture ("notes");
        core::Note n;
        n.startBeat = 1.0;
        model.addNote (n);
        n.startBeat = 2.1;
        n.pitch = 64;
        model.addNote (n);

        CHECK (measurePaint (pianoRoll) >= 0.0);

        const auto press = [&] (int key, int mods = 0)
        {
            return pianoRoll.keyPressed (juce::KeyPress (key, juce::ModifierKeys (mods), 0));
        };

        constexpr int ctrl = juce::ModifierKeys::ctrlModifier, shift = juce::ModifierKeys::shiftModifier,
                      alt = juce::ModifierKeys::altModifier;

        CHECK (press ('a', ctrl));
        CHECK (model.getNumSelected() == 2);
        CHECK (press ('d', ctrl));
        CHECK (model.getNumSelected() == 0);

        CHECK (press ('b'));
        CHECK (model.getTool() == Tool::paint);
        CHECK (press ('E'));
        CHECK (model.getTool() == Tool::select);

        CHECK (press (juce::KeyPress::upKey, shift));
        CHECK (model.getNotes()[0].values.pitch == 61);
        CHECK (press (juce::KeyPress::upKey, ctrl));
        CHECK (model.getNotes()[0].values.pitch == 73);

        CHECK (press ('q', ctrl));
        CHECK (model.getNotes()[1].values.startBeat == doctest::Approx (2.0));

        CHECK (press ('z', ctrl));
        CHECK (model.getNotes()[1].values.startBeat == doctest::Approx (2.1));
        CHECK (press ('y', ctrl));
        CHECK (model.getNotes()[1].values.startBeat == doctest::Approx (2.0));

        const auto ghosts = model.areGhostsVisible();
        CHECK (press ('v', alt));
        CHECK (model.areGhostsVisible() != ghosts);

        CHECK (press ('f'));
        CHECK (model.getLaneProperty() == core::NoteProperty::release);

        CHECK (press (juce::KeyPress::deleteKey));
        CHECK (model.getNotes().empty());

        CHECK_FALSE (press (juce::KeyPress::spaceKey));    // Space belongs to the transport
    }

    TEST_CASE ("drawing stays within a 60 fps frame with 20 000 notes")
    {
        test::ProjectWithClip p;
        fillClip (*p.clip, 20000, 256.0);

        PianoRollModel model (*p.project);
        model.setClip (p.clip);

        View view;
        NoteGrid grid (model, view);
        grid.setBounds (0, 0, 1600, 900);
        view.width = 1600.0f;
        view.height = 900.0f;
        view.contentBeats = 300.0;
        view.topRow = 30.0f;
        view.rowHeight = 12.0f;

        // Everything selected: the most expensive colour mix.
        model.selectAll();
        CHECK (model.getNumSelected() == 20000);

        view.pixelsPerBeat = 80.0;        // a normal working zoom: ~1500 notes on screen
        const auto normal = measurePaint (grid);

        view.pixelsPerBeat = 1600.0 / 256.0;   // the whole clip on screen
        const auto whole = measurePaint (grid);

        std::cout << "piano roll paint, 20000 notes: normal zoom " << normal << " ms, all on screen " << whole << " ms" << std::endl;

       #if ! JUCE_DEBUG
        // One 60 fps frame locally. Shared CI machines are slower and noisy: there it only catches big regressions.
        const auto budget = juce::SystemStats::getEnvironmentVariable ("CI", {}).isNotEmpty() ? 50.0 : 16.0;
        CHECK (normal < budget);
        CHECK (whole < budget);
       #endif
    }
}
