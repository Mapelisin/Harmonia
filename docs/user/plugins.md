# Plugins (VST3): cheat sheet

**English** | [Русский](plugins.ru.md)

Harmonia opens **VST3** plugins: instruments (Kontakt, Vital, Serum, Surge XT…) and effects.
VST2 plugins (`.dll` files) are not supported: Steinberg no longer gives out a license for them. Most modern
plugins also come in a VST3 version (a `.vst3` file or folder): install that one.

## Finding your plugins

The **Instrument** row above the piano roll:

| Button | What it does |
|---|---|
| **Scan plugins** | Looks for plugins and adds them to the lists of instruments and effects. Every file is checked in a separate program: a plugin that crashes during the check is simply skipped |
| **Folders...** | Opens **File → Settings → Plugins**: where to look. The standard VST3 folder (`C:\Program Files\Common Files\VST3`) is always searched. **Add a folder...** adds your own, for example `E:\VST`: the scan also looks into all folders inside it and starts by itself. **Remove**: select your folder in the list and take it out of the search (its plugins stay in the list until the next scan) |
| The instrument list | The built-in instruments (4OSC, Drums) and the instrument plugins found |
| **Show editor** | The window of the instrument |
| **Presets** | Presets of the instrument: save its sound, load a saved one (see "Presets" below) |

If the scan found nothing but there are plugins on the computer, they are most likely in a folder of their own:
add it with **Folders...**. The effects found are in the mixer: **+** → **Plugins**.

## Presets: your sound in any song

The **Presets** menu is available for an instrument (the button in the Instrument row), for an effect
(right-click its slot in the mixer → **Presets**) and in the slider window of the built-in instruments and
effects (the button at the top).

| Item | What it does |
|---|---|
| **Save preset...** | Save the current settings under a name. If a preset with this name already exists, it asks whether to replace it |
| Preset names | Load. The tick marks the last one loaded or saved. Not the right one? **Ctrl+Z** brings back what was there |
| **Presets of ...** | The presets the plugin came with (if it has any) |
| **Reset to first settings** | For built-in ones: all sliders as in a freshly added one |
| **Show presets folder** | Open the folder with the presets of this plugin |

- Presets are kept in **Documents → Harmonia → Presets**, one folder per plugin; one preset is one
  `.harmoniapreset` file. You can send it to a friend or copy it to another computer, into the same folder.
- A preset remembers which plugin it is for: it does not show up in the menu of another plugin and cannot be
  loaded there.
- It works with all the built-in ones (4OSC, Drums, all the effects) and with VST3 plugins: for a VST, the
  preset holds everything the plugin itself saves into the song (like its own "Save preset").

## If a plugin crashes or hangs

Every plugin runs in **a separate program of its own**. If a plugin crashes, Harmonia does not close and the
song is not lost:

- A crashed instrument goes silent; a crashed effect lets the sound through unprocessed. Everything else plays
  as before.
- A warning at the bottom of the window says which plugin and on which track.
- A plugin that has stopped responding (hung) is stopped by Harmonia itself after a few seconds.
- **To bring the plugin back:** for an effect, click its slot in the mixer (it shows a "!" in a frame) or
  right-click → **Reload plugin**; for an instrument, the **Reload plugin** button in the Instrument row (in
  place of Show editor). The plugin starts again with the same settings.
- You can save and export the song even while a plugin is crashed: its settings will be saved.

The plugin window is also a window of its separate program. If it has hidden behind Harmonia, click
**Show editor** or click the slot again: the window comes to the front.

**If a plugin does not work in a separate program** (rare, usually old plugins with copy protection):
**File → Settings → Plugins** → untick **Run each plugin in a process of its own**. Plugins loaded after that
run inside Harmonia, as in most programs, but then a plugin crash closes Harmonia.
Plugins that are already loaded stay where they were: open the project again. It is better to turn the option
back on afterwards.

## Settings (File → Settings, Ctrl+,)

| Tab | What is there |
|---|---|
| **Audio** | The sound card (ASIO is the fastest), sample rate, buffer size. At the bottom is the delay "from key to speakers": up to about 10 ms, playing feels instant; if the sound crackles, use a bigger buffer |
| **MIDI** | All connected MIDI keyboards, each with a switch. One that is on plays the track you are editing (and is recorded with R). One that is off stays silent, as if unplugged: handy when a pad controller sends things you do not want |
| **Plugins** | Folders to search for plugins, Scan plugins, plugins in separate programs (see above) |
