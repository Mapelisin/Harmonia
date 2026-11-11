# Mixer and effects: cheat sheet

**English** | [Русский](mixer.ru.md)

The mixer opens in place of the piano roll: the **Mixer** button on the right above the arrangement, or **F9**
(**F7** or the **Piano roll** button goes back), as in FL Studio.

On the left there is one strip for every track (in the colour of its clips), then the groups (orange), then
the return tracks (lilac), and on the right the **Master**: the whole song goes through it.
A click on a strip makes that track the current one (the keyboards play it). **+ Track** adds a new track,
group or return track.

## The strip

From top to bottom:

| What | How |
|---|---|
| Effects (slots) | The sound goes from top to bottom: from the instrument through the effects to the fader |
| **+** | Add an effect: a built-in one or a plugin (VST3) from "Plugins"; **Find an effect...** searches by name; your favourites and the effects used lately are at the top. The effect window opens right away |
| Click an effect | Open its window (for built-in ones, Harmonia's sliders) |
| Drag an effect with the mouse | Move it up / down, or onto another track, group, return track or the master, together with its settings. A blue line shows where it will land. With **Ctrl**, a copy |
| **+** → **Chains** | Save all the effects of the strip as a chain (**Save these effects as a chain...**), add a saved one (**Add ...**) or put it in place of the current effects (**Replace the effects with**). Chains are kept in `Documents\Harmonia\Chains` and work in any song |
| The green dot on the left | Turn the effect off / on (bypass): grey means the sound goes past it |
| Right-click an effect | A menu: open, turn off, **Presets** (save and load settings, see [`plugins.md`](plugins.md)), up / down in the chain, what it listens to (**Listen to (sidechain)**, see below), delete. An orange **SC** on the slot means the effect listens to another track (hover the mouse to see which) |
| Send bars | One for every return track ("Reverb -6"): how much of the track's sound to send to it. Drag with the mouse; double click for nothing |
| The knob | Pan (L/C/R). Double click for the centre |
| The fader | Volume. Double click for 0 dB. The number below it is in dB |
| The meter | The level after the fader. A red bar at the top means there was an overload above 0 dB (click to reset) |
| **M** / **S** | Mute / solo (the same as in the track header) |
| **→ Master** | Where the track's sound goes: straight to the master or into a group (**→ Drums**). Click to choose |

## Sends and return tracks (one reverb for everyone)

Usually a reverb or a delay is not put on every track. Instead you make **one shared** effect: this way it
sounds like one room and is lighter on the computer.

1. **+ Track → Reverb return** (or Delay return; Empty return is empty, you add the effects yourself with
   **+**). A lilac "Reverb" strip appears on the right: this is a return track. It has no clips, it is not
   shown in the arrangement, it lives only in the mixer.
2. A **Reverb** send bar has appeared on the track strips. Drag it to the right, and this track starts
   "sending" part of its sound to the reverb. The further right, the more reverb this track gets.
3. The overall volume of the reverb is set with the fader of the return track itself; the reverb settings
   open with a click on its slot.

- The send comes after the fader: a quieter track gets a quieter reverb; a muted (M) track sends nothing.
- Soloing a track keeps its reverb (solo does not mute the return tracks).
- Right-click a return strip to rename or delete it (together with all the sends to it).
- The effect on a return track is set to "effect only" (no dry sound): the dry sound comes from the tracks
  themselves.

## Groups: several tracks on one fader

Kick, snare, hats, cymbals: four tracks, but you want all the drums louder or quieter at once. For this you
route them into a **group** (other programs call it a "bus" or a "subgroup").

1. **+ Track → Group track**: an orange "Group" strip appears to the right of the tracks. Or straight from a
   track: the **→ Master** button at the bottom of its strip → **New group with this track**.
2. On the strip of every drum track, click **→ Master** → choose the group. The button becomes **→ Group**.
   The same in the arrangement: right-click the track header → **Output (master or a group)**.
3. Now the group fader moves all the drums together, the group's **M** mutes them all, and effects on the
   group (a compressor, an EQ) process the drums as a whole, as if "glued" together.

- Inside a group every track still has its own fader, effects and sends to the reverb.
- Soloing a track in a group: you hear it alone, but through the group (with the group's effects and volume).
- A group can also send to a reverb / delay (it has send bars too).
- Right-click a group strip to rename it (for example, "Drums") or delete it; when it is deleted, its tracks
  go back straight to the master. Everything can be undone with **Ctrl+Z**.
- You can add, delete and reorder tracks: a track does not lose its group (also after saving).
- The stem of a track in a group (File → Export stems) comes with the effects and volume of the group, the
  way it sounds in the song, and without the group's other tracks. The group itself can be a stem too: all
  its tracks in one file. **Render to audio track** gives the sound of the track itself, and the new audio
  track goes into the same group.

## Sidechain: the bass "ducks" under the kick

A compressor usually listens to its own track. **Sidechain** is when it listens to **another** one: for
example, a compressor on the bass listens to the kick and turns the bass down on every hit. The kick and the
bass stop fighting for the low end, and in dance music this is also that well-known "pumping" sound.

1. On the bass: **+** → **Compressor**.
2. Right-click the compressor slot → **Listen to (sidechain)** → **Kick** (the name of the track with the
   kick). An orange **SC** appears on the slot.
3. Set up the compressor (click the slot): **Threshold** lower (−20 dB and below), so the kick "wakes" it;
   **Ratio** is how strongly to turn down (4 : 1 and more is noticeable); **Attack** short (1–5 ms), so it
   keeps up with the hit; **Release** is how fast the bass comes back (50–150 ms, within a beat).

- You can mute the kick (**M**) and the bass will still duck: this is done when you want the "pumping" but
  do not want to hear the kick itself.
- You can listen to any track of the song except those that themselves depend on this one (otherwise the
  sound would go in a circle; such tracks are not in the list). To go back: **Nothing (its own track)**.
- In the export, in stems and in Render to audio track it sounds the same as during playback.
- Sidechain is available in the built-in **Compressor** and in plugins with a sidechain input (VST3 with an
  extra input). If you want the "pumping" without a kick, exactly in tempo, use the **Pump** effect.

## Built-in effects

| Effect | What it is for |
|---|---|
| EQ | 4 bands: low, two mids, high, each with frequency, gain and width (Q) |
| Compressor | Evens out the volume: threshold, ratio, attack, release, output gain |
| Limiter | Makes the sound louder and keeps peaks below the ceiling: **Gain** (how much to raise, up to +24 dB), **Ceiling** (usually −1 dB), **Release** (how fast the volume comes back after a peak). It looks 2 ms ahead, so a peak is turned down smoothly, without distortion. It goes last on the master |
| Reverb | Space: room size, damping, how much effect / dry sound, width |
| Delay | Echo: time in ms, repeats (feedback), how much effect |
| Chorus | Thickening: depth, speed, width, how much effect |
| Phaser | A "floating" sound: depth, speed, feedback |
| Filter | Low-pass (cuts the highs) or High-pass (cuts the lows), cutoff frequency |
| Pump | "Pumping" like a kick sidechain in dance music: on every beat the volume dips and comes back smoothly, by itself, in the tempo of the song, no kick needed. **Depth** is how far it dips (70% is noticeable), **Rate** is how often: 1/8, 1/4 (every beat), 1/2, 1 bar, **Length** is the part of the step it takes to come back (less means a shorter "breath"). Used on pads, bass, chords. It pumps only while the song is playing |

The **Show editor** button of the built-in **4OSC** synth now also opens a window with its sliders.

Everything in the mixer can be undone with **Ctrl+Z** and is saved in the project. Effects on the master and
on return tracks also go into the WAV export.
New effect plugins are found by the same **Scan plugins** button as instruments (your own plugin folders:
**Folders...** next to it). A crashed effect plugin lets the sound through, and its slot shows a "!": a click
on the slot starts it again. Details: [`plugins.md`](plugins.md).

## Loudness for streaming (the first step of mastering)

**File → Check loudness for streaming...**: Harmonia plays the song "to itself" (like an export, a few
seconds) and measures it the same way Spotify, YouTube and Apple Music do: **integrated loudness in LUFS**
(the EBU R128 standard) and the **peak** in dB. These services bring all songs to about **−14 LUFS**: if
yours is quieter, it will sound quieter than the songs around it; if louder, it will simply be turned down.

The window tells you what to do and offers a button:
- quiet, and there is headroom: **Raise the master by … dB** (the master fader goes up; up to +6 dB);
- quiet, but the peaks are already at the ceiling: it raises only until the peaks reach −1 dB; and the
  **Use the limiter** button does everything itself: it puts a **Limiter** as the last effect on the master
  (ceiling −1 dB), sets the master fader to 0 dB, and finds the limiter gain that makes the song −14 LUFS
  (it measures again several times; the progress is shown at the bottom);
- loud: **Lower the master** (the services will turn it down anyway; this way you hear how it will sound
  there);
- peaks above −1 dB: turn down, so MP3 and streaming do not distort (or **Use the limiter** here too).

The change is one **Ctrl+Z**. What is measured is what you hear: muted tracks do not count; the effects and
volume of the master do.

## Mix Doctor: what is wrong in the mix

**File → Check the mix (Mix Doctor)...**: Harmonia plays the song "to itself", then every track separately
(like stems: with its effects and its sends to the reverb / delay; at the bottom you see "listening to track
2 of 5..."), and shows a report:
- the **loudness** of the song (LUFS) and the peak; the **correlation** of the left and right channels (1 is
  mono, 0 is completely different, below 0 the channels cancel each other); the **lows** (below 120 Hz) and
  the **highs** (above 6 kHz) in dB relative to the mids;
- **what is worth fixing**, if anything, with advice:
  - **overload**: the peaks hit 0 dB, loud places are clipped and distort;
  - **channels out of phase** or **very wide stereo**: on a phone or a single speaker, parts of the song
    will disappear;
  - **too much low end** (it booms and eats up loudness) or **almost no low end** (thin);
  - **too bright / harsh** or **dull** (almost no highs);
  - **a track is buried**: it is 15+ LU quieter than the loudest one (maybe that is intended, then leave
    it);
- **the tracks from loud to quiet**, with the loudness of each.

The limits are wide on purpose: Mix Doctor speaks only about what is clearly wrong, not about taste. By
itself it changes nothing. But if the problem is in the levels, the window has a **Fix the levels** button
(the report says exactly what it will do):
- **buried tracks** are raised with the fader to 10 LU below the loudest one (as far as the fader allows, up
  to +6 dB);
- **overload**: it lowers the master until the peaks sit at −1 dB. It listens to the song once more (or
  several times: from a peak above 0 dB you cannot tell how far it went over), so it takes a few seconds.
All of it together is one **Ctrl+Z** step. Loudness for Spotify / YouTube is separate:
*Check loudness for streaming*.

## MIDI Learn: controller knobs

A knob, fader or wheel of a MIDI keyboard can be linked to any setting of the song.

- **One setting:** right-click a fader, a pan knob, a send bar or a slider in an effect / instrument window
  → **MIDI Learn** → turn the knob on the controller. Done: the bottom of the window says what is linked to
  what.
- **Several in a row:** **Ctrl+Shift+L** (or Edit → MIDI Learn). Touch a slider with the mouse (the plugin's
  own window works too), then turn a knob; the next slider, the next knob. Ctrl+Shift+L again to leave.
- **Forget:** right-click a slider → **Forget CC…**, or Edit → **MIDI controls of this song**, where the
  whole list is.

The links are saved with the song. One knob moves one setting. On a slider in a plugin window the link is
shown as a "CC 21" label; on a fader, in the tooltip. Every turn of a knob is one **Ctrl+Z** step.
To record a knob movement into automation, use the **Write** button; see [automation](automation.md).
