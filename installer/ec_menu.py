"""Install.exe front end: a small menu around ec_install.py (same safety rules - see README.txt).
The game folder = the folder this one sits in (next to pun.exe); if pun.exe is not there, it asks for the folder.
"""
import os
import sys

import ec_install


def ask(prompt):
    try:
        return input(prompt).strip().strip('"')
    except EOFError:
        return ''


def game_folder():
    here = ec_install.HERE
    for cand in (os.path.dirname(here), here):
        if ec_install.find(cand, 'pun.exe'):
            return cand
    print('pun.exe was not found next to this folder.')
    print('Tip: put the Extended Cut folder ITSELF inside your game folder (the one with pun.exe) - do not unpack its contents there - then start Install.exe again.')
    while True:
        g = ask('Or type / paste the game folder here (empty = quit): ')
        if not g:
            return None
        if ec_install.find(g, 'pun.exe'):
            return g
        print('No pun.exe in "%s".' % g)


def main():
    print('The Punisher: Extended Cut %s - installer' % ec_install.VERSION)
    print('=' * 60)
    g = game_folder()
    if not g:
        return 1
    print('Game folder: %s\n' % g)
    while True:
        print('  1  Install / update Extended Cut')
        print('  2  Check only (shows what would change, changes nothing)')
        print('  3  Uninstall (puts your original files back)')
        print('  4  Quit')
        c = ask('Choose 1-4: ')
        if c in ('4', '', 'q'):
            return 0
        cmd = {'1': 'install', '2': 'check', '3': 'uninstall'}.get(c)
        if not cmd:
            continue
        if cmd == 'uninstall' and ask('Uninstall Extended Cut? (y / n): ').lower() not in ('y', 'yes'):
            continue
        print()
        ec_install.main([cmd, g])
        print()


if __name__ == '__main__':
    code = main()
    ask('\nPress Enter to close.')
    sys.exit(code or 0)
