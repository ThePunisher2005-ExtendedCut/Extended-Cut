"""Install.exe front end: a small menu around ec_install.py (same safety rules - see README.txt).
The game folder = the folder this one sits in (next to pun.exe); if pun.exe is not there, it asks for the folder.
"""
import os
import sys

import ec_install

# 0.3.6 (a player, Nov 24: "PermissionError: [Errno 13] Permission denied: ...\Program Files (x86)\...\ExtendedCut.asi"):
# before changing anything, check that the game is closed and that every file we change can be written; if not,
# say why in plain words and offer to start again as administrator (Program Files needs it).
CHANGED = ['pun.exe', 'dinput8.dll', 'dvd_pc/data/tables.vpp', 'dvd_pc/data/mini.vpp', 'dvd_pc/data/misc.vpp',
           'scripts/ExtendedCut.asi', 'scripts/ExtendedCut.ini', 'scripts/Extended Cut/ExtendedCut.asi',
           'scripts/Extended Cut/ExtendedCut.ini', 'dvd_pc/data/shipping/Chop/Chop1.vpp',
           'dvd_pc/data/shipping/Mansion/Mansion3.vpp', 'dvd_pc/data/shipping/Meat/Meat1.vpp', 'dvd_pc/data/shipping/zoo/zoo3.vpp']


def game_running():
    """True when pun.exe is running (it keeps the plugin and the game files locked)"""
    if os.name != 'nt':
        return False
    try:
        import subprocess
        out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq pun.exe', '/NH'], capture_output=True, text=True,
                             creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0)).stdout
        return 'pun.exe' in out.lower()
    except Exception:
        return False


def is_admin():
    try:
        import ctypes
        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:
        return False


def blocked_paths(g):
    """the folders / files the installer must write but cannot (no rights, read-only or locked)"""
    bad = []
    for d in (g, os.path.join(g, 'scripts'), os.path.join(g, 'dvd_pc', 'data')):
        if not os.path.isdir(d):
            continue
        probe = os.path.join(d, '.ec_write_test.tmp')
        try:
            with open(probe, 'wb') as f:
                f.write(b'x')
            os.remove(probe)
        except OSError:
            bad.append(d)
    for rel in CHANGED:
        f = ec_install.find(g, rel)
        if not f or not os.path.isfile(f):
            continue
        try:
            with open(f, 'r+b'):
                pass
        except OSError:
            bad.append(f)
    return bad


def restart_as_admin(g):
    """start this installer again with administrator rights (Windows asks the player); True if it started"""
    try:
        import ctypes
        if getattr(sys, 'frozen', False):
            exe, params = sys.executable, '"%s"' % g
        else:
            exe, params = sys.executable, '"%s" "%s"' % (os.path.abspath(__file__), g)
        return ctypes.windll.shell32.ShellExecuteW(None, 'runas', exe, params, None, 1) > 32
    except Exception:
        return False


def ready_to_change(g):
    """True when the game folder can be changed now. Otherwise explains what to do (and may restart as admin)."""
    while game_running():
        print('The game is running. Close The Punisher first (it keeps its files locked), then press Enter.')
        if ask('(type q to go back) ').lower() == 'q':
            return False
    bad = blocked_paths(g)
    if not bad:
        return True
    print('Windows does not let this installer change these files / folders:')
    for b in bad[:6]:
        print('   %s' % b)
    if len(bad) > 6:
        print('   ... and %d more' % (len(bad) - 6))
    protected = 'program files' in os.path.abspath(g).lower()
    if not is_admin():
        why = 'The game is inside Program Files, which Windows protects.' if protected else 'This needs administrator rights here.'
        print(why)
        if ask('Start the installer again as administrator? (y / n): ').lower() in ('y', 'yes'):
            if restart_as_admin(g):
                print('The installer opened again as administrator - continue in that window.')
                ask('Press Enter to close this one.')
                sys.exit(0)
            print('Windows did not start it (cancelled?). You can also right-click Install.exe -> Run as administrator.')
    else:
        print('A file may be read-only, open in another program, or blocked by an antivirus.')
    if protected:
        print('Tip: move the whole game folder out of Program Files (for example to C:\\Games\\The Punisher) -')
        print('     then the game can also save the Extended Cut settings and logs where they belong.')
    print('Nothing was changed.')
    return False


def ask(prompt):
    try:
        return input(prompt).strip().strip('"')
    except EOFError:
        return ''


def game_folder():
    if len(sys.argv) > 1 and ec_install.find(sys.argv[1], 'pun.exe'):     # 0.3.6: restarted as administrator
        return sys.argv[1]
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
        if cmd != 'check' and not ready_to_change(g):
            print()
            continue
        try:
            ec_install.main([cmd, g])
        except OSError as e:          # 0.3.6: never a traceback for a blocked / locked file
            print('\nWindows stopped the installer: %s' % e)
            print('Close the game, then start Install.exe again with right-click -> Run as administrator.')
            print('(Files changed before this point keep their backups; running the installer again finishes the job.)')
        print()


if __name__ == '__main__':
    code = main()
    ask('\nPress Enter to close.')
    sys.exit(code or 0)
