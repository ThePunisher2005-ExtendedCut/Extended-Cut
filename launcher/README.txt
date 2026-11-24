THE PUNISHER (2005, PC) - COSTUME LAUNCHER  v1.0  -  HOW TO USE IT
======================================================================
The Costume Launcher dresses the Punisher in your own outfits: pick a costume for the whole game or for each
mission, put your own skull on the shirt and on the in-game skull icons, then press Apply (or PLAY).
It only changes the level files of missions you changed, and it never touches the prison and jungle levels
(their outfits stay as the game made them).

IN-GAME COSTUMES (with The Punisher: Extended Cut 0.3.0 or newer)
  Besides Apply (which writes your costumes into the level files), the launcher can send your costumes to the
  Extended Cut's in-game picker: Extended menu -> Costume tab -> Costume. He changes at once, even mid-level.
  - 'Send changes to the game': only the kits, costumes and mixes you added or changed since the last send (and
    the ones you deleted from your library disappear from the game too) - seconds.
  - 'Send whole library to the game': every kit, costume and mix, plus the game's three PC Exclusive T-shirt skins
    (1980, 1990, 2000 - made from your own misc_pc.vpp).
  - 'Send to in-game picker': only the costumes you picked for levels.
  Each costume gets smooth blacks on every dark part (up to 2048 x 1024) and its own in-game skulls: a clean skull
  (menus, loading screen, score), a worn one (over enemies, interrogation / special-kill hotspots), the orange
  hotspot skull and the HUD meter skull (Costume tab -> In-game skulls: Follow costume). The PC Exclusive skins
  take the skull from any picture in Costume Library\Skulls whose file name has the year and "PC Exclusive"
  (e.g. "1980 PC Exclusive.png"). The game running: 'Reload costume files' in the Costume tab after a send.
  Command line: picker changes / picker all.

Contents
  0. Quick start (zero to your costume in game)
  1. What you need
  2. Where things go
  3. The Costume Library (folder structure)
  4. First start
  5. The Costumes tab
  6. The Parts & Skulls tab (kit builder)
  7. Settings
  8. Steam Deck: start it from Steam / Gaming Mode
  9. Going back to the original look
 10. Tips and limits
 11. Problems
 12. Credits


---------------------------------------------------------------------------------------------------------------
0. QUICK START
---------------------------------------------------------------------------------------------------------------
 1. Download the launcher zip (Nexus Mods) and unpack it to its own folder, e.g.
        Steam Deck:  /home/deck/Games/The Punisher (PC) - Costume Launcher
        Windows:     C:\Games\The Punisher (PC) - Costume Launcher
 2. Download the Costume Launcher templates mod from Nexus Mods (shirt backgrounds + outfit pieces) and unpack it
    into the launcher folder's "Costume Library" folder (create it if it is not there).
 3. Start the launcher (see 4): Steam Deck - "Start Costume Launcher.sh"; Windows - "Costume Launcher.exe"
    (nothing to install) or "Start Costume Launcher (Windows).bat" (needs Python, see 1). Pick the GAME folder
    (the one with pun.exe) when it asks. The launcher uses the
    "Costume Library" folder inside its own folder by itself - nothing else to pick.
 4. Tab "Parts & Skulls" -> "Add a skull..." -> your skull picture (PNG, transparent background).
 5. On the right (Kit builder): pick the skull, a shirt background and the parts you want -> "Save kit...".
 6. Put it into the game - one of two ways:
    a) With The Punisher: Extended Cut 0.3.0 or newer (easiest): "Send changes to the game" (Costumes tab).
       In the game: pause -> Extended menu (Y) -> Costume tab -> Costume -> pick your kit. He changes at once;
       you can switch costumes any time while you play. After the next kit you make, "Send changes" again
       (the game running: "Reload costume files" in the Costume tab).
    b) Without Extended Cut: tab "Costumes" -> pick the kit -> "Use it for the whole game" -> "Apply to the game"
       (close the game first). The costume is written into the level files.
 7. Start the game. Steam Deck: to get the launcher every time you press Play in Gaming Mode, see 8.


---------------------------------------------------------------------------------------------------------------
1. WHAT YOU NEED
---------------------------------------------------------------------------------------------------------------
- The Punisher (PC), installed.
- Python 3 with Pillow and numpy (Windows: NOT needed if you start "Costume Launcher.exe"):
    Steam Deck: nothing to install by hand. The first start of "Start Costume Launcher.sh" sets up its own
                Python (~/punisher-env) with Pillow and numpy - it needs internet that one time and can take a
                minute. (python_setup.txt in the launcher folder shows what it did.)
    Windows:    two ways to start the launcher - pick one:
                a) "Costume Launcher.exe" - nothing to install. Python, Pillow and numpy are built in.
                b) "Start Costume Launcher (Windows).bat" - uses your own Python. Install Python 3 from python.org
                   (tick "Add python.exe to PATH") or from the Microsoft Store (no admin rights needed), then
                   open a command prompt once:
                       py -m pip install pillow numpy
- Your own pictures. NO costume art comes with the launcher - you bring the outfits, skulls and shirts.


---------------------------------------------------------------------------------------------------------------
2. WHERE THINGS GO
---------------------------------------------------------------------------------------------------------------
Keep three things apart:

  The launcher folder      e.g.  ~/Games/The Punisher (PC) - Costume Launcher        (Deck)
                                 C:\Games\The Punisher (PC) - Costume Launcher       (Windows)
                           = launcher.py, costume_manager.py, the .sh files, launcher_settings.json
  Your Costume Library     recommended: INSIDE the launcher folder:  <launcher folder>\Costume Library\
                           (any folder works - choose it with "Library folder..." at the top of the window)
  The game folder          the folder with pun.exe. The launcher keeps its own data there in PunisherCostumes\
                           (backups, a cache, your choices). Do not edit or delete that folder by hand.

Why the library should NOT live inside the game folder: if you reinstall or replace the game, your library
stays safe. (Without a chosen library the launcher uses <game folder>\PunisherCostumes\costumes.)


---------------------------------------------------------------------------------------------------------------
3. THE COSTUME LIBRARY (FOLDER STRUCTURE)
---------------------------------------------------------------------------------------------------------------
    Costume Library\
        Parts\                     one folder per body part, one picture per style
        Skulls\                    skull pictures (transparent background), in groups
        Shirt Backgrounds\         blank shirt fronts the skull is printed on
        Kits\                      saved outfits (made by the launcher - you do not make these by hand)
        <old-style costumes>\      optional: complete costume folders (see 3e)

Every folder already holds a short README.txt. A folder or file whose name starts with _ is ignored
(handy for parking things: _unused, _removed).

3a. PARTS
    Folder name = any words + the texture code in brackets. The code says which game texture the pictures replace:
        (05)        texture 05 of BOTH outfits (T-shirt and trench coat)   e.g.  Pants & Gloves (05)
        (t07)       T-shirt outfit only                                     e.g.  T-shirt Back (t07)
        (c06)       trench coat outfit only                                 e.g.  Coat Back (c06)
        (c06-c11)   several textures at once: then every STYLE is a SUBFOLDER whose files are named by number
    Inside a part folder, each picture is one style; the file name is the style's name.

        Parts\
            Pants & Gloves (05)\
                Modern.png
                Black Suit.png
            T-shirt Back (t07)\
                Modern.png
            Coats (c06-c11)\
                Black Trench Coat\
                    06.png  07.png  08.png  09.png
                Brown Trench Coat\
                    06.png  07.png  08.png  09.png

    Rename anything BEFORE the brackets as you like; keep the brackets.
    Texture numbers known so far:
        01  face, belt, boots                  02  teeth                  03  neck and collar
        04  shirt front = the skull shirt       05  pants and gloves
        10  hair cap   11  eyelashes (transparent)   12  hair (transparent)   13  hairline edge (transparent)
        t06 strap   t07 T-shirt back (mirrored)   t08 sleeves (with the bare upper arm)   t09 thigh holsters
        c06 coat back   c07 coat sleeves & front   c08 coat skirt   c09 coat shoulders
    (10, 11 and 13 are separate per outfit in the game - c10 / c11 on the coat - but look the same.)
    The skull shirt (04) is normally made FOR you from a skull + a shirt background (see 3b / 3c), so you rarely
    need a part folder for it.

3b. SKULLS
        Skulls\
            MAX\
                Tim Bradstreet.png
            616\
                Klaus Janson.png
                Klaus Janson - Alt.png
    PNG with a transparent background, square-ish, 512 px or more. The folders are just groups - name them as
    you like. One skull picture is used for:
        - the print on the shirt (texture 04, both outfits),
        - the in-game skulls: the white interrogation hotspot skull (also the enemy icon and menu headers), the
          orange environmental quick-kill skull, the loading-screen skull, the small HUD skull and the
          mission-list marker.
    The two hotspot skulls (white + orange) get a worn, chipped look and the orange one the game's exact orange
    shading, made from your picture automatically. The loading screen, HUD and mission-list skulls use your
    picture as it is. (A skull set that brings its own orange picture keeps it - no wear is added then.)

3c. SHIRT BACKGROUNDS
        Shirt Backgrounds\
            Modern.png
            1980 - Black.png
    A blank shirt front (no skull), PNG, the size of texture 04 (for example 512 x 1024).
    A new library has NONE - the launcher does not ship game textures. Get the ready-made backgrounds (and the
    other outfit templates) from the Costume Launcher templates mod on Nexus Mods, put the PNGs into
    Shirt Backgrounds\ and press Rescan. Until then the Kit builder can use parts but cannot print a skull shirt.

3d. KITS
    A kit = one complete outfit: which style of each part + a skull + a shirt background + how the skull sits on
    the shirt. It only POINTS at your pictures (nothing is copied), so renaming a part / skull in the launcher
    updates every kit that uses it. Kits are saved as Kits\<group>\<name>.json (+ <name>.shirt.png).

3e. OLD-STYLE COSTUME FOLDERS (optional)
    A complete costume can also be a folder with these subfolders, holding pictures named like the game's
    textures (pun_tshirt_04.png, punps2_trench06.png ... - PNG, DDS or TGA):
        My Costume\
            T-shirt\            the T-shirt outfit's textures
            Trench\             the trench coat outfit's textures
            in-game skull\      the in-game skull picture(s)
    A folder inside a costume folder = a variant of it (a variant without its own in-game skull uses its
    parent's).
    Good use: a full set of upscaled default textures as your BASE LAYER (see 5). Its old tools (Shirt creator,
    Library tab) were retired in 0.9.15 - the Kit builder and Parts & Skulls do their jobs.


---------------------------------------------------------------------------------------------------------------
4. FIRST START
---------------------------------------------------------------------------------------------------------------
Steam Deck (Desktop Mode): double-click "Start Costume Launcher.sh".
    If it opens as TEXT instead of running: right-click it -> Properties -> Permissions -> tick "Is executable"
    (or "Allow executing file as program") -> OK, then double-click again. Or right-click -> Run In Konsole.
Windows: double-click "Costume Launcher.exe" (no Python needed), or "Start Costume Launcher (Windows).bat" if you
installed Python (see 1). Not launcher.py - that may just open in an editor. Keep the .exe in the launcher folder,
next to launcher.py (it runs the launcher files next to it). The first start of the .exe takes a few seconds.
If Windows says "Windows protected your PC": click "More info" -> "Run anyway" (the .exe is not signed).
1. It asks for the GAME folder once (the folder with pun.exe). Remembered in launcher_settings.json.
2. The library: a new install uses the "Costume Library" folder inside the launcher folder by itself. To use a
   different one, click "Library folder..." (top right). Press "Rescan library" after you add files in your file
   manager.
3. Close the game before you Apply - the launcher cannot write level files while the game is running.


---------------------------------------------------------------------------------------------------------------
5. THE COSTUMES TAB
---------------------------------------------------------------------------------------------------------------
- Kit: pick a kit, then "Use it for the whole game". "Also use its in-game skulls" puts the kit's skull on the
  in-game skull icons too.
- Per-level choices >: one costume per mission, plus two special rows:
    Base layer (under every costume)  textures a costume does not bring come from here instead of the game's
                                      low-resolution originals (e.g. a set of upscaled default textures)
    In-game skulls (whole game)       which skull the in-game icons use - one choice for the whole game
- Game files:
    Preview changes   lists what Apply would change (changes nothing)
    Apply to the game writes your choices (only the level files that changed are rebuilt)
    Check (verify)    is every costume really in the game?
- PLAY (Start): applies your changes first if there are any, then starts the game (when started from Steam).
- In-game costumes (Extended Cut 0.3.0 or newer - see the top of this file):
    Send changes to the game        only what you added or changed since the last send (fast)
    Send whole library to the game  every kit, costume and mix + the PC Exclusive skins (slower, rebuilds all)
    Send to in-game picker          only the costumes picked for levels
  In the game: Extended menu -> Costume tab -> Costume. Apply and the in-game costumes work side by side.


---------------------------------------------------------------------------------------------------------------
6. THE PARTS & SKULLS TAB (KIT BUILDER)
---------------------------------------------------------------------------------------------------------------
Left: your library (Parts, Skulls, Shirt Backgrounds, Kits). Add / rename / remove from here:
    "Add a picture to the selected part...", "New part folder...", "Add a skull...", "Add a shirt background...".
    Remove moves things into a _removed folder - nothing is deleted.
Right: the Kit builder.
    1. Skull (from Skulls in your library)
    2. Shirt background
    3. Parts - pick a style per part; EMPTY = keep what is underneath (your base layer, else the game's own).
       The rows are grouped: TRENCH COAT, BOTH OUTFITS, T-SHIRT ONLY; the face pieces (hair, eyelashes, teeth)
       sit folded under "Misc" - you normally leave them empty.
    4. "Adjust the skull fit >" - size and position of the skull on the shirt, and the Fabric amount
       (how much of the shirt's folds show through the print). Reset = back to the standard fit.
         Straighten teeth: 1.00 = the bend of the shirt is cancelled (measured on both outfits - the T-shirt and
           the trench coat each get their own correction). Leave it at 1.00.
         Teeth end: where the teeth stop. 0.89 ends them just above the belt on both outfits; 0.90 touches the
           belt on the T-shirt. More than ~0.91 goes behind the belt.
    5. "Save kit..." - pick a group (folder in Kits) and a name. "Use for the whole game" picks it for every mission -
       then press Apply (or PLAY) to put it into the game.
    To change a kit later: pick it under Kits, then "Edit in the builder".


---------------------------------------------------------------------------------------------------------------
7. SETTINGS
---------------------------------------------------------------------------------------------------------------
Look        colour preset (Black / Dark / Light), Custom colours (#rrggbb), Text size.
Behaviour   When you press Play in Steam: show this launcher first, or start the game straight away.
            Big-list costume picker in Desktop Mode too: a costume box opens the big full-window list (the one
              you get from Steam / Game Mode) instead of a small drop-down menu. Always on when started from Steam.
            Smooth blacks (ON by default): writes mostly dark parts uncompressed - smoother dark gradients,
              but 4x the memory, so only up to a size budget per outfit. With the Extended Cut mod installed the
              launcher sees its bigger level memory and allows more. If a level ever hangs on loading, switch it
              OFF and Apply again.
            Controller navigation (D-pad, A, B, LB / RB, Start).
            Level files rebuilt at the same time (Apply speed) - lower it if your machine struggles.
Gaming Mode the Steam launch options line, with a "Copy to clipboard" button (see 8).
"Save settings and restart the launcher" when you are done.


---------------------------------------------------------------------------------------------------------------
8. STEAM DECK: START IT FROM STEAM / GAMING MODE
---------------------------------------------------------------------------------------------------------------
This makes the launcher appear when you press Play on the game in Gaming Mode; PLAY in the launcher then starts
the game. Do it once, step by step:

A. ONE TIME IN DESKTOP MODE (Steam button -> Power -> Switch to Desktop)
   1. Start the launcher once: double-click "Start Costume Launcher.sh" in the launcher folder (or right-click it
      -> Run In Konsole). The first start installs what it needs; let it finish.
   2. Pick the game folder when it asks (the folder with pun.exe), and your Costume Library (see 4).
   3. Tab "Settings" -> box "Gaming Mode (Steam launch options)" -> press "Copy to clipboard".
      (That line already has YOUR launcher path in it.)
   4. Open Steam (desktop). If the game is not in Steam yet: Games menu (top) -> "Add a Non-Steam Game to My
      Library..." -> Browse -> go to the game folder -> pick pun.exe -> "Add Selected Programs".
   5. In the Steam library, right-click The Punisher -> Properties...
        - Compatibility: tick "Force the use of a specific Steam Play compatibility tool" and pick Proton
          (any recent version).
        - Shortcut (General): click into the LAUNCH OPTIONS box, press Ctrl+V to paste the line from step 3.
          If something is already in that box, put the launcher's part in FRONT of %command% and keep the rest.
      It looks like this (with your own path):
        WINEDLLOVERRIDES="dinput8,d3d8,SDL3=n,b" bash "/home/deck/Games/The Punisher (PC) - Costume Launcher/launch.sh" %command%
   6. Close the Properties window. Back to Gaming Mode (desktop icon "Return to Gaming Mode").

B. EVERY TIME IN GAMING MODE
   1. Library -> The Punisher -> Play.  The launcher appears first (it may take a few seconds).
   2. Pick costumes, then press PLAY (Start button). The launcher applies your choices and starts the game.
      "Quit" closes the launcher WITHOUT starting the game.

C. IF SOMETHING IS WRONG
   - The game starts straight away, no launcher: the launch options are missing or mistyped - repeat A3 + A5.
     (Or Settings -> "When you press Play in Steam" is set to "start the game straight away".)
   - Nothing happens / black screen: open launch_debug.txt and launcher.log in the launcher folder (Desktop Mode)
     - they say how far it got.
   - The Extended Cut mod needs the same WINEDLLOVERRIDES part; this line already has it, do not add it twice.

Controller: D-pad / left stick move, A press / open, B back, X reset box, Y preview, LB / RB tabs, LT / RT page,
Start = Play, View = help. Steam + X opens Steam's keyboard as usual.


---------------------------------------------------------------------------------------------------------------
9. GOING BACK TO THE ORIGINAL LOOK
---------------------------------------------------------------------------------------------------------------
Costumes tab -> Reset -> "Everything back to original": every mission, the skulls and the base layer go back to
Default and are applied. Your library is not touched.
Do NOT use the command-line "restore-all" with other mods installed: it puts back whole level files from the
launcher's first backups and would undo other mods' level changes (e.g. Extended Cut's blowtorch interrogation).


---------------------------------------------------------------------------------------------------------------
10. TIPS AND LIMITS
---------------------------------------------------------------------------------------------------------------
- Largest texture side written to the game: 2048 px (4096 hangs the loading screen). Bigger pictures are scaled.
- Keep lossless PNG masters; DDS (DXT5) and TGA also work.
- Transparent textures (hair 12 and the t10 / t11 / t13 / c10 / c11 details) keep their alpha channel.
- The launcher works together with Extended Cut: install / Apply in any order.
- launcher.log in the launcher folder records everything the window shows.


---------------------------------------------------------------------------------------------------------------
11. PROBLEMS
---------------------------------------------------------------------------------------------------------------
- "Could not write ... close the game and try again": the game is still running.
- A level hangs on loading: switch Smooth blacks off, Apply again.
- A costume does not show: Check (verify) tells you which textures are really in the game; Rescan library after
  adding files.
- Windows, "Costume Launcher.exe" will not start or your antivirus removes it: use "Start Costume Launcher
  (Windows).bat" with Python instead (see 1), or allow the .exe in your antivirus. If the .exe stops with an
  error, it writes launcher_crash.txt in the launcher folder.
- Send launcher.log (and launcher_error.txt / launcher_crash.txt if there is one).

---------------------------------------------------------------------------------------------------------------
12. CREDITS
---------------------------------------------------------------------------------------------------------------
- Inspired by bl4ckh4nd - his HD texture pack for The Punisher (PCSX2) - and SomberTwilight. Their work led to
  the custom textures and to this launcher.
- The in-game skull wear and the exact orange are measured from bl4ckh4nd's "Default HD" skull textures (used
  with his permission). They come with the templates mod on Nexus Mods (Costume Library\Default HD); with them in
  your library the launcher adds that wear to your skulls - without them, skulls are used as they are.
- Licence: GNU GPL v3.0 or later (LICENSE.txt) - this code only, not the game or anyone's textures.

Fan-made, not affiliated with or endorsed by Marvel, Disney, THQ or Volition. All trademarks belong to their owners.
