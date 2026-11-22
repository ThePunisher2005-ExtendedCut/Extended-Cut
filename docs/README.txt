THE PUNISHER: EXTENDED CUT  -  player guide (your version: see CHANGELOG.txt and the first line of ExtendedCut.log)
==================================================================================
A mod for The Punisher (2005, PC): console-style controller support, restored cut content (the blowtorch
interrogation, the tiki torch impale, the early development HUD and more), gore options and photo mode.
Works on Windows and on the Steam Deck (Proton).

NO GAME FILES ARE INCLUDED. You need your own copy of the game. The installer changes your own files, checks every
file before it touches it, and keeps each original as <file>.ec_original, so you can uninstall at any time.

---------------------------------------------------------------------
1. WHAT YOU NEED FIRST
---------------------------------------------------------------------
- The Punisher (PC), installed.
- Gore Patch 3.7 (US version) - see PCGamingWiki, "The Punisher (2005)", Essential improvements. It brings the
  pun.exe without the disc copy protection (Extended Cut needs it) and the tables Extended Cut is built on.
- Ultimate ASI Loader by ThirteenAG (x86 "dinput8.dll") next to pun.exe. ThirteenAG's Widescreen Fix for
  The Punisher brings it (recommended anyway).
- Nothing else for Install.exe. The other ways to install need Python 3 (Steam Deck: already there;
  Windows: python.org, tick "Add python.exe to PATH").

---------------------------------------------------------------------
2. INSTALL
---------------------------------------------------------------------
1. Put this whole folder INSIDE your game folder (the folder with pun.exe in it).
2. Pick ONE way - they all do exactly the same:
   a) Windows, easiest: double-click Install.exe, then choose 1 (Install). Python is built in.
      Some antivirus programs distrust any new .exe; CHECKSUMS.txt lists its SHA-256 if you want to verify it,
      or use b) instead.
   b) Windows, no .exe: double-click Install.bat (needs Python 3). It runs the plain .py scripts in this folder,
      which you can read first.
   c) Steam Deck / Linux (Desktop Mode): right-click install.sh -> Run In Konsole (double-clicking it may only
      open it as text)
      (or open a terminal in this folder and type:  sh install.sh). Python 3 is already on the Deck.
   d) By hand, any system with Python 3:  python ec_install.py install "<game folder>"
      (also: check = only show what would change; uninstall).
   Android (Winlator / GameHub and other Windows emulators): run Install.exe inside the emulator, in the same
   container as the game. NOT TESTED on Android yet - reports welcome.
3. Read the list it prints. Every line says what was changed or why something was left alone.

What gets installed:
   plugin     the Extended Cut plugin (scripts\ExtendedCut.asi + .ini) + SDL3.dll (controller library) next to pun.exe
   laa        lets pun.exe use 4 GB of memory (one flag in its header) - helps with heavy gore
   tables     restored kills and table fixes (tables.vpp) - incl. the tiki torch impale and the blowtorch death
   font       Xbox / PlayStation / Steam Deck button icons for the on-screen prompts (misc.vpp)
   pictures   Xbox / Steam Deck stick pictures, rebuilt icons, 2004 lifebar, early development HUD (mini.vpp)
   blowtorch  the cut blowtorch interrogation in Chop Shop (Chop1.vpp)
   hooks      the meat-hook quick kill in the Meat Packing Plant (Meat1.vpp, built from your own level files) and
              the blood drip / pool of the tiki torch kill in the Zoo (zoo1.vpp)
   stumps     the restored gib stumps for Gore -> "Gib stumps" (built from your own mini.vpp; the game's own stay)
   menuart    the Extended menu's pictures and the main menu's notebook paper (copied from your own game files)
   blackcolt  the Punisher's own Colt .45 in black, enemies keep the normal one (made from your own Colt texture;
              Extended menu -> Costume -> Weapon Skin -> Colt .45: 04 Movie)
   outfits    both Punisher outfits (trench coat + T-shirt) for the outfit switch (copied from your own level files)

If a line says a file "does not fit", that file is different from what this release expects (another mod or
another game version). That file is left alone - please send the whole list.
Running the installer again is safe: finished parts say "already installed".
The Costume Launcher can be used before or after the installer.

Steam Deck: the game needs one Launch Options line - see section 5.

---------------------------------------------------------------------
3. UNINSTALL
---------------------------------------------------------------------
Install.exe -> choose 3. Or: Uninstall.bat (Windows), uninstall.sh (Steam Deck), python ec_install.py uninstall.
Every original file goes back (only files that are still exactly what the installer wrote - if another mod changed
a file since, that file is left alone and the list says so).

---------------------------------------------------------------------
4. IN GAME
---------------------------------------------------------------------
Extended Menu: press Start (pause), then Y. Or from the in-game menu: Options -> Extended Menu (paper look, like the
main menu). Keyboard and mouse only (no controller): see 4a below.
Tabs (LB / RB): Controls, Gameplay, Gore, HUD & Display, Restored content, Cheats & Debug, Costume. LT / RT jump
between sections, A opens / closes a section. Changes save to scripts\ExtendedCut.ini; X = back to the default of
the highlighted setting.
Costume tab: Outfit (Level's own / Trench coat / T-shirt), Costume (your costumes - he changes at once, even in the
middle of a level), In-game skulls, Reload costume files, Weapon Skin. Costumes come from the Costume Launcher
(separate download): its "Send changes to the game" / "Send whole library to the game" buttons put them into
scripts\Extended Cut\Costumes. The PC version's exclusive T-shirt skins are there as PC Exclusive 1980 / 1990 / 2000.
Photo mode: L3 + R3 (keyboard: F9, [Photo] Key in the ini).
Blowtorch: Chop Shop (first level), grab a thug and take him to the welding torch - the skull marks the spot.
Tiki torch impale: the Zoo's tiki bar torches (environment quick kill).
Meat hook: Meat Packing Plant, grab a thug and take him to the wall hooks or the hook racks.

4a. KEYBOARD AND MOUSE ONLY (no controller needed, 0.3.1)
   Open the Extended Menu - two ways, both in a level:
     a) Press Pause (the Pause / Break key). The pause screen shows "F2  Extended Menu" at the bottom left.
        Press F2.
     b) Press Esc (in-game menu) -> Options -> Extended Menu. Use this if your keyboard has no Pause key (many
        laptops) - it is the same menu, on the notebook paper.
   In the menu:
     Up / Down arrows  (or W / S) ....... move between rows
     Left / Right arrows (or A / D) ..... change the highlighted setting; on a section heading: open / close it
     Enter or Space ..................... open / close a section, switch a setting, open an achievement level
     Q / E  (or Page Up / Page Down) .... previous / next tab
     Tab / Shift + Tab .................. jump to the next / previous section
     R or Delete ........................ put the highlighted setting back to its default
     Esc or Backspace (or F2 again) ..... save and close
   Changes work at once and are saved to scripts\Extended Cut\ExtendedCut.ini when you close the menu.
   The line at the bottom of the menu shows these keys once you use the keyboard (pad buttons again when you press
   a controller button). The mouse is not used in the menu. While the menu is open the game gets no key presses.
   Another key: ExtendedCut.ini -> [Enhancements] KeyboardKey=F2 (any key name: F3, F6, M, NUMPAD0 ...; NONE = off).
   Keys already taken: F1 = the game's Help, F9 = photo mode.
   With a controller nothing changes (Start, then Y); controller and keyboard can both be used, also in the same
   visit to the menu.

---------------------------------------------------------------------
5. STEAM DECK
---------------------------------------------------------------------
5a. Add the game to Steam (Desktop Mode)
   1. Steam -> Games -> "Add a Non-Steam Game to My Library..." -> Browse -> pun.exe in your game folder.
   2. Right-click the new entry -> Properties.
   3. Shortcut: Target = pun.exe, Start In = the game folder (Steam fills both in).
   4. Compatibility: tick "Force the use of a specific Steam Play compatibility tool" and pick a Proton.
      Tested with GE-Proton (11-11). Keep the old GE version installed when you update, so you can go back.
   5. General -> Game Resolution: Native (1280 x 800). Docked, the game then starts at your TV's resolution.

5b. Launch Options (Properties -> General -> Launch Options), ONE line:
   Without the Costume Launcher:
      WINEDLLOVERRIDES="dinput8,d3d8,SDL3=n,b" %command%
   With the Costume Launcher (shows the launcher first when you press Play - see 6f):
      WINEDLLOVERRIDES="dinput8,d3d8,SDL3=n,b" "/home/deck/Games/The Punisher (PC) - Costume Launcher/launch.sh" %command%
      (use your own launcher path; the launcher's Settings -> Gaming Mode copies the right line for you)
   WINEDLLOVERRIDES makes Proton load the mod files next to pun.exe (ASI loader, widescreen fix, SDL3) instead of
   its own. Without it the plugin does not start (no Extended Menu, no ExtendedCut.log).
   Using Decky LSFG-VK (frame generation)? Its ~/.lsfg goes right before %command%, e.g.
      WINEDLLOVERRIDES="dinput8,d3d8,SDL3=n,b" "<launcher path>/launch.sh" ~/.lsfg %command%

5c. Controller (Steam Input)
   Leave Steam Input ON with a layout that keeps the TRACKPADS AS MOUSE (e.g. Steam's "Gamepad with Mouse
   Trackpad" template). The
   plugin reads the sticks and buttons itself; the mouse is still handy for the game's menus and the launcher.
   Button prompts follow your pad by themselves; Extended Menu -> Controls can force Xbox / PlayStation / Deck icons.

5d. Recommended performance settings (less heat, quieter fan, longer battery)
   The game holds 60 fps easily - the GPU is nearly idle. The heat comes from one CPU core that the old engine
   keeps busy, which makes the Deck boost the whole CPU to full speed. Measured in the Zoo before the settings:
   60 fps, GPU 21 %, one core 82 %, all cores at 3.5 GHz, 14 W from the battery, fan ~3800 rpm.
   Press the ... button -> battery icon (Performance):
      1. Use per-game profile: ON (the settings below then only apply to The Punisher)
      2. Refresh Rate: 60 Hz   (the OLED is at 90 Hz by default - the game is made for 60)
      3. Framerate Limit: 60
      4. Advanced View: ON -> TDP Limit: 7 W
   Decky -> PowerTools (optional plugin, from the Decky store):
      5. SMT: OFF   (a 2005 game uses one or two cores)
      6. CPU max frequency: 2000 MHz. Still a steady 60 fps? Try 1600 MHz. Frame rate drops? Go back one step.
   Check with the Performance Overlay (level 4): fps should stay at 60 in a big firefight.
   Note: Steam screenshots do not show the overlay - take a phone photo if you want to send it.

5e. Files and logs
   Desktop Mode, Dolphin file manager: the game folder (e.g. /home/deck/Games/The Punisher (PC)).
   Logs for reports: <game folder>/scripts/ExtendedCut.log (+ ExtendedCut_crash.log).
   Install / uninstall from Desktop Mode (install.sh / uninstall.sh), with the game closed.

---------------------------------------------------------------------
6. COSTUME LAUNCHER (separate download - Nexus Mods, "Punisher Costume Launcher")
---------------------------------------------------------------------
Dresses the Punisher in your own outfits: a costume for the whole game or per mission, your own skull on the shirt
and on the in-game skull icons. NO costume art comes with it - you bring your own pictures. Its folder has a full
guide (README.txt in the launcher folder); this is the short version.

6a. What you need
   Python 3 with Pillow and numpy.
     Steam Deck: nothing to install - "Start Costume Launcher.sh" adds numpy the first time by itself.
     Windows: python.org (tick "Add python.exe to PATH"), then once in a command prompt:  py -m pip install pillow numpy

6b. Where things go - keep these three apart
   Launcher folder    anywhere, e.g. /home/deck/Games/The Punisher (PC) - Costume Launcher
                      (Windows e.g. C:\Games\The Punisher (PC) - Costume Launcher)
   Costume Library    your pictures. Best INSIDE the launcher folder: <launcher folder>/Costume Library/
                      (NOT inside the game folder - then a game reinstall cannot touch it)
   Game folder        the folder with pun.exe. The launcher keeps its own data in <game>/PunisherCostumes/
                      (backups, cache, your choices) - never edit or delete that folder by hand.

6c. Costume Library - where to put your textures
      Costume Library/
         Parts/               one folder per body part, one picture (PNG) per style
         Skulls/              skull pictures with a transparent background, in groups (any folder names)
         Shirt Backgrounds/   blank shirt fronts (no skull) the skull is printed on, e.g. 512 x 1024
         Kits/                saved outfits - the launcher makes these, not you
   Part folders = any name + the game texture code in brackets. Keep the brackets, rename the rest freely:
         Parts/Pants & Gloves (05)/Black Suit.png         (05)  = texture 05 of both outfits
         Parts/T-shirt Back (t07)/Modern.png              (t..) = T-shirt outfit only
         Parts/Coats (c06-c11)/Black Trench Coat/06.png   (c..) = trench coat only; a range = one SUBFOLDER per
                                                   07.png ...       style, files named by texture number
   Texture codes: 01 face, hair, belt, boots | 02 teeth | 03 neck and collar | 04 shirt front (made for you from
   skull + shirt background) | 05 pants and gloves | 12 hair (transparent) | t06 strap | t07 T-shirt back |
   t08 sleeves | t09 thigh holsters | c06 coat back | c07 coat sleeves and front | c08 coat skirt |
   c09 coat shoulders | t10 t11 t13 c10 c11 small details (transparent).
   Skulls: PNG, transparent background, square-ish, 512 px or more. One skull is used for the shirt print AND the
   in-game skulls (white interrogation skull, orange quick-kill skull - both get a worn look and the game's orange
   made for you - plus the loading-screen, HUD and mission-list skulls).
   Sizes: up to 2048 px per side (bigger is scaled down; 4096 hangs the loading screen). Keep PNG masters;
   DDS (DXT5) and TGA also work. A folder or file starting with _ is ignored (e.g. _unused).
   Each library folder has a short README.txt of its own.

6d. First start
   Steam Deck: in Desktop Mode, double-click "Start Costume Launcher.sh" (or right-click -> Run In Konsole).
   Do the first setup in Desktop Mode - Gaming Mode cannot show the folder pickers.
   Windows: double-click launcher.py (or  py launcher.py  in the launcher folder).
   1. Pick the GAME folder (the one with pun.exe) - asked once.
   2. "Library folder..." (top right) -> pick your Costume Library. After adding files: "Rescan library".

6e. Make and use a costume
   Parts & Skulls tab (kit builder): pick a skull, a shirt background and a style per part (empty = the game's
   own texture) -> "Adjust the skull fit >" for size / position / fabric -> "Save kit...".
   Costumes tab: pick the kit -> "Use it for the whole game" (tick "Also use its in-game skulls" for the icons),
   or "Per-level choices >" for one costume per mission.
   Then "Apply to the game" (CLOSE THE GAME FIRST). Only the changed level files are rebuilt.
   "Preview changes" = what Apply would do; "Check (verify)" = is every costume really in the game.
   Back to the original look: Reset -> "Everything back to original" (your library stays).

6f. Gaming Mode
   With the launcher line from 5b, pressing Play in Steam shows the launcher first: PLAY (Start button) applies any
   changes and starts the game; "Quit" does not start it. Pad: D-pad / left stick move, A select, B back,
   LB / RB tabs, Start = Play. Steam + X opens the keyboard.

6g. Together with Extended Cut
   Install / Apply in any order. After you replace a level file by hand (a test file), press Apply once more so
   your costume goes back into it.
   Settings -> "Smooth blacks": fine with Extended Cut 0.1.6 or newer (its 128 MB level memory makes room - tested in
   every heavy level). With an older Extended Cut or without it, keep it OFF (heavy levels can hang on loading).
   NEVER use the launcher's command-line "restore-all": it puts back whole level files from its first backups
   and would undo Extended Cut's level changes (e.g. the blowtorch interrogation).
   Problems: send launcher.log from the launcher folder (and launcher_error.txt if there is one).

6h. Costumes in the game (Costume tab, Extended Cut 0.3.0 + Costume Launcher 1.0)
   "Send changes to the game" (only what you added or changed) or "Send whole library to the game" writes your kits,
   costumes and mixes for the in-game picker. In the game: Extended Menu -> Costume -> Costume (the game running:
   "Reload costume files" first). No Apply and no level files needed for this; Apply still works as before.

---------------------------------------------------------------------
7. KNOWN ISSUES
---------------------------------------------------------------------
- Steam Deck: a thin vertical line near the right edge of the screen during black & white kills and Slaughter mode
  (Deck only - Windows does not show it). Cosmetic; still being looked into. Turning off "Black & white kill
  effects" / "Black & white Slaughter mode" (Extended Menu -> HUD & Display) avoids it.
- ThirteenAG's new Fusion Fix (development builds) does not work well together with Extended Cut on the Steam Deck
  yet (black & white screens split in two). Keep the old Widescreen Fix for now.

---------------------------------------------------------------------
8. PROBLEMS - please report
---------------------------------------------------------------------
Send these files from the scripts folder in your game folder (Steam Deck: e.g.
/home/deck/Games/The Punisher (PC)/scripts/):
   ExtendedCut.log           always - written fresh at every game start (first line = version)
   ExtendedCut_crash.log     if the game crashed
   ExtendedCut_hang.log      if the game froze
   ExtendedCut_install.log   what the installer did (every run is added at the end)
   ExtendedCut.ini           your settings
Close the game first, so the logs are complete. Screenshots (or phone photos) help.

Fan-made, not affiliated with or endorsed by Marvel, Disney, THQ or Volition. All trademarks belong to their owners.
