# Piano roll: cheat sheet

**English** | [Русский](piano-roll.ru.md)

The piano roll shows the clip that is open in the arrangement (click a clip at the top). The arrangement is
covered in [`arrangement.md`](arrangement.md).

**F1** (or the **?** button at the top right) opens the list of all shortcuts and mouse moves right in the
program, with a search: start typing "loop", "copy", "Ctrl+B"… **Esc** closes it.

Tools (the buttons above the notes, or keys):

| Key | Tool | What it does |
|---|---|---|
| P | Draw | Click: a note. Drag a note: move it. Drag its right edge: length. Right-click: erase. |
| B | Paint | Dragging draws a row of notes (with the length of the last note). |
| D | Delete | A click or a drag erases notes. |
| T | Mute | A click or a drag mutes / unmutes notes. |
| C | Slice | Draw a line through notes and they are cut. A click cuts the note under the cursor. |
| E | Select | A box selects, a click picks a note, dragging moves the selection. |

Mouse:
- **Shift + drag a note**: a copy. **Shift + drag on an empty spot** (Draw): a note of the length you want.
- **Ctrl + box**: select (in any tool), **Ctrl+Shift**: add to the selection, **Ctrl + click on an empty
  spot**: deselect.
- **Alt** while dragging: no snapping to the grid.
- **Double click a note**: its properties (velocity, pan, Mod X/Y, fine pitch, mute, colour). If the note is
  selected, all selected notes change.
- **Wheel**: up/down, **Shift+wheel**: left/right, **Ctrl+wheel**: zoom, **middle button**: move the view.
- **The keyboard on the left**: a click plays the sound (further right = louder); middle button up/down
  changes the row height.
- **The ruler**: a click sets the playback cursor; **Ctrl + drag** (or double click + drag) selects time.
  At the top of the ruler are the **song markers** ("Verse", "Chorus"; they are set in the arrangement): click
  a marker to play from it.
  In a looping clip (Loop) the end of the loop is marked with a **bracket**: drag it to make the loop longer
  or shorter. The repeats themselves are shown to the right, a little darker, with a thin line at the start
  of each pass.
- **The bottom panel**: drag the mouse across it to set velocity (or another property). If there is a
  selection, only the selected notes change (this is how you edit one note of a chord). **Alt** resets.
  **Alt+wheel over a note** does the same with the wheel (**Ctrl+Alt** for fine steps).
  The same panel holds the clip's **pitch bend, mod wheel and sustain pedal** (see "Controllers" below).

Keys:

| Keys | Action |
|---|---|
| Space | Play / Stop (Clip: the clip in a loop, Song: the whole song; switch with L) |
| Ctrl+Z / Ctrl+Y (or Ctrl+Shift+Z) | Undo / redo |
| Del | Delete the selection (with no selection, everything) |
| Ctrl+A / Ctrl+D | Select all / deselect |
| Ctrl+C / Ctrl+X / Ctrl+V | Copy / cut / paste (into the first visible bar) |
| Ctrl+B | Repeat the selection right after itself |
| Ctrl+Q / Shift+Q | Quantize to the grid / note starts only |
| Shift+↑↓ / Ctrl+↑↓ | By a semitone / by an octave |
| Shift+←→ | Move by one grid step |
| Alt+←→ | Nudge a tiny bit, off the grid (a couple of pixels; the closer the zoom, the finer) |
| Ctrl+Shift+←→ | Rotate inside the bar: what goes past the edge comes out on the other side |
| Ctrl+L | Legato: every note is stretched to the next one |
| Ctrl+U | Chop notes into pieces one grid step long (for rolls, hi-hats) |
| Ctrl+G | Glue notes of the same pitch that touch or overlap |
| Shift+D | Make all notes one grid step long (for drums) |
| Alt+M / Alt+Shift+M | Mute / unmute notes |
| Shift+I | Invert the selection |
| Ctrl+Insert / Ctrl+Delete | Insert / cut out empty time the size of the time selected on the ruler (moves all notes) |
| F / Shift+F | Bottom panel: next / previous note property, then pitch bend, mod wheel, sustain pedal |
| PgUp / PgDn | Zoom in / zoom out |
| Z | Show everything: the whole clip and its notes (or, if there is a selection, only the selected notes) |
| Alt+V | Show / hide the notes of other tracks |

All these commands are also in the **Tools** menu above the notes, which shows the keys too. Like everything
else, they act on the selected notes, or on all notes if nothing is selected. "Rotate" with time selected on
the ruler turns only that time.

## Drums: the built-in drum machine

Harmonia has drums of its own, with no plugins or samples: **+ Track → Drum track**, or **Drums (built-in)**
in the instrument chooser above the arrangement. It plays the standard General MIDI layout: kick C1 (36),
snare 38, clap 39, hats 42 / 44 / 46, toms, cymbals, cowbell, shaker… The closed hat cuts off the open one,
as on real drums.
The knobs (the instrument window button): **Kit** is the set: *Punchy* (tight, bright), *808* (a long deep
kick), *Lo-fi* (dark, soft); **Tune** is higher/lower by ±12 semitones; **Decay** is a shorter/longer sound.

**Your own samples.** Any drum can play a sound file of yours instead of its own sound: your kick, your clap.
- Open the drum machine's window (the instrument window button). Below the knobs is the list of drums.
  **Drag a sound** onto a drum, from the sound browser (F6) or from the file explorer, and it plays that
  sound at once. Or click the drum's sound (it says **Built-in**) and choose a file.
- In the same row: the sample's own **volume** and **tuning** (double-click: back to zero), **x** puts the
  kit's own sound back, the triangle plays the drum.
- Without the window: drop a file from the file explorer straight onto a key in the piano roll (the row
  lights up), or right-click a sound in the sound browser → **Use as a drum sound of Drums** → the drum.
  Right-click a key → **Back to the kit's own sound**.
- The key then shows the sample's name. **Tune** and **Decay** of the kit work on samples too (Decay under
  100% cuts them short). The samples are saved with the song and in the drum machine's presets (**Presets**
  in the same window: "my kit" for any song). The files stay where they are: if one is moved or deleted,
  its drum plays the kit's own sound again and the window says **Missing**.
- Every change is one Ctrl+Z step.

It is put in automatically when:
- **Alt+D** (Drum pattern) is opened on a new track with no notes and the built-in synth: the track becomes
  a drum track ("Track 2" is renamed to "Drums"); Cancel puts everything back. A track with notes or with a
  plugin keeps its own sound;
- a drum part (channel 10) comes from a MIDI file: it goes onto a track with the drum machine.

## Drums: the step sequencer

A beat is quicker to put in as a grid than as notes. **Steps** in the piano roll's toolbar (or **Alt+T**,
or View → Step sequencer) shows the open clip as a grid: drums down, steps across.

| What | How |
|---|---|
| Click an empty cell | A hit there; you hear it |
| Click a hit | Takes it away |
| Drag | The same along the way: a row of hits, or a row of them gone |
| Right button (also dragged) | Only takes hits away |
| Alt + wheel over a hit | Harder / softer (the cell is brighter / dimmer) |
| Click a drum's name | Plays it; right button: **Clear this row** |
| **Step** | How long a step is: 1/8, 1/16 (as it starts), 1/32, or triplets |
| **Clear** | Takes every hit of the clip away |
| **Piano roll** (or Alt+T) | Back to the notes |

- These are the same notes as in the piano roll, shown another way: what you put in here is there, and the
  other way round. A note off the grid shows in the step it starts in.
- The rows are the kit's main drums, plus every key the clip has notes on, plus the keys that have samples of
  yours. The names are those on the piano roll's keys.
- The grid is as long as the clip: make the clip longer in the arrangement for more bars. While the song
  plays, the column being played is lit.
- Every click is one Ctrl+Z step, a whole drag too.

## Drums: names on the keys

If a track is called "Drums", "Beat", "Perc"…, the keys on the left and the notes show **drum names** by
General MIDI: Kick (C2), Snare, Clap, Closed hat, Open hat, Crash, Ride, toms… Nearly all drum plugins and
kits follow this layout.

To turn it on or off for any track: **right-click the keys on the left** → *Drum names (General MIDI)*, or
the **Scale** menu → *Drum names on the keys*. It is remembered for the track and can be undone with Ctrl+Z.

## The sampler: any sound as an instrument

Any sound file can be an instrument: one note of a guitar, a piece of a voice, a bell. The keys play it higher
and lower, the way a tape does when it runs faster and slower.

- **The quickest way:** in the sound browser (F6) right-click a sound → **New sampler track playing this
  sound**. A new track named after the sound appears, and its keys play it already. One action; Ctrl+Z takes
  it back.
- Or **+ Track → Sampler track**, or **Sampler (built-in)** in the instrument chooser: the sampler's window
  opens. **Drag a sound** onto it (from the sound browser or the file explorer), or click **Choose a sound…**.
  A file from the file explorer can also be dropped onto the piano roll's keys.
- **Root key** is the key that plays the sound exactly as it is: C4 at first, with a dot on the piano roll's
  keys. The keys above it play the sound higher and shorter, the keys below lower and longer. To change it,
  drag the **Root key** number in the window (or turn the wheel over it), or right-click a key in the piano
  roll → **This key plays the sound as it is**. If the recorded note is an A, make an A the root key, and
  the keys are in tune with the rest of the song.
- The knobs: **Volume**; **Tune** (±24 semitones); **Attack** (a note fades in); **Release** (how long a note
  fades after its key is let go); **Mode**: *Hold the key* (the sound lasts while the key is down) or
  *One shot* (a note plays the sound to its end whatever the key does: for hits and phrases).
- Chords and several notes at once work; harder notes are louder; the pitch wheel bends by ±2 semitones, the
  sustain pedal holds the notes.
- The window shows the sound's picture, its length and format; the triangle plays the root key while you
  hold it.
- The sound is saved with the song and in the sampler's presets (**Presets** in its window). The file stays
  where it is: if it is moved or deleted, the sampler is silent and its window says **Missing**. Of a long
  file the first 30 seconds are played: this is an instrument, not an audio track.
- Every change is one Ctrl+Z step.

## Tools with a window

They open from the **Tools** menu or with keys. The notes change at once while you turn the knobs; **Space**
plays so you can listen. **Accept** (or **Enter**, or a click outside the window) keeps the result, as one
**Ctrl+Z** step. **Cancel** (or **Esc**) puts everything back. **Reset** sets the knobs to their defaults.
While the window is open, other keys do not touch the notes.

| Keys | Tool | What it does |
|---|---|---|
| Alt+S | **Strum** | A "guitar" spread of a chord: the notes come in one after another. Time is how late the last one is (ms), Tension is faster at the start or at the end, Direction is bottom to top, top to bottom or alternating, Velocity makes the last one quieter/louder, Ends keeps the note ends in place or moves them with the starts. |
| Alt+A | **Arpeggiate** | A chord (2+ notes with the same start) turns into an arpeggio over its whole length. Pattern: up, down, up and down; Step: 1/4…1/32 and triplets; Gate: note length; Octaves: how many octaves up. It does not touch single notes. |
| Alt+R | **Humanize** | "Bring to life": shifts the notes a tiny bit at random (Timing, ms), and their loudness (Velocity) and length (Length). Variation is another random variant (the same number gives the same result). |
| Alt+X | **Scale levels** | Velocity (or another property from the bottom panel) for all notes at once: Multiply is the spread (50% is more even and quieter, 150% is more contrast), Offset moves all of them higher/lower. |
| Alt+P | **Chord progression** | Writes chords for you, even into an empty clip. Key and Mode set the key (by default, the scale of the song), Chords is the progression (pop I–V–vi–IV, the "sad" vi–IV–I–V, 50s, rock, jazz ii–V–I, canon, minor ones: pop, the "epic" i–VII–VI–VII, a "dark" one…), Notes: triads or seventh chords, Each chord: half a bar / a bar / two bars, Rhythm: hold, quarter notes, eighth notes, on the offbeat (as in house), Octave, Voicing: Smooth (the voices move as little as possible, so it sounds connected) or root position, Bass: a bass note an octave below, Repeat: how many times to repeat. It starts at the start of the time selection (Shift + drag on the ruler), otherwise at the start of the clip; the clip grows by itself. |
| Alt+D | **Drum pattern** | Writes a drum groove for you, even into an empty clip, on the keys of the standard General MIDI layout (kick C1 = 36, snare 38, hat 42…), which most drum plugins understand. Style: rock, pop, house (a kick on every beat), disco, hip-hop (boom bap), trap (with hat "rolls"), reggaeton (dembow), funk (with snare "ghost" notes), drum and bass, half-time. Bars: how many bars (at first, the length of the clip or of the time selection), Fill: a fill on the last two beats of every 4th bar / of the last bar only / none, Crash: a cymbal at the start of every 4 bars, Swing: every second sixteenth note comes later (100% is like triplets), Velocity: the loudness of the strong hits (the others are quieter). It starts at the start of the time selection, otherwise at the start of the clip; the clip grows by itself. If the track is called "Drums", the drum names are shown on the left. |
| Alt+B | **Bass line** | Writes a bass line for the chords that **other tracks** play (drum tracks and muted tracks do not count): the same chords that are written on the ruler. A handy way: a new track "Bass", a clip, Alt+B. Pattern: hold (one note per chord), on every beat, eighth notes (drive), root–fifth, octaves (disco), "walking" (root, third, fifth and a semitone into the next chord: jazz, blues); Octave: where the bass is (2 means from C2); Slash chords: under "C/E" play E (as the chord asks) or C; Gate: note length; Velocity: the loudness on the beats (quieter between beats). It takes the chords in the time selection, otherwise over the length of the clip. If there are no chords, the bottom of the window says why. |
| Alt+L | **Melody** | Writes a melody over the chords of **other tracks** (like Bass line): chord notes on the strong beats, steps along the scale between them; 4-bar phrases rise and come back, and the last note is the root of the last chord. The scale comes from Scale, and if there is none, it is guessed from the chords (C–Am–F–G → C major). Rhythm: calm (long notes), medium (quarter and eighth notes), busy (eighth and sixteenth notes); **Variation**: another melody for the same chords (the same number gives the same melody; turn it until you like one); Octave: where the melody is (5 means around C5); Phrases: the third bar of a phrase repeats the rhythm and shape of the first (easy to remember), or free; Velocity. This is a sketch: keep what works and fix the rest by hand. |

The note properties window (double click) now works the same way: all changes made while it is open are one
**Ctrl+Z** step.

## Scale and chords

- **Scale** (the button above the notes) is the scale of the song: the root (Root) and the mode (Major,
  Minor, harmonic and melodic minor, Dorian, Phrygian, Lydian, Mixolydian, pentatonics, blues). The rows of
  scale notes are light, the others are dark, the rows of the root are highlighted, and the scale notes are
  marked on the keyboard on the left. The scale is saved in the project; changing it is a **Ctrl+Z** step.
- **Snap notes to the scale** (in the same menu; the button then shows "(snap)"): new notes (click, Paint)
  land on scale notes; when you drag notes up and down, they move along the steps of the scale (a chord
  stays a chord of this scale); **Shift+↑/↓** moves by a scale step. **Ctrl+↑/↓** is still an octave.
- **Fit notes to the scale** moves the selected (or all) notes to the nearest scale notes (handy for
  someone else's MIDI). It is also in **Tools**.
- **Detect from the notes** guesses the key from the selected (or all) notes: major or minor, by which
  notes sound longer. It is usually right; with few notes it can mix up relative keys (C major and A minor),
  and then you correct it by hand.
- **Chord** is a chord stamp: choose a chord, and a click in Draw places the whole chord (the lowest note is
  where you click); you can drag it right away, and you hear it as a chord. **Shift + drag** gives a chord
  of the length you want. **Scale chord** / **Scale chord 7** is the chord the scale gives on this step (in
  C major, D minor on D and G7 on G). With no scale it is a major chord and a seventh chord. **Off** goes
  back to single notes.
- **Which chord is this?** Select a few notes and its name appears on the right above the notes:
  "Chord Am7 · 4 selected". It knows major and minor, seventh chords (7, m7, maj7, also without the fifth),
  dim, aug, sus2/sus4, 6, 9, add9, m7b5, dim7, the fifth (C5). If the lowest note is not the root, it is
  written with a slash: "C/E". Octaves and doubled notes do not matter. Handy for understanding what is
  played in someone else's MIDI or in your own improvisation.
- **Chords on the ruler.** The top line of the ruler above the notes shows which chords sound along the
  clip: "C — Am — F — G". A melody over the chords does not get in the way (if the top note "breaks" the
  chord, it is not counted), and neither do passing notes shorter than half a beat. To turn it off: the
  **Scale** menu → *Chord names on the ruler*.

## Controllers: pitch bend, mod wheel, sustain pedal

Besides notes, a clip stores how the wheels and the pedal of a MIDI keyboard moved. In the list above the
bottom panel (where Velocity is) choose **Pitch bend**, **Mod wheel** or **Sustain pedal**, or press **F**
until you get there. The panel shows a line: every value holds until the next one.

- **Drag with the left button** to draw a line (a diagonal drag gives a smooth rise). **Alt** draws the
  rest position: bend to the middle, wheel and pedal to zero. **The right button** erases.
- **Pitch bend** goes up and down from the middle (on the built-in synth this is ±2 semitones; for plugins,
  whatever is set in the plugin). **Mod wheel** is 0…127: what it does is up to the instrument (usually
  vibrato or a filter; the built-in synth does not respond to it yet, Surge XT and most plugins do).
  **Sustain pedal** is pressed (above the middle) or released.
- The hint at the top right shows which value a click will set (+50%, 64, Down).
- You can write only inside the clip; in a looping clip, into the loop. Copies of the clip (linked ones)
  get the same.
- Controllers are saved in the project, go into the exported MIDI file and are read from other people's
  MIDI files (the pedal from files, as before, makes the notes longer).

## Recording notes

- **●** (or **R**) arms recording. Then **Space**: the clip plays in a loop, and everything you play is
  written into it. Every pass adds notes. **Space** again stops. The whole take is undone with one
  **Ctrl+Z**.
- While a key is held, the note is shown in red; the pressed key lights up on the left.
- The metronome icon (**Ctrl+M**) is the metronome.
- The list to the right of Keys sets how recorded notes are placed: **As played**, **Quantize starts**
  (starts on the grid), **Quantize starts and ends** (starts and ends). The grid is the one chosen in Snap.
- A **MIDI keyboard** connects by itself and plays the instrument of the selected track. The sustain pedal
  is recorded too: the notes hold while it is pressed. **Pitch bend and the mod wheel** are written into the
  clip while you move them (you can see them in the bottom panel, see "Controllers"). Which keyboard was
  found is shown at the bottom, in the status bar.

## The computer keyboard as a piano

**Keys** (or **Ctrl+T**) turns it on / off. It works in any keyboard layout, Russian too.

```
 2 3   5 6 7   9 0   =        <- black keys of the upper octave
Q W E R T Y U I O P [ ]       <- white keys: C5 D5 E5 F5 G5 A5 B5 C6 ...
 S D   G H J   L ;            <- black keys of the lower octave
Z X C V B N M , . /           <- white keys: C4 D4 E4 F4 G4 A4 B4 C5 ...
```

**↑ / ↓** is an octave up / down (the button shows which note the bottom row starts from: Keys C4).
While Keys is on, single-letter keys (P, B, D…) do not switch tools: they play notes.
Combinations with Ctrl (Ctrl+Z, Ctrl+T…) work as usual.

Snapping (Snap): **Line** snaps to the grid lines (depends on the zoom), **Cell** snaps to the start of a
cell, and further on: bar, beat, 1/8, 1/16, their triplets and finer. With no selection the commands act on
all notes.
