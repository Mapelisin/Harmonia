# Sounds (sample browser): cheat sheet

**English** | [Русский](sounds.ru.md)

The **Sounds** button above the bottom panel (or **F6**) opens the sound browser: places on the left, a folder
with samples and loops on the right. **F7** goes back to the piano roll.

## Places (on the left)

- **Favourites** (the first one, with a star): every sound you marked with a star, wherever it is. See
  below.
- Desktop, Music, Documents, Downloads and the drives are there from the start.
- **+ Folder** adds your own folder with samples (for example, `D:\Samples`). It is remembered.
- Right-click an added folder → **Remove from places** takes it off the list (the folder itself is not
  deleted).
- The program remembers which folder you were in last time.

## The folder (on the right)

Folders come first, then sounds (WAV, AIFF, FLAC, OGG, MP3…) and MIDI files. Other files are not shown.

| What | What it does |
|---|---|
| Click a sound or a MIDI file | Listen to it. Click again to stop |
| ↑ / ↓ | The next / previous sound, played right away (handy for going through kicks) |
| **Esc** or **Stop** | Stop the preview |
| Click the **star** at the end of a row | Keep the sound in **Favourites** (the star turns yellow). Click it again to let it go. The sound is not played |
| **In tempo** (the button next to Stop) | On: a loop is played at the song's tempo. Off: at its own. See below |
| Double click or **Enter** | Put it into the song: on the current track, at the playback cursor. If the track has an instrument, the sound goes onto a new audio track |
| Drag onto a track | Put it there (snapped to the grid). **Ctrl/Shift + click** selects several, and you drag them all |
| Click a folder | Open it |
| **Backspace** or **Up** | The folder above |
| The search box | Finds sounds whose name contains these letters, in this folder **and in all folders inside it**. Next to a sound found in a folder inside, grey text says which one (`Drums/Acoustic`). What is right here comes first, then by name; no more than 500 |
| Right-click a sound | Listen / put into the song / add to favourites / show in File Explorer / **New sampler track playing this sound** (the sound becomes an instrument: its keys play it higher and lower) / make it a drum of the current drum track |
| Right-click a folder | Open / add to places / show in File Explorer |

A MIDI file from the browser is placed the same way as a sound: the notes go onto a track with an instrument.
A MIDI file is previewed with a simple built-in sound (a soft tone, drums on channel 10), the first minute
only: this is for recognising the melody or the beat. In the song it will play with the instrument of the
track.
The preview plays over the song and a little quieter; it stops when you switch to another panel.

## Favourites

Click the star at the end of a sound's row, and the sound is in **Favourites**, the first place on the left.
There all your marked sounds are in one list by name, whatever folders they are in (the folder is written in
grey after the name). You listen to them, put them into the song and drag them onto tracks from there as
from a folder; the search box looks among them. A click on the star there takes the sound off the list.
**Up** or **Backspace** goes back to the folder you were in.

Favourites belong to the program, not to a song: they are the same in every song. A sound whose file is not
there right now (a drive that is not plugged in) is not shown, but it is not forgotten.

## Loops at the song's tempo

A loop that knows its tempo says it at the right of its row: **95 BPM**. Harmonia takes it from the file's
own data, from its name ("Drums 95 BPM", "loop_120_Am"), or, for a file with "loop" in its name, from its
length.

With **In tempo** on (it is on from the start), a click on such a loop plays it **at the song's tempo**:
faster or slower, at the same pitch. The row then says both: **95 → 120 BPM**. This is how the loop will
sound once it is in the song, where it follows the tempo by itself. Turn **In tempo** off to hear the loop
as its file is.

A drum hit, a sung phrase and a MIDI file always play as they are. Not possible yet: the preview does not
start on the bar of a playing song, and a sound whose name does not say its tempo is played at its own (in
the song, the **Follow tempo** button measures it).
