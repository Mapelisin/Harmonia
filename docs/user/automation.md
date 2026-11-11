# Automation: cheat sheet

**English** | [Русский](automation.ru.md)

Automation is a line that moves a knob of a track by itself while the song plays: the volume rises smoothly
at the start, more reverb comes in for the chorus, the filter opens towards the drop.

The panel opens in place of the piano roll: the **Automation** button on the right above the arrangement, or
**F8** (**F7** is the piano roll, **F9** is the mixer).

## How to draw a line

1. Click a track in the arrangement: the panel shows that track.
2. On the left there is a button with the name of the knob (**Volume** at first). Click it and choose what to
   move:
   - **Volume**, **Pan**: the volume and pan of the track;
   - **Send: Reverb**…: how much the track sends to a return track (if there are any);
   - the submenus of the effects and the instrument: any of their knobs (for example,
     **Low-pass → Frequency**).
   A dot `•` next to an item means this knob already has a line.
3. On the right is a lane lined up with the bars of the arrangement above it (they scroll and zoom together).
   Click in it: points appear, with a line between them. The top of the lane is the knob's maximum, the bottom
   is its minimum.

| What | How |
|---|---|
| Click on an empty spot | A new point (on the arrangement grid, Snap); keep holding to drag it |
| Alt + click / drag | Off the grid |
| Drag a point | Move it (no further than the points next to it) |
| Shift + drag a point | Up and down only, the bar does not change |
| Right-click a point, double click | Delete the point |
| Right-click and drag over points | Delete every point you pass over |
| Drag the small circle in the middle of a stretch | Bend the line between two points |
| Double click or right-click the small circle | Straight again |
| **Ctrl** + drag | A rectangle selects points (**Ctrl+Shift**: adds to the selected ones) |
| **Ctrl** + click a point | Take it into the selection, or out of it |
| Drag one of the selected points | They all move together |
| **Del** / **Ctrl+C** / **Ctrl+X** / **Ctrl+V** | Delete, copy, cut the selected points / paste at the playhead |
| **Ctrl+A** / **Esc** | Select every point / none |
| **Clear** | Remove the whole line of this knob |

The value is shown above the point under the mouse ("-6.0 dB · bar 5.1").
Every mouse action is one **Ctrl+Z** step.

## Curved lines

Between every two points there is a small circle in the middle of the line. Drag it up or down and the
line bends: the knob holds its value and then moves quickly at the end ("late"), or moves quickly first
and then settles ("early"). The circle stays under the mouse; the hint above it says how much the line is
bent. Double click the circle (or right-click it) to make the line straight again.

- There is no circle where there is nothing to bend: at a jump (two points one above the other), between
  two points with the same value, and where the two points are too close on the screen (zoom in).
- A point always wins: if the mouse is on a point, you take the point, not the circle.
- A new point put on a bent line keeps both halves bent the same way.
- What you see is what you hear: playback and the export follow the bent line.
- When you cut, move or repeat a piece of time that goes through a bent line, both parts keep their shape.
  If the bend is strong, the program adds a few points of its own along the cut part to hold the shape.

## Several points at once

Hold **Ctrl** and drag a rectangle over the points: they get a ring round them, and the left column says how
many are selected. **Ctrl+Shift** adds another rectangle to the selection, **Ctrl + click** on a point takes
it in or out, **Ctrl + click** on an empty spot (or **Esc**) selects nothing, **Ctrl+A** selects every
point of the line.

- **Move:** drag any of the selected points and they all go with it, keeping their shape (**Shift**: up
  and down only, **Alt**: off the grid). When one of them reaches a point that is not selected, the start
  of the song, the top or the bottom, they all stop.
- **Del** deletes them. **Ctrl+C** copies, **Ctrl+X** cuts.
- **Ctrl+V** pastes at the playhead (on the grid): put the playhead where the shape should start (click the
  bar numbers), then paste. The pasted points replace the points that were under them and stay selected,
  so you can move them right away. You can paste into the line of another knob or another track: the
  shape keeps its place between the bottom and the top of the knob's range.

The keys work after a click in the automation lane. A plain click (without Ctrl) works as before and
drops the selection.

## How it works

- Before the first point the knob stays at the value of the first point; after the last point, at the value of
  the last one. Between points the line is straight, unless you bend it (see "Curved lines").
- While a knob has a line, the line drives it during playback: moving the fader or the knob does nothing, the
  line puts it back. Remove the line (**Clear**, or delete the last point) and the knob is again where you left
  it.
- With no line, the panel shows a dashed line: the current value of the knob.
- The line is tied to the bars: when the tempo changes, the points stay on their beats.
- Every track remembers which knob you were looking at in the panel.
- The lines are saved in the project and are heard in the WAV export.

## Recording a knob movement "live"

1. Click **Write** next to the record button (or press **Ctrl+Shift+A**): the button turns red.
2. Start the song (Space).
3. Move a fader, a pan knob, a send or an effect slider, with the mouse or with a knob of a MIDI controller
   (how to link a knob: see the [mixer](mixer.md) cheat sheet, the "MIDI Learn" section).
4. When you let go, the recording of this knob ends and the line continues as it was. The same happens when
   you stop the song.

What you recorded appears in the line of this knob (F8) as ordinary points: you can edit them with the mouse.
One movement is one **Ctrl+Z** step. Turn **Write** off when you are done: otherwise any knob movement during
playback will be recorded again.

Not possible yet: drawing and recording a line on return tracks and the master, S-shaped curves (a line
bends one way only), seeing the lines right on the clips in the arrangement.
