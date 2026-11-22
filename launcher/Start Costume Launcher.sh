#!/bin/bash
# Starts the Punisher Costume Launcher with the Python that has Pillow (~/punisher-env).
# v0.1.1: installs numpy into that Python the first time; v0.9.18: also Pillow, and makes ~/punisher-env if needed.
cd "$(dirname "$0")"
PY="$HOME/punisher-env/bin/python"
[ -x "$PY" ] || PY=python3
# v0.9.18: first start on a fresh Deck - SteamOS's own Python has no Pillow / numpy and does not let pip change it,
# so a private Python (~/punisher-env) is made once and they are installed into it (needs internet the first time)
if ! "$PY" -c "import PIL, numpy" 2>/dev/null; then
    echo "First start: setting up Python for the launcher (one time, needs internet) ..."
    [ -x "$HOME/punisher-env/bin/python" ] || python3 -m venv "$HOME/punisher-env" > python_setup.txt 2>&1
    PY="$HOME/punisher-env/bin/python"
    "$PY" -m pip install pillow numpy >> python_setup.txt 2>&1
fi
"$PY" launcher.py "$@" 2> launcher_error.txt
[ -s launcher_error.txt ] || rm -f launcher_error.txt
