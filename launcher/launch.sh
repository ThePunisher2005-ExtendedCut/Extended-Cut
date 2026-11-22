#!/bin/bash
# Punisher Costume Launcher - Steam / Gaming Mode start (v0.4.3)
# Steam launch options (one line):
#   WINEDLLOVERRIDES="dinput8,d3d8,SDL3=n,b" bash "/path/to/this/launch.sh" %command%
#
# Gaming Mode only shows windows of a game that Steam has really started - and Steam counts a game as started when
# its "reaper" runs (it is inside %command%). So this script puts itself INSIDE that chain, right after reaper's
# "--": Steam's spinner ends, the launcher window is shown, and PLAY then starts the rest (Proton + pun.exe).
SELF="$(readlink -f "$0")"; DIR="$(dirname "$SELF")"; DBG="$DIR/launch_debug.txt"

if [ "$1" != "--inner" ]; then
    { echo "=== $(date '+%F %T') launch.sh"; echo "DISPLAY=$DISPLAY SteamGameId=$SteamGameId"; printf 'arg: %s\n' "$@"; } > "$DBG"
    out=(); seen_reaper=0; inserted=0
    for a in "$@"; do
        out+=("$a")
        case "$a" in */reaper) seen_reaper=1 ;; esac
        if [ $seen_reaper = 1 ] && [ $inserted = 0 ] && [ "$a" = "--" ]; then out+=("/bin/bash" "$SELF" "--inner"); inserted=1; fi   # v0.9.20: works without the execute flag
    done
    if [ $inserted = 1 ]; then
        echo "$(date '+%T') re-starting inside Steam's reaper" >> "$DBG"
        exec "${out[@]}"
    fi
    echo "$(date '+%T') no reaper found - starting the launcher directly" >> "$DBG"
else
    shift                                   # inside reaper now; "$@" = the rest of the game command
    echo "$(date '+%T') inside reaper; game command: $*" >> "$DBG"
fi

PY="$HOME/punisher-env/bin/python"
[ -x "$PY" ] || PY=python3
# v0.9.20 (review): the same one-time setup as "Start Costume Launcher.sh" (SteamOS's python3 has no Pillow / numpy)
if ! "$PY" -c "import PIL, numpy" 2>/dev/null; then
    echo "$(date '+%T') setting up ~/punisher-env (Pillow + numpy)" >> "$DBG"
    [ -x "$HOME/punisher-env/bin/python" ] || python3 -m venv "$HOME/punisher-env" > "$DIR/python_setup.txt" 2>&1
    PY="$HOME/punisher-env/bin/python"
    "$PY" -m pip install pillow numpy >> "$DIR/python_setup.txt" 2>&1
fi
echo "$(date '+%T') starting the launcher" >> "$DBG"
GAMEDIR=""                                  # v0.8.7: the folder of the pun.exe Steam starts -> no folder dialog on a first start
for a in "$@"; do case "$a" in *[Pp][Uu][Nn].[Ee][Xx][Ee]) GAMEDIR="$(dirname "$a")" ;; esac; done
env -u LD_PRELOAD "$PY" "$DIR/launcher.py" --steam "--game=$GAMEDIR" 2> "$DIR/launcher_error.txt"
code=$?
echo "$(date '+%T') launcher ended, exit code $code" >> "$DBG"
grep -v "LD_PRELOAD cannot be preloaded" "$DIR/launcher_error.txt" | grep -q . || rm -f "$DIR/launcher_error.txt"
[ "$code" = "2" ] && { echo "quit - game not started" >> "$DBG"; exit 0; }
echo "$(date '+%T') starting the game" >> "$DBG"
exec "$@"
