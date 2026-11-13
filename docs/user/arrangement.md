# Arrangement: cheat sheet

**English** | [Русский](arrangement.ru.md)

## The top of the window

- **The menu**: File, Edit, View, Track, Transport, Help. It holds every command, with its key written on the
  right, so you do not need to remember anything by heart. **Edit** works with whatever you are working on
  now: with clips, if your last click was in the arrangement (or the mixer is open at the bottom), otherwise
  with the notes of the piano roll. **Undo** says what exactly it will undo ("Undo "Change tempo"").
- On the right in the same row is **CPU**: how loaded the audio is. Near 100 % the sound starts to crackle.
  A bigger buffer (File → Settings → Audio) or fewer heavy plugins will help. **Click it** to see which
  plugin takes how much (see [plugins](plugins.md)).
- **The transport** (the second row): **|◀** goes to the start (**Home**), **■** is stop (if already
  stopped, go to the start), **▶** is play (Space), **●** is record (**R**; dim means armed, bright means
  recording). Next is a large **counter**: bar.beat.sixteenth (for example, `5.2.3`) with the time
  `0:08.250` next to it. Then the tempo, the time signature, Tap, the metronome (the icon, **Ctrl+M**), the
  count-in, Punch, Clip/Song and the note input settings.

## Tracks and clips

At the top are the tracks and clips (like the FL Studio playlist), at the bottom is the piano roll of the
open clip. You can drag the bar between them with the mouse. **F1** opens the list of all shortcuts, with a
search.
Samples and loops are easy to take from the sound browser (**F6**, [`sounds.md`](sounds.md)): listen and
drag them onto tracks.
**Z** (after a click in the arrangement) shows the whole song, or, if clips are selected, only those.
**File → Open recent** lists the last 10 projects (opened or saved).

## Clips

| Mouse | What it does |
|---|---|
| Click a clip | Open it in the piano roll (and select it) |
| Drag a clip **by its name** (the strip at its top) | Move it (also to another track). Everything selected moves |
| **Shift** + drag its name | **A linked copy**: edit one and all of them change (like pattern clones in FL) |
| **Ctrl** + drag its name | An ordinary (independent) copy |
| Drag **anywhere else in the rows**: over a clip below its name, or on an empty spot | **Select a piece of time** on the rows the mouse crosses, whatever the clips' borders (see "A piece of time" below) |
| **Ctrl** + click | Add to the selection / remove from it |
| Drag the left / right edge | Hide or show the start / end of the clip (the notes stay in place) |
| Right-click | Delete the clip (dragging with the right button deletes every clip the mouse passes over) |
| Double click on an empty spot | A new clip one bar long |
| **Shift** (or Ctrl) + drag on an empty spot | Select whole clips with a box |
| The ▾ triangle in the corner of a clip | A menu: rename, colour, make independent, copies, split, copy / cut / paste, delete |
| **Alt** while dragging | No snapping to the grid |
| Wheel / Shift+wheel / Ctrl+wheel | Up-down / left-right / zoom; the middle button moves the view |

The **∞** sign before a clip name means the clip has linked copies.

### Looping a clip (Loop)

The clip menu (▾) → **Loop (repeats when made longer)**: what the clip shows now becomes the loop.
Drag the right edge and the loop repeats as many times as you like (thin lines show where each repeat
starts, and a **↻** sign appears before the name). You can also drag the left edge earlier: the repeats stay
in their places.
- In the piano roll you edit the loop itself; the repeats are shown as grey ghost notes. A note past the end
  of the loop makes the loop longer (up to a whole bar); the clip itself does not change.
- In **Clip** mode one pass of the loop plays, which is handy for writing. Recording into such a clip (and in
  Song mode over the whole clip) puts what you play into the loop.
- **Make the repeats real notes** turns the repeats into real notes (for example, to make a variation in the
  last bar). It sounds the same; a linked copy is not converted this way: the clip first becomes independent.
- Turn **Loop** off and the clip keeps its length; its content plays once.
- Splitting (Ctrl+E), copying, saving, exporting to MIDI all include the repeats, the way it sounds.
Each of these actions is one **Ctrl+Z** step.

Keys (when your last click was in the arrangement):

| Keys | Action |
|---|---|
| Del | Delete the selected clips |
| Ctrl+B | Repeat the selection to the right, as linked copies |
| Ctrl+Shift+B | Repeat the selection to the right, as independent copies |
| Ctrl+A / Ctrl+D, Esc | Select all / deselect |
| Ctrl+E | **Split** clips at the playback cursor: the selected ones, or, if none of them is under the cursor, the clips of the current track. The notes and the sound stay in place; drag the edge of a half to bring back what is hidden |
| Ctrl+C / Ctrl+X | Copy / cut the selected clips |
| Ctrl+V | Paste what was copied at the playback cursor (on the grid), starting from the current track. The clips keep their positions relative to each other; a MIDI clip goes only onto a track with an instrument, audio onto an audio track (the nearest one below). What is pasted are independent copies |
| Ctrl+Z / Ctrl+Y | Undo / redo (works everywhere) |

**Snap** (next to "+ Track") sets what clips stick to: **Line** is the visible grid (bars, and beats when
zoomed in), Bar, Beat, 1/2 and 1/4 of a beat, or no snapping.

### A piece of time: cut anything, anywhere (as in Ableton)

Drag across the rows, over clips or empty space alike, and a **piece of time** is selected: from one grid
line to another, on one row or several. The clips' borders don't matter: the piece can begin in the middle of
one clip and end in the middle of another, notes and sound alike. (A clip itself is taken by its name strip.)

| With a piece selected | What happens |
|---|---|
| **Del** | What the rows have in the piece goes. The clips are cut at its edges by themselves |
| **Ctrl+X** / **Ctrl+C** | Cut / copy what is in the piece |
| **Ctrl+V** | Paste at the start of the selected time, from its first row down, **in place of** what is there. With no time selected: at the playback cursor |
| **Ctrl+B** | Repeat the piece right after itself; the selection moves onto the copy, so pressing again repeats it further |
| **Ctrl+E** | Split the clips at both edges of the piece |
| **Ctrl+L** | The piece becomes the loop region (Song mode plays it round) |
| **Ctrl+J** | **Consolidate**: what each row has in the piece becomes one clip, from one edge of the piece to the other |
| Drag the piece | Move what is in it to another place or other rows, in place of what is there. **Ctrl**: a copy. A frame shows where it will land |
| A click, or **Esc** | Let the piece go |

- **Consolidating (Ctrl+J).** Notes become one clip with everything the clips played (a clip that repeated
  has its repeats written out as notes). Sound is written to a new file next to the song's recordings, as the
  clips sounded together: their gain, fades, stretching and crossfades are in it; the track's effects,
  volume and pan are not (they go on working on the new clip, so the song sounds the same). If one of the
  clips followed the tempo, so does the new one. With no time selected, Ctrl+J joins the selected clips.
  While the sound is being written, a panel shows how far it is; **Esc** stops it and the clips stay as
  they were.
- **Automation goes with the piece.** When a piece is moved, copied, repeated (Ctrl+B) or cut and pasted on
  its own rows, the rows' automation lines (volume, pan, effect knobs) over that time go with it; where the
  piece was taken from, a line runs straight from edge to edge. **Del** leaves the lines alone. Onto other
  rows only the clips go: another track has its own knobs.
- Notes land only on instrument tracks, sound only on audio tracks: a piece dropped where it can't go stays
  where it was, and the status bar says why.
- A part moved or copied out of a linked clip is a clip of its own (not linked).
- Every one of these is one **Ctrl+Z** step.
- **Alt** while dragging selects off the grid.

## Audio clips (sound files)

- **Drag** a WAV, MP3, FLAC, AIFF or OGG file from File Explorer onto the arrangement. A blue line shows
  where the start will land (on the Snap grid):
  - on an **audio track**: the clip goes onto it;
  - on a **track with an instrument** or below all tracks: a new audio track appears, named after the file;
  - several files at once: each onto a new track of its own, all from the same place.
- Or **File → Import audio...**: the files go onto the current audio track (or onto new ones) from the song
  cursor.
- The clip shows the **waveform**. Moving, copying (Shift/Ctrl + drag, Ctrl+B), deleting and trimming the
  edges work the same as for MIDI clips. You cannot stretch a clip past the end of the file, unless you turn
  on looping for it (below).
- **Looping:** the ▾ menu → **Loop (repeats when made longer)**. What the clip plays now becomes the loop:
  drag the right edge and the sound repeats for as long as the clip lasts. A "↻" appears on the clip, with a
  thin line between passes. The same item again makes the clip play once.
- **When one audio clip overlaps another** (on the same track), over the overlap the first one fades out and
  the second fades in: a join with no click. Move them apart and each has its own fades again. If you want
  both to play at full strength: the ▾ menu → untick **Crossfade where it overlaps another clip**.
- Audio clips can be moved only onto audio tracks, MIDI clips only onto tracks with an instrument.
- A click on an audio clip makes its track the current one and opens the clip at the bottom in the **audio
  clip editor** (see below); in **Clip** mode, Space plays this audio clip in a loop.
- The ▾ menu of an audio clip: rename, show the file in its folder, fade shape, remove fades, gain to 0 dB,
  copy, delete.
- A clip points to the file where it is (the file is not copied into the project). If the file is moved or
  deleted, the clip stays and shows "File missing".

### Clip fades and gain

At the top of the waveform an audio clip has three small handles (if the clip is not too narrow):

| Handle | What it does |
|---|---|
| The left square | Drag right for a **fade in** at the start (the sound rises smoothly). The darkened part is what the fade removes |
| The right square | Drag left for a **fade out** at the end (the sound dies away smoothly) |
| The circle in the middle | Drag up and down for the **clip gain** (up to +6 dB, silence at the very bottom). A line across the clip shows the level, and the waveform gets smaller or bigger |
| Double click a handle | Reset: no fade, gain 0 dB |

- While you drag, the value is shown next to it ("Fade in 1.50 beats (0.75 s)", "Gain -3.0 dB").
- Fades do not snap to the grid: they are about the sound, not about bars. The fade in cannot run into the
  fade out.
- The fade shape is in the clip's ▾ menu: **Linear** (even), **S-curve** (soft on both sides), **Fast**
  (quick at the start), **Slow** (slow at the start).
- The clip gain comes before the effects and the track fader: handy for evening out a loud piece and a quiet
  piece on one track.
- When a clip is split, the fade in stays with the left part and the fade out with the right part.
- Every movement is one Ctrl+Z step; everything is saved and is heard in the export.

## The audio clip editor (at the bottom, F7)

On an audio track the bottom panel is **Audio clip** instead of the piano roll: the clip you clicked (or the
first clip of the track). The same clip has a white frame around it in the arrangement.

**The waveform.** You see the whole sound file: the part the clip plays is bright, the hidden start and end
are dim. Above it is a ruler with the bars of the song.

| What | How |
|---|---|
| The white line on the left / right | Drag to show or hide the start / end of the sound (on the **Snap** grid; with **Alt**, freely). It does not go past the file |
| The squares and the circle at the top | The clip's fades and gain, as on the clip in the arrangement; a double click resets |
| Click the waveform or the ruler | Put the song cursor there |
| Wheel / Ctrl+wheel | Scroll / zoom |
| The middle mouse button | Drag the view |
| **Show clip** | Show the whole part that plays |

**The panel at the top:**
- **Gain**: the clip gain as a number: drag up and down (Shift for finer steps) or double click and type
  (from −60 to +24 dB; with the handle on the waveform, up to +6). **Normalize** puts the loudest point at
  0 dB.
- **Pitch**: higher or lower in semitones (Shift for tenths); the speed does not change.
- **Follow tempo**: one click puts the sound on the song's grid. Harmonia listens to the sound itself,
  measures its tempo (any tempo, 72.3 BPM too) and finds its first beat, then stretches it so that its hits
  land on the song's beats; the status bar says what it found. **Loop** is the measured tempo (drag or
  type to correct it), **÷2** / **×2** halves or doubles it, **◀ ▶** slide the sound a sixteenth earlier or
  later under the grid (with Shift, a hair). Playing that speeds up and slows down is put on the grid beat
  by beat: **Loop** then shows its tempo on the whole, with a ~ before it. See "Loops in the tempo of the
  song" below.
- **Fades**: the fade shape: Linear, S-curve, Fast, Slow.
- **Reverse**: play backwards.
- On the right is the file: name, sample rate, mono or stereo, length.

Every change is one **Ctrl+Z** step. While the audio clip editor is open, Ctrl+C / Ctrl+V and the other edit
commands work with the clips of the arrangement.

## Recording audio (a microphone, a guitar)

1. Make an audio track (**+ Track → Audio track**).
2. Click the **●** button on it (record arm). It turns red, and an input level bar appears under the name:
   try singing, it should move. The input (`In: ...`) is changed in the track menu (right-click → Record
   from).
3. Turn on recording (**●** or **R**) and press **▶** (Space). The recording runs from the current place in
   the song straight on, with no loop; a red "Recording..." bar grows on the track.
4. **■** (Space): the recording becomes an audio clip. Ctrl+Z removes it completely.

- **Count-in:** the **Count-in** button at the top gives 1 or 2 bars of metronome clicks before the
  recording, so you have time to pick up the instrument. The song starts playing a little earlier, but the
  recording still starts from the place where you pressed Play (the counter shows "Count 3, 2, 1"). This is
  a setting of the program, not of the project. For now it works only for audio recording: notes in Clip
  mode are recorded in a loop, so you can come in on the second pass.

- You can record onto several tracks with ● at once (each from its own input).
- While a track is armed, you hear the input in your headphones. If an echo or hum from the speakers gets in
  the way, right-click the track → untick **Hear the input while armed**.
- Recording files: for a saved project, in the folder "<project name> Audio" next to it; for an unsaved one,
  in "Documents\Harmonia\Recordings".
- If a MIDI clip is open, the notes you play on the keyboard are recorded into it at the same time as the
  audio.

### Takes: record several times in a row and pick the best

1. **Song** mode (the Clip / Song button at the top, or **L**). Select a loop region on the ruler: **Ctrl +
   drag**.
2. A track with **●**, record (**R**), **▶**. The song plays the region in a loop, and every pass is
   recorded as a separate take; the counter at the top shows "take 1", "take 2"...
3. **■**: you get one clip as long as the region. It holds all the takes; the last one that was recorded in
   full plays.

- **Choose a take:** right-click the clip → **Takes**, or the **Take 2 of 4** button at the bottom, in the
  audio clip editor (F7).
- **Put it together from pieces:** split the clip (**Ctrl+E** at the cursor), and choose a take for each
  piece.
- **Keep only this take** forgets the other takes (the files stay on the disk).
- The whole recording is one Ctrl+Z step, and so is every choice of a take.

Without a loop region (or in Clip mode) recording works as usual: straight on, in one piece.
Not possible yet: takes for notes, seeing all the takes at once under the clip.

### Punch in / out: record one phrase again

1. **Song** mode. Select the phrase on the ruler: **Ctrl + drag** (the loop region).
2. Click **Punch** at the top (or **Transport → Punch in / out**). The button and the region turn red: this
   is where the recording will go.
3. A track with **●**, record (**R**), **▶**. The song starts **a bar before the region**, so you can sing
   your way in, and only what you sing **inside the region** is recorded. After the region the song plays
   on until you stop it.
4. **■**: the old sound inside the region is replaced with the new one; everything before and after it is as
   it was. One Ctrl+Z brings the old sound back.

- While the region is being recorded, the track's old sound there is silent: you hear yourself, not the
  previous take.
- With **Count-in** on, its clicks sound before the region instead of the one bar.
- Stopped before the region: nothing is recorded and nothing is cut.
- Notes work the same way: only what you play inside the region goes into the clip (it is added to the
  notes that are there). A note played a moment before the region is its first note.
- With Punch on, takes round the loop aren't recorded: the region is either a loop for takes or a place to
  punch into. Turn Punch off to get them back.
- Without a loop region, or in Clip mode, Punch does nothing (the button is dim). It isn't saved with the
  song.

Not possible yet: your own length of the way in, several punch takes in a row.

## Tracks

- **+ Track** opens a menu: **Instrument track** (the built-in synth and an empty clip at the same place as
  the open one), **Drum track** (the built-in drum machine, drum names on the keys, an empty clip) or
  **Audio track** (an empty track for sound files, with no instrument).
- Click a track header to make it the current track: its instrument is shown above the arrangement, and the
  computer keyboard and the MIDI keyboard play it.
- **Several tracks at once:** **Ctrl + click** a header to add that track to the selected ones (or take it
  out), **Shift + click** to select every track from the current one to that one. The selected tracks are
  lit, a little less than the current one. While several are selected, **M**, **S** and the right-click
  menu on any of them work for all of them: **Colour**, **Mute**, **Solo**, **Output** (the master, a
  group, or **New group with these tracks**), **Folder**, **Delete N tracks**. The menu then shows only
  what is done to them all. An action on a track that is not selected is for that track alone. A plain
  click on a header leaves one track selected again. The volume slider, Duplicate, Freeze and Render work
  on one track at a time.
- Double click the name to rename it (clips named after the track are renamed with it).
- **M** is mute, **S** is solo, the slider is the volume (double click for 0 dB).
- Right-click the header for a menu: rename, add an instrument track or an audio track, delete the track;
  an audio track also has record arm, the input (Record from) and input monitoring. And also:
  - **Duplicate track**: a copy right below it: the instrument, the effects with their settings and
    automation, the sends, the volume, the clips (the copies are separate: editing notes in them does not
    touch the originals).
  - **Move up / Move down**: reorder. Or just drag the header up or down with the mouse: a blue line shows
    where it will land.
  - **Freeze**: "freeze" the track in place: its sound is written to a file, the track plays that file, and
    its instrument and effects are turned off and stop loading the computer. "Frozen" appears in the header.
    The volume, pan, sends and group keep working. While a track is frozen, its clips and notes cannot be
    edited; **Unfreeze** in the same menu puts everything back. Unlike Render to audio track, no new track
    appears. While it freezes, a panel shows how far it is; **Esc** stops it and the track stays as it was.
  - **Track delay: 0 ms...**: make the track play a little **later** or **earlier** than the rest of the
    song. Type a number of milliseconds: `12` is 12 ms later, `-12` is 12 ms earlier (up to 100 either
    way), `0` is no delay. For a part that drags or rushes, or a sound with a slow attack (strings, a pad)
    that should land on the beat. While a track has a delay it is written under its name ("+12 ms"). The
    track's sends (reverb, delay) move with it. One thing to know about "earlier": a sound cannot come
    before it is asked for, so the rest of the song waits for this track instead; in the song and in the
    export everything is in its place, but what you play live is heard that many milliseconds later.
  - **Folder**: track folders, see below.
  - **Render to audio track**: "freeze" into audio: the track (instrument + effects + volume) is written to
    a WAV next to the project and placed on a new audio track "… (audio)" below it, at the same place; the
    sends (reverb) are carried over, and the original track is muted (M), so the song sounds as before.
    Handy when a heavy plugin loads the computer, or when you want to cut and reverse a part as audio.
    **Ctrl+Z** puts everything back (the file stays). **Esc** while it renders stops it: nothing is added.
  - **Output (master or a group)**: where the track's sound goes: straight to the master or into a group
    (one fader for several tracks, see the [mixer](mixer.md) cheat sheet), or **New group with this track**.
  - **Colour**: the track colour (16 colours). **Automatic** is the colour by the track's place in the song,
    as before. Your own colour stays when the track is moved, copied or "frozen" into audio; it is shown in
    the arrangement, the mixer and automation.
  Each of these actions is one **Ctrl+Z** step.
- Under the name is the track's instrument, or **Audio** for an audio track.
- The clip colour: the clip menu (▾) → **Colour**: if the clip is selected, all selected clips are coloured.
  **Same as the track** goes back to the track colour. Copies of a clip take its colour.

### Track folders

When there are many tracks, you can put neighbouring ones into a folder and collapse it into one row.

- **Make a folder:** right-click a track header → **Folder** → **New folder with this track**. If clips on
  other tracks are selected, those tracks go in too. The tracks of a folder are placed next to each other.
- **Add a track to a folder:** its menu → **Folder** → **Move into folder** → the folder name. Or drag the
  header between two tracks of the folder. **Take it out:** **Take out of the folder**, or drag the header
  away from the folder.
- **Collapse:** the button to the left of the name on the first track of the folder (or **Collapse to one
  row** in the menu). The folder becomes one row: the folder name, how many tracks it holds, and **M** and
  **S** for all of them at once. The row shows the clips of all the tracks in the folder; you cannot edit
  them there: expand the folder with the same button.
- A collapsed folder can be dragged by its header: it moves as a whole.
- **Rename folder...** and **Remove the folder (its tracks stay)** are in the same menu. When a folder is
  removed, its tracks stay where they are.

A folder only tidies up the screen and gives a shared mute / solo. A shared fader and shared effects for
several tracks is a **group** (see the [mixer](mixer.md) cheat sheet). Folders do not collapse in the mixer.

## Sound in a clip: reverse and normalize

In the audio clip menu (the ▾ triangle in the corner):
- **Reverse (play backwards)**: play backwards (a reverse cymbal, vocal effects). The program writes a
  reversed copy of the file by itself; while it is writing, the clip shows "Reversing..." (usually a
  fraction of a second). Once more, and it plays forwards again. The original file is not changed.
- **Normalize (loudest point to 0 dB)**: raise the clip gain so that the loudest point is at 0 dB (only the
  part that plays is counted). A quiet recording can be raised by up to +24 dB.
  To put it back: Ctrl+Z or *Reset gain to 0 dB*.

## Loops in the tempo of the song, and pitch

**A loop falls into the tempo by itself.** Drag a loop into the song: the program works out its tempo and
stretches or squeezes the sound so that its beats land on the beats of the song. The pitch does not change.
It takes the tempo:
- from the file itself (ACID or Apple Loops tags, as in sample libraries);
- from the name: "Drums 95 BPM.wav", "hat_loop_140_Am.wav";
- if the name has the word "loop" but no tempo, from the length: it looks for a whole number of bars that
  is closest to the tempo of the song.

The tempo found this way is then checked against the sound itself: if the loop is really at 94.8, it plays
at 94.8.

Hits, single sounds and a recorded voice (no tempo and no "loop" in the name) play at their own speed.

After the clip name you can see what was done to it: "· 95 BPM" means it follows the tempo of the song and
the loop was recorded at 95 BPM; "· +3 st" means it sounds 3 semitones higher.

If you change the tempo of the song, such a clip speeds up or slows down with it and stays on the same
bars.

**Any sound onto the grid: Follow tempo.** For a sound whose name says nothing (a downloaded sample at
72.3 BPM, a piece of a recording), click **Follow tempo** in the audio clip editor, or **Follow the song
tempo** in the clip's menu (▾). Harmonia then:
- finds the hits in the sound and measures the tempo they keep, to hundredths of a BPM;
- finds the first beat and makes the clip start on it (silence before it is hidden, not lost: drag the
  clip's left edge to see it again);
- stretches the sound so that its hits are on the song's grid, at whatever tempo the song is.

The same hits fit a tempo twice or half as fast: Harmonia takes the one nearest the song's tempo, so that the
sound is changed least. If it should be the other one, press **÷2** or **×2**. If the first beat was taken
a step off, **◀ ▶** slide the sound by sixteenths. A sound with no beat to hear (a pad, a phrase, a
single hit) gets a message saying so: then set **Loop** by hand.

**Playing that speeds up and slows down.** A take played by hand, without a click, doesn't keep one tempo:
no single stretch puts all of it on the grid. Harmonia hears that. It follows the hits along the sound,
finds where each beat really is, and puts every beat of the sound on a beat of the song, stretching the
sound a little differently from beat to beat. It is the same one click on **Follow tempo**, and:
- the status bar says so: "Its tempo isn't steady (around 97 BPM): its 64 beats were put on the grid one by
  one";
- **Loop** shows the tempo on the whole, with a ~ before it;
- in the audio clip editor the song's beat lines lie over the waveform where the sound's beats were found,
  unevenly if the playing was uneven: you can see what Harmonia heard;
- **÷2** / **×2** and **◀ ▶** work as for any other sound.

What stays as it was played is the feel inside a beat: a snare that is always a little late is still a
little late. After a long break with nothing to hear (more than about four bars) the count can come out a
sixteenth off: put it right by hand, as below.

**A beat by hand.** If Harmonia heard a beat in the wrong place (one hit played late, a first beat taken
wrong, the count off after a break), move that beat yourself. In the audio clip editor, with **Follow
tempo** on, the ruler above the waveform shows the song's grid lying over the sound:
- **drag a line of the ruler** to where the hit is in the waveform. Near a hit the line jumps onto it (the
  readout says "on a hit"); with **Alt** it stays where the mouse is;
- the lines either side stay where they are, and the clip stays in its place in the song: only the stretch
  of sound between those lines is played a little faster or slower. Zoomed in, the lines are sixteenths
  (you correct one hit); zoomed out, they are bars;
- **double-click a line** to let that beat go back between its neighbours;
- a small mark ▼ in the ruler shows every place where the sound's tempo changes: the beats Harmonia found
  in uneven playing, and the ones you moved.

Each move is one **Ctrl+Z**. A beat can't be put past its neighbour lines, nor very close to them.

In the audio clip menu (▾):
- **Follow the song tempo**: turn on or off. If you turn it off, the clip plays at its own speed again, from
  where its file starts.
- **Loop tempo**, if the tempo was guessed wrong: **Set...** to type your own (from 50 to 250 BPM),
  **Half** / **Double** to halve or double it. For example, a loop was guessed as 140, but it is really 70
  and twice as long.
- **Pitch**: higher or lower by a semitone or an octave, with no change of speed (up to 24 semitones each
  way); **Original pitch** goes back to the original pitch.
- **Slice to a drum track**: cuts the loop at its hits and puts every hit on a key of its own. See below.

**A loop cut into its hits.** To play a loop another way (other order, other rhythm, some hits left out),
choose **Slice to a drum track** in the audio clip's menu (▾). Harmonia finds the hits in the sound and:
- cuts what the clip plays from hit to hit, and writes each piece to a file of its own next to the song's
  recordings;
- adds a drum track right under the clip's track, called "<clip> slices", with the pieces on its keys from
  C1 up;
- puts a clip of notes there, a note for each hit, where the hit was. The audio clip is taken out: the notes
  play the loop now, and it sounds as it did.

Then it is drums like any other: move the notes in the piano roll or the step sequencer (Alt+T), delete
some, give a key its own volume and tuning in the drum machine's window. **Ctrl+Z** brings the audio clip
back and takes the drum track away. A loop with more than 64 hits keeps the 64 strongest; a sound with no
hits (a pad, a phrase) or a clip that repeats can't be sliced, and the status bar says so.

Every change is one **Ctrl+Z** step. The sound is stretched by Signalsmith Stretch (open source, MIT
license) during playback: you do not have to wait for a copy of the file to be prepared.

A sound in FLAC, Ogg or MP3 that follows the tempo or sounds higher or lower is first converted to WAV (only
WAV can be stretched on the fly). The WAV goes to the same place as the recordings of the song: the folder
"*song name* Audio" next to the song file, or, for a song not saved yet, "Documents/Harmonia/Recordings".
The original file is not changed.

## Markers (verse, chorus…)

The **Markers** lane above the bar numbers holds the labels of the song's sections.
- **+ Marker** (to the left of the lane) puts a marker at the playback cursor; the name is filled in by
  itself in order: Intro, Verse, Pre-chorus, Chorus, Bridge… A double click on an empty spot of the lane
  puts a marker there.
- Click a marker to play from it. Drag to move it (on the Snap grid; **Alt** for off the grid). Double click
  to rename.
- Right-click the ruler for a menu: **Add marker here** (ready-made names or your own), and on a marker:
  **Rename**, **Loop this section** (loop from this marker to the next one, handy for polishing a chorus),
  **Delete marker**.
- **Ctrl+→ / Ctrl+←** go to the next / previous marker (works everywhere, not only in the arrangement).
Markers are saved in the project; every change is one **Ctrl+Z** step.

## Bars on all tracks at once (repeat a chorus, insert a pause)

Right-click the ruler: there are three items at the bottom of the menu. They act on the **loop** (Ctrl +
drag on the ruler) if the click is inside it, otherwise on the bar under the mouse:

- **Duplicate bars 5-8 (all tracks)**: play these bars twice: a copy is placed right after them on all
  tracks, and the rest of the song moves further on. The copied clips are linked to the originals (like
  Ctrl+B): editing notes in one changes all of them. Automation is repeated too (a filter rise in the copy
  starts from the beginning again), and so are the markers. The loop moves onto the copy, so the same item
  once more adds a third repeat.
- **Insert 4 empty bars before bar 5 (all tracks)**: empty bars: everything after them moves later (clips,
  automation, tempo, markers). A clip that runs across this place is split.
- **Delete bars 5-8 (all tracks)**: cut the bars out of all tracks; the rest slides back.

From the keyboard (in the arrangement, on a loop): **Ctrl+Insert** inserts empty bars as long as the loop
before it, **Ctrl+Delete** deletes the bars of the loop. Every action is one **Ctrl+Z** step.

## A song from a loop

Do you have a loop of 2–8 bars where everything already plays (chords, drums, bass, melody)? Mark it (Ctrl
+ drag on the ruler), right-click inside the loop → **Make a song from bars 1-4** → a form:

| Form | Sections |
|---|---|
| **Pop** | Intro (harmony only) → Verse (drums, bass, harmony) → Chorus (everything) → Verse → Chorus → Outro (harmony and melody) |
| **Dance** | Intro (drums and harmony) → Build (harmony and melody) → Drop (everything) → Break (no drums or bass) → Drop → Outro |
| **Short** | Intro → Main (everything) → Break → Main → Outro |

Harmonia works out by itself who is who: the drum track (named "Drums", "Beat"… or marked as drums), the
bass (the lowest one, if it is below C3), the melody (the highest one, if there are several parts), and the
rest is harmony. Every section is the loop played for about 8 bars (a 4-bar loop, twice). Markers appear
above the sections; the copied clips are **linked** to the loop: fix the notes in one and they change
through the whole song (Make unique unlinks a clip). What was after the loop moves to after the song. All of
it is one **Ctrl+Z** step.

## Playback: Clip and Song

The **Clip / Song** button (or **L**) sets what Space plays, like PAT / SONG in FL:

- **Clip**: the open clip in a loop (handy for writing a part).
- **Song**: the whole arrangement. It plays the whole song in a loop, or the **loop**: **Ctrl + drag** on
  the ruler above the clips. **Ctrl + click** on the ruler removes the loop. An ordinary click on the ruler
  plays from that place (and turns on Song).

Recording (**R** or the Rec button) writes into the open clip: in Clip mode every pass adds notes. In Song
mode what is played while the song is inside this clip goes into it, and what is played anywhere else goes
into the clip the track has there or into a new clip (with no clip open at all, on the current track): you
don't have to prepare a clip. Armed to record in Song mode, the song plays on past its end instead of
going round; with a loop set, it goes round the loop.

## Tempo and time signature

At the top, to the right of the position:

| What | How |
|---|---|
| **120 BPM** | The tempo. Drag up / down with the mouse (with **Shift**, by tenths), use the mouse wheel, or double click and type a number (**Enter** to confirm, **Esc** to cancel). From 20 to 300 |
| **Tap** | Tap in time with the music (4 times or more) and the tempo adjusts. A pause of more than 2 seconds starts the count again |
| **4/4** | The time signature: how many beats in a bar (2/4 … 7/4) |

- When the tempo changes, clips, notes and automation stay on their beats: the song just plays faster or
  slower. Audio clips start on the same beat but play at the same speed (they are not stretched), so their
  length in bars changes.
- The time signature changes only the bar grid: the clips stay on the same beats.
- Every change is one **Ctrl+Z** step; the tempo and time signature are saved in the project.
- For now there is one time signature for the whole song. Signatures over 8 (6/8, 12/8) will come later.

### Tempo changes along the song

- **Add:** right-click the ruler with the bar numbers → **Tempo change here...** → type the tempo.
  From this place the song plays at the new tempo. An orange flag with the number appears on the ruler.
- **Move:** drag the flag with the mouse (on the grid; **Alt** for off the grid).
- **Change the tempo:** double click the flag, or right-click → **Set tempo...**. Or put the song at this
  place and change the tempo in the box at the top: it shows and changes the tempo where the song is now.
- **Smoothly:** right-click the flag → **Glide to it from the tempo before**: the tempo comes to this value
  smoothly from the previous change (a slowdown at the end, a speed-up before a chorus). A "~" appears on
  the flag.
- **Remove:** right-click the flag → **Remove tempo change**.

Clips, notes, automation and markers stay on their beats. The export and the metronome follow these
changes. Every action is one **Ctrl+Z** step.

## MIDI files

- **Add:** drag a `.mid` from File Explorer onto a track (or **File → Import audio or MIDI...**: it is
  placed at the cursor on the selected track). The clip starts at the nearest grid line to the left.
  - A file with one part goes onto the instrument track you dropped it on. Dropped on an audio track or
    below all tracks, it goes onto a new track.
  - A file with several parts (bass, drums, piano…): each part onto a new track of its own with the
    built-in synth, named after the part. You can put your own instrument on it afterwards.
  - If the song has no notes and no audio yet, the tempo and time signature are taken from the file.
  - The whole import is one **Ctrl+Z** step. You can drop audio and MIDI files together.
  - **The sustain pedal** from the file is turned into note length: a note released while the pedal is down
    sounds until the pedal is released (or until the same key is hit again). Piano MIDI files from the
    internet sound the way they were played, not "in scraps", on any instrument.
- **Save a clip:** right-click a MIDI clip → **Export as MIDI file...**: the notes of the clip as it plays
  them (muted notes and anything past the edge of the clip are left out).
- **Save the whole song:** **File → Export MIDI...**: one track in the file for every instrument track that
  has notes, plus the tempo and time signature. Handy for moving parts to another program.
- Note colour ↔ MIDI channel (colour 1 = channel 1, and so on).
- Files with time in SMPTE frames are not supported (rare, from video programs). Tempo changes inside a file
  are not carried over: the first tempo is taken.

## Files

The **File** menu (it also has **Settings...** for the sound card, MIDI keyboards and plugins, and
**Exit**):

| Keys | Action |
|---|---|
| Ctrl+N | New project (the empty song, or your template: see Templates below) |
| — | Templates: a new song from a template; this song saved as a template (see below) |
| Ctrl+O | Open (`.harmonia`) |
| Ctrl+S / Ctrl+Shift+S | Save / Save as |
| — | Earlier versions: the song as it was before each of the last saves (see below) |
| — | Collect all files into the song's folder: copy the sounds the song uses next to it (see below) |
| — | Find missing files...: sounds that were moved or deleted (see below) |
| Ctrl+R | Export the song (the whole song, until its reverbs and delays have died away), as in FL: first choose the format (see below), then where to save. It sounds as it does in playback: muted (M) tracks are left out; with solo (S), only the soloed ones are included |
| Ctrl+, | Settings: sound card and buffer, MIDI keyboards, plugin folders |
| — | Import audio or MIDI...: add audio or MIDI files (see "Audio clips", "MIDI files") |
| — | Export stems (a file per track or group)...: the tracks you tick, each into a file of its own (see below) |
| — | Export the loop region...: only the loop (Ctrl + drag on the ruler), exactly on its borders: a ready-made sample loop. Asks the format first |
| — | Export MIDI...: the notes of the whole song into a `.mid` |

**Export format** (Ctrl+R; Harmonia remembers the last one):

| Format | What it is for |
|---|---|
| WAV, 24-bit | Full quality: mastering, distributors (DistroKid…), other programs. The default |
| WAV, 16-bit (CD) | For CDs and old players. A very quiet noise (dither) is added so that quiet endings fade out smoothly |
| WAV, 32-bit float | Nothing is clipped even above 0 dB: for a mastering engineer or for further work |
| FLAC, 24 / 16-bit | The same sound as WAV, about half the size. Accepted by Bandcamp, SoundCloud and most players |
| MP3, 320 / 256 / 192 / 128 kbps | Small, plays everywhere: for sending, for listening on a phone. 320 is the best. Keep the peaks below −1 dB (a limiter) so it does not crackle |

MP3 is made by the Windows encoder. If MP3 is not in the list, this Windows does not have it (this happens
in "N" editions: the Media Feature Pack from Windows Settings will help).

By default files go to "Documents\Harmonia". An asterisk in the window title means there are unsaved
changes; when you close, start a new project or open another one, Harmonia asks whether to save them.

**While it exports.** A panel over the window says what is being done ("Rendering the song", "Stem 2 of 5:
Drums", "Writing Song.mp3") and how far it is. **Cancel**, or **Esc**, stops the export: no half-written
file is left and the song is as it was. Until the export ends, the song can't be played or changed and the
window can't be closed. (A file of the same name that is being replaced is removed when the export starts:
cancelling doesn't bring it back.)

The same panel, with the same **Cancel** and **Esc**, is shown by everything else that takes time: Freeze,
Render to audio track, Consolidate (Ctrl+J), Check loudness, the limiter and Mix Doctor. Stopped half way,
none of them leaves anything half-done: the song is as it was before the command.

**How long the exported file is.** The export doesn't stop at the last clip: it goes on while the song
still rings (a long reverb, a delay that repeats), and ends half a second after the sound has died away,
fading to nothing. It waits 30 seconds at the most. There is nothing to set.

**Stems** (File → Export stems). A window asks what to export:
- **which tracks**: a tick for every track that can be heard and has clips (all ticked), and for every
  group. A group's stem is all its tracks together in one file ("the drums as one file");
- **file names**: a pattern, with an example under it. `{song}`, `{n}` (the track's place from top to
  bottom), `{track}`, `{bpm}` and `{date}` are filled in. The usual one gives "Song - 01 Bass.wav";
- **format**: WAV, FLAC or MP3, as for the song.

Then choose a folder. Each file holds the track with its own effects and its own reverb/delay (sends), but
without the effects and volume of the master. All the files are as long as each other: in another program
you put them at the start and they line up, and the tracks' stems together sound like the mix. Muted (M)
tracks are left out; with solo (S), only the soloed ones are included. The pattern and the format are
remembered. The folder opens by itself.

**A loop as a file** (File → Export the loop region): first mark a loop: **Ctrl + drag** on the ruler
above the clips (or *Loop this section* in a marker's menu). Choose the format (WAV, FLAC or MP3: the loop
remembers its own, apart from the song's; WAV 24-bit at first), then where to save. Exactly this piece goes
into the file, with nothing added for the tail: you can loop it in any sampler or put it back in as a loop.
An MP3 starts with a moment of silence, so it does not repeat seamlessly: for a loop to use in a song,
choose WAV or FLAC. The suggested file name includes the bars: "Song - bars 5-8.wav". The menu item shows
which bars will go into the file.

**Templates** (File → Templates): a template is a song to start new songs from: your usual tracks with
their instruments and effects, sends, groups, the tempo, tracks armed for recording. Set a song up the way
you like to begin, then **Save this song as a template...** and type a name: the song itself stays as it
was. From then on the template's name is at the top of File → Templates: click it and a new, untitled song
opens with everything the template had (Ctrl+S asks where to save it; the template doesn't change).
*New songs start from* chooses what **Ctrl+N** and the start of Harmonia give you: the empty song or one of
your templates. A template keeps the clips too: delete them before saving if you want only the tracks.
Templates are ordinary song files in `Documents\Harmonia\Templates` (*Show the templates folder*): delete or
rename them there. A template doesn't copy the sounds its clips play: it remembers where they were.

**Autosave.** While there are unsaved changes, Harmonia quietly saves a copy of them every half a minute
(without touching your file). If the program crashed (for example, because of a plugin) or the power went
out, on the next start it offers **Restore** (bring the work back) or **Don't restore**. Save the restored
work with the usual **Ctrl+S**: it is written to your file. If you declined by mistake, the last copy is at
`%APPDATA%\Harmonia\Autosave\Discarded work.harmonia`; you can open it with **Ctrl+O**.

**Earlier versions.** Every time you save, what the file had before goes into the folder
"<song name> Backups" next to it, named by the time it was saved; the last 20 are kept. **File → Earlier
versions** lists them, the latest first. Click one, and the song opens as it was then, with an asterisk in
the title. Nothing is overwritten yet: **Ctrl+S** makes it the song again (and what the song was until now
becomes an earlier version too); to leave everything as it was, just open the song again without saving.
**Show their folder** opens the folder: the versions are ordinary `.harmonia` files.

**Collect all files into the song's folder** (File menu). A song remembers where its sounds are, not the
sounds themselves. This command copies every sound the song uses from elsewhere (audio clips and their
takes, your samples in the drum machine, the sampler's sound) into "<song name> Audio" next to the song, and
the song uses the copies from then on. The originals stay where they are. Then save (Ctrl+S): the
`.harmonia` file and the "<song name> Audio" folder next to it are now the whole song, and can be moved to
another disk or computer together. Save the song at least once first: before that it has no folder. One
Ctrl+Z takes it back (the copies stay on the disk). Plugins are not copied: the other computer needs them
installed.

**Find missing files** (File menu). If sounds were moved or deleted, Harmonia says so when the song opens,
and this menu item shows how many are missing. It lists them (where each was and what uses it) and asks for
a folder: it looks there, and in the folders inside it, for files with the same names, and the song uses the
ones it finds. One Ctrl+Z takes it back.

**Sound files and the project folder.** Sounds that are in the project folder (or deeper) are remembered
relative to it: the whole folder can be moved or copied to another disk or computer. The others are
remembered by their full path. "Save as" into another folder does not lose the sounds. The settings of VST
plugins that were changed in their own window are saved too, and are marked with an asterisk.
