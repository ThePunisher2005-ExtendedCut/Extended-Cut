#!/usr/bin/env python3
"""
Punisher Costume Launcher v0.2 - a window over costume_manager.py (The Punisher 2005, PC). Personal use.

Tab "Costumes":      base layer, in-game skulls and one costume per mission; Preview / Apply / Check (+ PLAY from Steam).
Tab "Shirt creator": skull image on a shirt template (sliders + Fabric, live preview) -> the costume's skull shirt (04),
                     or "New costume from a kit" (copies an existing costume's textures + the new shirt + skull).
Tab "Settings":      colours / text size / controller / Gaming Mode start / Apply speed.

Desktop:      "Start Costume Launcher.sh"      Steam / Gaming Mode: launch.sh as launch option (see Settings).
Controller:   D-pad / left stick move, A press / open, B back, X reset box, Y preview, LB / RB tabs, LT / RT page,
              Start = Play (from Steam) or Apply, View = help. Same on Steam Deck, Steam Controller and Xbox pads.
Exit codes (for launch.sh): 0 = start the game, 2 = do not start it.
"""
import os, sys, re, json, queue, threading, subprocess, shutil, time, traceback

HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
VERSION = "1.0"   # Nov 21 2026: first public release (was 0.9.28)
SETTINGS = os.path.join(HERE, "launcher_settings.json")
LOG_FILE = os.path.join(HERE, "launcher.log")
DEFAULT_GAME = os.path.expanduser("~/Games/The Punisher (PC)")

try:
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox
except ImportError:
    print("This Python has no tkinter (the window toolkit). Install it (Deck / Linux: your distribution's python3-tk package;"
          " Windows: re-run the Python installer and tick 'tcl/tk and IDLE').")
    sys.exit(1)

try:                                          # v0.8.7: a clear message instead of a crash when Pillow / numpy are missing
    import numpy  # noqa: F401
    from PIL import Image, ImageTk
except ImportError as _ex:
    _msg = ("The Costume Launcher needs two Python packages: Pillow and numpy (missing: %s).\n\n"
            "Install them once, then start the launcher again:\n"
            "    python3 -m pip install --user pillow numpy\n"
            "(Windows: py -m pip install pillow numpy)" % getattr(_ex, "name", _ex))
    print(_msg)
    try:
        _r = tk.Tk(); _r.withdraw(); messagebox.showerror("Costume Launcher", _msg); _r.destroy()
    except Exception:
        pass
    # v0.9.20 (review): from Steam, exit 0 = the game starts anyway (2 = "quit" left the player with nothing)
    sys.exit(0 if "--steam" in sys.argv else 1)
import costume_manager as cm
import shirt_creator as sc

# ---------------------------------------------------------------- log: everything the manager says goes here
LOG_Q = queue.Queue()
def _trim_log(limit=2 << 20, keep=1 << 20):
    """v0.8.7: launcher.log used to grow forever - over 2 MB it keeps its last 1 MB"""
    try:
        if os.path.getsize(LOG_FILE) > limit:
            with open(LOG_FILE, "rb") as f: f.seek(-keep, 2); tail = f.read()
            with open(LOG_FILE, "wb") as f: f.write(b"(older lines removed)\n" + tail[tail.find(b"\n") + 1:])
    except OSError:
        pass
_trim_log()
def log(text=""):
    LOG_Q.put(str(text))
    try:
        with open(LOG_FILE, "a", encoding="utf-8") as f: f.write(str(text) + "\n")
    except Exception:
        pass
cm.say = log                                  # the manager prints through say() - now it lands in the window

def load_settings():
    try: return json.load(open(SETTINGS))
    except Exception: return {}
def save_settings(s):
    cm.save_json(SETTINGS, s, indent=1)           # v0.8.7: safe write (temporary file, then swapped in)

# v0.1.2: dark mode (set "theme": "light" in launcher_settings.json for the old light look)
PAL = {}
# v0.2: presets + your own colours (Settings tab); stored in launcher_settings.json as "theme" and "colours"
THEMES = {
    "Black": dict(bg="#000000", panel="#121214", field="#1c1c1f", fg="#e6e6e6", dim="#8e8e93", sel="#2f5fb3", accent="#4a8bff", warn="#ff8a65"),
    "Dark":  dict(bg="#1e1f22", panel="#2a2c30", field="#33363b", fg="#e4e4e4", dim="#9a9ea6", sel="#3e6db5", accent="#4a7fd1", warn="#ff8a65"),
    "Light": dict(bg="#ececec", panel="#e2e2e2", field="#ffffff", fg="#111111", dim="#666666", sel="#3e6db5", accent="#3e6db5", warn="#b03030"),
}
COLOUR_KEYS = [("bg", "Background"), ("panel", "Panels / log"), ("field", "Fields / buttons"), ("fg", "Text"), ("accent", "Highlight")]

def theme_colours(settings):
    name = {"dark": "Dark", "light": "Light"}.get(settings.get("theme"), settings.get("theme", "Black"))
    c = dict(THEMES.get(name, THEMES["Black"]))
    for k, v in (settings.get("colours") or {}).items():
        if k in c and isinstance(v, str) and v.startswith("#") and len(v) in (4, 7): c[k] = v
    return c

def apply_theme(root, settings):
    style = ttk.Style(root)
    try: style.theme_use("clam")
    except Exception: pass
    c = theme_colours(settings)
    bg, panel, field, fg, dim, sel, accent, warn = (c[k] for k in ("bg", "panel", "field", "fg", "dim", "sel", "accent", "warn"))
    PAL.update(c)
    root.configure(background=bg)
    style.configure(".", background=bg, foreground=fg, fieldbackground=field, bordercolor=panel,
                    lightcolor=panel, darkcolor=bg, troughcolor=panel, focuscolor=accent, selectbackground=sel,
                    selectforeground="#ffffff", insertcolor=fg, arrowcolor=fg)
    style.configure("TFrame", background=bg)
    style.configure("TLabel", background=bg, foreground=fg)
    style.configure("Dim.TLabel", background=bg, foreground=dim)
    style.configure("Warn.TLabel", background=bg, foreground=warn)
    style.configure("TLabelframe", background=bg, bordercolor=panel)
    style.configure("TLabelframe.Label", background=bg, foreground=accent)
    style.configure("TButton", background=panel, foreground=fg, bordercolor=panel, padding=(10, 5))
    style.map("TButton", background=[("disabled", bg), ("pressed", sel), ("active", field)],
              foreground=[("disabled", dim)])
    style.configure("Accent.TButton", background=accent, foreground="#ffffff")
    style.map("Accent.TButton", background=[("disabled", panel), ("pressed", sel), ("active", sel)])
    style.configure("TCheckbutton", background=bg, foreground=fg, indicatorbackground=field, indicatorforeground=fg)
    style.map("TCheckbutton", background=[("active", bg)], indicatorbackground=[("selected", accent)])
    style.configure("TCombobox", fieldbackground=field, background=panel, foreground=fg, arrowcolor=fg)
    style.map("TCombobox", fieldbackground=[("readonly", field)], foreground=[("readonly", fg)],
              selectbackground=[("readonly", field)], selectforeground=[("readonly", fg)])
    style.configure("TEntry", fieldbackground=field, foreground=fg)
    style.configure("TNotebook", background=bg, bordercolor=panel)
    style.configure("TNotebook.Tab", background=panel, foreground=dim, padding=(14, 6))
    style.map("TNotebook.Tab", background=[("selected", field)], foreground=[("selected", fg)])
    style.configure("Horizontal.TScale", background=accent, troughcolor=panel)
    style.configure("Vertical.TScrollbar", background=panel, troughcolor=bg, arrowcolor=fg)
    style.configure("TSeparator", background=panel)
    style.configure("TPanedwindow", background=panel)
    style.configure("Sash", sashthickness=10, gripcount=8, background=panel, lightcolor=dim, darkcolor=bg)
    style.configure("Pick.TMenubutton", background=field, foreground=fg, arrowcolor=fg, padding=(8, 3), anchor="w")
    style.map("Pick.TMenubutton", background=[("active", sel)])
    style.configure("Focus.TLabel", background=bg, foreground=accent)
    style.configure("Treeview", background=panel, fieldbackground=panel, foreground=fg, rowheight=26, borderwidth=0)
    style.map("Treeview", background=[("selected", sel)], foreground=[("selected", "#ffffff")])
    style.configure("Treeview.Heading", background=field, foreground=fg)
    # v0.2: what has the focus (controller) gets a clear highlight border
    for st in ("TButton", "Pick.TMenubutton", "TCheckbutton", "TEntry", "TRadiobutton"):
        style.map(st, bordercolor=[("focus", accent)], lightcolor=[("focus", accent)], darkcolor=[("focus", accent)])
    style.map("Pick.TMenubutton", background=[("focus", sel), ("active", sel)])
    style.configure("TRadiobutton", background=bg, foreground=fg, indicatorbackground=field)
    style.map("TRadiobutton", background=[("active", bg)], indicatorbackground=[("selected", accent)])
    style.configure("Big.Accent.TButton", background=accent, foreground="#ffffff", padding=(14, 10), font=("TkDefaultFont", 13, "bold"))
    style.map("Big.Accent.TButton", background=[("disabled", panel), ("pressed", sel), ("active", sel), ("focus", sel)])
    style.configure("Horizontal.TScale", sliderlength=30, sliderthickness=26, borderwidth=1)
    # the drop-down list of a combobox is a plain Tk listbox
    root.option_add("*TCombobox*Listbox.background", field)
    root.option_add("*TCombobox*Listbox.foreground", fg)
    root.option_add("*TCombobox*Listbox.selectBackground", sel)
    root.option_add("*TCombobox*Listbox.selectForeground", "#ffffff")

def game_running():
    """True if pun.exe is running (Proton on the Deck or Windows)"""
    try:
        if os.name == "nt":
            out = subprocess.run(["tasklist"], capture_output=True, text=True).stdout.lower()
            return "pun.exe" in out
        # v0.4: the process NAME must be pun.exe (-x). "-f" also matched launch.sh, whose command line holds pun.exe
        return subprocess.run(["pgrep", "-x", "pun.exe"], capture_output=True).returncode == 0
    except Exception:
        return False


VARIANT_TAG = "  (costume - new variant inside)"
BAD_CHARS = '\\:*?"<>|'
def tree_parts(n):
    if cm.is_kit(n): return ["Kits"] + n[len(cm.KIT_PREFIX):].split("/")
    return ["My mixes", n[len(cm.MIX_PREFIX):]] if cm.is_mix(n) else n.split(" / ")

def rebase(old, new_base):
    """keep '[skull]' and '+ outfits' of a choice when only its costume changes"""
    if new_base == cm.DEFAULT or cm.is_mix(new_base) or cm.is_mix(old) or cm.is_kit(new_base) or cm.is_kit(old): return new_base
    b, v = cm.split_name(old)
    return cm.join_name(new_base, v, cm.is_skull_only(old))

IMG_EXT = (".png", ".tga", ".dds", ".jpg", ".jpeg", ".webp", ".bmp")


# ---------------------------------------------------------------- v0.4.2: "pop-ups" as panels inside the main window
class Overlay(tk.Frame):
    """Looks and works like a dialog window, but is a panel on top of the launcher's own window.
    Gaming Mode shows one window per game: real pop-up windows went missing there ("window not viewable")."""
    _seqs = set()
    def __init__(self, root):
        super().__init__(root, background=PAL["bg"], highlightthickness=2, highlightbackground=PAL["accent"], highlightcolor=PAL["accent"])
        self._host, self._geo, self._titled, self._keys = root, None, False, {}
    def bind(self, seq=None, func=None, add=None):
        """v0.8.1: key bindings of a panel apply to every widget inside it (as they did when panels were windows).
        Mouse / window events still bind to the panel itself."""
        if seq and func and seq.startswith("<") and not any(k in seq for k in ("Button", "Configure", "Destroy", "Map", "Motion", "Enter>", "Leave", "Focus", "Expose", "Visibility")):
            self._keys[seq] = func
            if seq not in Overlay._seqs:
                Overlay._seqs.add(seq)
                def dispatch(e, seq=seq):
                    g = e.widget.winfo_toplevel().grab_current() if hasattr(e.widget, "winfo_toplevel") else None
                    if isinstance(g, Overlay) and seq in g._keys and str(e.widget).startswith(str(g)):
                        return g._keys[seq](e)
                self._host.bind_all(seq, dispatch, add="+")
            return None
        return super().bind(seq, func, add)
    def title(self, text):
        if not self._titled:
            self._titled = True
            ttk.Label(self, text=text, style="Focus.TLabel", font=("TkDefaultFont", 12, "bold"), padding=(14, 10, 14, 0)).pack(anchor="w")
    def withdraw(self): pass
    def transient(self, *a): pass
    def protocol(self, *a): pass
    def geometry(self, spec):
        m = re.match(r"(?:(\d+)x(\d+))?\+(-?\d+)\+(-?\d+)", spec)
        if m: self._geo = tuple(int(v) if v else None for v in m.groups())
    def deiconify(self):
        r = self._host; r.update_idletasks()
        if self._geo and self._geo[0]:
            w, h, x, y = self._geo
            self.place(x=max(0, x - r.winfo_rootx()), y=max(0, y - r.winfo_rooty()), width=min(w, r.winfo_width()), height=min(h, r.winfo_height()))
        else:
            self.place(relx=0.5, rely=0.42, anchor="center")
        self.lift()
    def grab_set(self):
        try: super().grab_set()
        except tk.TclError: pass

# ---------------------------------------------------------------- v0.7: the launcher's own on-screen keyboard
APP = [None]              # the running Launcher (for module-level helpers)
PAD_A_LAST = [0.0]        # time of the last A from the controller (Steam's Desktop layout may also send Enter)

class Keyboard(Overlay):
    """Typing with the controller, a trackpad, touch or a mouse - the same in Gaming Mode, Big Picture and Desktop.
    A = press the key, B = delete, Y = space, X = shift, Start = done. A real keyboard keeps working too."""
    ROWS = ["1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm-_.", ]
    def __init__(self, root, entry):
        super().__init__(root); self.entry = entry; self.shift = False
        self.prev_grab = root.grab_current()
        self.title("Type a name")
        self.var = tk.StringVar(value=entry.get())
        ttk.Label(self, textvariable=self.var, font=("TkDefaultFont", 15, "bold"), padding=(16, 6), width=34, anchor="w",
                  style="Focus.TLabel").pack(fill="x")
        g = ttk.Frame(self, padding=(12, 4, 12, 4)); g.pack()
        self.keys = []
        for r, row in enumerate(self.ROWS):
            for c, ch in enumerate(row):
                b = ttk.Button(g, text=ch, width=3, command=lambda ch=ch: self.type(ch)); b.grid(row=r, column=c, padx=2, pady=2)
                self.keys.append((b, ch))
        r = len(self.ROWS)
        self.shift_b = ttk.Button(g, text="Shift  (X)", command=self.toggle_shift); self.shift_b.grid(row=r, column=0, columnspan=2, sticky="ew", padx=2, pady=(6, 2))
        ttk.Button(g, text="Space  (Y)", command=lambda: self.type(" ")).grid(row=r, column=2, columnspan=4, sticky="ew", padx=2, pady=(6, 2))
        ttk.Button(g, text="Delete  (B)", command=self.delete).grid(row=r, column=6, columnspan=2, sticky="ew", padx=2, pady=(6, 2))
        ttk.Button(g, text="Done  (Start)", style="Accent.TButton", command=self.done).grid(row=r, column=8, columnspan=2, sticky="ew", padx=2, pady=(6, 2))
        ttk.Label(self, text="D-pad / stick: move   A: type   hold L2: capitals   B: delete   Y: space   X: shift   Start: done   (or click / tap)",
                  style="Dim.TLabel", padding=(14, 0, 14, 10)).pack(anchor="w")
        self.auto_caps()
        self.place(relx=0.5, rely=0.62, anchor="center"); self.lift()
        self.grab_set(); self.keys[10][0].focus_set()
    def auto_caps(self):                         # a capital at the start and after a space
        t = self.var.get(); want = (not t) or t.endswith(" ")
        if want != self.shift: self.toggle_shift()
    def hold_caps(self, on):
        """L2 held = capitals while held (like Steam's keyboard); released = back to the automatic capitals"""
        self.held = on
        if on != self.shift: self.toggle_shift()
        if not on: self.auto_caps()
    def toggle_shift(self):
        self.shift = not self.shift
        for b, ch in self.keys: b.configure(text=ch.upper() if self.shift else ch)
    def type(self, ch):
        self.var.set(self.var.get() + (ch.upper() if self.shift else ch))
        if not getattr(self, "held", False): self.auto_caps()
    def delete(self):
        self.var.set(self.var.get()[:-1]); self.auto_caps()
    def done(self):
        e = self.entry
        try: e.delete(0, "end"); e.insert(0, self.var.get())
        except tk.TclError: pass
        self.close()
        try: e.focus_set(); e.icursor("end")
        except tk.TclError: pass
    def close(self):
        pg = self.prev_grab; self.destroy()
        if pg is not None:
            try: pg.grab_set()
            except tk.TclError: pass

def open_osk(entry):
    """the launcher's keyboard for a text box"""
    root = entry.winfo_toplevel()
    if isinstance(root.grab_current(), Keyboard): return
    Keyboard(root, entry)

FACE_TEXTURES = {2, 10, 11, 12, 13}     # v0.9.12: teeth, hair cap, eyelashes, hair, hairline (user's marker test, Nov 18)

OUTFIT_GROUPS = (("c", "Trench coat"), ("b", "Both outfits"), ("t", "T-shirt only"))   # v0.9.16: order of the part groups

def part_outfit(info_):
    """'c' = trench coat only, 't' = T-shirt only, 'b' = both outfits"""
    ms = set(m for m, _ in (info_.get("slots") or []))
    if ms == {"trench"}: return "c"
    if ms == {"tshirt"}: return "t"
    return "b"

def is_face_part(info_):
    """a part whose textures are all face / hair ones"""
    ns = [n for _, n in (info_.get("slots") or [])]
    return bool(ns) and all(n in FACE_TEXTURES for n in ns)

def kb_button(parent, entry):
    """the small keyboard button next to a text box (trackpad / touch / mouse)"""
    return ttk.Button(parent, text="Keyboard", command=lambda: open_osk(entry))

# ---------------------------------------------------------------- v0.2: dark message boxes (the system ones are bright)
def _dialog(root, title, text, buttons):
    d = Overlay(root); d.withdraw(); d.title(title); d.configure(background=PAL["bg"]); d.result = None
    ttk.Label(d, text=text, wraplength=560, justify="left", padding=18).pack(fill="x")
    bf = ttk.Frame(d, padding=(18, 0, 18, 16)); bf.pack(fill="x")
    first = None
    for i, (label, value) in enumerate(buttons):
        b = ttk.Button(bf, text=label, style="Accent.TButton" if i == 0 else "TButton",
                       command=lambda v=value: (setattr(d, "result", v), d.destroy()))
        b.pack(side="right", padx=4); first = first or b
    d.bind("<Escape>", lambda e: d.destroy())
    d.deiconify(); d.grab_set(); first.focus_set()
    root.wait_window(d); return d.result

def open_keyboard():
    """the Steam on-screen keyboard (Gaming Mode / Steam running); harmless elsewhere"""
    if os.name == "nt": return
    import shutil
    # v0.9.23: Steam's own command first (works in Gaming Mode), xdg-open as the fallback (Desktop Mode)
    for cmd in ([shutil.which("steam"), "steam://open/keyboard"], ["xdg-open", "steam://open/keyboard"]):
        if not cmd[0]: continue
        try:
            subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
            log("Steam keyboard: asked Steam to open it (%s)" % os.path.basename(cmd[0])); return
        except Exception as ex: log("Steam keyboard: %s failed (%s)" % (cmd[0], ex))

def entry_keys(entry, ok):
    """Enter (also the keypad / Steam keyboard Enter) = ok; Escape only leaves the text box, it never closes the window"""
    def enter(e):
        if APP[0] is not None and getattr(APP[0], "steam_kb", False):
            APP[0].steam_kb = False; log("Steam keyboard closed - the launcher uses the controller")
        elif time.time() - PAD_A_LAST[0] < 0.3: return "break"      # Steam's copy of the controller's A
        ok(); return "break"
    entry.bind("<Return>", enter); entry.bind("<KP_Enter>", enter)
    entry.bind("<Escape>", lambda e: (entry.tk_focusNext().focus_set(), "break")[1])


def ask(root, title, text, yes="Yes", no="No"):
    return _dialog(root, title, text, [(yes, True), (no, False)]) is True

def info(root, title, text):
    _dialog(root, title, text, [("OK", True)])

def ask_text(root, title, prompt, initial=""):
    """v0.2.1: dark one-line input; returns the text or None"""
    d = Overlay(root); d.withdraw(); d.title(title); d.configure(background=PAL["bg"]); d.result = None
    fr = ttk.Frame(d, padding=18); fr.pack(fill="both")
    ttk.Label(fr, text=prompt, wraplength=560, justify="left").pack(anchor="w")
    v = tk.StringVar(value=initial); er = ttk.Frame(fr); er.pack(anchor="w", fill="x", pady=8)
    e = ttk.Entry(er, textvariable=v, width=42); e.pack(side="left"); kb_button(er, e).pack(side="left", padx=6)
    bf = ttk.Frame(fr); bf.pack(fill="x")
    def ok(*a): d.result = v.get().strip(); d.destroy()
    ttk.Button(bf, text="Cancel", command=d.destroy).pack(side="right")
    ttk.Button(bf, text="OK", style="Accent.TButton", command=ok).pack(side="right", padx=6)
    entry_keys(e, ok); d.bind("<Escape>", lambda ev: d.destroy())
    d.transient(root); d.deiconify(); d.grab_set(); e.focus_set(); e.select_range(0, "end")
    root.wait_window(d); return d.result or None

def choose_from(root, title, prompt, options):
    """v0.2.1: dark list chooser; returns the chosen option or None"""
    d = Overlay(root); d.withdraw(); d.title(title); d.configure(background=PAL["bg"]); d.result = None
    fr = ttk.Frame(d, padding=18); fr.pack(fill="both", expand=True)
    ttk.Label(fr, text=prompt).pack(anchor="w")
    lb = tk.Listbox(fr, background=PAL["panel"], foreground=PAL["fg"], selectbackground=PAL["sel"], selectforeground="#ffffff",
                    font=("TkDefaultFont", 13), height=min(14, max(4, len(options))), width=44, activestyle="none",
                    highlightthickness=0, relief="flat", exportselection=False)
    for o in options: lb.insert("end", "  " + o)
    guard(lb)
    lb.pack(fill="both", expand=True, pady=8)
    if options: lb.selection_set(0)
    def ok(*a):
        i = lb.curselection()
        if i: d.result = options[i[0]]; d.destroy()
    bf = ttk.Frame(fr); bf.pack(fill="x")
    ttk.Button(bf, text="Cancel", command=d.destroy).pack(side="right")
    ttk.Button(bf, text="OK", style="Accent.TButton", command=ok).pack(side="right", padx=6)
    lb.bind("<Double-Button-1>", ok); lb.bind("<Return>", ok); d.bind("<Escape>", lambda ev: d.destroy())
    d.transient(root); d.deiconify(); d.grab_set(); lb.focus_set()
    root.wait_window(d); return d.result

# ---------------------------------------------------------------- v0.3.2: one move per D-pad press
PAD_LAST = [0.0]          # time of the last direction from the controller
STEAM_MODE = [False]      # started from Steam (launch.sh --steam)
BIG_PICKER = [False]      # v0.4: costume boxes open the big list only (no drop-down menu)
def pad_echo():
    """True for an arrow key that is only Steam's Desktop-Mode copy of a D-pad press we already handled"""
    return time.time() - PAD_LAST[0] < 0.25
def guard(w):
    """put the PadGuard tag in front of a widget's own bindings (lists, trees)"""
    tags = list(w.bindtags())
    if "PadGuard" not in tags: w.bindtags(tuple([tags[0], "PadGuard"] + tags[1:]))
    return w
def install_guard(root):
    for k in ("<Up>", "<Down>", "<Left>", "<Right>"):
        root.bind_class("PadGuard", k, lambda e: "break" if pad_echo() else None)

# ---------------------------------------------------------------- v0.2: controller (SDL2 through ctypes - nothing to install)
class Pad:
    """Reads the controller the way the game does (SDL2) and turns it into navigation:
    D-pad / left stick = move between fields, A = press / open, B = back / cancel, LB / RB = tabs, Start = Play (or Apply)."""
    A, B, X, Y, BACK, GUIDE, START, LS, RS, LB, RB, UP, DOWN, LEFT, RIGHT, LT, RT = range(17)
    def __init__(self, app):
        import ctypes, ctypes.util
        self.app = app; self.ok = False; self.ctrl = None; self.prev = [0] * 17; self.held = {}; self.lib = None
        self.mute_until = 0
        for nm in ("libSDL2-2.0.so.0", ctypes.util.find_library("SDL2") or "", "SDL2.dll"):
            if not nm: continue
            try: self.lib = ctypes.CDLL(nm); break
            except OSError: continue
        if not self.lib: log("controller: SDL2 not found - use touch / trackpad / mouse"); return
        L = self.lib; c = ctypes
        L.SDL_SetHint.argtypes = [c.c_char_p, c.c_char_p]
        L.SDL_GameControllerOpen.restype = c.c_void_p
        L.SDL_GameControllerGetButton.argtypes = [c.c_void_p, c.c_int]; L.SDL_GameControllerGetButton.restype = c.c_uint8
        L.SDL_GameControllerGetAxis.argtypes = [c.c_void_p, c.c_int]; L.SDL_GameControllerGetAxis.restype = c.c_int16
        L.SDL_GameControllerGetAttached.argtypes = [c.c_void_p]
        L.SDL_GameControllerName.argtypes = [c.c_void_p]; L.SDL_GameControllerName.restype = c.c_char_p
        L.SDL_SetHint(b"SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", b"1")
        if L.SDL_Init(0x200 | 0x2000) != 0: log("controller: SDL could not start"); return
        self.ok = True; self.next_scan = 0; self.errors = 0
        app.root.after(60, self.poll)

    def open(self):
        L = self.lib
        if self.ctrl and L.SDL_GameControllerGetAttached(self.ctrl): return True
        if time.time() < self.next_scan: return False
        self.next_scan = time.time() + 2.0; L.SDL_PumpEvents()
        for i in range(L.SDL_NumJoysticks()):
            if L.SDL_IsGameController(i):
                self.ctrl = L.SDL_GameControllerOpen(i)
                if self.ctrl:
                    log("controller: %s - D-pad moves, A selects, B goes back, LB/RB switch tabs" % (L.SDL_GameControllerName(self.ctrl) or b"?").decode("utf-8", "replace"))
                    return True
        self.ctrl = None; return False

    def safe_button(self, b):
        try: self.app.pad_button(b)
        except Exception as ex:                                        # never stop reading the pad over one odd widget
            if self.errors < 5: log("controller: %s" % ex); self.errors += 1

    def poll(self):
        try:
            if self.open():
                L = self.lib; L.SDL_GameControllerUpdate()
                cur = [L.SDL_GameControllerGetButton(self.ctrl, i) for i in range(15)] + [int(L.SDL_GameControllerGetAxis(self.ctrl, 4) > 16000),
                       int(L.SDL_GameControllerGetAxis(self.ctrl, 5) > 16000)]
                lx, ly = L.SDL_GameControllerGetAxis(self.ctrl, 0), L.SDL_GameControllerGetAxis(self.ctrl, 1)
                cur[self.UP] |= ly < -20000; cur[self.DOWN] |= ly > 20000; cur[self.LEFT] |= lx < -20000; cur[self.RIGHT] |= lx > 20000
                now = time.time()
                # v0.7.1: STEAM KEYBOARD MODE. Steam + X opens Steam's own keyboard; while it is open every button is
                # Steam's (A types, L2 held = capitals, Start moves it ...), so the launcher ignores the controller until
                # the keyboard is closed again: B, Steam + X, or Enter typed into the text box.
                if cur[self.GUIDE] and cur[self.X] and not self.prev[self.X]:
                    self.app.steam_kb = not getattr(self.app, "steam_kb", False)
                    log("Steam keyboard %s - the launcher %s the controller" % (("open", "ignores") if self.app.steam_kb else ("closed", "uses")))
                    # v0.9.23: Steam does not open its keyboard for the launcher's window by itself - ask Steam for it
                    if self.app.steam_kb: open_keyboard()
                if getattr(self.app, "steam_kb", False) and cur[self.B] and not self.prev[self.B] and not cur[self.GUIDE]:
                    self.app.steam_kb = False; self.mute_until = now + 0.3; log("Steam keyboard closed - the launcher uses the controller")
                # the Steam button (and its combos: Steam+Start moves the keyboard ...) belongs to Steam
                if cur[self.GUIDE]: self.mute_until = now + 0.6
                muted = now < getattr(self, "mute_until", 0) or getattr(self.app, "steam_kb", False)
                # v0.7.1: in the launcher's keyboard, L2 held = capitals (like Steam's keyboard)
                if cur[self.LT] != self.prev[self.LT]:
                    g_ = self.app.root.grab_current()
                    if isinstance(g_, Keyboard): g_.hold_caps(bool(cur[self.LT]))
                # v0.4: while you type in a text box the buttons belong to the Steam keyboard - only B (leave the box) counts
                try: typing = self.app.root.focus_get() is not None and self.app.root.focus_get().winfo_class() == "TEntry"
                except Exception: typing = False
                if not muted and not typing: self.app.cursor_step(L, self.ctrl, cur, self.prev)
                for b in range(17):
                    if muted or (typing and b in (self.START, self.X, self.Y, self.LB, self.RB, self.LT, self.RT, self.BACK)):
                        continue                                              # in a text box: move, A, B only
                    fire = False
                    if cur[b] and not self.prev[b]:
                        self.held[b] = now + 0.38; fire = True
                    elif cur[b] and b in (self.UP, self.DOWN, self.LEFT, self.RIGHT, self.LT, self.RT) and now >= self.held.get(b, 1e18):
                        self.held[b] = now + 0.09; fire = True        # a held direction repeats
                    if fire:
                        # v0.8.1: run the action after this poll returns - a panel it opens (costume list, question ...)
                        # waits until closed, and while it waited the controller was not read any more
                        self.app.root.after(0, lambda b=b: self.safe_button(b))
                self.prev = cur
        except Exception as ex:
            if self.errors < 5: log("controller: %s" % ex); self.errors += 1
        self.app.root.after(40, self.poll)

def nav_widgets(top):
    """focusable widgets inside a window (what the D-pad can land on)"""
    out = []
    def walk(w):
        for c in w.winfo_children():
            if isinstance(c, (tk.Toplevel,)) and c is not top: continue
            cls = c.winfo_class()
            if cls in ("TButton", "TMenubutton", "TCheckbutton", "TRadiobutton", "TScale", "TEntry", "TCombobox", "Listbox", "TNotebook", "Treeview"):
                try:
                    if c.winfo_viewable() and str(c.cget("takefocus")) != "0" and (cls in ("Listbox", "TNotebook", "Treeview") or "disabled" not in c.state()):
                        out.append(c)
                except Exception:
                    pass
            walk(c)
    walk(top); return out

def move_focus(top, direction):
    """spatial move: the nearest widget in that direction (like a console menu)"""
    cur = top.focus_get()
    ws = [w for w in nav_widgets(top) if w is not cur and w.winfo_class() != "TNotebook"]
    if not ws: return None
    if cur is None or not cur.winfo_viewable():
        ws[0].focus_set(); return ws[0]
    cx, cy = cur.winfo_rootx() + cur.winfo_width() / 2, cur.winfo_rooty() + cur.winfo_height() / 2
    if direction in ("up", "down"):
        # v0.8.7: inside one box (e.g. the Kit builder) up / down goes to the NEXT ROW, even when that row's fields sit
        # further right (shirt background -> parts, not past them to "Adjust the skull fit")
        def section(w):
            p = w
            while p is not None and p is not top:
                if p.winfo_class() == "TLabelframe": return p
                p = p.master
            return None
        sec = section(cur)
        if sec is not None:
            rows = []
            for w in ws:
                if section(w) is not sec: continue
                y = w.winfo_rooty() + w.winfo_height() / 2; dy = y - cy
                if (direction == "down" and dy > 4) or (direction == "up" and dy < -4):
                    rows.append((abs(dy), abs(w.winfo_rootx() + w.winfo_width() / 2 - cx), w))
            if rows:
                near = min(r[0] for r in rows)
                best = min((r for r in rows if r[0] <= near + 12), key=lambda r: r[1])[2]
                best.focus_set(); return best
    if direction in ("left", "right"):
        # v0.8.7: left / right stays on the SAME ROW when there is something there (Save kit -> Use for the whole game,
        # not up to "Adjust the skull fit" just because that button is a little to the right)
        same = []
        for w in ws:
            y = w.winfo_rooty() + w.winfo_height() / 2; x = w.winfo_rootx() + w.winfo_width() / 2
            dx = x - cx
            if abs(y - cy) <= max(cur.winfo_height(), w.winfo_height()) / 2 + 4 and \
                    ((direction == "right" and dx > 4) or (direction == "left" and dx < -4)):
                same.append((abs(dx), w))
        if same:
            best = min(same, key=lambda r: r[0])[1]; best.focus_set(); return best
    best, bd = None, 1e18
    for w in ws:
        x, y = w.winfo_rootx() + w.winfo_width() / 2, w.winfo_rooty() + w.winfo_height() / 2
        dx, dy = x - cx, y - cy
        if direction == "up" and dy >= -4: continue
        if direction == "down" and dy <= 4: continue
        if direction == "left" and dx >= -4: continue
        if direction == "right" and dx <= 4: continue
        d = (dx * dx * 4 + dy * dy) if direction in ("up", "down") else (dx * dx + dy * dy * 6)
        if d < bd: best, bd = w, d
    if best is not None: best.focus_set()
    return best

# ---------------------------------------------------------------- v0.2: big-list costume picker (controller / touch friendly)
class Picker(Overlay):
    """folders first, then costumes; A / double-tap = open or choose, B = back up a folder"""
    def __init__(self, app, names, current, with_default=True):
        root = app.root; super().__init__(root); self.withdraw(); self.app = app; self.result = None
        self.title("Choose a costume"); self.configure(background=PAL["bg"])
        sw, sh = root.winfo_screenwidth(), root.winfo_screenheight()
        self.geometry("%dx%d+%d+%d" % (int(sw * 0.8), int(sh * 0.82), int(sw * 0.1), int(sh * 0.06)))
        tree = {}
        for n in names:
            if n == cm.DEFAULT: continue
            node = tree
            for part in tree_parts(n): node = node.setdefault(part, {})
            node["__self__"] = n
        self.tree, self.path, self.with_default = tree, [], with_default
        top = ttk.Frame(self, padding=10); top.pack(fill="x")
        ttk.Button(top, text="<  Back", command=self.back).pack(side="left")
        self.where = tk.StringVar(); ttk.Label(top, textvariable=self.where, padding=(14, 0)).pack(side="left")
        ttk.Button(top, text="Cancel", command=self.destroy).pack(side="right")
        body = ttk.Frame(self, padding=(10, 0, 10, 10)); body.pack(fill="both", expand=True)
        pv = tk.Frame(body, background=PAL["bg"], width=int(sw * 0.3)); pv.pack(side="right", fill="y"); pv.pack_propagate(False)
        self.pv = tk.Label(pv, background=PAL["bg"]); self.pv.pack(expand=True)
        self.lb = tk.Listbox(body, background=PAL["panel"], foreground=PAL["fg"], selectbackground=PAL["sel"], selectforeground="#ffffff",
                             font=("TkDefaultFont", 15), activestyle="none", highlightthickness=0, relief="flat", exportselection=False)
        self.lb.pack(side="left", fill="both", expand=True); guard(self.lb)
        self.lb.bind("<Double-Button-1>", lambda e: self.choose()); self.lb.bind("<Return>", lambda e: self.choose())
        self.lb.bind("<BackSpace>", lambda e: self.back()); self.lb.bind("<<ListboxSelect>>", lambda e: self.show())
        self.bind("<Escape>", lambda e: self.destroy())
        # open the folder of the current costume
        if current and current != cm.DEFAULT and len(tree_parts(current)) > 1:
            node = tree
            for part in tree_parts(current)[:-1]:
                if part in node: self.path.append(part); node = node[part]
                else: break
        self.fill(select=current)
        self.transient(root); self.deiconify(); self.grab_set(); self.lb.focus_set()

    def node(self):
        n = self.tree
        for p in self.path: n = n[p]
        return n
    def fill(self, select=None):
        n = self.node(); self.items = []
        if not self.path and self.with_default: self.items.append(("pick", cm.DEFAULT, "Default (the original game)"))
        if self.path and "__self__" in n: self.items.append(("pick", n["__self__"], "%s  (main)" % self.path[-1]))
        for k in n:
            if k == "__self__": continue
            kids = [x for x in n[k] if x != "__self__"]
            self.items.append(("open", k, "%s   >" % k) if kids else ("pick", n[k]["__self__"], k))
        self.lb.delete(0, "end")
        for _, _, label in self.items: self.lb.insert("end", "  " + label)
        i = next((j for j, it in enumerate(self.items) if it[0] == "pick" and it[1] == select), 0)
        if self.items: self.lb.selection_set(i); self.lb.activate(i); self.lb.see(i)
        self.where.set(" / ".join(self.path) if self.path else "All costumes"); self.show()
    def sel(self):
        i = self.lb.curselection(); return self.items[i[0]] if i else None
    def choose(self):
        it = self.sel()
        if not it: return
        if it[0] == "open": self.path.append(it[1]); self.fill()
        else: self.result = it[1]; self.destroy()
    def back(self):
        if self.path:
            last = self.path.pop(); self.fill()
            i = next((j for j, it in enumerate(self.items) if it[1] == last), 0); self.lb.selection_clear(0, "end"); self.lb.selection_set(i); self.lb.see(i); self.show()
        else:
            self.destroy()
    def show(self):
        it = self.sel(); self.pv.configure(image="")
        if not it or it[0] != "pick" or it[1] == cm.DEFAULT: return
        files = find_skull_images(self.app.g, it[1], getattr(self.app, "_cdirs", {}) or cm.costume_dirs(self.app.g.lib))
        if files:
            try: self._img = thumb(files[0], (int(self.winfo_screenwidth() * 0.26),) * 2, "bg"); self.pv.configure(image=self._img)
            except Exception: pass

# ---------------------------------------------------------------- v0.1.3: costume picker grouped by folder
class CostumeMenu(ttk.Menubutton):
    """a button that opens the costumes as folder menus: 616 > Steve Dillon > Thunderbolts / Vol.4"""
    def __init__(self, parent, var, names, command=None, with_default=True, width=44):
        super().__init__(parent, textvariable=var, width=width, style="Pick.TMenubutton", takefocus=1)
        self.var, self.command, self.with_default = var, command, with_default
        self.bind("<Left>", lambda e: "break" if pad_echo() else (self.cycle(-1), "break")[1])
        self.bind("<Right>", lambda e: "break" if pad_echo() else (self.cycle(1), "break")[1])
        self.set_names(names)
    def _menu(self, parent):
        return tk.Menu(parent, tearoff=0, background=PAL["panel"], foreground=PAL["fg"], activebackground=PAL["sel"],
                       activeforeground="#ffffff", font=("TkDefaultFont", 12), borderwidth=1, relief="flat")
    def set_names(self, names):
        self._names = list(names)
        if BIG_PICKER[0]:
            self["menu"] = ""; return                   # v0.4: big list only - no second (drop-down) menu
        m = self._menu(self); self["menu"] = m
        m.configure(postcommand=lambda m=m: self._build(m))   # v0.4: filled when first opened (faster start)
    def _build(self, m):
        if getattr(m, "_built", False): return
        m._built = True; names = self._names
        if self.with_default:
            m.add_command(label="Default (the original game)", command=lambda: self.pick(cm.DEFAULT)); m.add_separator()
        tree = {}
        for n in names:
            if n == cm.DEFAULT: continue
            node = tree
            for part in tree_parts(n):
                node = node.setdefault(part, {})
            node["__self__"] = n
        self._fill(m, tree)
    def _fill(self, menu, tree):
        for key in tree:
            if key == "__self__": continue
            node = tree[key]; kids = [k for k in node if k != "__self__"]
            if not kids:
                menu.add_command(label=key, command=lambda n=node["__self__"]: self.pick(n))
            else:
                sub = self._menu(menu)
                if "__self__" in node:
                    sub.add_command(label="%s  (main)" % key, command=lambda n=node["__self__"]: self.pick(n)); sub.add_separator()
                self._fill(sub, node)
                menu.add_cascade(label=key + "   >", menu=sub)
    def pick(self, name):
        self.var.set(name)
        if self.command: self.command(name)
    def cycle(self, step):
        """v0.3: left / right = previous / next costume (the outfit part '+ ...' stays)"""
        names = [n for n in getattr(self, "_names", []) if n != cm.DEFAULT]
        if self.with_default: names = [cm.DEFAULT] + names
        if not names: return
        base, variants = cm.split_name(self.var.get())
        i = names.index(base) if base in names else -1
        self.pick(rebase(self.var.get(), names[(i + step) % len(names)]))
    def open_picker(self, app):
        """v0.2: the big-list picker (used from the controller, and for every click when started from Steam)"""
        names = getattr(self, "_names", None) or app.g.costumes()
        base, variants = cm.split_name(self.var.get())
        p = Picker(app, names, base, self.with_default); app.root.wait_window(p)
        if p.result: self.pick(rebase(self.var.get(), p.result))
        self.focus_set()

def find_skull_images(g, name, dirs):
    """images in the costume's in-game skull folder (or the nearest parent costume's) - for the preview"""
    path = dirs.get(name)
    lib = os.path.abspath(g.lib)
    while path and os.path.abspath(path).startswith(lib) and len(os.path.abspath(path)) > len(lib):
        try:
            for sub in sorted(os.listdir(path)):
                full = os.path.join(path, sub)
                if os.path.isdir(full) and not sub.startswith("_") and cm.kind_of(sub)[0] == "skull":
                    imgs = sorted(f for f in os.listdir(full) if f.lower().endswith(IMG_EXT))
                    if imgs: return [os.path.join(full, f) for f in imgs[:2]]
        except OSError:
            pass
        path = os.path.dirname(path)
    return []

def thumb(path, box, bg_key="panel"):
    im = Image.open(path).convert("RGBA"); im.thumbnail(box, Image.LANCZOS)
    bg = Image.new("RGBA", im.size, PAL[bg_key]); bg.alpha_composite(im)
    return ImageTk.PhotoImage(bg.convert("RGB"))

# ---------------------------------------------------------------- v0.1.3: dark file / folder browser with back / forward
class Browser(Overlay):
    def __init__(self, root, title, start, mode="file", places=()):
        super().__init__(root); self.withdraw()
        self.title(title); self.configure(background=PAL["bg"])
        sw, sh = root.winfo_screenwidth(), root.winfo_screenheight()
        self.geometry("%dx%d+%d+%d" % (int(sw * 0.86), int(sh * 0.8), int(sw * 0.07), int(sh * 0.06)))
        self.mode, self.result, self.hist, self.pos = mode, None, [], -1
        bar = ttk.Frame(self, padding=8); bar.pack(fill="x")
        self.b_back = ttk.Button(bar, text="<  Back", command=self.back); self.b_back.pack(side="left")
        self.b_fwd = ttk.Button(bar, text="Forward  >", command=self.fwd); self.b_fwd.pack(side="left", padx=4)
        ttk.Button(bar, text="Up one folder", command=self.up).pack(side="left", padx=(4, 16))
        for lab, path in places:
            if path and os.path.isdir(path): ttk.Button(bar, text=lab, command=lambda p=path: self.go(p)).pack(side="left", padx=2)
        self.path_var = tk.StringVar()
        ttk.Label(self, textvariable=self.path_var, style="Dim.TLabel", padding=(10, 0)).pack(fill="x")
        body = ttk.Frame(self, padding=8); body.pack(fill="both", expand=True)
        self.lb = tk.Listbox(body, background=PAL["panel"], foreground=PAL["fg"], selectbackground=PAL["sel"],
                             selectforeground="#ffffff", font=("TkDefaultFont", 13), activestyle="none",
                             highlightthickness=0, relief="flat", exportselection=False)
        sb = ttk.Scrollbar(body, command=self.lb.yview); self.lb.configure(yscrollcommand=sb.set)
        # v0.1.4: the preview is packed first (right side, fixed size) so a wide list can never push it off screen
        pvf = tk.Frame(body, background=PAL["panel"], width=340, height=340); pvf.pack(side="right", fill="y", padx=(10, 0))
        pvf.pack_propagate(False)
        self.pv = tk.Label(pvf, background=PAL["panel"]); self.pv.pack(expand=True)
        sb.pack(side="right", fill="y"); self.lb.pack(side="left", fill="both", expand=True); guard(self.lb)
        bot = ttk.Frame(self, padding=8); bot.pack(fill="x")
        ttk.Label(bot, text="Double-click (or Enter) opens a folder.  Backspace = up one folder.", style="Dim.TLabel").pack(side="left")
        ttk.Button(bot, text="Cancel", command=self.destroy).pack(side="right")
        ttk.Button(bot, text="Use this folder" if mode == "dir" else "Use this image", style="Accent.TButton",
                   command=self.use).pack(side="right", padx=6)
        self.lb.bind("<Double-Button-1>", lambda e: self.open_sel())
        self.lb.bind("<Return>", lambda e: self.open_sel())
        self.lb.bind("<BackSpace>", lambda e: self.up())
        self.lb.bind("<<ListboxSelect>>", lambda e: self.show())
        self.bind("<Alt-Left>", lambda e: self.back()); self.bind("<Alt-Right>", lambda e: self.fwd())
        self.bind("<Escape>", lambda e: self.destroy())
        start = start if start and os.path.isdir(start) else os.path.expanduser("~")
        self.go(start)
        self.transient(root); self.deiconify(); self.grab_set(); self.lb.focus_set()

    def go(self, path, record=True):
        path = os.path.abspath(path)
        try: entries = sorted(os.listdir(path), key=str.lower)
        except OSError as ex: log("Browser: %s" % ex); return
        if record:
            self.hist = self.hist[:self.pos + 1] + [path]; self.pos = len(self.hist) - 1
        self.cur = path; self.path_var.set(path)
        self.items = [("dir", e) for e in entries if os.path.isdir(os.path.join(path, e)) and not e.startswith(".")]
        if self.mode == "file":
            self.items += [("file", e) for e in entries if e.lower().endswith(IMG_EXT) and os.path.isfile(os.path.join(path, e))]
        self.lb.delete(0, "end")
        for kind, e in self.items: self.lb.insert("end", ("  [ ]  " if kind == "dir" else "         ") + e + ("/" if kind == "dir" else ""))
        if self.items: self.lb.selection_set(0); self.lb.activate(0)
        self.b_back.configure(state="normal" if self.pos > 0 else "disabled")
        self.b_fwd.configure(state="normal" if self.pos < len(self.hist) - 1 else "disabled")
        self.show()

    def back(self):
        if self.pos > 0: self.pos -= 1; self.go(self.hist[self.pos], record=False)
    def fwd(self):
        if self.pos < len(self.hist) - 1: self.pos += 1; self.go(self.hist[self.pos], record=False)
    def up(self):
        parent = os.path.dirname(self.cur)
        if parent and parent != self.cur: self.go(parent)
    def sel(self):
        i = self.lb.curselection()
        return self.items[i[0]] if i else None
    def open_sel(self):
        it = self.sel()
        if not it: return
        if it[0] == "dir": self.go(os.path.join(self.cur, it[1]))
        else: self.use()
    def show(self):
        it = self.sel()
        if it and it[0] == "file":
            try: self._pv = thumb(os.path.join(self.cur, it[1]), (300, 300)); self.pv.configure(image=self._pv); return
            except Exception: pass
        self.pv.configure(image="")
    def use(self):
        it = self.sel()
        if self.mode == "dir":
            self.result = self.cur                      # the folder you are in
        elif it and it[0] == "file":
            self.result = os.path.join(self.cur, it[1])
        else:
            return
        self.destroy()

def import_picture(src, dst_noext, solid=True):
    """v0.8.8: add a picture to the library as a clean PNG (any format Pillow reads: PNG, DDS incl. BC7, TGA, JPG, WEBP,
    BMP). solid = the game texture has no see-through parts -> the alpha is made fully solid (HD packs often carry a
    half alpha of 128). Returns the written path; falls back to a plain copy if the picture cannot be read."""
    dst = dst_noext + ".png"
    try:
        im = Image.open(src); im.load(); im = im.convert("RGBA")
        if solid: im.putalpha(255)
        im.save(dst)
        log("  picture saved as PNG: %s (%d x %d%s)" % (os.path.basename(dst), im.size[0], im.size[1], ", made solid" if solid else ""))
        return dst
    except Exception as ex:
        dst = dst_noext + os.path.splitext(src)[1].lower()
        shutil.copy2(src, dst); log("  picture copied as it is (%s): %s" % (ex, os.path.basename(dst)))
        return dst


def find_duplicate_pictures(lib):
    """v0.8.8: pictures in Parts / Shirt Backgrounds / Skulls that are byte-for-byte or pixel-for-pixel the same"""
    import hashlib
    seen, dups = {}, []
    for sub in ("Parts", "Shirt Backgrounds", "Skulls"):
        for dp, dn, fs in os.walk(os.path.join(lib, sub)):
            dn[:] = [d for d in dn if not d.startswith(("_", "."))]
            for f in fs:
                if not f.lower().endswith(IMG_EXT): continue
                p = os.path.join(dp, f)
                try:
                    im = Image.open(p); im.load(); key = hashlib.md5(im.convert("RGBA").tobytes() + repr(im.size).encode()).hexdigest()
                except Exception:
                    continue
                if key in seen: dups.append((seen[key], p))
                else: seen[key] = p
    return dups


def plugin_level_memory(game):
    """v0.8.8: Extended Cut's level memory in MB (ini [Settings] LevelPoolMB; plugin v0.80+ default 128), 0 = none"""
    for rel in (("scripts", "ExtendedCut.ini"), ("scripts", "Extended Cut", "ExtendedCut.ini")):
        ini = os.path.join(game, *rel)
        if os.path.isfile(ini):
            try:
                for line in open(ini, encoding="latin-1"):
                    m = re.match(r"\s*LevelPoolMB\s*=\s*(\d+)", line)
                    if m: return int(m.group(1))
            except OSError:
                pass
            return 0
    return 0


def browse(root, title, start, mode="file", places=()):
    if os.name == "nt":                                # v0.8.7: Windows - one button per drive (C:, D: ...), so you can leave C:
        import string
        places = tuple(places) + tuple((d + ":", d + ":" + chr(92)) for d in string.ascii_uppercase if os.path.isdir(d + ":" + chr(92)))
    b = Browser(root, title, start, mode, places); root.wait_window(b); return b.result

# ---------------------------------------------------------------- the window
class Launcher:
    def __init__(self, root, game_dir, play=False):
        self.root = root; self.busy = False; self.play = play
        root.title("Punisher Costume Launcher v%s" % VERSION)
        sw, sh = root.winfo_screenwidth(), root.winfo_screenheight()
        root.geometry("%dx%d+0+0" % (min(sw, 1600), min(sh, 1000)))
        try: root.attributes("-zoomed", True)          # v0.1.4: start maximised (fits the screen)
        except Exception: pass
        root.protocol("WM_DELETE_WINDOW", self.on_close)
        self.handheld = sh <= 820                     # v0.2: the Deck's own 1280x800 screen
        self.settings = load_settings()
        BIG_PICKER[0] = play or bool(self.settings.get("big_picker", False))
        mult = {"Small": 0.88, "Normal": 1.0, "Large": 1.15}.get(self.settings.get("text_size", "Normal"), 1.0)
        try: root.tk.call("tk", "scaling", (1.12 if self.handheld else 1.4) * mult)
        except Exception: pass
        if play:
            try: root.attributes("-fullscreen", True)    # Gaming Mode
            except Exception: pass
        apply_theme(root, self.settings)
        cm.WORKERS = max(1, int(self.settings.get("workers", cm.WORKERS)))
        install_guard(root); APP[0] = self
        def esc(e):
            g_ = root.grab_current()
            try: cls_ = root.focus_get().winfo_class() if root.focus_get() else ""
            except KeyError: cls_ = ""
            if isinstance(g_, Overlay) and "<Escape>" in g_._keys: return      # the panel has its own Escape
            if g_ is not None and g_ is not root and cls_ != "TEntry":
                (g_.close() if isinstance(g_, Keyboard) else g_.destroy())
        root.bind_all("<Escape>", esc, add="+")
        self.game_dir = game_dir
        self.g = cm.Game(game_dir)
        ba = os.path.join(HERE, "templates", "Body Armor")                     # v0.5: shirt backgrounds for mixes
        tdir = ba if os.path.isdir(ba) else os.path.join(HERE, "templates")
        cm.TEMPLATES_DIR = tdir
        try:
            os.makedirs(self.g.cdir, exist_ok=True); open(os.path.join(self.g.cdir, "templates_path.txt"), "w").write(tdir)
        except OSError: pass
        self.setup_library_folders(tdir)
        self.root.after(1500, self.report_duplicates)   # v0.8.8: identical pictures in the library -> a note in the log
        cm.UNCOMPRESSED_DARK = bool(self.settings.get("uncompressed_dark", True))
        cm.UNCOMPRESSED_MAX = int(self.settings.get("uncompressed_max", 512))
        cm.UNCOMPRESSED_BUDGET_MB = int(self.settings.get("uncompressed_budget", 6))
        lm = plugin_level_memory(game_dir)                                   # v0.8.8: Extended Cut's bigger level memory
        if lm >= 96 and cm.UNCOMPRESSED_BUDGET_MB <= 6:
            cm.UNCOMPRESSED_BUDGET_MB = 20
            log("smooth blacks: Extended Cut level memory %d MB found - budget 20 MB per outfit (was %d)" % (lm, int(self.settings.get("uncompressed_budget", 6))))
        vd = os.path.join(HERE, "templates", "Outfit Variants")                # v0.3: outfits for "Costume + Outfit"
        if os.path.isdir(vd):
            cm.VARIANTS_DIR = vd
            try:
                os.makedirs(self.g.cdir, exist_ok=True); open(os.path.join(self.g.cdir, "variants_path.txt"), "w").write(vd)
            except OSError: pass

        top = ttk.Frame(root, padding=(10, 6)); top.pack(fill="x")
        self.paths = tk.StringVar(); self._update_paths()
        ttk.Label(top, textvariable=self.paths).pack(side="left")
        ttk.Button(top, text="Library folder...", command=self.change_library).pack(side="right")
        ttk.Button(top, text="Game folder...", command=self.change_game).pack(side="right", padx=(0, 6))   # v0.8.7
        ttk.Button(top, text="Rescan library", command=self.rescan).pack(side="right", padx=6)
        root.bind("<F5>", lambda e: self.rescan())

        # v0.2: tabs and log on a draggable divider (drag the bar above the log up or down)
        # v0.8.4: the controller buttons, always on screen (same layout on Steam Deck, Steam Controller, Xbox pads)
        self.footer = ttk.Label(root, style="Dim.TLabel", padding=(12, 2, 12, 6),
                                text="A select    B back    X reset box    Y preview    LB / RB tabs    LT / RT page    "
                                     "Start play / apply    View help    right stick / trackpad = pointer (R3 / RT click)")
        self.footer.pack(side="bottom", fill="x")
        self.vpw = ttk.PanedWindow(root, orient="vertical"); self.vpw.pack(fill="both", expand=True, padx=10, pady=(0, 8))
        nb = ttk.Notebook(self.vpw); self.vpw.add(nb, weight=5); self.nb = nb
        old = False                                               # v0.9.15: the old tabs are retired (Kit builder + Parts replace them)
        self.tab_costumes = ttk.Frame(nb, padding=8); nb.add(self.tab_costumes, text="  Costumes  ")
        self.tab_parts = ttk.Frame(nb, padding=8); nb.add(self.tab_parts, text="  Parts & Skulls  ")
        self.tab_shirt = ttk.Frame(nb, padding=8)
        self.tab_library = ttk.Frame(nb, padding=8)
        if old: nb.add(self.tab_shirt, text="  Shirt creator  "); nb.add(self.tab_library, text="  Library (old)  ")
        self.tab_settings = ttk.Frame(nb, padding=12); nb.add(self.tab_settings, text="  Settings  ")
        nb.bind("<<NotebookTabChanged>>", lambda e: self.root.after(60, self.restore_all_panes))
        self.build_costumes(self.tab_costumes)
        self.build_parts(self.tab_parts)
        self.build_shirt(self.tab_shirt)
        self.build_library(self.tab_library)
        self.build_settings(self.tab_settings)

        logf = ttk.Frame(self.vpw, padding=(0, 4, 0, 0)); self.vpw.add(logf, weight=1)
        self.add_pane("log", self.vpw, [0.86 if self.handheld else 0.80])
        self.logbox = tk.Text(logf, height=4 if self.handheld else 7, wrap="word", font=("monospace", 9), background=PAL["panel"],
                              foreground=PAL["fg"], insertbackground=PAL["fg"], relief="flat", highlightthickness=0)
        sb = ttk.Scrollbar(logf, command=self.logbox.yview); self.logbox.configure(yscrollcommand=sb.set)
        self.logbox.pack(side="left", fill="both", expand=True); sb.pack(side="right", fill="y")
        log("Launcher v%s - game: %s - library: %s - %d costumes, %d missions" %
            (VERSION, game_dir, self.g.lib, len(self.g.costumes()) - 1, len(self.g.missions)))
        self.pump_log()
        root.after(400, self.restore_panes)
        # v0.2: controller + big-list pickers (always when started from Steam, else Settings)
        self.exit_code = 0
        self.big_picker = self.play or bool(self.settings.get("big_picker", False))
        self.pad = Pad(self) if self.settings.get("controller", True) else None
        # v0.8.5: started from Steam -> the selector starts on PLAY, so A launches the game right away when nothing
        # needs changing; otherwise (Settings / desktop) on the first picker as before
        def first_focus():
            if self.play and getattr(self, "play_btn", None) is not None:
                self.play_btn.focus_set()
            elif self.combos:
                self.combos[2 if len(self.combos) > 2 else 0].focus_set()
        root.after(500, first_focus)

    # v0.2.1: every section has draggable dividers; each set of dividers is remembered (handheld and docked apart)
    def add_pane(self, name, pw, fracs):
        if not hasattr(self, "panes"): self.panes = {}
        self.panes[name] = {"pw": pw, "fracs": fracs, "done": False}

    def restore_pane(self, name):
        pn = self.panes.get(name)
        if not pn or pn["done"]: return
        pw = pn["pw"]; vert = str(pw.cget("orient")) == "vertical"
        size = pw.winfo_height() if vert else pw.winfo_width()
        if size < 80: return                            # not on screen yet (another tab) - done when it is shown
        lay = (load_settings().get("layout") or {}).get("handheld" if self.handheld else "docked") or {}
        saved = (lay.get("panes2") or {}).get(name)
        try:
            for i, frac in enumerate(pn["fracs"]):
                v = saved[i] if saved and i < len(saved) else int(size * frac)
                pw.sashpos(i, v)
            pn["done"] = True
        except Exception:
            pass

    def restore_all_panes(self):
        for n in list(getattr(self, "panes", {})): self.restore_pane(n)

    def save_layout(self):
        try:
            st = load_settings(); key = "handheld" if self.handheld else "docked"
            lay = st.setdefault("layout", {}).setdefault(key, {}); p2 = lay.setdefault("panes2", {})
            for n, pn in getattr(self, "panes", {}).items():
                if pn["done"]: p2[n] = [pn["pw"].sashpos(i) for i in range(len(pn["fracs"]))]
            save_settings(st)
        except Exception:
            pass

    def on_close(self):
        self.save_layout()
        if self.busy:
            log("Busy - wait until the current job is done."); return
        self.exit_code = 0                             # from Steam: closing the window = start the game as it is
        self.root.quit()

    def restore_panes(self):
        self.restore_all_panes()

    # ------------------------------------------------------------ pending changes (for Play)
    def pending_work(self):
        st = load_settings()
        return st.get("applied") != self.g.state or st.get("library_changed", False)

    def mark_applied(self):
        st = load_settings(); st["applied"] = dict(self.g.state); st["library_changed"] = False; save_settings(st)

    def mark_library_changed(self):
        st = load_settings(); st["library_changed"] = True; save_settings(st)

    def _update_paths(self):
        self.paths.set("Game: %s     Library: %s" % (self.game_dir, self.g.lib))

    def pump_log(self):
        try:
            while True:
                line = LOG_Q.get_nowait()
                self.logbox.insert("end", line + "\n"); self.logbox.see("end")
        except queue.Empty:
            pass
        self.root.after(100, self.pump_log)

    def run_bg(self, title, fn):
        """run a manager job in the background so the window stays responsive"""
        if self.busy: log("Still busy - wait for the current job to finish."); return
        self.busy = True; self.set_buttons("disabled"); log(""); log("=== %s ===" % title)
        def work():
            err = None
            try: fn()
            except SystemExit as ex: log(str(ex)); err = str(ex)
            except Exception as ex: log("ERROR: %s" % ex); log(traceback.format_exc()); err = "%s: %s" % (type(ex).__name__, ex)
            finally: self.root.after(0, self.job_done)
            if err:                                    # v0.8.7: a failed job gets a clear panel, not only a log line
                self.root.after(50, lambda: info(self.root, "%s - did not finish" % title,
                    "%s\n\nLevel files that were already finished keep their new pictures; the one that failed was left as it was"
                    " (no half-written files). Fix the problem above, then press Apply again - it continues where it stopped.\n\n"
                    "Details are in launcher.log (launcher folder)." % err))
        threading.Thread(target=work, daemon=True).start()

    def job_done(self):
        self.busy = False; self.set_buttons("normal"); self.refresh_costumes()
        if getattr(self, "_rescan_after_job", False):
            self._rescan_after_job = False; self.rescan()

    def set_buttons(self, state):
        for b in self.job_buttons: b.configure(state=state)

    def change_game(self):
        """v0.8.7: pick another game folder (game moved / a second copy) - before, only by editing launcher_settings.json"""
        if self.busy: return
        p = browse(self.root, "The game folder (the one with pun.exe - open it, then 'Use this folder')", self.game_dir, "dir",
                   places=(("Games", os.path.expanduser("~/Games")), ("Home", os.path.expanduser("~"))))
        if not p: return
        if not has_pun(p):
            info(self.root, "Game folder", "pun.exe is not in that folder - nothing changed."); return
        st = load_settings(); st["game"] = p; save_settings(st)
        log("Game folder is now: %s - restarting." % p)
        self.exit_code = 10; self.root.quit()

    def change_library(self):
        if self._busy_note(): return                     # v0.9.20 (review): not while Apply runs
        p = browse(self.root, "Your costume library (open it, then 'Use this folder')", self.g.lib, "dir",
                   places=(("Library", self.g.lib), ("Games", os.path.expanduser("~/Games")), ("Home", os.path.expanduser("~"))))
        if not p: return
        os.makedirs(self.g.cdir, exist_ok=True)
        open(os.path.join(self.g.cdir, "library_path.txt"), "w", encoding="utf-8").write(p)
        self.g = cm.Game(self.game_dir); self._update_paths(); self.refresh_costumes(); self.refresh_shirt_costumes()
        log("Library is now: %s (%d costumes)" % (self.g.lib, len(self.g.costumes()) - 1))

    # ------------------------------------------------------------ Costumes tab
    def build_costumes(self, f):
        # v0.1.4: three panes with draggable dividers (rows | skull preview | buttons); positions are remembered
        self.pw = ttk.PanedWindow(f, orient="horizontal"); self.pw.pack(fill="both", expand=True)
        left = ttk.Frame(self.pw); mid = ttk.Frame(self.pw, padding=(12, 0)); right = ttk.Frame(self.pw, padding=(12, 0, 0, 0))
        self.pw.add(left, weight=3); self.pw.add(mid, weight=2); self.pw.add(right, weight=0)
        right = self.scroll_frame(right)                 # v0.3: the button column scrolls on the small screen
        self.add_pane("costumes", self.pw, [0.45, 0.70])
        canvas = tk.Canvas(left, highlightthickness=0, background=PAL["bg"]); sb = ttk.Scrollbar(left, orient="vertical", command=canvas.yview)
        self.rows_canvas = canvas
        self.rows_f = ttk.Frame(canvas); self.rows_f.columnconfigure(1, weight=1)
        self.rows_f.bind("<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
        win = canvas.create_window((0, 0), window=self.rows_f, anchor="nw"); canvas.configure(yscrollcommand=sb.set)
        canvas.bind("<Configure>", lambda e: canvas.itemconfigure(win, width=e.width))   # rows stretch with the pane
        sb.pack(side="right", fill="y"); canvas.pack(side="left", fill="both", expand=True)
        self.scroll_areas = getattr(self, "scroll_areas", []) + [canvas]
        def wheel(e, step):
            w = self.root.winfo_containing(e.x_root, e.y_root)       # v0.2.2: the list under the pointer scrolls
            while w is not None:
                if w in self.scroll_areas: w.yview_scroll(step, "units"); return
                w = w.master
        canvas.bind_all("<Button-4>", lambda e: wheel(e, -2))
        canvas.bind_all("<Button-5>", lambda e: wheel(e, 2))
        canvas.bind_all("<MouseWheel>", lambda e: wheel(e, -1 if e.delta > 0 else 1))
        self.vars = {}; self.combos = []

        # skull preview: big picture, the costume name, one short note
        self.pv_name = tk.StringVar(); ttk.Label(mid, textvariable=self.pv_name, font=("TkDefaultFont", 11, "bold"),
                                                 wraplength=420, justify="center").pack(anchor="n", pady=(4, 10))
        self.pv_imgs = ttk.Frame(mid); self.pv_imgs.pack(anchor="n")
        self.pv_note = tk.StringVar(); ttk.Label(mid, textvariable=self.pv_note, style="Dim.TLabel", wraplength=420, justify="center").pack(anchor="n", pady=8)
        self._pv_cache = {}

        if self.play:                                  # v0.2: started from Steam (launch.sh)
            pf = ttk.LabelFrame(right, text=" The Punisher ", padding=10); pf.pack(fill="x", pady=(0, 12))
            self.play_btn = ttk.Button(pf, text="PLAY   (Start)", style="Big.Accent.TButton", command=self.play_game); self.play_btn.pack(fill="x")
            ttk.Label(pf, text="applies your changes first (if any)", style="Dim.TLabel").pack(anchor="w")
            ttk.Button(pf, text="Quit - do not start the game", command=self.quit_no_game).pack(fill="x", pady=(8, 0))
        # v0.6: 1) choose a kit (or an old costume / mix)  2) the kit builder: skull + shirt + one style per body part
        k = ttk.LabelFrame(right, text=" Kit ", padding=10); k.pack(fill="x")
        self.kit_var = tk.StringVar()
        self.kit_combo = CostumeMenu(k, self.kit_var, [], command=lambda n: (setattr(self, "_pv_shown", None), self.preview_skull(n)), with_default=False, width=32)
        self.kit_combo.pack(anchor="w", pady=(2, 6)); self.root.after(10, lambda: self.hook_picker(self.kit_combo))
        kb = ttk.Frame(k); kb.pack(anchor="w")
        ttk.Button(kb, text="Use it for the whole game", command=lambda: self.use_for_all(self.kit_var.get()), style="Accent.TButton").pack(side="left")
        ttk.Button(kb, text="Edit in the builder", command=self.kit_edit).pack(side="left", padx=6)

        q = ttk.LabelFrame(right, text=" Kit builder ", padding=10); q.pack(fill="x", pady=(12, 0))
        ttk.Label(q, text="Skull (from Skulls in your library)").pack(anchor="w")
        self.kb_skull = tk.StringVar()
        self.kb_skull_box = CostumeMenu(q, self.kb_skull, [], command=lambda n: self.kb_changed(), with_default=False, width=32)
        self.kb_skull_box.pack(anchor="w", pady=(2, 6)); self.root.after(10, lambda: self.hook_picker(self.kb_skull_box))
        ttk.Label(q, text="Shirt background").pack(anchor="w")
        self.mix_shirt = tk.StringVar(); self.mix_shirt_frame = ttk.Frame(q); self.mix_shirt_frame.pack(anchor="w", pady=(2, 6))
        ttk.Label(q, text="Parts (empty = your base layer)").pack(anchor="w")
        self.kb_parts_frame = ttk.Frame(q); self.kb_parts_frame.pack(anchor="w", fill="x", pady=(2, 6)); self.kb_parts = {}
        self.kb_fit_btn = ttk.Button(q, text="Adjust the skull fit  >", command=self.kb_toggle_fit); self.kb_fit_btn.pack(anchor="w")
        self.kb_fit_frame = ttk.Frame(q); self.kb_fit = {}
        for i, (key, label, lo, hi, d) in enumerate([("fabric", "Fabric", 0, 1, 1.0), ("x0", "Left edge", 0, 0.4, 0.13), ("x1", "Right edge", 0.6, 1, 0.83),
                                                     ("top", "Top", 0.05, 0.4, 0.19), ("aspect", "Squeeze", 0.35, 0.9, 0.55), ("bottom", "Teeth end", 0.6, 0.98, 0.90),
                                                     ("straighten", "Straighten teeth", 0, 1, 0.0)]):          # v0.8.9
            v = tk.DoubleVar(value=d); self.kb_fit[key] = (v, d)
            self.fine_slider(self.kb_fit_frame, i, label, v, lo, hi, self.kb_changed)
        ttk.Label(self.kb_fit_frame, text="Straighten teeth: the shirt bends the bottom of the skull into a curve -\n"
                  "1.00 cancels the measured bend - the T-shirt and the trench coat each get their own correction", style="Dim.TLabel",
                  justify="left").grid(row=7, column=0, columnspan=5, sticky="w", pady=(4, 0))
        ttk.Button(self.kb_fit_frame, text="Reset", command=lambda: ([v.set(d) for v, d in self.kb_fit.values()], self.kb_changed())).grid(row=8, column=0, sticky="w", pady=(4, 0))
        bb = ttk.Frame(q); bb.pack(anchor="w", pady=(10, 0))
        ttk.Button(bb, text="Save kit...", command=self.kit_save, style="Accent.TButton").pack(side="left")
        ttk.Button(bb, text="Use for the whole game", command=self.kit_use).pack(side="left", padx=6)
        ttk.Button(bb, text="Delete kit", command=self.kit_delete).pack(side="left")
        self.kb_name = None                                     # the kit loaded into the builder (None = new)

        self.skull_too = tk.BooleanVar(value=True)
        ttk.Checkbutton(right, text="Also use its in-game skulls", variable=self.skull_too).pack(anchor="w", pady=(8, 0))

        # v0.5: the per-level list is folded away until asked for
        self.rows_btn = ttk.Button(right, text="Per-level choices  >", command=self.toggle_rows); self.rows_btn.pack(anchor="w", pady=(10, 0))
        self._rows_pane = left; self._rows_open = True
        self.root.after(30, lambda: self.toggle_rows(False))

        a = ttk.LabelFrame(right, text=" Game files ", padding=10); a.pack(fill="x", pady=(12, 0))
        b1 = ttk.Button(a, text="Preview changes", command=self.preview); b1.pack(fill="x", pady=2)
        ttk.Label(a, text="lists what Apply would change", style="Dim.TLabel").pack(anchor="w")
        b2 = ttk.Button(a, text="Apply to the game", command=self.apply, style="Accent.TButton"); b2.pack(fill="x", pady=(8, 2))
        ttk.Label(a, text="writes your choices (game must be closed)", style="Dim.TLabel").pack(anchor="w")
        b5 = ttk.Button(a, text="Send to in-game picker", command=self.send_picker); b5.pack(fill="x", pady=(8, 2))   # v0.9.24
        ttk.Label(a, text="your picked costumes -> Extended menu > Costume tab\n(switch them while you play; Extended Cut plugin v1.12+)", style="Dim.TLabel", justify="left").pack(anchor="w")
        b7 = ttk.Button(a, text="Send changes to the game", command=self.send_changes, style="Accent.TButton"); b7.pack(fill="x", pady=(8, 2))   # v0.9.28
        ttk.Label(a, text="only kits / costumes you added or changed since the last send\n(fast - removed ones disappear from the game too)", style="Dim.TLabel", justify="left").pack(anchor="w")
        b6 = ttk.Button(a, text="Send whole library to the game", command=self.send_library); b6.pack(fill="x", pady=(8, 2))   # v0.9.25
        ttk.Label(a, text="every kit, costume and mix + the PC Exclusive skins\n-> the Costume tab (nothing has to be picked for a level)", style="Dim.TLabel", justify="left").pack(anchor="w")
        b3 = ttk.Button(a, text="Check (verify)", command=self.verify); b3.pack(fill="x", pady=(8, 2))
        ttk.Label(a, text="is every costume really in the game?", style="Dim.TLabel").pack(anchor="w")
        self.pending = tk.StringVar(); ttk.Label(a, textvariable=self.pending, style="Warn.TLabel", wraplength=400).pack(anchor="w", pady=(8, 0))

        r = ttk.LabelFrame(right, text=" Reset ", padding=10); r.pack(fill="x", pady=(12, 0))
        b4 = ttk.Button(r, text="Everything back to original", command=self.all_original); b4.pack(fill="x", pady=2)
        ttk.Label(r, text="all to Default + apply; your library\nis not touched", style="Dim.TLabel").pack(anchor="w")
        self.job_buttons = [b1, b2, b3, b4, b5]
        self.migrate_part_names()                                    # v0.9.13
        self.refresh_costumes(); self.refresh_mixes(); self.refresh_builder()

    def migrate_part_names(self):
        """v0.9.13 (user): the texture-01 part is "Face, Belt & Boots" (the hair has its own textures 10-13). An old folder
        name is renamed once, and every kit that uses it follows; never overwrites"""
        try:
            st = load_settings()
            if st.get("migrated_face_part"): return      # v0.9.20 (review): once - a folder the author renames back stays
            parts = os.path.join(self.g.lib, "Parts"); new_b = "Face, Belt & Boots (01)"
            st["migrated_face_part"] = True; save_settings(st)
            if not os.path.isdir(parts) or os.path.exists(os.path.join(parts, new_b)): return
            for old_b in ("Head, Hair, Belt & Boots (01)", "Head, Belt & Boots (01)"):
                old = os.path.join(parts, old_b)
                if os.path.isdir(old):
                    os.rename(old, os.path.join(parts, new_b))
                    n = self.kits_rewrite(lambda r: old_b in (r.get("parts") or {}) and (r["parts"].__setitem__(new_b, r["parts"].pop(old_b)) or True))
                    log("Parts: '%s' renamed to '%s' (%d kit(s) updated)" % (old_b, new_b, n)); return
        except Exception as ex: log("Parts: rename to 'Face, Belt & Boots' skipped: %s" % ex)

    def refresh_costumes(self):
        # v0.8.9 (review): a rebuild (Rescan, Save kit, after a job ...) used to put every row back to the last SAVED
        # state - unsaved choices were lost. Now a row keeps what it shows, if that costume still exists.
        keep = {k: v.get() for k, v in getattr(self, "vars", {}).items()}
        names = self.all_choices(); self._cdirs = cm.costume_dirs(self.g.lib)
        for w in self.rows_f.winfo_children(): w.destroy()
        self.vars = {}; self.combos = []
        rows = [(cm.BASE_KEY, "Base layer (under every costume)"), (cm.GLOBAL_KEY, "In-game skulls (whole game)")] + [(m, m) for m, _ in self.g.missions]
        for r, (key, label) in enumerate(rows):
            lab = ttk.Label(self.rows_f, text=label); lab.grid(row=r, column=0, sticky="w", pady=2, padx=(0, 10))
            shown = keep.get(key)
            v = tk.StringVar(value=shown if shown and (shown == cm.DEFAULT or shown in names) else self.g.costume_of(key)); self.vars[key] = v
            c = CostumeMenu(self.rows_f, v, names, command=lambda n: (self.choices_changed(), self.preview_skull(n)), width=12)
            c.grid(row=r, column=1, sticky="ew", pady=2, padx=(0, 6)); self.hook_picker(c)
            c.bind("<FocusIn>", lambda e, c=c, v=v: (self.see_row(c), self.preview_skull(v.get())), add="+")
            for w in (lab, c): w.bind("<Enter>", lambda e, v=v: self.preview_skull(v.get()))
            lab.bind("<Button-1>", lambda e, c=c: c.focus_set())
            c.bind("<FocusIn>", lambda e, l=lab: l.configure(style="Focus.TLabel"), add="+")
            c.bind("<FocusOut>", lambda e, l=lab: l.configure(style="TLabel"), add="+")
            self.combos.append(c)
        self.kit_combo.set_names(names)
        if not self.kit_var.get() and len(names) > 1: self.kit_var.set(names[1])
        self.choices_changed()

    def preview_skull(self, name):
        """v0.1.3: show the in-game skull(s) of a costume in the middle of the Costumes tab"""
        if getattr(self, "_pv_shown", None) == name: return
        self._pv_shown = name
        for w in self.pv_imgs.winfo_children(): w.destroy()
        self.pv_name.set(name); self.pv_note.set("")
        if name == cm.DEFAULT: self.pv_note.set("the game's own skull"); return
        size = 300 if self.handheld else 380
        mix_note = None
        if cm.is_kit(name):                                    # v0.6: a kit shows its skull picture and its parts
            for w in self.pv_imgs.winfo_children(): w.destroy()
            r = cm.load_kit(self.g, name) or {}; sk = cm.kit_skull(self.g, r)
            self.pv_name.set(name[len(cm.KIT_PREFIX):])
            if sk and os.path.isfile(sk):
                key = (sk, os.path.getmtime(sk))
                if key not in self._pv_cache: self._pv_cache[key] = thumb(sk, (size, size))
                tk.Label(self.pv_imgs, image=self._pv_cache[key], background=PAL["bg"]).pack()
            parts = ["%s: %s" % (re.sub(r"\s*\([^)]*\)\s*$", "", p), st) for p, st in (r.get("parts") or {}).items()]
            self.pv_note.set(("shirt: %s" % os.path.splitext(r.get("shirt", ""))[0] if r.get("skull") else "no skull shirt") + ("\n" + ", ".join(parts) if parts else ""))
            return
        if cm.is_mix(name):                                    # v0.5: a mix shows its skull costume's skull
            r = cm.load_mix(self.g, name) or {}
            mix_note = "shirt: %s%s" % (os.path.splitext(r.get("shirt", "?"))[0], "\noutfit: " + ", ".join(r["outfits"]) if r.get("outfits") else "")
            name = r.get("skull_from", "")
        base, variants = cm.split_name(name)
        files = find_skull_images(self.g, base, getattr(self, "_cdirs", {}) or cm.costume_dirs(self.g.lib))
        if not files: self.pv_note.set("no in-game skull"); return
        for f in files:
            key = (f, os.path.getmtime(f))
            try:
                if key not in self._pv_cache: self._pv_cache[key] = thumb(f, (size // len(files), size), "bg")
                tk.Label(self.pv_imgs, image=self._pv_cache[key], background=PAL["bg"], borderwidth=0).pack(side="left", padx=4)
            except Exception as ex:
                self.pv_note.set("could not show %s (%s)" % (os.path.basename(f), ex))
        own = os.path.abspath(os.path.dirname(os.path.dirname(files[0]))) == os.path.abspath(self._cdirs.get(base, ""))
        notes = [] if own else ["uses the skull of %s" % os.path.basename(os.path.dirname(os.path.dirname(files[0])))]
        if variants: notes.append("outfit: " + ", ".join(variants))
        if mix_note: notes.append(mix_note)
        self.pv_note.set("\n".join(notes))

    def choices_changed(self):
        changed = [k for k, v in self.vars.items() if v.get() != self.g.costume_of(k)]
        self.pending.set("%d not applied yet" % len(changed) if changed else "")

    def store_choices(self):
        for k, v in self.vars.items(): self.g.state[k] = v.get()
        self.g.save()

    # ---- v0.6 kit builder ---------------------------------------------------------------------------------------------
    def setup_library_folders(self, tdir):
        """v0.6: Parts / Skulls / Kits / Shirt Backgrounds in the library, each with a short README"""
        lib = self.g.lib
        if not lib or not os.path.isdir(lib): return
        texts = {
            "Parts": """PARTS - one picture per body part, stored once. Folder name = any words + the textures in brackets:
  (05)        texture 05 of BOTH outfits (pun_tshirt_05 and punps2_trench05)  - e.g. "Pants & Gloves (05)"
  (t07)       T-shirt only  (pun_tshirt_07)                                     - e.g. "T-shirt Back (t07)"
  (c06)       trench coat only (punps2_trench06)                                - e.g. "Coat (c06)"
  (c06-c11)   several textures: then every style is a SUBFOLDER whose files are named by number (06.png, 07.png ...)
Inside a part folder: the pictures (PNG, or DDS / TGA). The file name is the style you pick ("Black Suit.png").
Rename anything before the brackets as you like; keep the brackets. A folder starting with _ is ignored.
Mostly dark pictures are written uncompressed (smooth blacks) - keep the lossless PNG masters for those.
Textures known so far: 01 face, hair, belt, boots - 02 ? - 03 neck and collar - 04 shirt front (the skull shirt)
- 05 pants and gloves - 12 hair (transparent) - t06, t07 back (mirrored), t08 sleeves, t09 - c04 skull shirt, c06-c11 coat.
""",
            "Skulls": """SKULLS - one picture per skull, transparent background:  Skulls/<group>/<artist>.png
  e.g. Skulls/616/Klaus Janson.png, Skulls/MAX/Tim Bradstreet.png, Skulls/616/Klaus Janson - Alt.png
The same picture is printed on the shirt (texture 04, both outfits) and used as the in-game skull (white and orange).
""",
            "Kits": """KITS - made by the launcher's Kit builder: Kits/<group>/<name>.json (+ <name>.shirt.png = the saved skull shirt).
A kit only points at parts, a skull and a shirt background - no pictures are copied.
""",
            "Shirt Backgrounds": """SHIRT BACKGROUNDS - the shirt the skull is printed on (PNG, the size of texture 04, e.g. 512 x 1024).
""",
        }
        for sub, txt in texts.items():
            d = os.path.join(lib, sub)
            try:
                os.makedirs(d, exist_ok=True)
                rp = os.path.join(d, "README.txt")
                if not os.path.isfile(rp): open(rp, "w").write(txt)
            except OSError:
                pass
        sf = os.path.join(lib, "Parts", "Shirt Front (04)")       # v0.8.8: a place for FINISHED shirts (skull already on)
        try:
            if not any(re.search(r"\(\s*0?4\s*\)$", d) for d in os.listdir(os.path.join(lib, "Parts"))):
                os.makedirs(sf, exist_ok=True)
                open(os.path.join(sf, "README.txt"), "w").write(
                    "SHIRT FRONT (04) - finished shirt fronts with the skull ALREADY printed on (512 x 1024 PNG).\n"
                    "Pick one in the Kit builder (Shirt Front) and it is used as it is - the kit's skull + shirt background\n"
                    "are then not printed. Choose the kit's skull too if you want it for the in-game skulls.\n"
                    "Both outfits; for one outfit only rename the folder to (t04) T-shirt or (c04) trench coat.\n")
        except OSError:
            pass
        sb = os.path.join(lib, "Shirt Backgrounds")             # start with the Body Armor backgrounds (copied, not moved)
        try:
            if not any(f.lower().endswith(".png") for f in os.listdir(sb)) and os.path.isdir(tdir):
                for f in os.listdir(tdir):
                    if f.lower().endswith(".png"): shutil.copy2(os.path.join(tdir, f), os.path.join(sb, f))
        except OSError:
            pass

    def fine_slider(self, parent, row, label, var, lo, hi, on_change, step=0.01):
        """v0.7: a slider row that works the same with every input: click / select its name (or the slider), then
        D-pad / arrow keys left-right = one step (Shift: 5), up-down = next row; - and + buttons; type a value + Enter"""
        nm = ttk.Label(parent, text=label); nm.grid(row=row, column=0, sticky="w", pady=2)
        def setv(x):
            var.set(round(min(hi, max(lo, x)), 3)); on_change()
        ttk.Button(parent, text="-", width=3, command=lambda: setv(var.get() - step)).grid(row=row, column=1, padx=(6, 0))
        sc_ = ttk.Scale(parent, from_=lo, to=hi, variable=var, length=170, command=lambda e: on_change(), takefocus=1)
        sc_.grid(row=row, column=2, padx=4)
        ttk.Button(parent, text="+", width=3, command=lambda: setv(var.get() + step)).grid(row=row, column=3)
        ent = ttk.Entry(parent, width=6); ent.grid(row=row, column=4, padx=(6, 0)); ent.insert(0, "%.2f" % var.get())
        def sync(*a):
            if ent.focus_get() is not ent: ent.delete(0, "end"); ent.insert(0, "%.2f" % var.get())
        var.trace_add("write", sync)
        def typed(e=None):
            try: setv(float(ent.get().replace(",", ".")))
            except ValueError: sync()
            return "break"
        ent.bind("<Return>", typed); ent.bind("<KP_Enter>", typed); ent.bind("<FocusOut>", lambda e: typed())
        for k, dlt in (("<Left>", -1), ("<Right>", 1), ("<Shift-Left>", -5), ("<Shift-Right>", 5)):
            sc_.bind(k, lambda e, dlt=dlt: "break" if pad_echo() else (setv(var.get() + step * dlt), "break")[1])
        def nav(e, d):
            if pad_echo(): return "break"
            move_focus(parent.winfo_toplevel() if parent.grab_current() is None else parent.grab_current(), d); return "break"
        sc_.bind("<Up>", lambda e: nav(e, "up")); sc_.bind("<Down>", lambda e: nav(e, "down"))
        sc_.bind("<Button-1>", lambda e: sc_.focus_set(), add="+")
        sc_.bind("<FocusIn>", lambda e: nm.configure(style="Focus.TLabel")); sc_.bind("<FocusOut>", lambda e: nm.configure(style="TLabel"))
        nm.bind("<Button-1>", lambda e: sc_.focus_set())
        sc_._nudge = lambda dlt: setv(var.get() + step * dlt)
        return sc_

    def show_help(self):
        """v0.7: Back / View button = the controls, for any way of playing"""
        txt = ("CONTROLLER\n"
               "  D-pad / left stick   move between fields; left / right changes a value (costume box, slider, list)\n"
               "  A                    press, tick, open a list (text boxes: use their Keyboard button)\n"
               "  B                    back / close a panel (in the keyboard: delete)\n"
               "  X                    selected costume box back to Default\n"
               "  Y                    preview what Apply would change\n"
               "  LB / RB              switch tabs          LT / RT   page up / down in long lists\n"
               "  Right stick / pad    mouse pointer; R3 (or RT right after moving the pointer) clicks\n"
               "  Start                Play (from Steam) / Apply        Back / View   this help\n\n"
               "TRACKPAD / MOUSE / TOUCH\n"
               "  click a field's name to select it, then use the D-pad / arrow keys; every text box has a Keyboard button\n\n"
               "STEAM'S OWN KEYBOARD (Steam + X)\n"
               "  while it is open the launcher ignores the controller (A, L2, Start ... are Steam's);\n"
               "  B, Steam + X again or Enter closes it and gives the controller back\n\n"
               "KEYBOARD\n"
               "  arrow keys like the D-pad, Enter = OK in text boxes, Esc = leave a text box")
        info(self.root, "Controls", txt)

    # ---- v0.8: PARTS & SKULLS tab -------------------------------------------------------------------------------------
    PART_CHOICES = [  # (shown in the list, model, numbers) - the body part names come from the in-game colour test
        ("Face, Belt & Boots (01)", "both", [1]), ("Part 02 (02)", "both", [2]), ("Neck & Collar (03)", "both", [3]),
        ("Shirt Front (04) - replaces the skull shirt", "both", [4]), ("Pants & Gloves (05)", "both", [5]), ("Hair (12)", "both", [12]),
        ("T-shirt Back (t07)", "tshirt", [7]), ("T-shirt Sleeves (t08)", "tshirt", [8]), ("Thigh Holsters (t09)", "tshirt", [9]),
        ("T-shirt 06 (t06)", "tshirt", [6]), ("T-shirt details (t10)", "tshirt", [10]), ("T-shirt details (t11)", "tshirt", [11]),
        ("T-shirt details (t13)", "tshirt", [13]),
        ("Coat Back (c06)", "trench", [6]), ("Coat Sleeves & Front (c07)", "trench", [7]), ("Coat Skirt (c08)", "trench", [8]),
        ("Coat Shoulders (c09)", "trench", [9]), ("Coat details (c10)", "trench", [10]), ("Coat details (c11)", "trench", [11]),
        ("Whole Coat (c06-c09) - one subfolder per coat", "trench", [6, 7, 8, 9]),
    ]

    def build_parts(self, f):
        pw = ttk.PanedWindow(f, orient="horizontal"); pw.pack(fill="both", expand=True); self.add_pane("parts", pw, [0.34, 0.66])
        left = ttk.Frame(pw); mid = ttk.Frame(pw, padding=(10, 0)); right = ttk.Frame(pw, padding=(10, 0))
        pw.add(left, weight=3); pw.add(mid, weight=3); pw.add(right, weight=2)
        self.ps_tree = guard(ttk.Treeview(left, show="tree", selectmode="browse"))
        sb = ttk.Scrollbar(left, command=self.ps_tree.yview); self.ps_tree.configure(yscrollcommand=sb.set)
        self.ps_tree.pack(side="left", fill="both", expand=True); sb.pack(side="right", fill="y")
        self.ps_tree.bind("<<TreeviewSelect>>", lambda e: self.ps_show())
        self.ps_name = tk.StringVar(); self.ps_info = tk.StringVar()
        ttk.Label(mid, textvariable=self.ps_name, style="Focus.TLabel", font=("TkDefaultFont", 12, "bold")).pack(anchor="w")
        self.ps_img = tk.Label(mid, background=PAL["bg"]); self.ps_img.pack(anchor="w", pady=8)
        ttk.Label(mid, textvariable=self.ps_info, style="Dim.TLabel", justify="left", wraplength=420).pack(anchor="w")
        a = ttk.LabelFrame(right, text=" Add ", padding=10); a.pack(fill="x")
        for t, fn in (("New part folder...", self.ps_new_part), ("Add a picture to the selected part...", self.ps_add_picture),
                      ("Add a skull...", self.ps_add_skull), ("Add a shirt background...", self.ps_add_background),
                      ("Add the PC-exclusive T-shirts (from your game)", self.ps_add_pc_exclusives)):            # v0.8.9
            ttk.Button(a, text=t, command=fn).pack(fill="x", pady=2)
        e = ttk.LabelFrame(right, text=" Selected ", padding=10); e.pack(fill="x", pady=(12, 0))
        for t, fn in (("Rename...", self.ps_rename), ("Remove (to _removed)", self.ps_remove), ("Open its folder", self.ps_open)):
            ttk.Button(e, text=t, command=fn).pack(fill="x", pady=2)
        ttk.Label(right, text="Renaming or removing also updates every kit that uses it.", style="Dim.TLabel", wraplength=260).pack(anchor="w", pady=(10, 0))
        self.ps_items = {}; self.refresh_parts()

    def ps_add_pc_exclusives(self):
        """v0.8.9: the PC version's 3 exclusive T-shirt outfits (EB / GS / WM), from the player's own misc_pc.vpp"""
        if not ask(self.root, "PC-exclusive T-shirts", "Copy the three PC-exclusive T-shirt outfits from your game into your library?\n\n"
                   "They come from your own game files (misc_pc.vpp) and are saved as T-shirt-only styles named\n"
                   "'PC Exclusive - EB / GS / WM' in the Parts folders - a starting point to play with.\n"
                   "Nothing in the game is changed; existing files are never overwritten."): return
        self._rescan_after_job = True                   # v0.9.20 (review): rescan once the job is done (it skipped while busy)
        self.run_bg("Add the PC-exclusive T-shirts", lambda: cm.extract_pc_exclusives(self.g))

    def refresh_parts(self, select=None):
        t = self.ps_tree; t.delete(*t.get_children()); self.ps_items = {}
        lib = self.g.lib
        def add(parent, text, kind, path, open_=False):
            it = t.insert(parent, "end", text=text, open=open_); self.ps_items[it] = (kind, path)
            if select and path == select: self.root.after(20, lambda: (t.see(it), t.selection_set(it), t.focus(it)))
            return it
        pr = add("", "Parts", "root", os.path.join(lib, "Parts"), True)
        # v0.9.16: branches - Trench coat, Both outfits, T-shirt only, then Misc (face parts, closed)
        allp = cm.list_parts(self.g)
        for code, title in OUTFIT_GROUPS + (("m", "Misc (hair, eyelashes, teeth)"),):
            grp = [(pt, i) for pt, i in allp.items() if (is_face_part(i) if code == "m" else (not is_face_part(i) and part_outfit(i) == code))]
            if not grp: continue
            par = add(pr, title, "root", os.path.join(lib, "Parts"), code != "m")
            for part, info_ in grp:
                pi = add(par, part, "part", os.path.join(lib, "Parts", part))
                for st, p in info_["styles"].items(): add(pi, st, "style", p)
        sr = add("", "Skulls", "root", os.path.join(lib, "Skulls"), True)
        groups = {}
        for nm, rel in self.skull_files().items():
            parts = rel.split("/"); par = sr
            for i, g_ in enumerate(parts[:-1]):
                key = "/".join(parts[:i + 1])
                if key not in groups: groups[key] = add(par, g_, "group", os.path.join(lib, "Skulls", *parts[:i + 1]))
                par = groups[key]
            add(par, os.path.splitext(parts[-1])[0], "skull", os.path.join(lib, "Skulls", *parts))
        br = add("", "Shirt Backgrounds", "root", os.path.join(lib, "Shirt Backgrounds"), True)
        d = os.path.join(lib, "Shirt Backgrounds")
        if os.path.isdir(d):
            for fn in sorted((x for x in os.listdir(d) if x.lower().endswith(".png")), key=str.lower): add(br, os.path.splitext(fn)[0], "background", os.path.join(d, fn))

    def ps_sel(self):
        s_ = self.ps_tree.selection()
        return self.ps_items.get(s_[0], (None, None)) if s_ else (None, None)

    def ps_show(self):
        kind, path = self.ps_sel(); self.ps_name.set(os.path.basename(path or "")); info_ = ""; pic = None
        if kind == "part":
            slots = cm.part_slots(os.path.basename(path))
            info_ = "Fills: " + ", ".join(n.replace(".tga", "") for m, n_ in slots for k, n in cm.slot_textures(m, n_)) + \
                    "\n%d style(s). Select one to see it." % len(cm.list_parts(self.g).get(os.path.basename(path), {}).get("styles", {}))
        elif kind == "style" and os.path.isfile(path): pic = path
        elif kind == "style" and os.path.isdir(path):
            fs = sorted(x for x in os.listdir(path) if x.lower().endswith(IMG_EXT)); pic = os.path.join(path, fs[0]) if fs else None
            info_ = "Files: " + ", ".join(fs)
        elif kind in ("skull", "background"): pic = path
        elif kind in ("root", "group"): info_ = "Folder: %s" % path
        if pic:
            try:
                im = Image.open(pic); w, h = im.size
                lum = sum(im.convert("L").resize((32, 32)).getdata()) / 1024.0
                extra = "  -  mostly dark: written uncompressed (smooth blacks)" if kind == "style" and lum < 48 else ""
                info_ = (info_ + "\n" if info_ else "") + "%d x %d px%s" % (w, h, extra)
                self._ps_photo = thumb(pic, (320, 320)); self.ps_img.configure(image=self._ps_photo)
            except Exception as ex:
                self.ps_img.configure(image=""); info_ += "\n(cannot show: %s)" % ex
        else:
            self.ps_img.configure(image="")
        self.ps_info.set(info_)

    def _pick(self, title):
        return browse(self.root, title, os.path.expanduser("~/Downloads") if os.path.isdir(os.path.expanduser("~/Downloads")) else os.path.expanduser("~"),
                      places=(("Downloads", os.path.expanduser("~/Downloads")), ("Pictures", os.path.expanduser("~/Pictures")),
                              ("Library", self.g.lib), ("Home", os.path.expanduser("~"))))

    def ps_new_part(self):
        if self._busy_note(): return
        labels = [c[0] for c in self.PART_CHOICES]
        ch = choose_from(self.root, "New part folder", "Which body part?", labels)
        if not ch: return
        lab, model, nums = next(c for c in self.PART_CHOICES if c[0] == ch)
        code = (cm.slot_code(model, nums[0]) + ("-" + cm.slot_code(model, nums[-1]) if len(nums) > 1 else ""))
        words = ask_text(self.root, "New part folder", "Name for this part (the texture code (%s) is added for you):" % code, lab.split(" (")[0])
        if not words or not self._check_name(words): return
        p = os.path.join(self.g.lib, "Parts", "%s (%s)" % (words.strip(), code))
        if os.path.exists(p): info(self.root, "New part folder", "That part folder already exists."); return
        os.makedirs(p); log("New part folder: %s" % os.path.basename(p)); self.refresh_parts(select=p); self.refresh_builder()

    def ps_add_picture(self):
        if self._busy_note(): return
        kind, path = self.ps_sel()
        if kind == "style": path = os.path.dirname(path) if os.path.isfile(path) else os.path.dirname(path); kind = "part"
        if kind != "part": info(self.root, "Add a picture", "Select a part folder (under Parts) first."); return
        slots = cm.part_slots(os.path.basename(path))
        if len(slots) == 1:
            src = self._pick("Picture for %s" % os.path.basename(path))
            if not src: return
            name = ask_text(self.root, "Style name", "Name of this style (what you pick in the kit builder):", os.path.splitext(os.path.basename(src))[0])
            if not name or not self._check_name(name): return
            if any(os.path.splitext(x)[0] == name for x in os.listdir(path)) and not ask(self.root, "Add a picture", "%s already has a style called %s. Replace it?" % (os.path.basename(path), name)): return
            for x in os.listdir(path):                     # one file per style name
                if os.path.splitext(x)[0] == name and os.path.isfile(os.path.join(path, x)):
                    # v0.9.20 (review): the old picture goes to _replaced (ignored by the launcher) instead of being deleted
                    rp = os.path.join(path, "_replaced"); os.makedirs(rp, exist_ok=True)
                    os.replace(os.path.join(path, x), os.path.join(rp, "%s %s%s" % (name, time.strftime("%Y%m%d-%H%M%S"), os.path.splitext(x)[1])))
            dst = import_picture(src, os.path.join(path, name), solid=slots[0][1] < 11)   # v0.8.8: clean PNG
        else:
            name = ask_text(self.root, "Style name", "Name of this style (a subfolder with one picture per texture):")
            if not name or not self._check_name(name): return
            dst = os.path.join(path, name); os.makedirs(dst, exist_ok=True); n_ok = 0
            for model, n in slots:
                src = self._pick("Picture for texture %s (Cancel = skip)" % cm.slot_code(model, n))
                if src: import_picture(src, os.path.join(dst, "%02d" % n), solid=n < 11); n_ok += 1   # v0.8.8
            if not n_ok: shutil.rmtree(dst, ignore_errors=True); return
        log("Added style %s to %s" % (name, os.path.basename(path))); self.refresh_parts(select=dst); self.refresh_builder()

    def ps_add_skull(self):
        if self._busy_note(): return
        src = self._pick("Skull picture (transparent background)")
        if not src: return
        root = os.path.join(self.g.lib, "Skulls")
        groups = sorted(d for d in os.listdir(root) if os.path.isdir(os.path.join(root, d)) and not d.startswith(("_", "."))) if os.path.isdir(root) else []
        g_ = choose_from(self.root, "Add a skull", "Which group? (or pick 'New group...')", groups + ["New group..."])
        if not g_: return
        if g_ == "New group...":
            g_ = ask_text(self.root, "New group", "Name of the group (like 616, MAX, Movies):")
            if not g_ or not self._check_name(g_): return
        name = ask_text(self.root, "Add a skull", "Artist / name of this skull:", os.path.splitext(os.path.basename(src))[0])
        if not name or not self._check_name(name): return
        dst = os.path.join(root, g_, name + ".png")
        if os.path.isfile(dst) and not ask(self.root, "Add a skull", "%s / %s already exists. Replace it?" % (g_, name)): return
        os.makedirs(os.path.dirname(dst), exist_ok=True); Image.open(src).convert("RGBA").save(dst)
        log("Skull added: %s / %s" % (g_, name)); self.refresh_parts(select=dst); self.refresh_builder()

    def ps_add_background(self):
        if self._busy_note(): return
        src = self._pick("Shirt background (blank shirt front, 512 x 1024)")
        if not src: return
        name = ask_text(self.root, "Add a shirt background", "Name of this background:", os.path.splitext(os.path.basename(src))[0])
        if not name or not self._check_name(name): return
        d = os.path.join(self.g.lib, "Shirt Backgrounds"); os.makedirs(d, exist_ok=True); dst = os.path.join(d, name + ".png")
        Image.open(src).convert("RGB").save(dst); log("Shirt background added: %s" % name)
        self.refresh_parts(select=dst); self.refresh_mixes()

    def kits_rewrite(self, fn):
        """apply fn(recipe) -> bool (changed) to every kit file; returns how many kits changed"""
        root = os.path.join(self.g.lib, "Kits"); n = 0
        for dp, dn, fs in os.walk(root):
            for f in fs:
                if not f.endswith(".json"): continue
                p = os.path.join(dp, f)
                try: r = json.load(open(p, encoding="utf-8"))
                except Exception: continue
                if fn(r): json.dump(r, open(p, "w", encoding="utf-8"), indent=1); n += 1
        return n

    def _safe_rename(self, path, new):
        """v0.8.9 (review): os.rename silently replaced an existing file of that name on the Deck - never overwrite"""
        if os.path.exists(new) and os.path.normcase(os.path.abspath(new)) != os.path.normcase(os.path.abspath(path)):
            info(self.root, "Rename", "'%s' already exists there - nothing was renamed." % os.path.basename(new)); return False
        try: os.rename(path, new); return True
        except OSError as ex: info(self.root, "Rename", "Could not rename: %s" % ex); return False

    def _busy_note(self):
        """v0.8.9 (review): while Apply / Check runs, library changes wait (they could change files the job reads)"""
        if self.busy: info(self.root, "Busy", "Wait until the current job is finished, then try again."); return True
        return False

    def ps_rename(self):
        if self._busy_note(): return
        kind, path = self.ps_sel()
        if kind not in ("part", "style", "skull", "background"): info(self.root, "Rename", "Select a part, a style, a skull or a background."); return
        lib = self.g.lib
        if kind == "part":
            m = re.search(r"\s*(\([^)]*\))\s*$", os.path.basename(path)); code = m.group(1) if m else ""
            old_words = os.path.basename(path)[:m.start()] if m else os.path.basename(path)
            w = ask_text(self.root, "Rename part", "New name (the texture code %s stays):" % code, old_words)
            if not w or not self._check_name(w): return
            new = os.path.join(os.path.dirname(path), "%s %s" % (w.strip(), code)); old_b, new_b = os.path.basename(path), os.path.basename(new)
            if not self._safe_rename(path, new): return
            n = self.kits_rewrite(lambda r: old_b in (r.get("parts") or {}) and (r["parts"].__setitem__(new_b, r["parts"].pop(old_b)) or True))
        elif kind == "style":
            part = os.path.basename(os.path.dirname(path)); old = os.path.splitext(os.path.basename(path))[0] if os.path.isfile(path) else os.path.basename(path)
            w = ask_text(self.root, "Rename style", "New name for the style %s:" % old, old)
            if not w or not self._check_name(w): return
            new = os.path.join(os.path.dirname(path), w + (os.path.splitext(path)[1] if os.path.isfile(path) else ""))
            if not self._safe_rename(path, new): return
            n = self.kits_rewrite(lambda r: (r.get("parts") or {}).get(part) == old and (r["parts"].__setitem__(part, w) or True))
        elif kind == "skull":
            old_rel = os.path.relpath(path, os.path.join(lib, "Skulls")).replace(os.sep, "/")
            w = ask_text(self.root, "Rename skull", "New name:", os.path.splitext(os.path.basename(path))[0])
            if not w or not self._check_name(w): return
            new = os.path.join(os.path.dirname(path), w + ".png")
            if not self._safe_rename(path, new): return
            new_rel = os.path.relpath(new, os.path.join(lib, "Skulls")).replace(os.sep, "/")
            n = self.kits_rewrite(lambda r: r.get("skull") == old_rel and (r.__setitem__("skull", new_rel) or True))
        else:
            old = os.path.basename(path); w = ask_text(self.root, "Rename background", "New name:", os.path.splitext(old)[0])
            if not w or not self._check_name(w): return
            new = os.path.join(os.path.dirname(path), w + ".png")
            if not self._safe_rename(path, new): return
            n = self.kits_rewrite(lambda r: r.get("shirt") == old and (r.__setitem__("shirt", w + ".png") or True))
        log("Renamed %s -> %s%s" % (os.path.basename(path), os.path.basename(new), " (%d kit(s) updated)" % n if n else ""))
        self.refresh_parts(select=new); self.refresh_builder(); self.refresh_mixes()

    def ps_remove(self):
        if self._busy_note(): return
        kind, path = self.ps_sel()
        if kind not in ("part", "style", "skull", "background"): return
        lib = self.g.lib
        if kind == "part": users = [r for r in self._kit_recipes() if os.path.basename(path) in (r.get("parts") or {})]
        elif kind == "style":
            part = os.path.basename(os.path.dirname(path)); st = os.path.splitext(os.path.basename(path))[0] if os.path.isfile(path) else os.path.basename(path)
            users = [r for r in self._kit_recipes() if (r.get("parts") or {}).get(part) == st]
        elif kind == "skull":
            rel = os.path.relpath(path, os.path.join(lib, "Skulls")).replace(os.sep, "/"); users = [r for r in self._kit_recipes() if r.get("skull") == rel]
        else: users = [r for r in self._kit_recipes() if r.get("shirt") == os.path.basename(path)]
        msg = "Move %s to _removed?" % os.path.basename(path) + ("\n%d kit(s) use it - they will go back to the base layer for it." % len(users) if users else "")
        if not ask(self.root, "Remove", msg): return
        top = next(r for r in ("Parts", "Skulls", "Shirt Backgrounds") if os.path.abspath(path).startswith(os.path.abspath(os.path.join(lib, r))))
        dst = os.path.join(lib, top, "_removed", time.strftime("%Y%m%d-%H%M%S") + " " + os.path.basename(path))
        os.makedirs(os.path.dirname(dst), exist_ok=True); shutil.move(path, dst)
        log("Removed (kept in %s/_removed): %s" % (top, os.path.basename(path)))
        self.refresh_parts(); self.refresh_builder(); self.refresh_mixes()

    def _kit_recipes(self):
        out = []
        for dp, dn, fs in os.walk(os.path.join(self.g.lib, "Kits")):
            for f in fs:
                if f.endswith(".json"):
                    try: out.append(json.load(open(os.path.join(dp, f), encoding="utf-8")))
                    except Exception: pass
        return out

    def ps_open(self):
        kind, path = self.ps_sel()
        if not path: return
        d = path if os.path.isdir(path) else os.path.dirname(path)
        try: subprocess.Popen(["xdg-open", d] if os.name != "nt" else ["explorer", d])
        except Exception as ex: log("could not open the folder: %s" % ex)

    def skull_files(self):
        """{"616 / Klaus Janson": "616/Klaus Janson.png"} from <library>/Skulls"""
        root = os.path.join(self.g.lib, "Skulls"); out = {}
        for dp, dn, fs in os.walk(root):
            dn[:] = [d for d in dn if not d.startswith(("_", "."))]
            for f in fs:
                if f.lower().endswith(IMG_EXT):
                    rel = os.path.relpath(os.path.join(dp, f), root).replace(os.sep, "/")
                    out[" / ".join(os.path.splitext(rel)[0].split("/"))] = rel
        return dict(sorted(out.items(), key=lambda kv: kv[0].lower()))

    def refresh_builder(self):
        self._skulls = self.skull_files(); self.kb_skull_box.set_names(list(self._skulls))
        if self.kb_skull.get() not in self._skulls: self.kb_skull.set(next(iter(self._skulls), ""))
        keep = {k: v.get() for k, v in self.kb_parts.items()}
        for w in self.kb_parts_frame.winfo_children(): w.destroy()
        self.kb_parts = {}; self._parts = cm.list_parts(self.g)
        # v0.9.12 (user: hair, eyelashes and teeth are not things you change - clean up the list): the face parts
        # (textures 02 teeth, 10 hair cap, 11 eyelashes, 12 hair, 13 hairline - found with the marker test) go in a
        # folded "Face & hair" group at the end; it opens by itself when one of them is set in the kit
        main = [(pt, i) for pt, i in self._parts.items() if not is_face_part(i)]
        face = [(pt, i) for pt, i in self._parts.items() if is_face_part(i)]
        def row(frame, r, part, info_):
            label = re.sub(r"\s*\([^)]*\)\s*$", "", part)
            ttk.Label(frame, text=label).grid(row=r, column=0, sticky="w", padx=(0, 8), pady=1)
            v = tk.StringVar(value=keep.get(part, "")); self.kb_parts[part] = v
            cb = ttk.Combobox(frame, textvariable=v, values=[""] + list(info_["styles"]), state="readonly", width=20)
            cb.grid(row=r, column=1, sticky="w", pady=1); cb.bind("<<ComboboxSelected>>", lambda e: self.kb_changed())
        # v0.9.16 (user): grouped - trench coat parts first, then both outfits, then T-shirt only (each with a heading)
        r = 0
        for code, title in OUTFIT_GROUPS:
            grp = [(pt, i) for pt, i in main if part_outfit(i) == code]
            if not grp: continue
            ttk.Label(self.kb_parts_frame, text=title.upper(), style="Dim.TLabel").grid(row=r, column=0, columnspan=2, sticky="w", pady=(8 if r else 0, 1)); r += 1
            for part, info_ in grp: row(self.kb_parts_frame, r, part, info_); r += 1
        main = [None] * r                                  # rows used (the Misc button goes below them)
        if face:
            ff = ttk.Frame(self.kb_parts_frame)
            def toggle(ff=ff):
                if ff.winfo_ismapped(): ff.grid_remove(); btn.configure(text="Misc (%d)  >" % len(face))
                else: ff.grid(); btn.configure(text="Misc (%d)  v" % len(face))
            btn = ttk.Button(self.kb_parts_frame, text="Misc (%d)  >" % len(face), command=toggle)
            btn.grid(row=len(main), column=0, columnspan=2, sticky="w", pady=(6, 2))
            ff.grid(row=len(main) + 1, column=0, columnspan=2, sticky="w", padx=(12, 0))
            for r, (part, info_) in enumerate(face): row(ff, r, part, info_)
            if not any(keep.get(pt) for pt, _ in face): ff.grid_remove()
            else: btn.configure(text="Misc (%d)  v" % len(face))
        if not self._parts:
            ttk.Label(self.kb_parts_frame, text="(no part folders yet - see Parts/README.txt in your library)", style="Dim.TLabel").grid(row=0, column=0)

    def kb_toggle_fit(self):
        if self.kb_fit_frame.winfo_ismapped():
            self.kb_fit_frame.pack_forget(); self.kb_fit_btn.configure(text="Adjust the skull fit  >")
        else:
            self.kb_fit_frame.pack(after=self.kb_fit_btn, anchor="w", pady=(4, 0)); self.kb_fit_btn.configure(text="Adjust the skull fit  v")

    def kb_recipe(self):
        return {"parts": {p: v.get() for p, v in self.kb_parts.items() if v.get()},
                "skull": self._skulls.get(self.kb_skull.get(), ""), "shirt": self.mix_shirt.get(),
                "fit": {k: round(float(v.get()), 3) for k, (v, d) in self.kb_fit.items()}}

    def kb_changed(self):
        if getattr(self, "_kb_job", None): self.root.after_cancel(self._kb_job)
        self._kb_job = self.root.after(250, self.kb_preview)

    def kb_preview(self):
        """the middle shows the skull shirt as it will look (texture + rough chest view)"""
        self._kb_job = None; r = self.kb_recipe()
        skull = os.path.join(self.g.lib, "Skulls", *r["skull"].split("/")) if r["skull"] else None
        tpl = os.path.join(cm.shirt_backgrounds_dir(self.g) or "", r["shirt"])
        for w in self.pv_imgs.winfo_children(): w.destroy()
        self._pv_shown = None; self.pv_name.set("Kit builder: " + (self.kb_name or "new kit"))
        if not (skull and os.path.isfile(skull) and os.path.isfile(tpl)):
            self.pv_note.set("pick a skull and a shirt background"); return
        try:
            f = r["fit"]
            out, info_ = sc.make(skull, tpl, 0.0, f["fabric"], 7, x0=f["x0"], x1=f["x1"], top=f["top"], aspect=f["aspect"], bottom=f["bottom"], extend=True,
                                 straighten=f.get("straighten", 0.0))
            hh = 300 if self.handheld else 380
            t1 = out.resize((int(out.width * hh / out.height), hh), Image.LANCZOS)
            ch = sc.chest_view(out, info_, f["aspect"]); t2 = ch.resize((int(ch.width * hh / ch.height), hh), Image.LANCZOS)
            self._kb_imgs = (ImageTk.PhotoImage(t1), ImageTk.PhotoImage(t2))
            for im in self._kb_imgs: tk.Label(self.pv_imgs, image=im, background=PAL["bg"]).pack(side="left", padx=6)
            picked = ["%s: %s" % (re.sub(r"\s*\([^)]*\)\s*$", "", p), st) for p, st in r["parts"].items()]
            self.pv_note.set("texture  |  in game (approx.)" + ("\n" + ", ".join(picked) if picked else ""))
        except Exception as ex:
            self.pv_note.set("preview failed: %s" % ex)

    def kit_edit(self):
        n = self.kit_var.get()
        if not cm.is_kit(n): info(self.root, "Kit builder", "Pick a kit (under 'Kits') to edit it."); return
        r = cm.load_kit(self.g, n)
        if not r: return
        self.kb_name = n[len(cm.KIT_PREFIX):]
        inv = {v: k for k, v in self._skulls.items()}
        if r.get("skull") in inv: self.kb_skull.set(inv[r["skull"]])
        if r.get("shirt"): self.mix_shirt.set(r["shirt"])
        for p, v in self.kb_parts.items(): v.set((r.get("parts") or {}).get(p, ""))
        for k, (v, d) in self.kb_fit.items(): v.set((r.get("fit") or {}).get(k, d))
        self.kb_preview(); log("Kit %s loaded into the builder." % self.kb_name)

    def kit_save(self, then_use=False):
        if self._busy_note(): return
        r = self.kb_recipe()
        if not r["skull"] and not r["parts"]: info(self.root, "Kit builder", "Pick a skull and/or parts first."); return None
        groups = sorted(d for d in os.listdir(os.path.join(self.g.lib, "Kits")) if os.path.isdir(os.path.join(self.g.lib, "Kits", d))) if os.path.isdir(os.path.join(self.g.lib, "Kits")) else []
        sk = self.kb_skull.get()
        g0, n0 = (self.kb_name.rsplit("/", 1) if self.kb_name and "/" in self.kb_name else ((sk.split(" / ")[0] if " / " in sk else ""), self.kb_name or (sk.split(" / ")[-1] if sk else "")))
        d = Overlay(self.root); d.title("Save kit"); fr = ttk.Frame(d, padding=16); fr.pack(fill="both")
        grp, nm = tk.StringVar(value=g0), tk.StringVar(value=n0)
        ttk.Label(fr, text="Group (folder in Kits)").grid(row=0, column=0, sticky="w", pady=4)
        ttk.Combobox(fr, textvariable=grp, values=groups, width=30).grid(row=0, column=1, sticky="w")
        ttk.Label(fr, text="Name").grid(row=1, column=0, sticky="w", pady=4)
        ne = ttk.Entry(fr, textvariable=nm, width=34); ne.grid(row=1, column=1, sticky="w")
        kb_button(fr, ne).grid(row=1, column=2, sticky="w", padx=6)
        ttk.Label(fr, text="The skull shirt is made now and saved with the kit (it stays the same until you save the kit again).",
                  style="Dim.TLabel", wraplength=520).grid(row=2, column=0, columnspan=2, sticky="w", pady=(6, 0))
        res = {}
        def ok(*a):
            g_, n_ = grp.get().strip().strip("/"), nm.get().strip()
            if not n_ or any(c in g_ + n_ for c in BAD_CHARS) or "/" in n_:
                info(self.root, "Save kit", "Type a name without / \\ : * ? quotes < > |"); return
            res["name"] = (g_ + "/" if g_ else "") + n_; d.destroy()
        bf = ttk.Frame(fr); bf.grid(row=3, column=0, columnspan=2, sticky="e", pady=(12, 0))
        ttk.Button(bf, text="Cancel", command=d.destroy).pack(side="right")
        ttk.Button(bf, text="Save", style="Accent.TButton", command=ok).pack(side="right", padx=6)
        entry_keys(ne, ok); d.deiconify(); d.grab_set(); ne.focus_set(); self.root.wait_window(d)
        if not res: return None
        name = res["name"]; path = cm.kit_file(self.g, cm.KIT_PREFIX + name)
        if os.path.isfile(path) and name != self.kb_name and not ask(self.root, "Save kit", "A kit called %s already exists. Replace it?" % name): return None
        os.makedirs(os.path.dirname(path), exist_ok=True)
        json.dump(r, open(path, "w", encoding="utf-8"), indent=1)
        shirt = os.path.splitext(path)[0] + ".shirt.png"
        shirt_ok = True
        if r["skull"]:
            try: cm.make_shirt(self.g, os.path.join(self.g.lib, "Skulls", *r["skull"].split("/")), r["shirt"], r["fit"], out=shirt)
            except Exception as ex:                      # v0.9.20 (review): say it, do not claim "skull shirt saved"
                log("could not make the skull shirt: %s" % ex); shirt_ok = False
                info(self.root, "Save kit", "The kit was saved, but its skull shirt could not be made:\n%s\n\n"
                     "Try other skull-fit settings, or pick a shirt background." % ex)
        elif os.path.isfile(shirt): os.remove(shirt)
        tmp = cm.KIT_PREFIX + self.UNSAVED; moved = 0
        for k, v in self.vars.items():
            if v.get() == tmp: v.set(cm.KIT_PREFIX + name); moved += 1
        if moved: self.choices_changed(); log("%d mission(s) moved from the unsaved builder kit to %s" % (moved, name))
        self.kb_name = name; self.refresh_costumes(); self.kit_var.set(cm.KIT_PREFIX + name)
        log("Kit saved: %s (%d part(s)%s)" % (name, len(r["parts"]), (", skull shirt saved" if shirt_ok else ", NO skull shirt") if r["skull"] else ""))
        return cm.KIT_PREFIX + name

    UNSAVED = "_Unsaved/Kit builder (not saved)"

    def kit_use(self):
        """v0.7.2: use what is in the builder for the whole game WITHOUT asking for a name. Unchanged saved kit -> that
        kit; otherwise a working copy (Kits/_Unsaved, hidden from the lists). "Save kit..." later moves the missions over."""
        r = self.kb_recipe()
        if not r["skull"] and not r["parts"]: info(self.root, "Kit builder", "Pick a skull and/or parts first."); return
        if self.kb_name and cm.load_kit(self.g, cm.KIT_PREFIX + self.kb_name) == r:
            self.use_for_all(cm.KIT_PREFIX + self.kb_name); return
        name = cm.KIT_PREFIX + self.UNSAVED; path = cm.kit_file(self.g, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        json.dump(r, open(path, "w", encoding="utf-8"), indent=1)
        shirt = os.path.splitext(path)[0] + ".shirt.png"
        if os.path.isfile(shirt): os.remove(shirt)               # made fresh at Apply from the current fit
        self.use_for_all(name)
        log("Using the kit builder's choices (not saved) - 'Save kit...' keeps them as a kit.")

    def kit_delete(self):
        if self._busy_note(): return
        n = self.kit_var.get()
        if not cm.is_kit(n) or not ask(self.root, "Delete kit", "Move the kit %s to Kits/_removed?\n(missions that use it go back to Default; parts and skulls stay)" % n[len(cm.KIT_PREFIX):]): return
        p = cm.kit_file(self.g, n)                       # v0.8.7: moved to Kits/_removed like everything else, not deleted
        rd = os.path.join(self.g.lib, "Kits", "_removed"); stamp = time.strftime("%Y%m%d-%H%M%S")
        for f in (p, os.path.splitext(p)[0] + ".shirt.png"):
            try:
                if os.path.isfile(f):
                    os.makedirs(rd, exist_ok=True); b_, e_ = os.path.splitext(os.path.basename(f))
                    shutil.move(f, os.path.join(rd, "%s_%s%s" % (b_, stamp, e_)))
            except OSError: pass
        for k, v in list(self.g.state.items()):
            if v == n: self.g.state[k] = cm.DEFAULT
        self.g.save(); self.kit_var.set(""); self.refresh_costumes()

    # ---- v0.5: per-level list on demand, kits, mixes
    def toggle_rows(self, show=None):
        show = (not self._rows_open) if show is None else show
        if show and not self._rows_open:
            self.pw.insert(0, self._rows_pane, weight=3); self._rows_open = True
            self.rows_btn.configure(text="Per-level choices  v")
            self.panes["costumes"]["done"] = False; self.root.after(60, lambda: self.restore_pane("costumes"))
        elif not show and self._rows_open:
            self.pw.forget(self._rows_pane); self._rows_open = False
            self.rows_btn.configure(text="Per-level choices  >")

    def use_for_all(self, name, skull_from=None):
        if not name: return
        for k, v in self.vars.items():
            if k == cm.BASE_KEY: continue
            if k == cm.GLOBAL_KEY:
                if not self.skull_too.get(): continue
                src = skull_from or (name if cm.is_kit(name) else cm.split_name(name)[0])
                if not cm.skull_sources(self.g, src):
                    log("In-game skulls not changed: %s has no in-game skull folder." % src); continue
                v.set(src); continue
            v.set(name)
        self.choices_changed(); self._pv_shown = None; self.preview_skull(name)
        log("Every mission: %s - Apply / Play writes it to the game." % name)

    def mix_dir(self): return os.path.join(self.g.lib, "_Mixes")
    def mix_names(self):
        d = self.mix_dir()
        return sorted((os.path.splitext(f)[0] for f in os.listdir(d) if f.endswith(".json")), key=str.lower) if os.path.isdir(d) else []
    def kit_names(self):
        root = os.path.join(self.g.lib, "Kits"); out = []
        for dp, dn, fs in os.walk(root):
            dn[:] = [d for d in dn if not d.startswith(("_", "."))]
            for f in fs:
                if f.endswith(".json"): out.append(os.path.relpath(os.path.join(dp, f[:-5]), root).replace(os.sep, "/"))
        return sorted(out, key=str.lower)
    def all_choices(self):
        return [cm.KIT_PREFIX + n for n in self.kit_names()] + self.g.costumes() + [cm.MIX_PREFIX + n for n in self.mix_names()]
    def refresh_mixes(self):
        for w in self.mix_shirt_frame.winfo_children(): w.destroy()
        td = cm.shirt_backgrounds_dir(self.g) or ""
        shirts = sorted((f for f in os.listdir(td) if f.lower().endswith(".png")), key=lambda f: os.path.splitext(f)[0].lower()) if os.path.isdir(td) else []
        for i, f in enumerate(shirts):
            ttk.Radiobutton(self.mix_shirt_frame, text=os.path.splitext(f)[0], value=f, variable=self.mix_shirt,
                            command=self.kb_changed).grid(row=i // 2, column=i % 2, sticky="w", padx=(0, 14))
        if shirts and self.mix_shirt.get() not in shirts: self.mix_shirt.set(shirts[0])
        if not shirts:                                    # v0.9.17: a new library has none - they come from the templates mod
            ttk.Label(self.mix_shirt_frame, style="Dim.TLabel", wraplength=420, justify="left",
                      text="No shirt backgrounds yet. Get the Costume Launcher templates (Nexus Mods - see README 3c) and put "
                           "the PNGs into your library's Shirt Backgrounds folder, then Rescan.").grid(row=0, column=0, sticky="w")

    def play_game(self):
        if self.busy: return
        self.store_choices()
        # v0.8.3: always apply - settings (smooth-blacks budget / size) or edited pictures change the textures without
        # changing any choice; the engine skips every level that is already right (about 1.5 s)
        def done_then_play():
            cm.apply_all(self.g, dry=False); self.mark_applied()
            self.root.after(800, self.leave_to_game)
        self.run_bg("Apply, then start the game", done_then_play)

    def leave_to_game(self):
        self.save_layout(); self.exit_code = 0; self.root.quit()

    def quit_no_game(self):
        if self.busy: return
        self.save_layout(); self.exit_code = 2; self.root.quit()       # launch.sh: 2 = do not start the game

    def preview(self):
        self.store_choices(); self.run_bg("Preview (nothing is written)", lambda: cm.apply_all(self.g, dry=True))

    def apply(self):
        if game_running():
            info(self.root, "Game is running", "Close The Punisher first - the level files are in use."); return
        self.store_choices(); self.run_bg("Apply", lambda: (cm.apply_all(self.g, dry=False), self.mark_applied()))

    def verify(self):
        self.run_bg("Check", lambda: cm.verify(self.g))

    def send_changes(self):   # v0.9.28: only new / changed kits, costumes and mixes
        self.store_choices(); self.run_bg("Send changes to the game", lambda: cm.export_picker_changes(self.g))

    def send_library(self):   # v0.9.25: the whole library -> the in-game picker
        self.store_choices(); self.run_bg("Send whole library to the game", lambda: cm.export_picker_all(self.g))

    def send_picker(self):   # v0.9.24: the in-game picker reads scripts\...\Costumes - the game may stay open
        self.store_choices(); self.run_bg("Send to in-game picker", lambda: cm.export_picker_used(self.g))

    def all_original(self):
        if game_running():
            info(self.root, "Game is running", "Close The Punisher first."); return
        if not ask(self.root, "Back to original", "Set every mission, the skulls and the base layer to Default and apply?\nYour library is not touched."): return
        for v in self.vars.values(): v.set(cm.DEFAULT)
        self.store_choices(); self.run_bg("Everything back to original", lambda: (cm.apply_all(self.g, dry=False), self.mark_applied()))


    # ------------------------------------------------------------ v0.2: controller / touch helpers
    def hook_picker(self, menubutton):
        def click(e, m=menubutton):
            if self.big_picker:                      # v0.8.1: opened after the click is finished (not inside it)
                self.root.after(0, lambda: m.open_picker(self)); return "break"
        menubutton.bind("<Button-1>", click, add="+")
        menubutton.bind("<Button-3>", lambda e, m=menubutton: self.costume_context(e, m))

    def see_row(self, w):
        """scroll the mission list so the focused row is visible"""
        try:
            c = self.rows_canvas; total = max(1, self.rows_f.winfo_height())
            y = w.winfo_rooty() - self.rows_f.winfo_rooty(); top, bot = c.yview()
            if y / total < top or (y + w.winfo_height()) / total > bot:
                c.yview_moveto(max(0.0, (y - c.winfo_height() / 3) / total))
        except Exception:
            pass

    def scroll_frame(self, parent):
        """a frame inside a canvas with a scrollbar (wheel / controller scroll it); returns the inner frame"""
        cv = tk.Canvas(parent, highlightthickness=0, background=PAL["bg"]); sb = ttk.Scrollbar(parent, orient="vertical", command=cv.yview)
        inner = ttk.Frame(cv); win = cv.create_window((0, 0), window=inner, anchor="nw"); cv.configure(yscrollcommand=sb.set)
        inner.bind("<Configure>", lambda e: cv.configure(scrollregion=cv.bbox("all")))
        cv.bind("<Configure>", lambda e: cv.itemconfigure(win, width=e.width))
        sb.pack(side="right", fill="y"); cv.pack(side="left", fill="both", expand=True)
        self.scroll_areas = getattr(self, "scroll_areas", []) + [cv]
        self.scroll_pairs = getattr(self, "scroll_pairs", []) + [(cv, inner)]
        return inner

    # ---- v0.3.2: right-click (or LT on the controller): new variant of a costume, new folder in the library
    def context_action(self, w):
        if isinstance(w, CostumeMenu):
            name = cm.split_name(w.var.get())[0]
            if name and name != cm.DEFAULT: self.new_variant(name)
        elif w is getattr(self, "tree", None):
            path, tag = self.lib_sel()
            if path and not tag.startswith("kind:"): self.lib_new_costume()
        elif self.nb.select() == str(self.tab_shirt):
            self.new_costume()

    def context_menu(self, event, items):
        m = tk.Menu(self.root, tearoff=0, background=PAL["panel"], foreground=PAL["fg"], activebackground=PAL["sel"],
                    activeforeground="#ffffff", font=("TkDefaultFont", 12))
        for label, fn in items: m.add_command(label=label, command=fn)
        try: m.tk_popup(event.x_root, event.y_root)
        finally: m.grab_release()

    def costume_context(self, event, menubutton):
        name = cm.split_name(menubutton.var.get())[0]
        if not name or name == cm.DEFAULT: return "break"
        self.context_menu(event, [("New variant of %s (with the Shirt creator)..." % name, lambda: self.new_variant(name)),
                                  ("New empty variant folder in %s..." % name, lambda: self.new_variant(name, empty=True))])
        return "break"

    def new_variant(self, name, empty=False):
        """a new costume folder INSIDE an existing costume = a variant of it; never overwrites anything"""
        path = self.costume_dir(name)
        if not path: info(self.root, "New variant", "Costume folder not found: %s" % name); return
        if empty:
            n = ask_text(self.root, "New variant", "Name of the new variant inside %s:" % name)
            if not n or not self._check_name(n): return
            p = os.path.join(path, n)
            if os.path.exists(p): info(self.root, "New variant", "%s already has a folder called %s - pick another name." % (name, n)); return
            for sub in ("T-shirt", "Trench Coat"): os.makedirs(os.path.join(p, sub))
            log("New empty variant: %s" % self.name_of(p)); self.rescan(select=p); return
        self._variant_of = name
        if self.last is None:
            self.nb.select(self.tab_shirt); self.show_step(1)
            info(self.root, "New variant", "Make the shirt first (steps 1 - 4). On step 5, 'Create the new costume' will offer\n%s as the place for the new variant." % name)
            return
        self.new_costume()

    # ---- v0.4: right stick moves a pointer, RT (or R3) clicks where it points - for everything the D-pad cannot reach
    def cursor_step(self, L, ctrl, cur, prev):
        rx, ry = L.SDL_GameControllerGetAxis(ctrl, 2) / 32767.0, L.SDL_GameControllerGetAxis(ctrl, 3) / 32767.0
        rt = L.SDL_GameControllerGetAxis(ctrl, 5) > 16000
        if not hasattr(self, "_cx"):
            self._cx, self._cy = self.root.winfo_pointerxy(); self._rt = False; self._lastclick = 0
        mag = (rx * rx + ry * ry) ** 0.5
        if mag > 0.18:
            self._moved = time.time()                      # v0.8.4: RT clicks only while the pointer is in use
            sp = 4 + 34 * ((mag - 0.18) / 0.82) ** 2                        # slow near the centre, fast at full tilt
            sw, sh = self.root.winfo_screenwidth(), self.root.winfo_screenheight()
            self._cx = int(min(sw - 1, max(0, self._cx + rx / mag * sp))); self._cy = int(min(sh - 1, max(0, self._cy + ry / mag * sp)))
            try: self.root.event_generate("<Motion>", warp=True, x=self._cx - self.root.winfo_rootx(), y=self._cy - self.root.winfo_rooty())
            except tk.TclError: pass
        pointer = time.time() - getattr(self, "_moved", 0) < 2.0
        click = (rt and not self._rt and pointer) or (cur[Pad.RS] and not prev[Pad.RS])
        self._rt = rt
        if click: self.pointer_click(self._cx, self._cy)

    def pointer_click(self, x, y):
        w = self.root.winfo_containing(x, y)
        if w is None: return
        now = time.time(); double = now - self._lastclick < 0.45; self._lastclick = now
        cls = w.winfo_class(); lx, ly = x - w.winfo_rootx(), y - w.winfo_rooty()
        try:
            if cls in ("TButton", "TCheckbutton", "TRadiobutton", "Button"):
                w.focus_set(); w.invoke()
            elif isinstance(w, CostumeMenu):
                w.focus_set(); w.open_picker(self)
            elif cls == "TEntry":
                w.focus_set(); w.icursor("end")
            elif cls == "Listbox":
                i = w.nearest(ly); w.focus_set(); w.selection_clear(0, "end"); w.selection_set(i); w.activate(i)
                w.event_generate("<<ListboxSelect>>")
                if double: w.event_generate("<Double-Button-1>", x=lx, y=ly)
            elif cls == "Treeview":
                it = w.identify_row(ly)
                if it: w.focus_set(); w.selection_set(it); w.focus(it)
                if it and double and w.get_children(it): w.item(it, open=not w.item(it, "open"))
            elif cls == "TNotebook":
                i = w.index("@%d,%d" % (lx, ly)); w.select(i)
            elif cls == "TScale":
                frac = min(1, max(0, lx / max(1, w.winfo_width()))); lo, hi = float(w.cget("from")), float(w.cget("to"))
                w.focus_set(); w.set(lo + (hi - lo) * frac); w.event_generate("<<Changed>>"); self.schedule()
            elif cls == "TCombobox":
                vals = list(w.cget("values")); cur = w.get(); w.focus_set()
                if vals: w.set(vals[(vals.index(cur) + 1) % len(vals)] if cur in vals else vals[0]); w.event_generate("<<ComboboxSelected>>")
            else:
                w.event_generate("<ButtonPress-1>", x=lx, y=ly); w.event_generate("<ButtonRelease-1>", x=lx, y=ly)
        except tk.TclError:
            pass

    def see_in(self, c, frame, w):
        try:
            total = max(1, frame.winfo_height()); y = w.winfo_rooty() - frame.winfo_rooty(); top, bot = c.yview()
            if y / total < top or (y + w.winfo_height()) / total > bot: c.yview_moveto(max(0.0, (y - c.winfo_height() / 3) / total))
        except Exception:
            pass

    def pad_button(self, b):
        # v0.8.3: an open drop-down list (opened by touch / mouse) has focus in Tk's own popdown, which tkinter cannot
        # name ('popdown') - steer it with keys instead
        try: fpath = str(self.root.tk.call("focus"))
        except tk.TclError: fpath = ""
        if ".popdown" in fpath:
            key = {Pad.UP: "<Up>", Pad.DOWN: "<Down>", Pad.A: "<Return>", Pad.B: "<Escape>"}.get(b)
            if key:
                try: self.root.tk.call("event", "generate", fpath, key)
                except tk.TclError: pass
            return
        return self._pad_button(b)

    def _pad_button(self, b):
        P = Pad
        grab = self.root.grab_current()
        try: focus = self.root.focus_get()
        except KeyError: focus = None                    # focus is in a desktop drop-down menu
        win = grab if grab is not None else self.root
        cls = focus.winfo_class() if focus is not None else ""
        dirs = {P.UP: "up", P.DOWN: "down", P.LEFT: "left", P.RIGHT: "right"}
        if b == P.A: PAD_A_LAST[0] = time.time()
        if isinstance(grab, Keyboard):                    # v0.7: the on-screen keyboard
            if b == P.B: grab.delete(); return
            if b == P.Y: grab.type(" "); return
            if b == P.X: grab.toggle_shift(); return
            if b == P.START: grab.done(); return
            if b in (P.LB, P.RB, P.LT, P.RT, P.BACK): return      # LT is held for capitals (hold_caps)
        if b == P.BACK and grab is None:
            self.show_help(); return
        if b == P.B:
            if cls == "TEntry":                           # closing the Steam keyboard - do not close the window too
                move_focus(win, "down"); return
            if isinstance(grab, Picker): grab.back()
            elif isinstance(grab, Browser): (grab.back() if grab.pos > 0 else grab.destroy())
            elif grab is not None and grab is not self.root: grab.destroy()
            return
        if b in dirs:
            d = dirs[b]; PAD_LAST[0] = time.time()
            if cls == "Listbox" and d in ("up", "down"):
                i = (focus.curselection() or (0,))[0] + (1 if d == "down" else -1)
                if 0 <= i < focus.size():
                    focus.selection_clear(0, "end"); focus.selection_set(i); focus.activate(i); focus.see(i); focus.event_generate("<<ListboxSelect>>")
                return
            if cls == "Treeview":
                sel = focus.selection() or focus.get_children("")[:1]
                if sel:
                    it = sel[0]
                    if d in ("up", "down"):
                        nxt = focus.prev(it) if d == "up" else focus.next(it)
                        if d == "down" and focus.item(it, "open") and focus.get_children(it): nxt = focus.get_children(it)[0]
                        if not nxt and d == "up": nxt = focus.parent(it)
                        if not nxt and d == "down":
                            p = focus.parent(it)
                            while p and not nxt: nxt = focus.next(p); p = focus.parent(p)
                        if nxt: focus.selection_set(nxt); focus.focus(nxt); focus.see(nxt)
                        return
                    if d == "right" and focus.get_children(it) and not focus.item(it, "open"): focus.item(it, open=True); return
                    if d == "left" and focus.item(it, "open"): focus.item(it, open=False); return
                    if d == "left" and focus.parent(it): p = focus.parent(it); focus.selection_set(p); focus.focus(p); focus.see(p); return
            if isinstance(grab, Picker) and d in ("left", "right"):
                (grab.back() if d == "left" else grab.choose()); return
            if cls == "TScale" and d in ("left", "right"):
                # v0.7: adjust directly (the old way sent an arrow key, which the duplicate filter then swallowed)
                if hasattr(focus, "_nudge"): focus._nudge(-1 if d == "left" else 1)
                return
            if cls == "TScale" and d in ("up", "down") and hasattr(focus, "_nudge"):
                w = move_focus(win, d)
                if w is not None and str(w).startswith(str(self.shirt_frame)): self.see_in(self.shirt_canvas, self.shirt_frame, w)
                return
            if d in ("left", "right") and isinstance(focus, CostumeMenu):
                focus.cycle(-1 if d == "left" else 1); return
            if d in ("left", "right") and cls == "TCombobox":
                vals = list(focus.cget("values")); cur = focus.get()
                if vals:
                    i = (vals.index(cur) if cur in vals else -1) + (1 if d == "right" else -1)
                    focus.set(vals[i % len(vals)]); focus.event_generate("<<ComboboxSelected>>")
                return
            if d in ("left", "right") and cls == "TRadiobutton":
                sib = [w for w in focus.master.winfo_children() if w.winfo_class() == "TRadiobutton"]
                if len(sib) > 1:
                    w = sib[(sib.index(focus) + (1 if d == "right" else -1)) % len(sib)]; w.focus_set(); w.invoke()
                return
            w = move_focus(win, d)
            if w is not None and str(w).startswith(str(self.rows_f)): self.see_row(w)
            elif w is not None and str(w).startswith(str(self.shirt_frame)): self.see_in(self.shirt_canvas, self.shirt_frame, w)
            elif w is not None:
                for cv_, fr_ in getattr(self, "scroll_pairs", []):
                    if str(w).startswith(str(fr_)): self.see_in(cv_, fr_, w); break
            return
        if b == P.A:
            if focus is None or (grab is None and focus.winfo_toplevel() is not self.root):
                move_focus(win, "down"); return
            if cls in ("TButton", "TCheckbutton", "TRadiobutton"): focus.invoke()
            elif cls == "TEntry": pass                  # v0.8.1: the keyboard opens only from its Keyboard button
            elif cls == "TMenubutton" and isinstance(focus, CostumeMenu): focus.open_picker(self)
            elif cls == "Listbox": focus.event_generate("<Return>")
            elif cls == "Treeview":
                sel = focus.selection()
                if sel and focus.get_children(sel[0]): focus.item(sel[0], open=not focus.item(sel[0], "open"))
            elif cls == "TCombobox":
                vals = list(focus.cget("values")); cur = focus.get()
                if vals: focus.set(vals[(vals.index(cur) + 1) % len(vals)] if cur in vals else vals[0]); focus.event_generate("<<ComboboxSelected>>")
            return
        # v0.8.4: universal layout - X resets the selected costume box, Y previews, LT / RT page through lists
        if b in (P.LT, P.RT):
            if b == P.RT and time.time() - getattr(self, "_moved", 0) < 2.0: return       # RT = pointer click
            step = 8 if b == P.RT else -8
            lb = focus if cls == "Listbox" else getattr(grab, "lb", None)
            if lb is not None and lb.size():
                i = max(0, min(lb.size() - 1, (lb.curselection() or (0,))[0] + step))
                lb.selection_clear(0, "end"); lb.selection_set(i); lb.activate(i); lb.see(i); lb.event_generate("<<ListboxSelect>>")
            elif cls == "Treeview":
                for _ in range(abs(step)): self._pad_button(P.DOWN if step > 0 else P.UP)
            else:
                # v0.9.9 (user: the triggers do nothing): on the pages of rows (missions, Kit builder, Settings ...) LT / RT
                # jump 6 rows up / down (the same move as the D-pad, so the page scrolls along)
                for _ in range(6): self._pad_button(P.DOWN if step > 0 else P.UP)
            return
        if b == P.X and grab is None:
            if isinstance(focus, CostumeMenu) and focus.with_default:
                focus.pick(cm.DEFAULT); log("controller: X - box back to Default")
            return
        if b == P.Y and grab is None:
            self.preview(); return
        if b in (P.LB, P.RB) and grab is None:
            tabs = self.nb.tabs(); i = tabs.index(self.nb.select()) + (1 if b == P.RB else -1)
            self.nb.select(tabs[i % len(tabs)]); self.root.after(80, lambda: move_focus(self.root, "down")); return
        if b == P.START and grab is None:
            (self.play_game if self.play else self.apply)()


    # ------------------------------------------------------------ v0.2.1: Library tab (manage folders inside the launcher)
    KIND_NAMES = {"tshirt": "T-shirt", "trench": "Trench Coat", "skull": "in-game skull"}

    def build_library(self, f):
        lpw = ttk.PanedWindow(f, orient="horizontal"); lpw.pack(fill="both", expand=True)
        left = ttk.Frame(lpw); mid = ttk.Frame(lpw, padding=(12, 0)); right = ttk.Frame(lpw, padding=(12, 0, 0, 0))
        lpw.add(left, weight=2); lpw.add(mid, weight=2); lpw.add(right, weight=0); self.add_pane("library", lpw, [0.36, 0.72])
        self.tree = guard(ttk.Treeview(left, show="tree", selectmode="browse"))
        tsb = ttk.Scrollbar(left, orient="vertical", command=self.tree.yview); self.tree.configure(yscrollcommand=tsb.set)
        tsb.pack(side="right", fill="y"); self.tree.pack(side="left", fill="both", expand=True)
        self.tree.bind("<<TreeviewSelect>>", lambda e: self.lib_show())
        def tree_menu(e):
            it = self.tree.identify_row(e.y)
            if it: self.tree.selection_set(it); self.tree.focus(it)
            path, tag = self.lib_sel()
            if not path: return
            items = []
            if not tag.startswith("kind:"):
                items.append(("New costume / variant inside %s..." % os.path.basename(path), self.lib_new_costume))
                items.append(("Rename...", self.lib_rename))
            items += [("Add images...", self.lib_add_images), ("Open in Dolphin", self.lib_open)]
            self.context_menu(e, items)
        self.tree.bind("<Button-3>", tree_menu)
        self.lib_title = tk.StringVar(); ttk.Label(mid, textvariable=self.lib_title, font=("TkDefaultFont", 11, "bold"), wraplength=460).pack(anchor="w")
        self.lib_info = tk.StringVar(); ttk.Label(mid, textvariable=self.lib_info, style="Dim.TLabel", wraplength=460, justify="left").pack(anchor="w", pady=(2, 8))
        self.lib_img = tk.Label(mid, background=PAL["bg"]); self.lib_img.pack(anchor="w")
        self.lib_files = tk.Listbox(mid, background=PAL["panel"], foreground=PAL["fg"], selectbackground=PAL["sel"], height=8,
                                    highlightthickness=0, relief="flat", activestyle="none", font=("TkDefaultFont", 10))
        self.lib_files.pack(fill="both", expand=True, pady=(8, 0)); guard(self.lib_files)
        self.lib_files.bind("<<ListboxSelect>>", lambda e: self.lib_show_file())
        a = ttk.LabelFrame(right, text=" Folders ", padding=10); a.pack(fill="x")
        for label, fn in (("New group folder", self.lib_new_group), ("New costume here", self.lib_new_costume),
                          ("Rename", self.lib_rename), ("Move to another group", self.lib_move), ("Remove (to _removed)", self.lib_remove)):
            ttk.Button(a, text=label, command=fn).pack(fill="x", pady=2)
        b = ttk.LabelFrame(right, text=" Files ", padding=10); b.pack(fill="x", pady=(12, 0))
        ttk.Button(b, text="Add images...", command=self.lib_add_images).pack(fill="x", pady=2)
        ttk.Button(b, text="Open the folder", command=self.lib_open).pack(fill="x", pady=2)
        ttk.Label(b, text="drag and drop works in your file manager;\npress Rescan afterwards", style="Dim.TLabel").pack(anchor="w", pady=(4, 0))
        ttk.Button(right, text="Rescan library", command=self.rescan).pack(fill="x", pady=(12, 0))
        self._lib_imgs = {}
        self.refresh_library_tree()

    def refresh_library_tree(self, select=None):
        t = self.tree; openp = {t.item(i, "values")[0] for i in self._all_items() if t.item(i, "open")} if t.get_children("") else set()
        t.delete(*t.get_children(""))
        dirs = cm.costume_dirs(self.g.lib); self._cdirs = dirs
        cpaths = {os.path.abspath(p): n for n, p in dirs.items()}
        def add(parent, path):
            try: entries = sorted(os.listdir(path), key=str.lower)
            except OSError: return
            for e in entries:
                full = os.path.join(path, e)
                if not os.path.isdir(full) or e.startswith((".", "_")): continue
                kind = cm.kind_of(e)[0]
                if kind in ("tshirt", "trench", "skull"):
                    n = len([x for x in os.listdir(full) if x.lower().endswith(IMG_EXT)])
                    t.insert(parent, "end", text="   %s  (%d)" % (e, n), values=(full, "kind:" + kind), open=False); continue
                tag = "costume" if os.path.abspath(full) in cpaths else "group"
                iid = t.insert(parent, "end", text=("[+] " if tag == "group" else "") + e, values=(full, tag), open=full in openp)
                add(iid, full)
        add("", self.g.lib)
        if select:
            for i in self._all_items():
                if t.item(i, "values")[0] == select:
                    p = t.parent(i)
                    while p: t.item(p, open=True); p = t.parent(p)
                    t.selection_set(i); t.focus(i); t.see(i); break

    def _all_items(self, parent=""):
        out = []
        for i in self.tree.get_children(parent): out.append(i); out += self._all_items(i)
        return out

    def lib_sel(self):
        sel = self.tree.selection()
        if not sel: return None, None
        v = self.tree.item(sel[0], "values"); return v[0], v[1]

    def name_of(self, path):
        return " / ".join(os.path.relpath(path, self.g.lib).split(os.sep))

    def lib_show(self):
        path, tag = self.lib_sel(); self.lib_files.delete(0, "end"); self.lib_img.configure(image="")
        if not path: return
        self.lib_title.set(self.name_of(path))
        if tag.startswith("kind:"):
            files = sorted(x for x in os.listdir(path) if x.lower().endswith(IMG_EXT))
            self.lib_info.set("%s folder - %d image(s)" % (self.KIND_NAMES.get(tag[5:], ""), len(files)))
            for x in files: self.lib_files.insert("end", x)
            self._lib_dir = path
            if files: self.lib_files.selection_set(0); self.lib_show_file()
            return
        self._lib_dir = None
        subs = [e for e in sorted(os.listdir(path)) if os.path.isdir(os.path.join(path, e)) and not e.startswith((".", "_"))]
        kinds = [cm.kind_of(e)[0] for e in subs]
        if tag == "costume":
            have = [self.KIND_NAMES[k] for k in ("tshirt", "trench", "skull") if k in kinds]
            miss = [self.KIND_NAMES[k] for k in ("tshirt", "trench", "skull") if k not in kinds]
            self.lib_info.set("Costume - has: %s%s" % (", ".join(have) or "nothing yet", ("\nmissing: " + ", ".join(miss)) if miss else ""))
            files = find_skull_images(self.g, self.name_of(path), self._cdirs)
            if files:
                try:
                    key = (files[0], os.path.getmtime(files[0]))
                    if key not in self._lib_imgs: self._lib_imgs[key] = thumb(files[0], (260, 260), "bg")
                    self.lib_img.configure(image=self._lib_imgs[key])
                except Exception: pass
        else:
            self.lib_info.set("Group folder - %d folder(s) inside" % len(subs))
        for e in subs: self.lib_files.insert("end", e + "/")

    def lib_show_file(self):
        if not getattr(self, "_lib_dir", None): return
        i = self.lib_files.curselection()
        if not i: return
        f = os.path.join(self._lib_dir, self.lib_files.get(i[0]))
        try:
            key = (f, os.path.getmtime(f))
            if key not in self._lib_imgs: self._lib_imgs[key] = thumb(f, (260, 260), "bg")
            self.lib_img.configure(image=self._lib_imgs[key])
        except Exception:
            self.lib_img.configure(image="")

    def rename_in_choices(self, old, new):
        """keep the mission choices pointing at a costume that was renamed / moved (new=None: removed -> Default)"""
        changed = 0
        for k, v in list(self.g.state.items()):
            if isinstance(v, str) and (v == old or v.startswith(old + " / ")):
                self.g.state[k] = cm.DEFAULT if new is None else new + v[len(old):]; changed += 1
        if changed: self.g.save(); log("  %d mission choice(s) updated" % changed)

    def report_duplicates(self):
        # v0.9.20 (review): this decodes every library picture - in the background now (it froze the window for
        # seconds after the start and every Rescan with a big library). log() is thread-safe (a queue).
        lib = self.g.lib
        def job():
            try:
                d = find_duplicate_pictures(lib)
            except Exception:
                return
            for a_, b_ in d[:10]:
                log("note: these two pictures are the same: %s  =  %s" % (os.path.relpath(a_, lib), os.path.relpath(b_, lib)))
            if len(d) > 10: log("note: %d more pairs of identical pictures" % (len(d) - 10))
        threading.Thread(target=job, daemon=True).start()

    def rescan(self, select=None):
        if self.busy: log("Busy - rescan after the current job."); return
        self.root.after(200, self.report_duplicates)          # v0.8.8
        self.g = cm.Game(self.game_dir); self._update_paths()
        self.refresh_costumes(); self.refresh_shirt_costumes(); self.refresh_library_tree(select); self.refresh_templates()
        self.refresh_mixes(); self.refresh_builder(); self.refresh_parts()
        self._pv_shown = None; self.mark_library_changed()
        log("Library rescanned: %d costumes." % (len(self.g.costumes()) - 1))

    def _check_name(self, n):
        bad = [c for c in '/\\:*?"<>|' if c in n]
        if not n or bad or n.startswith(("_", ".")):
            info(self.root, "Name", "Not usable as a folder name%s." % (" (no %s)" % " ".join(bad) if bad else "")); return False
        return True

    def lib_new_group(self):
        n = ask_text(self.root, "New group folder", "Name of the new group (like 616, MAX, Movies):")
        if not n or not self._check_name(n): return
        p = os.path.join(self.g.lib, n)
        if os.path.exists(p): info(self.root, "New group", "That folder already exists."); return
        os.makedirs(p); log("New group folder: %s" % n); self.rescan(select=p)

    def lib_new_costume(self):
        path, tag = self.lib_sel()
        parent = self.g.lib if not path else (os.path.dirname(path) if tag.startswith("kind:") else path)
        where = self.name_of(parent) if parent != self.g.lib else "the library itself"
        n = ask_text(self.root, "New costume", "New costume inside %s\n(a costume inside a costume = a variant).\nName:" % where)
        if not n or not self._check_name(n): return
        p = os.path.join(parent, n)
        if os.path.exists(p): info(self.root, "New costume", "That folder already exists."); return
        for sub in ("T-shirt", "Trench Coat", "in-game skull"): os.makedirs(os.path.join(p, sub))
        log("New costume: %s (empty T-shirt, Trench Coat and in-game skull folders)" % self.name_of(p)); self.rescan(select=p)

    def lib_rename(self):
        if self._busy_note(): return
        path, tag = self.lib_sel()
        if not path: return
        old_name = self.name_of(path)
        n = ask_text(self.root, "Rename", "New name for %s:" % old_name, os.path.basename(path))
        if not n or n == os.path.basename(path) or not self._check_name(n): return
        newp = os.path.join(os.path.dirname(path), n)
        if os.path.exists(newp): info(self.root, "Rename", "That name is already used here."); return
        os.rename(path, newp); log("Renamed %s -> %s" % (old_name, n))
        if not tag.startswith("kind:"): self.rename_in_choices(old_name, self.name_of(newp))
        self.rescan(select=newp)

    def lib_move(self):
        path, tag = self.lib_sel()
        if not path or tag.startswith("kind:"): info(self.root, "Move", "Pick a costume or group folder first."); return
        targets = ["(library top level)"]
        for dp, dn, fs in os.walk(self.g.lib):
            dn[:] = [d for d in dn if not d.startswith((".", "_")) and cm.kind_of(d)[0] not in ("tshirt", "trench", "skull")]
            if os.path.abspath(dp).startswith(os.path.abspath(path)): continue
            if dp != self.g.lib and not any(os.path.abspath(dp) == os.path.abspath(p) for p in self._cdirs.values()):
                targets.append(self.name_of(dp))
        t = choose_from(self.root, "Move", "Move %s into which group?" % self.name_of(path), targets)
        if not t: return
        dest_parent = self.g.lib if t == targets[0] else os.path.join(self.g.lib, *t.split(" / "))
        newp = os.path.join(dest_parent, os.path.basename(path))
        if os.path.exists(newp): info(self.root, "Move", "There is already a folder with that name there."); return
        old_name = self.name_of(path); shutil.move(path, newp)
        log("Moved %s -> %s" % (old_name, self.name_of(newp))); self.rename_in_choices(old_name, self.name_of(newp)); self.rescan(select=newp)

    def lib_remove(self):
        if self._busy_note(): return
        path, tag = self.lib_sel()
        if not path: return
        nm = self.name_of(path)
        if not ask(self.root, "Remove", "Move %s into the library's _removed folder?\n(nothing is deleted - move it back in Dolphin if needed;\nmissions that used it go back to Default)" % nm): return
        dest = os.path.join(self.g.lib, "_removed", time.strftime("%Y%m%d-%H%M%S"), *os.path.relpath(path, self.g.lib).split(os.sep))
        os.makedirs(os.path.dirname(dest), exist_ok=True); shutil.move(path, dest)
        log("Moved %s to _removed" % nm)
        if not tag.startswith("kind:"): self.rename_in_choices(nm, None)
        self.rescan(select=os.path.dirname(path))

    def lib_add_images(self):
        path, tag = self.lib_sel()
        if not path: info(self.root, "Add images", "Pick a costume (or its T-shirt / Trench Coat / in-game skull folder) first."); return
        if tag.startswith("kind:"): folder = path
        else:
            kinds = {cm.kind_of(e)[0]: os.path.join(path, e) for e in os.listdir(path) if os.path.isdir(os.path.join(path, e))}
            opts = [self.KIND_NAMES[k] for k in ("tshirt", "trench", "skull")]
            ch = choose_from(self.root, "Add images", "Into which folder of %s?" % self.name_of(path), opts)
            if not ch: return
            k = [k for k, v in self.KIND_NAMES.items() if v == ch][0]
            folder = kinds.get(k) or os.path.join(path, ch); os.makedirs(folder, exist_ok=True)
        added = 0
        while True:
            f = browse(self.root, "Add an image to %s (Cancel when done)" % self.name_of(folder), getattr(self, "_last_add", os.path.expanduser("~")), "file",
                       places=(("Library", self.g.lib), ("Downloads", os.path.expanduser("~/Downloads")), ("Pictures", os.path.expanduser("~/Pictures")), ("Home", os.path.expanduser("~"))))
            if not f: break
            self._last_add = os.path.dirname(f); dst = os.path.join(folder, os.path.basename(f))
            if os.path.exists(dst) and not ask(self.root, "Add images", "%s already exists there. Replace it?\n(the old one goes to _replaced)" % os.path.basename(f)): continue
            if os.path.exists(dst):
                rd = os.path.join(folder, "_replaced"); os.makedirs(rd, exist_ok=True)
                b, e = os.path.splitext(os.path.basename(f)); shutil.move(dst, os.path.join(rd, "%s_%s%s" % (b, time.strftime("%Y%m%d-%H%M%S"), e)))
            shutil.copy2(f, dst); added += 1; log("  added %s to %s" % (os.path.basename(f), self.name_of(folder)))
        if added: self.rescan(select=folder)

    def lib_open(self):
        path, tag = self.lib_sel(); path = path or self.g.lib
        try: subprocess.Popen(["xdg-open", path] if os.name != "nt" else ["explorer", path])
        except Exception as ex: log("Could not open the folder: %s" % ex)

    # ------------------------------------------------------------ v0.2: Settings tab
    def build_settings(self, f):
        st = self.settings
        tpw = ttk.PanedWindow(f, orient="horizontal"); tpw.pack(fill="both", expand=True)
        left = ttk.Frame(tpw, padding=(0, 0, 12, 0)); right = ttk.Frame(tpw, padding=(18, 0, 0, 0))
        tpw.add(left, weight=1); tpw.add(right, weight=2); self.add_pane("settings", tpw, [0.38])
        right = self.scroll_frame(right)                 # v0.8.7: the right column scrolls (Save was below the Deck screen)
        ttk.Button(right, text="Save settings and restart the launcher", style="Accent.TButton",
                   command=self.save_settings_restart).pack(anchor="w", pady=(0, 10))   # v0.8.7: also at the top

        t = ttk.LabelFrame(left, text=" Look ", padding=10); t.pack(fill="x")
        self.set_theme = tk.StringVar(value={"dark": "Dark", "light": "Light"}.get(st.get("theme"), st.get("theme", "Black")))
        for name in THEMES: ttk.Radiobutton(t, text=name, value=name, variable=self.set_theme, command=self.theme_preset).pack(anchor="w")
        self.set_cols = {}
        cols = st.get("colours") or {}
        cbtn = ttk.Button(t, text="Custom colours  >"); cbtn.pack(anchor="w", pady=(10, 0))
        cbox = ttk.Frame(t)                                  # v0.3.2: folded away until "Custom colours" is pressed
        def toggle_cols():
            if cbox.winfo_ismapped(): cbox.pack_forget(); cbtn.configure(text="Custom colours  >")
            else: cbox.pack(after=cbtn, anchor="w", fill="x"); cbtn.configure(text="Custom colours  v")
        cbtn.configure(command=toggle_cols)
        if cols: self.root.after(50, toggle_cols)           # open when you already use your own colours
        ttk.Label(cbox, text="#rrggbb - empty = from the preset", style="Dim.TLabel").pack(anchor="w", pady=(6, 4))
        grid = ttk.Frame(cbox); grid.pack(anchor="w")
        for i, (k, label) in enumerate(COLOUR_KEYS):
            ttk.Label(grid, text=label).grid(row=i, column=0, sticky="w", pady=2)
            v = tk.StringVar(value=cols.get(k, "")); self.set_cols[k] = v
            ttk.Entry(grid, textvariable=v, width=10).grid(row=i, column=1, padx=6)
            sw = tk.Label(grid, width=3, background=v.get() or theme_colours({"theme": self.set_theme.get()})[k], relief="flat"); sw.grid(row=i, column=2)
            v.trace_add("write", lambda *a, v=v, sw=sw, k=k: self._swatch(sw, v.get(), k))
        acc = ttk.Frame(cbox); acc.pack(anchor="w", pady=(8, 0))
        ttk.Label(acc, text="Highlight:").pack(side="left")
        for hexc in ("#4a8bff", "#e53935", "#43a047", "#fb8c00", "#8e24aa", "#e0e0e0"):
            tk.Button(acc, background=hexc, activebackground=hexc, width=2, relief="flat", borderwidth=0,
                      command=lambda h=hexc: self.set_cols["accent"].set(h)).pack(side="left", padx=3)
        self.set_scale = tk.StringVar(value=st.get("text_size", "Normal"))
        ttk.Label(t, text="Text size", style="Dim.TLabel").pack(anchor="w", pady=(10, 2))
        sz = ttk.Frame(t); sz.pack(anchor="w")
        for n in ("Small", "Normal", "Large"): ttk.Radiobutton(sz, text=n, value=n, variable=self.set_scale).pack(side="left", padx=(0, 10))

        o = ttk.LabelFrame(right, text=" Behaviour ", padding=10); o.pack(fill="x")
        self.set_steam = tk.StringVar(value=st.get("steam_show", "always"))
        ttk.Label(o, text="When you press Play in Steam / Gaming Mode:").pack(anchor="w")
        ttk.Radiobutton(o, text="show this launcher first", value="always", variable=self.set_steam).pack(anchor="w")
        ttk.Radiobutton(o, text="start the game straight away", value="never", variable=self.set_steam).pack(anchor="w")
        self.set_big = tk.BooleanVar(value=bool(st.get("big_picker", False)))
        ttk.Checkbutton(o, text="Big-list costume picker in Desktop Mode too (always on from Steam)", variable=self.set_big).pack(anchor="w", pady=(8, 0))
        self.set_unc = tk.BooleanVar(value=bool(st.get("uncompressed_dark", True)))
        ttk.Checkbutton(o, text="Smooth blacks: write mostly dark parts uncompressed (4x the memory)", variable=self.set_unc).pack(anchor="w")
        uf = ttk.Frame(o); uf.pack(anchor="w", padx=(24, 0))
        ttk.Label(uf, text="largest uncompressed size (pixels per side):").pack(side="left")
        self.set_umax = tk.StringVar(value=str(st.get("uncompressed_max", 512)))
        ttk.Combobox(uf, textvariable=self.set_umax, values=["256", "512", "1024"], width=6, state="readonly").pack(side="left", padx=6)
        bf_ = ttk.Frame(o); bf_.pack(anchor="w", padx=(24, 0), pady=(2, 0))
        ttk.Label(bf_, text="smooth-blacks budget per outfit (MB):").pack(side="left")
        self.set_ubud = tk.StringVar(value=str(st.get("uncompressed_budget", 6)))
        ttk.Combobox(bf_, textvariable=self.set_ubud, values=["4", "5", "6", "7", "8", "10", "12", "16", "20", "32"], width=4, state="readonly").pack(side="left", padx=6)
        ttk.Label(o, text="most visible parts first. Over 6 MB: first raise Level memory in the game (Extended menu > HUD & Display > Level memory, then restart the game)",
                  style="Dim.TLabel", wraplength=640).pack(anchor="w", padx=(24, 0))   # v0.8.7: the box was pushed off screen
        self.set_old = tk.BooleanVar(value=False)        # v0.9.15 (user): the old Shirt creator / Library tabs are retired
        self.set_pad = tk.BooleanVar(value=bool(st.get("controller", True)))
        ttk.Checkbutton(o, text="Controller navigation (D-pad, A, B, LB / RB, Start)", variable=self.set_pad).pack(anchor="w")
        wf = ttk.Frame(o); wf.pack(anchor="w", pady=(8, 0))
        ttk.Label(wf, text="Level files rebuilt at the same time (Apply speed):").pack(side="left")
        self.set_workers = tk.StringVar(value=str(st.get("workers", cm.WORKERS)))
        ttk.Combobox(wf, textvariable=self.set_workers, values=["1", "2", "3", "4", "6"], width=4, state="readonly").pack(side="left", padx=6)

        g = ttk.LabelFrame(right, text=" Gaming Mode (Steam launch options) ", padding=10)
        if os.name != "nt": g.pack(fill="x", pady=(12, 0))   # v0.8.7: Deck / Linux only
        launch = os.path.join(HERE, "launch.sh")
        lsfg = " ~/.lsfg" if os.path.exists(os.path.expanduser("~/.lsfg")) else ""   # v0.8.7: only if you use that wrapper
        self.launch_opts = 'WINEDLLOVERRIDES="dinput8,d3d8,SDL3=n,b" bash "%s"%s %%command%%' % (launch, lsfg)   # v0.9.20: bash = no execute flag needed
        ttk.Label(g, text="Put this in the game's Launch Options (Steam > right-click the game > Properties):", wraplength=640).pack(anchor="w")
        e = ttk.Entry(g, width=90); e.insert(0, self.launch_opts); e.pack(anchor="w", fill="x", pady=4)
        ttk.Button(g, text="Copy to clipboard", command=lambda: (self.root.clipboard_clear(), self.root.clipboard_append(self.launch_opts), log("Launch options copied."))).pack(anchor="w")
        ttk.Label(g, text="If you already have launch options, put the launcher's part in front of %command% and keep the rest.", style="Dim.TLabel", wraplength=640).pack(anchor="w", pady=(4, 0))

        ttk.Button(right, text="Save settings and restart the launcher", style="Accent.TButton", command=self.save_settings_restart).pack(anchor="w", pady=(16, 0))

    def _swatch(self, sw, val, k):
        try: sw.configure(background=val if val.startswith("#") and len(val) in (4, 7) else theme_colours({"theme": self.set_theme.get()})[k])
        except tk.TclError: pass

    def theme_preset(self):
        for v in self.set_cols.values(): v.set("")

    def save_settings_restart(self):
        if self.busy: return
        st = load_settings()
        st["theme"] = self.set_theme.get()
        st["colours"] = {k: v.get().strip() for k, v in self.set_cols.items() if v.get().strip().startswith("#")}
        st["text_size"] = self.set_scale.get(); st["steam_show"] = self.set_steam.get()
        st["big_picker"] = self.set_big.get(); st["controller"] = self.set_pad.get(); st["uncompressed_dark"] = self.set_unc.get(); st["old_tabs"] = self.set_old.get(); st["uncompressed_max"] = int(self.set_umax.get() or 512); st["uncompressed_budget"] = int(self.set_ubud.get() or 6); st["workers"] = int(self.set_workers.get() or 4)
        save_settings(st); self.save_layout(); log("Settings saved - restarting.")
        self.exit_code = 10; self.root.quit()                 # main() starts the window again

    # ------------------------------------------------------------ v0.2: new costume from a kit
    def new_costume(self):
        if self.last is None:
            info(self.root, "New costume", "First pick a shirt background and a skull image (steps 2 and 3)."); return
        d = Overlay(self.root); d.withdraw(); d.title("Create the new costume"); d.configure(background=PAL["bg"])
        fr = ttk.Frame(d, padding=16); fr.pack(fill="both", expand=True)
        ticked = [k for k, v in self.var_checks.items() if v.get()]
        ttk.Label(fr, text="New folder in your library with: the skull shirt (T-shirt and Trench Coat)%s%s.\n"
                           "Everything else comes from your base layer (Default HD)." %
                  (", the outfit files of " + ", ".join(ticked) if ticked else "", ", and the skull as in-game skull"),
                  style="Dim.TLabel", justify="left", wraplength=620).grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 10))
        groups = self.plain_folders()
        want = getattr(self, "_variant_of", None); self._variant_of = None
        pre = (want + VARIANT_TAG) if want and (want + VARIANT_TAG) in groups else getattr(self, "_last_group", "")
        grp = tk.StringVar(value=pre if pre in groups else (groups[0] if groups else ""))
        ttk.Label(fr, text="Put it in").grid(row=1, column=0, sticky="w", pady=4)
        gb = ttk.Combobox(fr, textvariable=grp, values=groups, width=40, state="readonly"); gb.grid(row=1, column=1, sticky="w")
        ttk.Label(fr, text="(your groups, and empty folders you made - an empty folder is marked 'empty')", style="Dim.TLabel").grid(row=2, column=1, sticky="w")
        nm = tk.StringVar()
        ttk.Label(fr, text="Name").grid(row=3, column=0, sticky="w", pady=4)
        ne = ttk.Entry(fr, textvariable=nm, width=40); ne.grid(row=3, column=1, sticky="w")
        kb_button(fr, ne).grid(row=3, column=2, sticky="w", padx=6)
        hint = tk.StringVar()
        ttk.Label(fr, textvariable=hint, style="Dim.TLabel", wraplength=520, justify="left").grid(row=4, column=1, sticky="w")
        def upd(*a):
            if grp.get().endswith(VARIANT_TAG):
                hint.set("Type the variant's name - a new folder is made inside that costume (nothing in it is changed).")
            elif grp.get().endswith("  (empty)"):
                hint.set("Leave the name empty to make THIS empty folder the costume (or type a name for a folder inside it).")
            else:
                hint.set("A new folder with this name is made inside it.  Name / Variant = a variant folder.")
        grp.trace_add("write", upd); upd()
        skull_too = tk.BooleanVar(value=True)
        ttk.Checkbutton(fr, text="Use the skull image as its in-game skull", variable=skull_too).grid(row=5, column=0, columnspan=2, sticky="w", pady=(8, 0))
        bf = ttk.Frame(fr); bf.grid(row=6, column=0, columnspan=2, sticky="e", pady=(14, 0))
        ttk.Button(bf, text="Cancel", command=d.destroy).pack(side="right")
        go = lambda: self.create_costume(d, grp.get(), nm.get(), skull_too.get())
        ttk.Button(bf, text="Create", style="Accent.TButton", command=go).pack(side="right", padx=6)
        entry_keys(ne, go); d.bind("<Escape>", lambda e: d.destroy())
        d.transient(self.root); d.deiconify(); d.grab_set(); (ne if not grp.get().endswith("(empty)") else gb).focus_set()

    def plain_folders(self):
        """folders a new costume can go into: group folders and empty folders (anything that is not a costume itself)"""
        cos = {os.path.abspath(p) for p in cm.costume_dirs(self.g.lib).values()}
        out = []
        for dp, dn, fs in os.walk(self.g.lib):
            dn[:] = sorted(x for x in dn if not x.startswith(("_", ".")) and not cm.kind_of(x)[0])
            if dp == self.g.lib or os.path.abspath(dp) in cos: continue
            if any(os.path.abspath(dp).startswith(c + os.sep) for c in cos): continue          # inside a costume
            rel = self.name_of(dp)
            empty = not dn and not any(f.lower().endswith(IMG_EXT) for f in fs)
            out.append(rel + ("  (empty)" if empty else ""))
        out += sorted(n + VARIANT_TAG for n, p in cm.costume_dirs(self.g.lib).items() if n not in cm.VARIANT_LABEL)
        return out

    SHIRT_FILES = {"tshirt": ("T-shirt", "pun_tshirt_04.png"), "trench": ("Trench Coat", "punps2_trench04.png")}

    def copy_variants(self, folders):
        """copy the ticked outfit files into {kind: folder}; texture 04 is skipped (the new skull shirt wins)"""
        n = 0; seen = {}
        for kind, path in self.variant_files():
            f = os.path.basename(path)
            if cm.texture_number(os.path.splitext(f)[0]) == 4:
                log("  outfit file %s skipped (texture 04 = the skull shirt)" % f); continue
            for k in ([kind] if kind else ["tshirt", "trench"]):
                dst = os.path.join(folders[k], f)
                if dst in seen: log("  %s: %s replaces the one from %s" % (f, os.path.basename(os.path.dirname(path)), seen[dst]))
                shutil.copy2(path, dst); seen[dst] = os.path.basename(os.path.dirname(path)); n += 1
            if not kind: log("  %s: name does not say T-shirt or trench - copied into both (the unused one is ignored)" % f)
        return n

    def create_costume(self, dlg, group, name, skull_too=True):
        is_var = group.endswith(VARIANT_TAG)
        group = group.replace(VARIANT_TAG, "").replace("  (empty)", "").strip().strip("/"); name = name.strip().strip("/")
        if is_var and not name:
            info(self.root, "New variant", "Type a name for the variant."); return
        bad = [c for c in '\\:*?"<>|' if c in name]
        if not group or bad:
            info(self.root, "New costume", "Pick where to put it%s." % (" (no %s in the name)" % " ".join(bad) if bad else "")); return
        parts = [p.strip() for p in name.split("/") if p.strip()]
        base_dir = self.costume_dir(group) if is_var else os.path.join(self.g.lib, *group.split(" / "))
        dest = os.path.join(base_dir, *parts)
        if not parts and os.path.abspath(os.path.dirname(dest)) == os.path.abspath(self.g.lib):
            info(self.root, "New costume", "Type a name - a group folder itself cannot be the costume."); return
        if os.path.exists(dest):
            busy = [x for x in os.listdir(dest) if not x.startswith(("_", ".")) and (cm.kind_of(x)[0] or x.lower().endswith(IMG_EXT) or os.path.isdir(os.path.join(dest, x)))]
            if busy:
                info(self.root, "New costume", "That folder is not empty:\n%s\n\nPick an empty folder or type a new name." % self.name_of(dest)); return
            log("Using your empty folder %s" % self.name_of(dest))
        log("Creating %s ..." % os.path.relpath(dest, self.g.lib))
        folders = {}
        for kind, (folder_name, fname) in self.SHIRT_FILES.items():
            folders[kind] = os.path.join(dest, folder_name); os.makedirs(folders[kind], exist_ok=True)
            self.last.save(os.path.join(folders[kind], fname))
        n = self.copy_variants(folders)
        if skull_too and self.sc_skull.get() and os.path.isfile(self.sc_skull.get()):
            sd = os.path.join(dest, "in-game skull"); os.makedirs(sd, exist_ok=True)
            Image.open(self.sc_skull.get()).convert("RGBA").save(os.path.join(sd, "skull.png"))
        fit = dict(self.params(), extend=self.sc_extend.get(), template=self.sc_tpl.get(), skull=self.sc_skull.get(),
                   variants=[k for k, v in self.var_checks.items() if v.get()], quick_start=self.qs_var.get(),
                   saved=time.strftime("%Y%m%d-%H%M%S"))
        json.dump({"_": fit}, open(os.path.join(dest, "fit.json"), "w"), indent=1)
        dlg.destroy(); self._last_group = group if not os.path.isdir(os.path.join(self.g.lib, *group.split(" / "), "T-shirt")) else ""
        new_name = " / ".join([group] + parts)
        self.refresh_costumes(); self.refresh_shirt_costumes(); self.refresh_library_tree(dest)
        self.mark_library_changed()
        log("Created %s: skull shirt + %d outfit file(s)%s. Pick it on the Costumes tab (or Fill all), then Apply." %
            (new_name, n, " + in-game skull" if skull_too else ""))

    # ------------------------------------------------------------ Shirt creator tab
    def build_shirt(self, f):
        # v0.2.2: step by step - quick start, shirt background (templates/Body Armor), skull, outfit (templates/Outfit
        # Variants, ticked = added), fit sliders folded away, then create a new costume (or put the shirt into one)
        self.tpl_root = os.path.join(HERE, "templates"); os.makedirs(self.tpl_root, exist_ok=True)
        ba = os.path.join(self.tpl_root, "Body Armor")
        self.tpl_dir = ba if os.path.isdir(ba) else self.tpl_root
        self.var_dir = os.path.join(self.tpl_root, "Outfit Variants")
        self.qs_file = os.path.join(self.tpl_root, "Quick starts.json")
        spw = ttk.PanedWindow(f, orient="horizontal"); spw.pack(fill="both", expand=True)
        lf = ttk.Frame(spw); prev = ttk.Frame(spw, padding=(14, 0, 0, 0))
        spw.add(lf, weight=1); spw.add(prev, weight=2); self.add_pane("shirt", spw, [0.46])
        cv = tk.Canvas(lf, highlightthickness=0, background=PAL["bg"]); vsb = ttk.Scrollbar(lf, orient="vertical", command=cv.yview)
        c = ttk.Frame(cv); win = cv.create_window((0, 0), window=c, anchor="nw"); cv.configure(yscrollcommand=vsb.set)
        c.bind("<Configure>", lambda e: cv.configure(scrollregion=cv.bbox("all")))
        cv.bind("<Configure>", lambda e: cv.itemconfigure(win, width=e.width))
        # v0.3 wizard: step buttons on top, one page at a time, Back / Next at the bottom
        self.step_bar = ttk.Frame(lf); self.step_bar.pack(side="top", fill="x", pady=(0, 6))
        nav = ttk.Frame(lf, padding=(0, 8, 0, 0)); nav.pack(side="bottom", fill="x")
        self.btn_back = ttk.Button(nav, text="<  Back", command=lambda: self.show_step(self.cur_step - 1)); self.btn_back.pack(side="left")
        self.btn_next = ttk.Button(nav, text="Next  >", style="Accent.TButton", command=lambda: self.show_step(self.cur_step + 1)); self.btn_next.pack(side="right")
        self.step_text = tk.StringVar(); ttk.Label(nav, textvariable=self.step_text, style="Dim.TLabel").pack(side="left", padx=12)
        vsb.pack(side="right", fill="y"); cv.pack(side="left", fill="both", expand=True)
        self.scroll_areas = getattr(self, "scroll_areas", []) + [cv]; self.shirt_canvas, self.shirt_frame = cv, c
        self.pages = []; body = c
        def step(n, text):
            pg = ttk.Frame(body); pg.title = text; self.pages.append(pg)
            ttk.Label(pg, text="Step %s:  %s" % (n, text), style="Focus.TLabel", font=("TkDefaultFont", 12, "bold")).pack(anchor="w", pady=(4, 10))
            return pg

        c = step("1", "Shirt")
        def head(t): ttk.Label(c, text=t, font=("TkDefaultFont", 10, "bold")).pack(anchor="w", pady=(10, 2))
        head("Quick start - a saved recipe (optional)")
        q = ttk.Frame(c); q.pack(anchor="w", fill="x", pady=(2, 12))
        self.qs_var = tk.StringVar()
        self.qs_box = ttk.Combobox(q, textvariable=self.qs_var, state="readonly", width=28); self.qs_box.pack(side="left")
        self.qs_box.bind("<<ComboboxSelected>>", lambda e: self.qs_load())
        ttk.Button(q, text="Save as...", command=self.qs_save).pack(side="left", padx=4)
        ttk.Button(q, text="Delete", command=self.qs_delete).pack(side="left")

        head("Shirt background")
        self.sc_tpl = tk.StringVar(); self.tpl_frame = ttk.Frame(c); self.tpl_frame.pack(anchor="w", pady=(2, 0))

        head("Skull  (transparent background - the preview on the right shows it on the shirt)")
        sk = ttk.Frame(c); sk.pack(anchor="w", fill="x")
        ttk.Button(sk, text="Pick skull image...", command=self.pick_skull).pack(side="left")
        self.sc_skull = tk.StringVar(); self.sc_skull_name = tk.StringVar(value="none yet")
        ttk.Label(sk, textvariable=self.sc_skull_name, style="Dim.TLabel", padding=(10, 0)).pack(side="left")
        self.sc_skull.trace_add("write", lambda *a: self.sc_skull_name.set(os.path.basename(self.sc_skull.get()) or "none yet"))

        head("Outfit  (tick what to add - nothing ticked = the normal outfit)")
        self.var_frame = ttk.Frame(c); self.var_frame.pack(anchor="w", fill="x"); self.var_checks = {}
        self.var_note = tk.StringVar(); ttk.Label(c, textvariable=self.var_note, style="Dim.TLabel", wraplength=520, justify="left").pack(anchor="w")

        c = step("2", "Fit (optional)")
        ttk.Label(c, text="The default fit is usually right - just press Next. Click a slider (or its name),\nthen left / right (D-pad) moves it 0.01, up / down goes to the next slider.",
                  style="Dim.TLabel", justify="left").pack(anchor="w", pady=(0, 6))
        ctrl = ttk.Frame(c, padding=(0, 4, 0, 0)); ctrl.pack(anchor="w", fill="x"); self.fit_frame = ctrl
        self.sl = {}
        specs = [("fabric", "Fabric (0 = flat print)", 0, 1, 1.0),
                 ("x0", "Left edge", 0.0, 0.4, 0.13), ("x1", "Right edge", 0.6, 1.0, 0.83),
                 ("top", "Top", 0.05, 0.4, 0.19), ("aspect", "Squeeze (width : height)", 0.35, 0.9, 0.55),
                 ("bottom", "Teeth end", 0.6, 0.98, 0.90), ("straighten", "Straighten teeth (1 = full, both outfits)", 0, 1, 0.0)]
        # each slider: - / + buttons (0.01), a value box, and the arrow keys / D-pad once it is clicked
        self.scales = []
        for i, (k, label, lo, hi, dflt) in enumerate(specs):
            name_l = ttk.Label(ctrl, text=label); name_l.grid(row=i, column=0, sticky="w", pady=(6, 0))
            v = tk.DoubleVar(value=dflt); self.sl[k] = (v, dflt, lo, hi)
            ttk.Button(ctrl, text="-", width=3, command=lambda k=k: self.nudge(k, -1)).grid(row=i, column=1, sticky="e", pady=(6, 0))
            sc_ = ttk.Scale(ctrl, from_=lo, to=hi, variable=v, length=240, command=lambda e: self.schedule(), takefocus=1)
            sc_.grid(row=i, column=2, sticky="w", padx=4, pady=(6, 0))
            ttk.Button(ctrl, text="+", width=3, command=lambda k=k: self.nudge(k, +1)).grid(row=i, column=3, sticky="w", pady=(6, 0))
            ent = ttk.Entry(ctrl, width=6); ent.grid(row=i, column=4, sticky="w", padx=(6, 0), pady=(6, 0))
            ent.insert(0, "%.2f" % dflt)
            def sync(*a, v=v, ent=ent):
                if ent.focus_get() is not ent:
                    ent.delete(0, "end"); ent.insert(0, "%.2f" % v.get())
            v.trace_add("write", sync)
            ent.bind("<Return>", lambda e, k=k, ent=ent: self.typed(k, ent))
            ent.bind("<FocusOut>", lambda e, k=k, ent=ent: self.typed(k, ent))
            idx = len(self.scales); self.scales.append((sc_, name_l))
            sc_._nudge = lambda dlt, k=k: self.nudge(k, dlt)
            sc_.bind("<Left>", lambda e, k=k: "break" if pad_echo() else (self.nudge(k, -1), "break")[1])
            sc_.bind("<Right>", lambda e, k=k: "break" if pad_echo() else (self.nudge(k, +1), "break")[1])
            sc_.bind("<Shift-Left>", lambda e, k=k: (self.nudge(k, -5), "break")[1])
            sc_.bind("<Shift-Right>", lambda e, k=k: (self.nudge(k, +5), "break")[1])
            sc_.bind("<Up>", lambda e, i=idx: "break" if pad_echo() else (self.focus_scale(i - 1), "break")[1])
            sc_.bind("<Down>", lambda e, i=idx: "break" if pad_echo() else (self.focus_scale(i + 1), "break")[1])
            sc_.bind("<Button-1>", lambda e, s_=sc_: s_.focus_set(), add="+")
            sc_.bind("<FocusIn>", lambda e, l=name_l: l.configure(style="Focus.TLabel"))
            sc_.bind("<FocusOut>", lambda e, l=name_l: l.configure(style="TLabel"))
            name_l.bind("<Button-1>", lambda e, s_=sc_: s_.focus_set())
        r = len(specs)
        self.sc_extend = tk.BooleanVar(value=True)
        ttk.Checkbutton(ctrl, text="Lengthen the teeth bars to 'Teeth end'", variable=self.sc_extend, command=self.schedule).grid(row=r, column=0, columnspan=5, sticky="w", pady=(6, 0))
        ttk.Button(ctrl, text="Reset (Fit A)", command=self.reset_sliders).grid(row=r + 1, column=0, sticky="w", pady=(6, 0))

        c = step("3", "Make it")
        ttk.Button(c, text="Create the new costume...", style="Big.Accent.TButton", command=self.new_costume).pack(anchor="w")
        ttk.Label(c, text="asks for the group and the name, then makes the folder in your library", style="Dim.TLabel").pack(anchor="w", pady=(2, 0))
        ex = ttk.LabelFrame(c, text=" or put just the shirt into an existing costume ", padding=8); ex.pack(anchor="w", fill="x", pady=(12, 8))
        self.sc_costume = tk.StringVar()
        self.sc_costume_box = CostumeMenu(ex, self.sc_costume, [], command=lambda n: self.costume_picked(), with_default=False, width=34)
        self.root.after(10, lambda: self.hook_picker(self.sc_costume_box)); self.sc_costume_box.pack(anchor="w")
        self.sc_with_outfit = tk.BooleanVar(value=False)
        ttk.Checkbutton(ex, text="also add the ticked outfit to that costume\n(copies those coat / shirt textures into its folders)", variable=self.sc_with_outfit).pack(anchor="w", pady=(4, 0))
        self.sc_skull_ingame = tk.BooleanVar(value=True)
        ttk.Checkbutton(ex, text="also use the skull image as its in-game skull", variable=self.sc_skull_ingame).pack(anchor="w", pady=(2, 0))
        ttk.Button(ex, text="Save the shirt there", command=self.save_shirt).pack(anchor="w", pady=(6, 0))
        self.sc_targets = tk.StringVar()
        ttk.Label(ex, textvariable=self.sc_targets, wraplength=520, style="Dim.TLabel", justify="left").pack(anchor="w", pady=(6, 0))

        self.cv_tex = tk.Label(prev, background=PAL["bg"]); self.cv_tex.grid(row=0, column=0, padx=(0, 40), sticky="n")
        self.cv_chest = tk.Label(prev, background=PAL["bg"]); self.cv_chest.grid(row=0, column=1, sticky="n")
        ttk.Label(prev, text="Texture file", font=("TkDefaultFont", 11, "bold")).grid(row=1, column=0, padx=(0, 40), pady=(6, 0))
        ttk.Label(prev, text="the picture that is saved\n(squeezed - the model stretches it)", style="Dim.TLabel", justify="center").grid(row=2, column=0, padx=(0, 40))
        ttk.Label(prev, text="IN GAME (approx.)", font=("TkDefaultFont", 11, "bold"), style="Focus.TLabel").grid(row=1, column=1, pady=(6, 0))
        ttk.Label(prev, text="how the skull looks on\nthe Punisher's chest", style="Dim.TLabel", justify="center").grid(row=2, column=1)
        self._job = None; self._imgs = None; self.last = None
        for i, pg in enumerate(self.pages):
            b = ttk.Button(self.step_bar, text="%d  %s" % (i + 1, pg.title), command=lambda i=i: self.show_step(i)); b.pack(side="left", padx=(0, 4))
            pg.chip = b
        self.cur_step = 0; self.show_step(0)
        self.refresh_shirt_costumes(); self.refresh_templates()

    def show_step(self, i):
        i = max(0, min(len(self.pages) - 1, i)); self.cur_step = i
        for j, pg in enumerate(self.pages):
            if j == i: pg.pack(anchor="w", fill="x")
            else: pg.pack_forget()
            pg.chip.configure(style="Accent.TButton" if j == i else "TButton")
        self.btn_back.configure(state="normal" if i > 0 else "disabled")
        self.btn_next.configure(state="normal" if i < len(self.pages) - 1 else "disabled")
        self.step_text.set("Step %d of %d" % (i + 1, len(self.pages)))
        self.shirt_canvas.yview_moveto(0)
        if i > 0 and not self.sc_skull.get(): log("Pick a skull image first (step 1).")

    # ---- shirt backgrounds, outfit variants, quick starts
    def refresh_templates(self):
        for w in self.tpl_frame.winfo_children(): w.destroy()
        t = sorted((f for f in os.listdir(self.tpl_dir) if f.lower().endswith(".png")), key=lambda f: os.path.splitext(f)[0].lower()) if os.path.isdir(self.tpl_dir) else []
        for f in t:
            ttk.Radiobutton(self.tpl_frame, text=os.path.splitext(f)[0], value=f, variable=self.sc_tpl,
                            command=lambda: (setattr(self, "_imgs", None), self.schedule())).pack(side="left", padx=(0, 14))
        if t and self.sc_tpl.get() not in t: self.sc_tpl.set(t[0])
        pass                                              # v0.9.17: the old Shirt creator's message is gone (tab retired)
        self.refresh_variants(); self.refresh_quickstarts()

    def variant_group(self, name):
        return re.sub(r"\s+\d+$", "", name).strip().lower()           # "White Gloves & Boots 2" -> same group as without the 2

    def refresh_variants(self):
        keep = {k for k, v in self.var_checks.items() if v.get()}
        for w in self.var_frame.winfo_children(): w.destroy()
        self.var_checks = {}
        names = sorted(d for d in os.listdir(self.var_dir) if os.path.isdir(os.path.join(self.var_dir, d)) and not d.startswith((".", "_"))) if os.path.isdir(self.var_dir) else []
        for n in names:
            v = tk.BooleanVar(value=n in keep); self.var_checks[n] = v
            files = [f for f in os.listdir(os.path.join(self.var_dir, n)) if f.lower().endswith(IMG_EXT)]
            ttk.Checkbutton(self.var_frame, text="%s   (%d)" % (n, len(files)), variable=v,
                            command=lambda n=n: self.variant_ticked(n)).pack(anchor="w")
        self.var_note.set("" if names else "No outfit variants - put folders into %s" % self.var_dir)

    def variant_ticked(self, n, checks=None):
        """one at a time inside a group (White Gloves & Boots / White Gloves & Boots 2)"""
        main = checks is not None; checks = checks if main else self.var_checks
        if checks[n].get():
            g = self.variant_group(n)
            for o, v in checks.items():
                if o != n and self.variant_group(o) == g and v.get(): v.set(False); log("  outfit: %s replaces %s (one at a time)" % (n, o))
        if main: return
        ticked = [k for k, v in checks.items() if v.get()]
        self.var_note.set(("Adds: " + ", ".join(ticked)) if ticked else "")

    def variant_files(self):
        """[(kind or None, path)] of the ticked variants; kind from the file name (T-shirt / Trench Coat), None = both"""
        out = []
        for n, v in self.var_checks.items():
            if not v.get(): continue
            d = os.path.join(self.var_dir, n)
            for f in sorted(os.listdir(d)):
                full = os.path.join(d, f)
                if not os.path.isfile(full) or not f.lower().endswith(IMG_EXT): continue
                low = f.lower()
                kind = "trench" if "trench" in low else ("tshirt" if ("tshirt" in low or "t-shirt" in low or "t_shirt" in low) else None)
                out.append((kind, full))
        return out

    def load_qs(self):
        try: return json.load(open(self.qs_file)) if os.path.isfile(self.qs_file) else {}
        except Exception: return {}

    def refresh_quickstarts(self):
        names = sorted(self.load_qs()); self.qs_box.configure(values=names)
        if self.qs_var.get() not in names: self.qs_var.set("")

    def qs_save(self):
        n = ask_text(self.root, "Quick start", "Name for this quick start\n(shirt background + ticked outfit + fit):", self.qs_var.get())
        if not n: return
        qs = self.load_qs()
        qs[n] = dict(self.params(), extend=self.sc_extend.get(), template=self.sc_tpl.get(),
                     variants=[k for k, v in self.var_checks.items() if v.get()])
        json.dump(qs, open(self.qs_file, "w"), indent=1); self.refresh_quickstarts(); self.qs_var.set(n)
        log("Quick start saved: %s" % n)

    def qs_load(self):
        q = self.load_qs().get(self.qs_var.get())
        if not q: return
        for k, (v, d, lo, hi) in self.sl.items():
            if k in q: v.set(q[k])
        self.sc_extend.set(bool(q.get("extend", True)))
        if q.get("template") and os.path.isfile(os.path.join(self.tpl_dir, q["template"])): self.sc_tpl.set(q["template"]); self._imgs = None
        for k, v in self.var_checks.items(): v.set(k in q.get("variants", []))
        missing = [x for x in q.get("variants", []) if x not in self.var_checks]
        ticked = [k for k, v in self.var_checks.items() if v.get()]
        self.var_note.set(("Adds: " + ", ".join(ticked)) if ticked else "")
        log("Quick start loaded: %s%s" % (self.qs_var.get(), (" (missing outfit folders: %s)" % ", ".join(missing)) if missing else ""))
        self.schedule()

    def qs_delete(self):
        n = self.qs_var.get()
        if not n or not ask(self.root, "Quick start", "Delete the quick start '%s'?" % n): return
        qs = self.load_qs(); qs.pop(n, None); json.dump(qs, open(self.qs_file, "w"), indent=1); self.refresh_quickstarts()

    def refresh_shirt_costumes(self):
        self._dirs = cm.costume_dirs(self.g.lib)          # walked once; costume_dir() reads from this
        names = [n for n in self.g.costumes() if n != cm.DEFAULT]
        self.sc_costume_box.set_names(names)

    def nudge(self, k, steps):
        v, d, lo, hi = self.sl[k]
        v.set(round(min(hi, max(lo, v.get() + 0.01 * steps)), 2)); self.schedule()

    def typed(self, k, ent):
        v, d, lo, hi = self.sl[k]
        try: v.set(round(min(hi, max(lo, float(ent.get().replace(",", ".")))), 2)); self.schedule()
        except ValueError: pass
        ent.delete(0, "end"); ent.insert(0, "%.2f" % v.get())

    def focus_scale(self, i):
        if 0 <= i < len(self.scales): self.scales[i][0].focus_set()

    def costume_dir(self, name):
        return self._dirs.get(name) if getattr(self, "_dirs", None) is not None else cm.costume_dirs(self.g.lib).get(name)

    def fit_file(self, name):
        d = self.costume_dir(name)
        return os.path.join(d, "fit.json") if d else None

    def costume_picked(self):
        name = self.sc_costume.get(); fp = self.fit_file(name)
        if fp and os.path.isfile(fp):                      # this costume's saved shirt settings
            try:
                fit = json.load(open(fp)); key = cm.VARIANT_LABEL.get(name, "") or "_"
                fit = fit.get(key, fit)
                for k, (v, d, lo, hi) in self.sl.items():
                    if k in fit: v.set(fit[k])
                if "extend" in fit: self.sc_extend.set(bool(fit["extend"]))
                if fit.get("template") and os.path.isfile(os.path.join(self.tpl_dir, fit["template"])): self.sc_tpl.set(fit["template"])
                if fit.get("skull") and os.path.isfile(fit["skull"]): self.sc_skull.set(fit["skull"])
                if "variants" in fit:
                    for k, v in self.var_checks.items(): v.set(k in fit["variants"])
                log("Shirt creator: loaded the saved settings of %s" % name)
            except Exception as ex:
                log("Shirt creator: could not read %s (%s)" % (fp, ex))
        self.show_targets(); self.schedule()

    def targets(self, name):
        """{kind: folder} the costume's skull shirt goes into (T-shirt and Trench Coat)"""
        d = self.costume_dir(name)
        if not d: return {}
        out = {}
        for sub in cm.chosen_kind_folders(d, cm.VARIANT_LABEL.get(name, "")):
            k = cm.kind_of(sub)[0]
            if k in ("tshirt", "trench"): out[k] = os.path.join(d, sub)
        return out

    def show_targets(self):
        name = self.sc_costume.get(); t = self.targets(name)
        if not name: self.sc_targets.set(""); return
        lines = []
        for k, lab in (("tshirt", "T-shirt"), ("trench", "Trench Coat")):
            lines.append("%s: %s" % (lab, os.path.relpath(t[k], self.g.lib) if k in t else "no folder - will be created"))
        shared = [n for n in self.g.costumes() if n not in (name, cm.DEFAULT) and any(p in self.targets(n).values() for p in t.values())]
        if shared: lines.append("Shared with: %s (same folder)" % ", ".join(shared))
        self.sc_targets.set("Saves into\n" + "\n".join(lines))

    def pick_skull(self):
        cur = self.sc_skull.get(); d = self.costume_dir(self.sc_costume.get()) if self.sc_costume.get() else None
        start = os.path.dirname(cur) if cur and os.path.isfile(cur) else (d or self.g.lib)
        p = browse(self.root, "Skull image (transparent background)", start, "file",
                   places=(("Library", self.g.lib), ("This costume", d), ("Downloads", os.path.expanduser("~/Downloads")),
                           ("Pictures", os.path.expanduser("~/Pictures")), ("Home", os.path.expanduser("~"))))
        if p: self.sc_skull.set(p); self._imgs = None; self.schedule()

    def reset_sliders(self):
        for k, (v, d, lo, hi) in self.sl.items(): v.set(d)
        self.sc_extend.set(True); self.schedule()

    def params(self):
        p = {k: round(float(v.get()), 2) for k, (v, d, lo, hi) in self.sl.items()}
        return p

    def schedule(self):
        if self._job: self.root.after_cancel(self._job)
        self._job = self.root.after(200, self.render)

    def render(self):
        self._job = None
        skull, tpl = self.sc_skull.get(), self.sc_tpl.get()
        if not (skull and os.path.isfile(skull) and tpl): return
        p = self.params()
        try:
            key = (skull, tpl)
            if not self._imgs or self._imgs[0] != key:
                self._imgs = (key, Image.open(skull).convert("RGBA"), Image.open(os.path.join(self.tpl_dir, tpl)).convert("RGB"))
            _, sk_img, shirt = self._imgs
            W, H = shirt.size
            sk, x, y, natural = sc.place(sk_img, W, H, x0=p["x0"], x1=p["x1"], top=p["top"], aspect=p["aspect"],
                                         bottom=p["bottom"], extend=self.sc_extend.get())
            sk, x, y = sc.straighten(sk, x, y, W, H, p.get("straighten", 0.0))     # v0.9.0: measured map
            sk = sc.fabric_shade(shirt, sk, x, y, p["fabric"])
            out = shirt.convert("RGBA"); out.alpha_composite(sk, (x, y)); out = out.convert("RGB")
            self.last = out
            hh = 470
            t1 = out.resize((int(W * hh / H), hh), Image.LANCZOS)
            chest = sc.chest_view(out, (x, y, sk.width, sk.height, natural), p["aspect"])
            t2 = chest.resize((int(chest.width * hh / chest.height), hh), Image.LANCZOS)
            self._tk1, self._tk2 = ImageTk.PhotoImage(t1), ImageTk.PhotoImage(t2)
            self.cv_tex.configure(image=self._tk1); self.cv_chest.configure(image=self._tk2)
        except Exception as ex:
            log("Shirt creator: %s" % ex)

    def save_shirt(self):
        name = self.sc_costume.get()
        if not name: info(self.root, "Shirt creator", "Pick a costume first."); return
        if self.last is None: info(self.root, "Shirt creator", "Pick a skull image and a template first."); return
        d = self.costume_dir(name); t = self.targets(name)
        if not d: log("Costume folder not found: %s" % name); return
        stamp = time.strftime("%Y%m%d-%H%M%S"); done = []
        files = {"tshirt": ("T-shirt", "pun_tshirt_04.png", "pun_tshirt_04.tga"),
                 "trench": ("Trench Coat", "punps2_trench04.png", "punps2_trench04.tga")}
        log("Saving the shirt for %s - checking which files the costume uses now ..." % name); self.root.update_idletasks()
        try:
            sets = cm.build_mapping(self.g, name, d)       # which files the game uses now (to move the old skull shirt away)
        except Exception as ex:
            sets = {}; log("note: could not read the current files (%s)" % ex)
        for kind, (folder_name, fname, tex) in files.items():
            folder = t.get(kind) or os.path.join(d, folder_name)
            os.makedirs(folder, exist_ok=True)
            sub = os.path.basename(folder); old = []
            for f, v in sets.get(sub, {}).get("files", {}).items():
                if v.get("texture") == tex or f.lower() == fname: old.append(f)
            if os.path.isfile(os.path.join(folder, fname)) and fname not in old: old.append(fname)
            for f in old:                                   # never deleted: moved to _replaced (folders starting with _ are ignored)
                src = os.path.join(folder, f)
                if not os.path.isfile(src): continue
                dst_dir = os.path.join(folder, "_replaced"); os.makedirs(dst_dir, exist_ok=True)
                base, ext = os.path.splitext(f)
                shutil.move(src, os.path.join(dst_dir, "%s_%s%s" % (base, stamp, ext)))
                log("  moved the old %s to %s/_replaced" % (f, sub))
            self.last.save(os.path.join(folder, fname)); done.append("%s/%s" % (sub, fname))
        if self.sc_skull_ingame.get() and self.sc_skull.get() and os.path.isfile(self.sc_skull.get()):
            sd = None
            for e in os.listdir(d):                                        # its own in-game skull folder, or a new one
                if os.path.isdir(os.path.join(d, e)) and cm.kind_of(e)[0] == "skull": sd = os.path.join(d, e); break
            sd = sd or os.path.join(d, "in-game skull"); os.makedirs(sd, exist_ok=True)
            for f in os.listdir(sd):
                if f.lower().endswith(IMG_EXT) and os.path.isfile(os.path.join(sd, f)):
                    rd = os.path.join(sd, "_replaced"); os.makedirs(rd, exist_ok=True)
                    b_, e_ = os.path.splitext(f); shutil.move(os.path.join(sd, f), os.path.join(rd, "%s_%s%s" % (b_, stamp, e_)))
            Image.open(self.sc_skull.get()).convert("RGBA").save(os.path.join(sd, "skull.png")); done.append("in-game skull")
        if self.sc_with_outfit.get() and any(v.get() for v in self.var_checks.values()):
            folders = {k: (t.get(k) or os.path.join(d, fn)) for k, (fn, _, _) in files.items()}
            n = self.copy_variants(folders); done.append("%d outfit file(s)" % n)
        fp = os.path.join(d, "fit.json")
        try: allfit = json.load(open(fp)) if os.path.isfile(fp) else {}
        except Exception: allfit = {}
        fit = dict(self.params(), extend=self.sc_extend.get(), template=self.sc_tpl.get(), skull=self.sc_skull.get(), saved=stamp,
                   variants=[k for k, v in self.var_checks.items() if v.get()] if self.sc_with_outfit.get() else [])
        allfit[cm.VARIANT_LABEL.get(name, "") or "_"] = fit
        json.dump(allfit, open(fp, "w"), indent=1)
        self.mark_library_changed()
        log("Shirt saved for %s: %s. Settings in %s. Press Apply (Costumes tab) to put it in the game." % (name, ", ".join(done), os.path.relpath(fp, self.g.lib)))
        self.show_targets()


def has_pun(folder):
    """v0.9.20 (review): pun.exe in any letter case (Linux file names are case-sensitive; launch.sh matches any case)"""
    try: return bool(folder) and os.path.isdir(folder) and any(f.lower() == "pun.exe" for f in os.listdir(folder))
    except OSError: return False

def main():
    steam = "--steam" in sys.argv; STEAM_MODE[0] = steam
    # v0.4.1: start-up trace (Gaming Mode problems show up here even if no window ever appears)
    log("=== start %s  steam=%s  DISPLAY=%s  WAYLAND=%s  SteamGameId=%s  python=%s" % (
        time.strftime("%Y-%m-%d %H:%M:%S"), steam, os.environ.get("DISPLAY"), os.environ.get("WAYLAND_DISPLAY"),
        os.environ.get("SteamGameId") or os.environ.get("SteamAppId"), sys.executable))
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    s = load_settings()
    if steam and s.get("steam_show", "always") == "never":
        sys.exit(0)                                    # straight into the game
    game = s.get("game") or (args[0] if args else DEFAULT_GAME)
    from_steam = next((a.split("=", 1)[1] for a in sys.argv[1:] if a.startswith("--game=")), "")   # v0.8.7: from launch.sh
    # v0.9.20 (review): from Steam, the copy of the game Steam is starting wins - with two installs the costumes used to
    # go into the saved (other) copy
    if has_pun(from_steam) and (steam or not has_pun(game)):
        game = from_steam                              # the folder of the pun.exe Steam starts - no folder dialog in Gaming Mode
    first = True
    while True:
        if not first:                                  # v0.9.20 (review): "Game folder..." restarts with code 10 - use the new choice
            game = load_settings().get("game") or game
        first = False
        t0 = time.time()
        try: root = tk.Tk()
        except Exception as ex:
            log("could not open a window: %s - starting the game without the launcher" % ex); sys.exit(0)
        log("window toolkit ready (%.1f s)" % (time.time() - t0))
        def _cb_error(exc, val, tb, r=root):               # v0.8.9 (review): a failing button showed nothing on the Deck
            log("ERROR in a button: %s: %s" % (exc.__name__, val)); log("".join(traceback.format_exception(exc, val, tb)))
            try: info(r, "Something went wrong", "%s: %s\n\nDetails are in launcher.log (launcher folder)." % (exc.__name__, val))
            except Exception: pass
        root.report_callback_exception = _cb_error
        root.bind("<Map>", lambda e, r=root, t=t0: (e.widget is r and not getattr(r, "_mapped", False)
                                                    and (setattr(r, "_mapped", True), log("window is on screen (%.1f s)" % (time.time() - t)))))
        if not has_pun(game):
            if steam:                                  # v0.9.20 (review): no folder dialog in Gaming Mode - start the game instead
                log("no game folder with pun.exe known - starting the game without the launcher (run it once in Desktop Mode)")
                try: root.destroy()
                except tk.TclError: pass
                sys.exit(0)
            root.withdraw()
            game = filedialog.askdirectory(title="The Punisher game folder (the one with pun.exe)", initialdir=os.path.expanduser("~/Games"))
            if not has_pun(game):
                messagebox.showerror("Costume Launcher", "pun.exe not found in that folder."); sys.exit(1)
            root.deiconify()
        s = load_settings(); s["game"] = game; save_settings(s)
        app = Launcher(root, game, play=steam)
        root.mainloop()
        code = getattr(app, "exit_code", 0)
        try:                                            # v0.8.3: stop pending timers first (no "invalid command name ... poll")
            for job in root.tk.call("after", "info"): root.after_cancel(job)
        except tk.TclError: pass
        try: root.destroy()
        except tk.TclError: pass
        if code != 10: sys.exit(code)                  # 10 = settings changed: open the window again


if __name__ == "__main__":
    main()
