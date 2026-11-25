# The Punisher: Extended Cut

A fan mod for **The Punisher (2005, PC)** - on **Windows** and on the **Steam Deck / Linux** (Proton).
One download works on both.

- **Console-style controller support** (Xbox / PlayStation / Steam Deck button prompts, analog interrogations)
- **Restored cut content** - e.g. the cut Chop Shop blowtorch interrogation, the tiki torch impale, the developers'
  own torn gib stumps (shipped turned inward), the meat-hook quick kill, early-development HUD
- **Gore and engine fixes** - more blood options, raised engine limits, retail bug fixes
- An in-game **Extended menu** for every option (pause with Start, then press Y - keyboard only: Pause, then F2)
- **Costumes in game** - switch your own costumes, the outfit (trench coat / T-shirt) and the Colt skin from the
  Extended menu while you play; costumes are made with the **Costume Launcher** (separate download)

> **Pre-release / tester builds.** Things may break - please report what you find (see *Something went wrong?*).

**No game files are included.** You need your own copy of the game. The installer changes **your own** files: it
checks every file first, keeps each original as `<file>.ec_original`, and leaves alone anything it does not
recognise. Uninstall puts every original back.

---

## Before you start (everyone)

You need these in your game folder (the folder with `pun.exe`) **before** installing Extended Cut:

1. **The Punisher (PC)**, installed.
2. **Gore Patch 3.7 (US version)** - see PCGamingWiki, *The Punisher (2005)*, Essential improvements.
   Extended Cut needs the `pun.exe` it brings.
3. **ThirteenAG's Widescreen Fix for The Punisher** - it brings the *Ultimate ASI Loader* (`dinput8.dll`) that
   starts the mod.

Then download the latest zip from **[Releases](../../releases)** (tester builds are marked *Pre-release*).

---

## Install on Windows

1. **Unzip** the download. You get a folder called `ExtendedCut_<version>`.
2. **Move that whole folder into your game folder**, so it sits next to `pun.exe`:
   `...\The Punisher\ExtendedCut_<version>\Install.exe`
3. **Double-click `Install.exe`** and type **1** (Install), then Enter.
   - Windows SmartScreen may say *"Windows protected your PC"*: click **More info** -> **Run anyway**.
     (`CHECKSUMS.txt` lists the file's SHA-256 if you want to check it. No .exe wanted? Use `Install.bat` -
     it needs Python 3.)
4. **Read the list it prints.** Every line says what was changed, or why something was left alone.
5. Start the game. Pause (Start / Esc) - the hint at the bottom shows how to open the **Extended menu**.
   No controller? Press **Pause**, then **F2** (or Esc -> Options -> Extended Menu). Arrows move / change, Enter opens
   sections, Q / E switch tabs, Esc saves and closes - full list in README.txt section 4a.

**Uninstall:** run `Install.exe` again and type **3**.

---

## Install on the Steam Deck / Linux

Do all of this in **Desktop Mode** (Steam button -> Power -> Switch to Desktop), with the game closed.

1. **Unzip** the download (Dolphin file manager: right-click the zip -> *Extract* -> *Extract archive here*).
2. **Move the `ExtendedCut_<version>` folder into your game folder**, next to `pun.exe`
   (e.g. `/home/deck/Games/The Punisher (PC)/ExtendedCut_<version>/`).
3. Open that folder, **right-click `install.sh` -> Run In Konsole**.
   (Double-clicking it may only open it as text. Python 3 is already on the Deck.)
4. **Read the list it prints**, then press Enter to close the window.
5. **Steam setup - only once:**
   1. Steam -> Games -> *Add a Non-Steam Game to My Library...* -> Browse -> `pun.exe` in your game folder.
   2. Right-click the new entry -> **Properties**.
   3. **Compatibility:** tick *Force the use of a specific Steam Play compatibility tool* and pick a Proton
      (tested with GE-Proton).
   4. **Launch Options** - paste this exact line (the mod does not start without it):

      ```
      WINEDLLOVERRIDES="dinput8,d3d8,SDL3=n,b" %command%
      ```

   5. Controller: keep Steam Input **on** with a layout that keeps the **trackpads as mouse**
      (e.g. *Gamepad with Mouse Trackpad*).
6. Back to Gaming Mode and play. In game: press **Start**, then **Y** for the Extended menu.

**Uninstall:** Desktop Mode -> right-click `uninstall.sh` -> Run In Konsole.

The full Steam Deck guide (performance settings, the Costume Launcher line, logs) is in
[docs/README.txt](docs/README.txt), section 5.

---

## Costume Launcher (optional)

Dresses the Punisher in your own outfits - your skull on the shirt and on the in-game skull icons, outfits built
from parts. With Extended Cut 0.3.0+ it sends your costumes into the game, where you switch them while you play
(Extended menu -> Costume tab). Windows and Steam Deck (incl. Gaming Mode).

- Download: the **Punisher Costume Launcher** zip from **[Releases](../../releases)** (or Nexus Mods). The
  optional templates (shirt backgrounds, outfit parts, Default HD textures) are on Nexus Mods.
- Start it: **Steam Deck** - `Start Costume Launcher.sh`. **Windows** - two ways: `Costume Launcher.exe`
  (nothing to install), or `Start Costume Launcher (Windows).bat` with your own Python 3 + Pillow + numpy.
- Source: the [`launcher/`](launcher) folder. Guide: [`launcher/README.txt`](launcher/README.txt).
- No costume art or game textures are included - you bring your own pictures.


## Something went wrong?

| What you see | What to do |
|---|---|
| A line says a file **"does not fit"** | That file is not the version the mod expects (another mod, or no Gore Patch 3.7). That part was skipped, nothing was changed. Send us the whole list. |
| **No Extended menu** / no `ExtendedCut.log` in `scripts` | Windows: the Widescreen Fix (ASI loader) is missing. Steam Deck: the Launch Options line is missing or mistyped. |
| The game will not start after installing | Run the installer again and choose Uninstall (3) - every original comes back - then tell us. |
| Running the installer twice | Safe. Finished parts just say *already installed*. |

**Report a problem:** open an **[Issue](../../issues)** - one per problem, please. Attach from your game's
`scripts` folder: `ExtendedCut.log`, `ExtendedCut_install.log` and (if there is one) `ExtendedCut_crash.log`.
The first line of `ExtendedCut.log` says which build you have.

---

## Building the plugin

`plugin/build.bat` (Visual Studio 2022+ with C++, x86) builds `ExtendedCut.asi`; it needs SDL3 3.4.x
(`set SDL3_DIR=...`). The plugin is loaded by ThirteenAG's Ultimate ASI Loader.

## How this mod was made

The Punisher: Extended Cut is directed, designed and tested by myself (Javi-096), with Claude (Anthropic's AI) as the
programming partner. Claude wrote most of the code and did most of the reverse engineering - reading the game's
machine code in Ghidra and comparing every build from the October 2004 PS2 demo to retail on PC, Xbox and PS2.
Every feature was my call, and every change is tested in the game, on Windows and the Steam Deck, before it ships.

The source code is open in this repository, and every release lists what changed and why (docs/CHANGELOG.txt).

## Credits and licence

The Extended Cut code and tools are licensed under the **GNU General Public License v3.0 or later** ([LICENSE.txt](LICENSE.txt)): use, change and share them freely - a released mod built on this code must publish its source under the same licence and keep the credits. The licence covers our code only, not the game or its files; Ultimate ASI Loader and SDL3 keep their own licences.

See [docs/CREDITS.txt](docs/CREDITS.txt).
Fan-made; not affiliated with or endorsed by Marvel, Disney, THQ or Volition. All trademarks belong to their owners.
