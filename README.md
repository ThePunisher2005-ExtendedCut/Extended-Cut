# The Punisher: Extended Cut

A fan mod for **The Punisher (2005, PC)** - also plays on the Steam Deck (Proton).

- **Console-style controller support** (Xbox / PlayStation / Steam Deck button prompts, analog interrogations)
- **Restored cut content** - e.g. the cut Chop Shop blowtorch interrogation, the tiki torch impale, the developers'
  own torn gib stumps (shipped turned inward), early-development HUD
- **Gore and engine fixes** - more blood options, raised engine limits, retail bug fixes
- An in-game **Extended menu** for every option

> **Pre-release / tester builds.** Things may break - please report what you find (see *Feedback* below).

## Download

Get the latest zip from **[Releases](../../releases)** (tester builds are marked *Pre-release*).

## Install

1. Unzip the release **into your game folder** (the one with `pun.exe`).
2. Run `Install.exe` (Windows / Steam Deck via Proton) - or `Install.bat` / `install.sh` with Python 3.
3. Read the list it prints: every line says what was changed or why something was left alone.

`Uninstall` puts every original file back. Full guide: [docs/README.txt](docs/README.txt).

## No game files

This mod ships **no game files**. The installer changes **your own** copy of the game: every file is checked
first, the original is saved as `<file>.ec_original`, and anything it does not recognise is left alone.

## Feedback

Open an **[Issue](../../issues)** - one per problem, please. Attach from your game's `scripts` folder:
`ExtendedCut.log`, `ExtendedCut_install.log` and (if there is one) `ExtendedCut_crash.log`, and say which build
you use (the first line of `ExtendedCut.log`).

## Building the plugin

`plugin/build.bat` (Visual Studio 2022+ with C++, x86) builds `ExtendedCut.asi`; it needs SDL3 3.4.x
(`set SDL3_DIR=...`). The plugin is loaded by ThirteenAG's Ultimate ASI Loader.

## Credits and licence

The Extended Cut code and tools are licensed under the **GNU General Public License v3.0 or later** ([LICENSE.txt](LICENSE.txt)): use, change and share them freely - a released mod built on this code must publish its source under the same licence and keep the credits. The licence covers our code only, not the game or its files; Ultimate ASI Loader and SDL3 keep their own licences.

See [docs/CREDITS.txt](docs/CREDITS.txt).
Fan-made; not affiliated with or endorsed by Marvel, Disney, THQ or Volition. All trademarks belong to their owners.
