#!/bin/sh
# The Punisher: Extended Cut - installer for the Steam Deck / Linux (put this folder itself - not its contents - inside your game folder,
# next to pun.exe; in Desktop Mode right-click this file -> Run In Konsole)
cd "$(dirname "$0")"
python3 ec_install.py install "$(pwd)/.."
echo
printf 'Press Enter to close. '; read x
