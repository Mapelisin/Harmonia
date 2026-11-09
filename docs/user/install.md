# Installing Harmonia

**English** | [Русский](install.ru.md)

## Where to get the installer

The file is called `Harmonia-Setup-<version>.exe` (for example, `Harmonia-Setup-0.54.0.exe`).

- **The easiest way:** the [Releases](https://github.com/Mapelisin/Harmonia/releases/latest) page → **Assets** at
  the bottom → `Harmonia-Setup-<version>.exe`. You do not need to sign in to GitHub. A release comes out after
  every fifth development stage.
- The newest build (between releases). GitHub builds it on every change to the program: the
  [Actions](https://github.com/Mapelisin/Harmonia/actions) page → the latest green **CI** run → **Artifacts** at
  the bottom → **Harmonia-Setup** (a zip archive downloads, the installer is inside). You need to sign in to
  GitHub.
- Or on your own computer: `tools\build.cmd windows-release`, then `tools\make-installer.cmd`. The file appears
  in `build\installer`.

## Installing

1. Run the installer. Windows may warn you with "Windows protected your PC": the installer is not yet signed
   with a paid digital signature. Click **More info** → **Run anyway**.
2. The first question is who to install for:
   - **for all users**: into `Program Files`; Windows will ask for administrator rights;
   - **for me only**: into your own folder, no administrator rights needed.
3. The checkboxes:
   - **Create a desktop shortcut**;
   - **Open .harmonia projects with Harmonia**: a project opens with a double click in File Explorer.
4. At the end you can start Harmonia right away.

The installer uses the language of your Windows (English or Russian). The program itself is in English for now.

## How it works afterwards

- **Opening a project with a double click.** If Harmonia is already open, the project opens in the same window:
  first it asks whether to save the changes in the current one.
- **Updating.** Just run the installer of the new version over the old one. If Harmonia is open, the installer
  offers to close it.
- **Uninstalling.** Windows Settings → Apps → Harmonia → Uninstall (or "Uninstall" in the Start menu). This
  removes the program, the shortcuts and the link to `.harmonia` files. Your projects (`Documents\Harmonia`) and
  settings (the plugin folder, the sound card, the plugins found) stay: they will be useful the next time you
  install.

You need 64-bit Windows 10 or 11.
