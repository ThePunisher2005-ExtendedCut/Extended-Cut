// The Punisher: Extended Cut (formerly PunisherPad) - controller support and enhancements for The Punisher (2005, PC) - v0.29
// Loads as an .asi through Ultimate ASI Loader (dinput8.dll). Needs SDL3.dll (x86) next to pun.exe.
//
// v0.1 approach (simple, safe, keeps the real mouse working):
//   * right stick  -> added to the game's DirectInput mouse movement (camera / aim)
//   * triggers etc -> mouse buttons / wheel, injected into the game's mouse data
//   * buttons and left stick -> keys, sent through the game's own key-event function
// Everything is mapped in ExtendedCut.ini. Analog movement, analog interrogations, button
// prompts and menus come in later versions once the game's own action functions are hooked.

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstdlib>
#include <cctype>
#include <intrin.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------- logging
static char g_dir[MAX_PATH];        // folder of this .asi (ini + log live here)
static FILE* g_log = nullptr;
static void Log(const char* fmt, ...)
{
    if (!g_log) return;
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
}

// ---------------------------------------------------------------- config
enum TargetKind { T_NONE, T_KEY, T_MOUSE_BUTTON, T_WHEEL };
struct Target { TargetKind kind = T_NONE; int value = 0; };

static Target ParseTarget(const char* s)
{
    Target t;
    char u[64]; size_t i = 0;
    for (; s[i] && i < sizeof(u) - 1; ++i) u[i] = (char)toupper((unsigned char)s[i]);
    u[i] = 0;
    // trim trailing spaces / comments
    for (char* p = u; *p; ++p) if (*p == ';' || *p == '#') { *p = 0; break; }
    for (int n = (int)strlen(u) - 1; n >= 0 && (u[n] == ' ' || u[n] == '\t'); --n) u[n] = 0;

    if (!u[0] || !strcmp(u, "NONE")) return t;
    if (!strcmp(u, "MOUSE1")) { t.kind = T_MOUSE_BUTTON; t.value = 0; return t; }
    if (!strcmp(u, "MOUSE2")) { t.kind = T_MOUSE_BUTTON; t.value = 1; return t; }
    if (!strcmp(u, "MOUSE3")) { t.kind = T_MOUSE_BUTTON; t.value = 2; return t; }
    if (!strcmp(u, "WHEELUP"))   { t.kind = T_WHEEL; t.value = 120; return t; }
    if (!strcmp(u, "WHEELDOWN")) { t.kind = T_WHEEL; t.value = -120; return t; }

    struct { const char* n; int vk; } names[] = {
        {"SPACE",VK_SPACE},{"ENTER",VK_RETURN},{"ESCAPE",VK_ESCAPE},{"ESC",VK_ESCAPE},{"TAB",VK_TAB},
        {"BACKSPACE",VK_BACK},{"SHIFT",VK_SHIFT},{"LSHIFT",VK_LSHIFT},{"RSHIFT",VK_RSHIFT},
        {"CTRL",VK_CONTROL},{"LCTRL",VK_LCONTROL},{"RCTRL",VK_RCONTROL},{"ALT",VK_MENU},
        {"UP",VK_UP},{"DOWN",VK_DOWN},{"LEFT",VK_LEFT},{"RIGHT",VK_RIGHT},
        {"INSERT",VK_INSERT},{"DELETE",VK_DELETE},{"HOME",VK_HOME},{"END",VK_END},
        {"PAGEUP",VK_PRIOR},{"PAUSE",VK_PAUSE},{"PAGEDOWN",VK_NEXT},{"CAPSLOCK",VK_CAPITAL},
        {"NUMPAD0",VK_NUMPAD0},{"NUMPAD1",VK_NUMPAD1},{"NUMPAD2",VK_NUMPAD2},{"NUMPAD3",VK_NUMPAD3},
        {"NUMPAD4",VK_NUMPAD4},{"NUMPAD5",VK_NUMPAD5},{"NUMPAD6",VK_NUMPAD6},{"NUMPAD7",VK_NUMPAD7},
        {"NUMPAD8",VK_NUMPAD8},{"NUMPAD9",VK_NUMPAD9},
    };
    for (auto& n : names) if (!strcmp(u, n.n)) { t.kind = T_KEY; t.value = n.vk; return t; }
    if (u[0] == 'F' && u[1] >= '1' && u[1] <= '9') {           // F1..F12
        int f = atoi(u + 1);
        if (f >= 1 && f <= 12) { t.kind = T_KEY; t.value = VK_F1 + f - 1; return t; }
    }
    if (!u[1] && ((u[0] >= 'A' && u[0] <= 'Z') || (u[0] >= '0' && u[0] <= '9'))) { t.kind = T_KEY; t.value = u[0]; return t; }
    Log("ini: unknown key name '%s' (ignored)", s);
    return t;
}

// pad buttons we read (SDL naming: SOUTH = A / Cross, EAST = B / Circle, WEST = X / Square, NORTH = Y / Triangle)
enum PadInput {
    P_SOUTH, P_EAST, P_WEST, P_NORTH, P_LB, P_RB, P_LT, P_RT, P_L3, P_R3,
    P_BACK, P_START, P_DUP, P_DDOWN, P_DLEFT, P_DRIGHT,
    P_LS_UP, P_LS_DOWN, P_LS_LEFT, P_LS_RIGHT,
    P_COUNT
};
static const char* kPadNames[P_COUNT] = {
    "A","B","X","Y","LB","RB","LT","RT","L3","R3",
    "Back","Start","DpadUp","DpadDown","DpadLeft","DpadRight",
    "LeftStickUp","LeftStickDown","LeftStickLeft","LeftStickRight",
};

struct Config {
    Target map[P_COUNT];
    Target menuMap[P_COUNT];          // [Menu] overrides used while a menu is open
    Target hostageMap[P_COUNT];       // [Hostage] overrides used while holding a human shield
    Target interMap[P_COUNT];         // v0.11 [Interrogation] overrides used while an interrogation runs
    int   helpKeyScan = 0;            // v0.11 [Settings] HelpKey: the game's Help action is moved from F1 to this key
    float lookSensX = 900.0f;     // mouse counts per second at full right-stick deflection
    float lookSensY = 600.0f;
    float lookCurve = 2.0f;       // 1 = linear, 2 = finer aim near the centre
    float deadzone  = 0.15f;      // right stick
    float moveDeadzone = 0.35f;   // left stick -> keys threshold
    float triggerThreshold = 0.35f;
    bool  invertY = false;
    float interrogationBoost = 4.0f;   // right stick multiplier during the 4 standard interrogations (choke, gun, punch, face)
    float envInterrogationBoost = 12.0f; // ... during environment interrogations (wood chipper, blowtorch, grinder ...)
    int   keyMethod = 1;               // 1 = send real key messages to the game window, 0 = call the game's key function directly
    int   logKeys = 1;                 // write key events to the log (first 300)
    int   nativeLeftStick = 1;         // feed the left stick to the game's own (console) stick reader
    int   nativeRightStick = 0;        // same for the right stick (0 = right stick stays on the mouse path)
    int   leftStickKeys = 0;           // also send W/A/S/D from the left stick (0 when the native stick works)
    float nativeDeadzone = 0.15f;
    float interrogationCurve = 1.0f;   // response curve during interrogations (1 = linear: half tilt = half force)
    int   logInterrogation = 1;
    int   swapTriggersDual = 1;      // while dual-wielding: RT = right-hand gun, LT = left-hand gun (console)
    float lookScale = 1.0f;          // v0.29: one look sensitivity for the menu (multiplies LookSensitivityX / Y)
    int   consoleStick = 1;          // v0.28: interrogations read the right stick like the PS2 / Xbox (mouse / trackpad still work)
    int   interrogationLeftStick = 1;  // v0.28.1: like the Xbox, the left stick works too (the stick moved last wins)
    int   interrogationNativeStick = 0;  // during interrogations the right stick goes to the console stick reader
                                         // (the interrogation code reads it at 0x4D7308) instead of the mouse        // log mouse/trackpad and stick force during interrogations
    int   invertNativeY = 0;
    int   promptStyle = 4;             // v0.10 [Prompts] Style: 0 Keyboard, 1 Xbox, 2 PlayStation, 3 SteamDeck, 4 Auto
    int   stickPicture = 1;            // v0.10 [Prompts] StickPicture: stick picture instead of W/A/S/D on the interrogation choice
    int   logPrompts = 1;              // v0.10 [Prompts] LogPrompts: log the first 60 button prompts
    int   rumble = 1;                  // v0.14 [Rumble] Enabled
    float rumbleStrength = 1.0f;       // v0.14 [Rumble] Strength (0.0 - 2.0, scales everything)
    int   rumbleWeapons = 1;           // v0.14 [Rumble] Weapons: your shots, per weapon (console force feedback names)
    int   rumbleShake = 1;             // v0.14 [Rumble] Explosions: whenever the game shakes the camera
    int   rumbleMelee = 1;             // v0.14.1 [Rumble] Melee: quick kills, interrogations, punches, kicks
    int   rumbleHits = 1;              // v0.15 [Rumble] Hits: when the Punisher loses health
    float qkFrameRate = 30.0f;         // v0.16 [Rumble] QuickKillFPS: animation frames per second for the kill moment
    float hitMinimum = 1.0f;           // v0.17 [Rumble] HitMinimum: smallest health loss that rumbles
    int   logRumble = 1;               // v0.14 [Rumble] Log: log the first rumbles
} g_cfg;

static void LoadConfig()
{
    char ini[MAX_PATH]; snprintf(ini, sizeof(ini), "%s\\ExtendedCut.ini", g_dir);
    char buf[64];
    for (int i = 0; i < P_COUNT; ++i) {
        GetPrivateProfileStringA("Buttons", kPadNames[i], "", buf, sizeof(buf), ini);
        g_cfg.map[i] = ParseTarget(buf);
        GetPrivateProfileStringA("Menu", kPadNames[i], "", buf, sizeof(buf), ini);
        g_cfg.menuMap[i] = ParseTarget(buf);
        GetPrivateProfileStringA("Hostage", kPadNames[i], "", buf, sizeof(buf), ini);
        g_cfg.hostageMap[i] = ParseTarget(buf);
        GetPrivateProfileStringA("Interrogation", kPadNames[i], "", buf, sizeof(buf), ini);
        g_cfg.interMap[i] = ParseTarget(buf);
    }
    // defaults for menus if the ini has no [Menu] section
    if (g_cfg.menuMap[P_SOUTH].kind == T_NONE) { g_cfg.menuMap[P_SOUTH].kind = T_KEY; g_cfg.menuMap[P_SOUTH].value = VK_RETURN; }
    if (g_cfg.menuMap[P_EAST].kind  == T_NONE) { g_cfg.menuMap[P_EAST].kind  = T_KEY; g_cfg.menuMap[P_EAST].value  = VK_ESCAPE; }
    auto F = [&](const char* k, float def) {
        GetPrivateProfileStringA("Settings", k, "", buf, sizeof(buf), ini);
        return buf[0] ? (float)atof(buf) : def;
    };
    g_cfg.lookSensX = F("LookSensitivityX", g_cfg.lookSensX);
    g_cfg.lookSensY = F("LookSensitivityY", g_cfg.lookSensY);
    g_cfg.lookCurve = F("LookCurve", g_cfg.lookCurve);
    g_cfg.deadzone  = F("LookDeadzone", g_cfg.deadzone);
    g_cfg.moveDeadzone = F("MoveDeadzone", g_cfg.moveDeadzone);
    g_cfg.triggerThreshold = F("TriggerThreshold", g_cfg.triggerThreshold);
    g_cfg.invertY = F("InvertY", 0.0f) != 0.0f;
    g_cfg.interrogationBoost = F("InterrogationBoost", g_cfg.interrogationBoost);
    g_cfg.envInterrogationBoost = F("EnvInterrogationBoost", g_cfg.envInterrogationBoost);
    g_cfg.keyMethod = (int)F("KeyMethod", (float)g_cfg.keyMethod);
    g_cfg.logKeys = (int)F("LogKeys", (float)g_cfg.logKeys);
    g_cfg.nativeLeftStick  = (int)F("NativeLeftStick",  (float)g_cfg.nativeLeftStick);
    g_cfg.nativeRightStick = (int)F("NativeRightStick", (float)g_cfg.nativeRightStick);
    g_cfg.leftStickKeys    = (int)F("LeftStickKeys",    (float)g_cfg.leftStickKeys);
    g_cfg.nativeDeadzone   = F("NativeDeadzone", g_cfg.nativeDeadzone);
    g_cfg.invertNativeY    = (int)F("InvertNativeY", (float)g_cfg.invertNativeY);
    g_cfg.interrogationCurve = F("InterrogationCurve", g_cfg.interrogationCurve);
    g_cfg.logInterrogation   = (int)F("LogInterrogation", (float)g_cfg.logInterrogation);
    g_cfg.interrogationNativeStick = (int)F("InterrogationNativeStick", (float)g_cfg.interrogationNativeStick);
    g_cfg.consoleStick = (int)F("InterrogationConsoleStick", (float)g_cfg.consoleStick);
    g_cfg.lookScale = F("LookSensitivity", g_cfg.lookScale);
    g_cfg.interrogationLeftStick = (int)F("InterrogationLeftStick", (float)g_cfg.interrogationLeftStick);
    g_cfg.swapTriggersDual = (int)F("SwapTriggersWhenDualWielding", (float)g_cfg.swapTriggersDual);
    {   // v0.10: button prompts
        GetPrivateProfileStringA("Prompts", "Style", "Auto", buf, sizeof(buf), ini);
        for (char* c = buf; *c; ++c) { if (*c == ';' || *c == ' ' || *c == '\t') { *c = 0; break; } *c = (char)toupper((unsigned char)*c); }
        if      (!strcmp(buf, "KEYBOARD"))    g_cfg.promptStyle = 0;
        else if (!strcmp(buf, "XBOX"))        g_cfg.promptStyle = 1;
        else if (!strcmp(buf, "PLAYSTATION")) g_cfg.promptStyle = 2;
        else if (!strcmp(buf, "STEAMDECK"))   g_cfg.promptStyle = 3;
        else                                  g_cfg.promptStyle = 4;
        g_cfg.stickPicture = (int)GetPrivateProfileIntA("Prompts", "StickPicture", 1, ini);
        g_cfg.logPrompts   = (int)GetPrivateProfileIntA("Prompts", "LogPrompts", 1, ini);
        g_cfg.rumble        = (int)GetPrivateProfileIntA("Rumble", "Enabled", 1, ini);
        g_cfg.rumbleWeapons = (int)GetPrivateProfileIntA("Rumble", "Weapons", 1, ini);
        g_cfg.rumbleShake   = (int)GetPrivateProfileIntA("Rumble", "Explosions", 1, ini);
        g_cfg.rumbleMelee   = (int)GetPrivateProfileIntA("Rumble", "Melee", 1, ini);
        g_cfg.rumbleHits    = (int)GetPrivateProfileIntA("Rumble", "Hits", 1, ini);
        GetPrivateProfileStringA("Rumble", "QuickKillFPS", "30", buf, sizeof(buf), ini);
        g_cfg.qkFrameRate = (float)atof(buf);
        if (g_cfg.qkFrameRate < 5 || g_cfg.qkFrameRate > 120) g_cfg.qkFrameRate = 30;
        GetPrivateProfileStringA("Rumble", "HitMinimum", "1.0", buf, sizeof(buf), ini);
        g_cfg.hitMinimum = (float)atof(buf);
        if (g_cfg.hitMinimum < 0.05f) g_cfg.hitMinimum = 0.05f;
        g_cfg.logRumble     = (int)GetPrivateProfileIntA("Rumble", "Log", 1, ini);
        GetPrivateProfileStringA("Rumble", "Strength", "1.0", buf, sizeof(buf), ini);
        g_cfg.rumbleStrength = (float)atof(buf);
        if (g_cfg.rumbleStrength < 0) g_cfg.rumbleStrength = 0;
        if (g_cfg.rumbleStrength > 2) g_cfg.rumbleStrength = 2;
        GetPrivateProfileStringA("Settings", "HelpKey", "NONE", buf, sizeof(buf), ini);
        Target h = ParseTarget(buf);
        g_cfg.helpKeyScan = h.kind == T_KEY ? -h.value : 0;   // negative = virtual key, converted to a scan code later
    }
    Log("config loaded from %s", ini);
}

// ---------------------------------------------------------------- v0.8.1: player memory snapshot (to find the dual-wield state)
// Hold L3 + R3 together: the player's data is written to playerdump_N.bin next to the .asi.
static int g_dumpCount = 0;
static void DumpBlock(FILE* f, DWORD addr, DWORD size)
{
    BYTE buf[256];
    for (DWORD o = 0; o < size; o += sizeof(buf)) {
        __try { memcpy(buf, (void*)(addr + o), sizeof(buf)); }
        __except (1) { memset(buf, 0xEE, sizeof(buf)); }
        fwrite(buf, 1, sizeof(buf), f);
    }
}
static void DumpPlayer()
{
    DWORD score = 0, ent = 0;
    __try { score = *(DWORD*)0x7CF804; } __except (1) { score = 0; }
    if (!score) { Log("dump: no player yet"); return; }
    __try { DWORD h = *(DWORD*)(score + 0x2C); ent = ((DWORD (__cdecl*)(DWORD))0x4167C0)(h); } __except (1) { ent = 0; }
    char path[MAX_PATH]; snprintf(path, sizeof(path), "%s\\playerdump_%d.bin", g_dir, ++g_dumpCount);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    DWORD hdr[4] = { 0x504D5544, score, ent, 0 };
    fwrite(hdr, 4, 4, f);
    DumpBlock(f, score, 0x2000);           // player score / control object
    if (ent) DumpBlock(f, ent, 0x3000);    // player entity
    fclose(f);
    Log("dump %d written (score object %08X, entity %08X)", g_dumpCount, score, ent);
}

// Player animation set (entity +0x4C8, from the snapshots): 14 pistol, 15 pistol dual, 16 rifle,
// 17 rifle dual, 18+ other weapons. Dual-wielding = 15 or 17.
static int PlayerAnimSet()
{
    __try {
        DWORD score = *(DWORD*)0x7CF804;
        if (!score) return -1;
        DWORD ent = ((DWORD (__cdecl*)(DWORD))0x4167C0)(*(DWORD*)(score + 0x2C));
        if (!ent) return -1;
        return *(int*)(ent + 0x4C8);
    } __except (1) { return -1; }
}
// built-in sets 2-7 = "using shield melee/pistol", "human shield melee/pistol/rifle/russian" = holding a hostage
// (the log prints the exact set number so this can be narrowed down)
static bool g_holdingHostage = false;

static bool PlayerDualWielding()
{
    DWORD ent = 0;
    __try {
        DWORD score = *(DWORD*)0x7CF804;
        if (!score) return false;
        ent = ((DWORD (__cdecl*)(DWORD))0x4167C0)(*(DWORD*)(score + 0x2C));
        if (!ent) return false;
        int set = *(int*)(ent + 0x4C8);
        return set == 15 || set == 17;
    } __except (1) { return false; }
}

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep);
// ---------------------------------------------------------------- pad state (SDL3)
static bool g_sdlReady = false, g_sdlOk = false;
static DWORD g_lastChoiceRead = 0;   // last time the interrogation-choice HUD (0x464A1E) read the sticks
static SDL_Gamepad* g_pad = nullptr;
static int g_padType = 0;            // SDL_GamepadType of the connected pad (v0.10, for the prompt style)
static bool g_down[P_COUNT];
static bool g_wheelPrev[P_COUNT];   // wheel "click" edges (sent once per press)
static bool g_mouseSent[P_COUNT];   // last mouse-button state sent in buffered data
static float g_rx = 0, g_ry = 0;          // processed right stick, -1..1
static float g_rxLin = 0, g_ryLin = 0;    // right stick after deadzone only (linear)
static LARGE_INTEGER g_qpf, g_lastUpdate;
static DWORD g_lastScan = 0;
static bool  g_raw[P_COUNT];         // v0.20: pad buttons before the menu mask (the enhancements menu reads these)
static bool  g_padIgnore[P_COUNT];   // v0.20: buttons held when the menu opened/closed - ignored until released
static bool  g_enhOpen = false;
static bool  g_photo = false;           // v0.41: photo mode (free camera, frozen game)
static void  PhotoFrame();
static void  HudToggleFrame();
// v0.42: Display page - resolution for the next start (written to the widescreen fix's ini)
struct ResChoice { const char* name; int w, h; };
static const ResChoice kRes[] = { { "Leave as is", 0, 0 }, { "Auto (screen size)", -1, -1 }, { "1280x720", 1280, 720 },
    { "1280x800", 1280, 800 }, { "1366x768", 1366, 768 }, { "1600x900", 1600, 900 }, { "1920x1080", 1920, 1080 },
    { "1920x1200", 1920, 1200 }, { "2560x1440", 2560, 1440 }, { "2560x1600", 2560, 1600 }, { "3840x2160", 3840, 2160 } };
static const int kResCount = 11;
static int   g_resIdx = 0, g_resSaved = 0;       // [Display] Resolution
static void  ResolutionSave();
static void  ResolutionStartupCheck(const char* ourIni);
static bool  g_enhFromOptions = false;   // v0.31: opened from the game's Options list (pause or main menu)
static bool  g_showLayout = false;       // v0.33: full-screen controller layout picture (pause_controller.tga)
static int   g_betaPausePic = 0;         // v0.50.9: [Beta] PauseControllerPicture - the picture IS the plain pause screen
static bool  g_pausePicLoaded = false;   // v0.50.9: int-pause.peg loaded for the pause screen
static int   g_betaDigitalAmmo = 0;      // v0.33: [Beta] DigitalAmmo - ammo counter in the unused digital_small font
static int   g_betaComboHud = 0;         // v0.34: [Beta] ComboHud - 2004 "TOTAL / COMBO / BODY COUNT" score display
static int   g_betaComboSplash = 1;      // v0.34.5: [Beta] ComboHudSplash - faint black ink bar behind it
// v0.35: weapon upgrades. The bought upgrades Scope / Silencer / Grenade Launcher / Extended Magazine attach to every
// weapon that supports them, for good. These switches decide per add-on whether it is attached (On = retail).
static int   g_modAllow[4] = { 1, 1, 1, 1 };   // [Weapons] AttachScope, AttachSilencer, AttachGrenadeLauncher, AttachExtendedMagazine
static int   g_hudM16GL = 1;                   // v0.37: [Beta] M16LauncherIcon (was [Weapons]) - HUD shows the M16 + M203 picture when it has one
static int   g_betaLifebar = 0;               // v0.38: [Beta] Lifebar2004 - striped 2004 enemy lifebar (hs_lifebar_2004.tga)
static int   g_betaTargetIcon = 0;             // v0.37: [Beta] TargetHostageIcon - 2004 target-cutout hostage icon instead of ws_human_shield
static BYTE  g_skipText = 0;             // v0.31: 1 = hide the game's menu text this call      // v0.20: enhancements menu open -> the pad talks only to the menu
static DWORD g_frameNo = 0;          // v0.20: counts mouse polls (one per frame)

static float Axis(SDL_GamepadAxis a) { return g_pad ? SDL_GetGamepadAxis(g_pad, a) / 32767.0f : 0.0f; }
static bool  Btn(SDL_GamepadButton b) { return g_pad && SDL_GetGamepadButton(g_pad, b); }

static void PhotoExit();
extern int g_devLogBones;
static void PadUpdate()
{
    if (!g_sdlReady) {
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        g_sdlReady = true;
        if (!SDL_Init(SDL_INIT_GAMEPAD)) { Log("SDL_Init failed: %s", SDL_GetError()); return; }
        SDL_SetGamepadEventsEnabled(false);   // we poll; no event queue needed
        g_sdlOk = true; Log("SDL3 ready (%d.%d.%d)", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
    }
    if (!g_sdlOk) return;
    {   // every 30 s: keep the crash reporter installed (the game may replace it) and log a heartbeat
        static DWORD beat = 0;
        if (GetTickCount() - beat > 30000) {
            beat = GetTickCount();
            SetUnhandledExceptionFilter(CrashFilter);
            float hp = -1, hp2 = -1;
            __try { DWORD e = *(volatile DWORD*)0x6B70FC; if (e) { hp = *(volatile float*)(e + 0x288); hp2 = *(volatile float*)(e + 0xD8); } } __except (1) {}
            Log("alive (interrogation %08lX, hostage %d, health %.1f / %.1f)", *(volatile DWORD*)0x7E2DBC, (int)g_holdingHostage, hp, hp2);
        }
    }
    SDL_UpdateGamepads();

    if (g_pad && !SDL_GamepadConnected(g_pad)) { Log("controller disconnected"); SDL_CloseGamepad(g_pad); g_pad = nullptr; }
    if (!g_pad && GetTickCount() - g_lastScan > 1000) {
        g_lastScan = GetTickCount();
        int n = 0; SDL_JoystickID* ids = SDL_GetGamepads(&n);
        if (ids && n > 0) {
            g_pad = SDL_OpenGamepad(ids[0]);
            if (g_pad) {
                g_padType = (int)SDL_GetGamepadType(g_pad);
                Log("controller connected: %s (type %d)", SDL_GetGamepadName(g_pad), g_padType);
            }
        }
        SDL_free(ids);
    }

    memset(g_down, 0, sizeof(g_down));
    memset(g_raw, 0, sizeof(g_raw));
    if (!g_pad && g_photo) PhotoExit();      // v0.50.1: pad gone - leave photo mode (else the game stays frozen)
    if (!g_pad) { g_rx = g_ry = 0; g_rxLin = g_ryLin = 0; return; }

    g_down[P_SOUTH] = Btn(SDL_GAMEPAD_BUTTON_SOUTH);
    g_down[P_EAST]  = Btn(SDL_GAMEPAD_BUTTON_EAST);
    g_down[P_WEST]  = Btn(SDL_GAMEPAD_BUTTON_WEST);
    g_down[P_NORTH] = Btn(SDL_GAMEPAD_BUTTON_NORTH);
    g_down[P_LB]    = Btn(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    g_down[P_RB]    = Btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    g_down[P_L3]    = Btn(SDL_GAMEPAD_BUTTON_LEFT_STICK);
    g_down[P_R3]    = Btn(SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    g_down[P_BACK]  = Btn(SDL_GAMEPAD_BUTTON_BACK);
    g_down[P_START] = Btn(SDL_GAMEPAD_BUTTON_START);
    g_down[P_DUP]   = Btn(SDL_GAMEPAD_BUTTON_DPAD_UP);
    g_down[P_DDOWN] = Btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    g_down[P_DLEFT] = Btn(SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    g_down[P_DRIGHT]= Btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    g_down[P_LT]    = Axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER)  > g_cfg.triggerThreshold;
    g_down[P_RT]    = Axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > g_cfg.triggerThreshold;

    {
        int set = PlayerAnimSet();
        bool h = set >= 2 && set <= 7;
        if (h != g_holdingHostage) { Log("hostage %s (animation set %d)", h ? "held" : "released", set); g_holdingHostage = h; }
    }
    if (g_cfg.swapTriggersDual) {
        static bool wasDual = false;
        bool dual = PlayerDualWielding();
        if (dual != wasDual) { Log("dual-wield %s", dual ? "on (triggers swapped)" : "off"); wasDual = dual; }
        if (dual) { bool t = g_down[P_LT]; g_down[P_LT] = g_down[P_RT]; g_down[P_RT] = t; }
    }

    static bool dumpHeld = false;
    bool dumpNow = g_down[P_L3] && g_down[P_R3];
    if (g_devLogBones && dumpNow && !dumpHeld) DumpPlayer();   // v0.50.1: only with developer logging on
    dumpHeld = dumpNow;

    float lx = Axis(SDL_GAMEPAD_AXIS_LEFTX), ly = Axis(SDL_GAMEPAD_AXIS_LEFTY);
    // W/A/S/D from the left stick: always if configured, otherwise only while the interrogation-choice
    // HUD is up (it reads the sticks for its picture, but the PC picks the interrogation from W/A/S/D)
    bool choice = GetTickCount() - g_lastChoiceRead < 250;
    static bool wasChoice = false;
    if (choice != wasChoice) { Log("interrogation choice %s", choice ? "shown" : "gone"); wasChoice = choice; }
    if (g_cfg.leftStickKeys || !g_cfg.nativeLeftStick || choice) {
        g_down[P_LS_UP]    = ly < -g_cfg.moveDeadzone;
        g_down[P_LS_DOWN]  = ly >  g_cfg.moveDeadzone;
        g_down[P_LS_LEFT]  = lx < -g_cfg.moveDeadzone;
        g_down[P_LS_RIGHT] = lx >  g_cfg.moveDeadzone;
    }

    // right stick: radial deadzone + response curve
    float x = Axis(SDL_GAMEPAD_AXIS_RIGHTX), y = Axis(SDL_GAMEPAD_AXIS_RIGHTY);
    float mag = sqrtf(x * x + y * y);
    if (mag <= g_cfg.deadzone) { g_rx = g_ry = 0; g_rxLin = g_ryLin = 0; }
    else {
        float m = (fminf(mag, 1.0f) - g_cfg.deadzone) / (1.0f - g_cfg.deadzone);
        g_rxLin = x / mag * m; g_ryLin = y / mag * m;
        m = powf(m, g_cfg.lookCurve);
        g_rx = x / mag * m; g_ry = y / mag * m;
    }

    // v0.20: the enhancements menu takes the pad while it is open; buttons still held from opening /
    // closing it are ignored until released (B that closed the menu must not dodge or pick something up)
    memcpy(g_raw, g_down, sizeof(g_down));
    for (int i = 0; i < P_COUNT; ++i) {
        if (!g_padIgnore[i]) continue;
        if (g_raw[i]) g_down[i] = false; else g_padIgnore[i] = false;
    }
    if (g_enhOpen) { memset(g_down, 0, sizeof(g_down)); g_rx = g_ry = g_rxLin = g_ryLin = 0; }
    HudToggleFrame();                      // v0.50.5: LB + D-pad = HUD on / off ([Developer] HudToggle)
    PhotoFrame();                         // v0.41: L3 + R3 photo mode (takes the pad while on)
    if (g_photo) { memset(g_down, 0, sizeof(g_down)); g_rx = g_ry = g_rxLin = g_ryLin = 0; }
}

// ---------------------------------------------------------------- v0.41: photo mode
// The game camera is a fixed object at 0x67AB50: position 0x67AB6C (x, y, z), orientation 0x67AB78 (3 x 3 floats,
// rows right / up / forward, y up) - the address ermaccer's Punisher Menu free camera uses. The 3rd-person camera
// update (0x405A55..0x405A7B) writes the position (0x405A60 / 0x405A66 / 0x405A78, NOPed like ermaccer does) and copies
// 9 floats of orientation from a stack buffer with rep movsd; "lea esi,[esp+0xAC]" at 0x405A71 is replaced by
// "mov esi, g_photoRot" so the game itself copies OUR orientation every frame. Freeze = time scale 0x6769FC very high.
// Controls: left stick move, right stick look, LT / RT down / up, LB slow, RB fast, Y freeze on / off,
// L3 + R3 or B = exit. HUD hidden (0x736738). (L3 + R3 also writes the old DumpPlayer log line - harmless.)
static DWORD g_placeStart = 0; static int g_placeStep = 4;
static bool InterrogationActiveForPhoto();
// v0.49.4: measure where the game REALLY puts the Punisher and his victim (altar pose work): entity +0x1C pos,
// +0x28 3x3 orientation (rows right / up / forward); victim = handle at Punisher +0x878 (+0x87C) -> 0x4A8D20.
// v0.49.5: every bone of an entity as the game draws it. mesh = [ent+0x94]; 0x529800(mesh) evaluates the pose
// (cached per frame); bone matrices [mesh+8]: N x 3x4 (stride 0x30, rows +0 / +0xC / +0x18, translation +0x24) in the
// entity's MODEL space; skeleton [mesh+0x474], N = [skel+0x48] + [skel+0x4C], bone records [skel+0x50] stride 0x88,
// name at +0x6C (24 chars). Model -> world: x*right + y*up + z*fwd + ent pos (ent +0x28 rows, +0x1C).
static void LogBones(const char* who, DWORD ent, const char* why)
{
    __try {
        DWORD mesh = *(volatile DWORD*)(ent + 0x94);
        if (!mesh) { Log("BONES %s %s: no mesh", why, who); return; }
        ((void (__cdecl*)(DWORD))0x529800)(mesh);
        DWORD skel = *(volatile DWORD*)(mesh + 0x474);
        DWORD mats = *(volatile DWORD*)(mesh + 8);
        if (!skel || !mats) { Log("BONES %s %s: no skeleton / matrices", why, who); return; }
        int n = *(volatile int*)(skel + 0x48) + *(volatile int*)(skel + 0x4C);
        DWORD recs = *(volatile DWORD*)(skel + 0x50);
        if (n <= 0 || n > 80) { Log("BONES %s %s: bone count %d?", why, who, n); return; }
        for (int i = 0; i < n; ++i) {
            volatile float* m = (volatile float*)(mats + 0x30 * i);
            char name[25] = {};
            if (recs) for (int k = 0; k < 24; ++k) { char c = *(volatile char*)(recs + 0x88 * i + 0x6C + k); name[k] = c; if (!c) break; }
            Log("BONES %s %s %2d %-22s pos %.4f %.4f %.4f | rot %.3f %.3f %.3f / %.3f %.3f %.3f / %.3f %.3f %.3f",
                why, who, i, name, m[9], m[10], m[11], m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8]);
        }
    } __except (1) { Log("BONES %s %s: read failed", why, who); }
}
static void LogPairPlacement(const char* why)
{
    __try {
        DWORD p = *(volatile DWORD*)0x6B70FC;
        if (!p) return;
        volatile float* pp = (volatile float*)(p + 0x1C);
        volatile float* pr = (volatile float*)(p + 0x28);
        Log("PLACEMENT %s: punisher pos %.4f %.4f %.4f | rot %.4f %.4f %.4f / %.4f %.4f %.4f / %.4f %.4f %.4f", why,
            pp[0], pp[1], pp[2], pr[0], pr[1], pr[2], pr[3], pr[4], pr[5], pr[6], pr[7], pr[8]);
        LogBones("punisher", p, why);
        for (int k = 0; k < 2; ++k) {
            DWORD h = *(volatile DWORD*)(p + 0x878 + 4 * k);
            DWORD o = h ? ((DWORD (__cdecl*)(DWORD))0x4A8D20)(h) : 0;
            if (!o) continue;
            volatile float* vp = (volatile float*)(o + 0x1C);
            volatile float* vr = (volatile float*)(o + 0x28);
            float d[3] = { vp[0] - pp[0], vp[1] - pp[1], vp[2] - pp[2] };
            float lx = d[0] * pr[0] + d[1] * pr[1] + d[2] * pr[2];
            float ly = d[0] * pr[3] + d[1] * pr[4] + d[2] * pr[5];
            float lz = d[0] * pr[6] + d[1] * pr[7] + d[2] * pr[8];
            Log("PLACEMENT %s: victim(+0x%X) pos %.4f %.4f %.4f | rot %.4f %.4f %.4f / %.4f %.4f %.4f / %.4f %.4f %.4f | in Punisher space %.4f %.4f %.4f",
                why, 0x878 + 4 * k, vp[0], vp[1], vp[2], vr[0], vr[1], vr[2], vr[3], vr[4], vr[5], vr[6], vr[7], vr[8], lx, ly, lz);
            if (k == 0) LogBones("victim", o, why);
        }
    } __except (1) { Log("PLACEMENT %s: read failed", why); }
}
static float g_photoRot[9];
static float g_photoPos[3], g_photoYaw = 0, g_photoPitch = 0;
static bool  g_photoFreeze = true;
// v0.49 camera tool (altar camera): X = save the camera to the log, D-pad up / down = lens zoom (FOV).
// FOV: global float 0x61F930 (set from the scene's +0x214, 60.0), copied to the camera block 0x67AEFC at 0x406E63.
// v0.49.1: the FOV the camera USES is camera +0x214 = 0x67AD64 (0x40741F blends 0x67AEFC -> 0x67AF00 into it);
// during an interrogation the 3rd-person code (our patched rotation copy) does not run -> write the rotation
// 0x67AB78 directly every frame too. Right stick up = look up (v0.41 was inverted), slower move / turn.
static float g_photoFov = 60.0f, g_photoFovSaved = 60.0f, g_photoFovSaved2 = 60.0f;
static bool  g_photoFovChanged = false;
static int   g_photoSaves = 0, g_photoOverrides = 0;
// v0.49.2: photo mode options ([Photo] in the ini, Developer page)
static float g_photoMoveSpeed = 1.2f, g_photoTurnSpeed = 1.0f;
static int   g_photoInvertY = 0;
static DWORD g_photoLastOverrideLog = 0;
static BYTE  g_photoSaved[4][8];
struct PhotoPatch { DWORD at; int n; };
static const PhotoPatch kPhotoPatch[4] = { { 0x405A60, 2 }, { 0x405A66, 3 }, { 0x405A78, 3 }, { 0x405A71, 7 } };
static void PhotoPatchApply(bool on)
{
    for (int k = 0; k < 4; ++k) {
        BYTE* p = (BYTE*)kPhotoPatch[k].at; int n = kPhotoPatch[k].n;
        DWORD old; VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &old);
        if (on) {
            memcpy(g_photoSaved[k], p, n);
            if (k < 3) memset(p, 0x90, n);
            else { p[0] = 0xBE; *(DWORD*)(p + 1) = (DWORD)g_photoRot; p[5] = 0x90; p[6] = 0x90; }
        } else memcpy(p, g_photoSaved[k], n);
        VirtualProtect(p, n, old, &old);
    }
    FlushInstructionCache(GetCurrentProcess(), (void*)0x405A60, 0x20);
}
static void PhotoBuildRot()
{
    float cy = cosf(g_photoYaw), sy = sinf(g_photoYaw), cp = cosf(g_photoPitch), sp = sinf(g_photoPitch);
    float f[3] = { cp * sy, sp, cp * cy };
    float r[3] = { cy, 0.0f, -sy };
    float u[3] = { f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0] };
    for (int i = 0; i < 3; ++i) { g_photoRot[i] = r[i]; g_photoRot[3 + i] = u[i]; g_photoRot[6 + i] = f[i]; }
}
static void PhotoEnter()
{
    __try {
        if (!*(volatile DWORD*)0x7CF804) return;
        static const BYTE expect[7] = { 0x8D, 0xB4, 0x24, 0xAC, 0x00, 0x00, 0x00 };
        if (memcmp((void*)0x405A71, expect, 7) != 0 || *(BYTE*)0x405A60 != 0x89) { Log("photo mode: camera code not as expected - off"); return; }
        volatile float* pos = (volatile float*)0x67AB6C;
        volatile float* rot = (volatile float*)0x67AB78;
        for (int i = 0; i < 3; ++i) g_photoPos[i] = pos[i];
        for (int i = 0; i < 9; ++i) g_photoRot[i] = rot[i];
        g_photoFov = g_photoFovSaved = *(volatile float*)0x67AD64;
        g_photoFovSaved2 = *(volatile float*)0x61F930;
        if (!(g_photoFov > 5.0f && g_photoFov < 170.0f)) g_photoFov = 60.0f;
        g_photoFovChanged = false; g_photoOverrides = 0;
        g_photoYaw = atan2f(rot[6], rot[8]);
        g_photoPitch = asinf(fmaxf(-1.0f, fminf(1.0f, rot[7])));
        PhotoPatchApply(true);
        g_photo = true; g_photoFreeze = true;
        Log("photo mode on (camera %.2f %.2f %.2f, rot f %.2f %.2f %.2f, FOV %.1f / %.1f, %s)", g_photoPos[0], g_photoPos[1], g_photoPos[2],
            rot[6], rot[7], rot[8], g_photoFovSaved, g_photoFovSaved2, InterrogationActiveForPhoto() ? "during an interrogation" : "not interrogating");
    } __except (1) { Log("photo mode: could not start"); }
}
// v0.49.3: photo camera applied AFTER the game's camera update (0x406C90 calls 0x406A50(0x67AB50) for every camera
// mode: follow / shield / scripted). The human-shield camera re-placed itself every frame and beat the early write.
static void PhotoApplyNow()
{
    if (!g_photo) return;
    __try {
        volatile float* pos = (volatile float*)0x67AB6C;
        volatile float* rot = (volatile float*)0x67AB78;
        for (int i = 0; i < 3; ++i) pos[i] = g_photoPos[i];
        for (int i = 0; i < 9; ++i) rot[i] = g_photoRot[i];
        if (g_photoFovChanged) *(volatile float*)0x67AD64 = g_photoFov;
    } __except (1) {}
}
static void TeleportTick();
static void FakeStickTick();
static void __cdecl Wrap_CameraUpdate(DWORD cam)
{
    FakeStickTick();                      // v0.66 debug (off unless [Developer] FakeStickTest=1)
    TeleportTick();                       // v0.49.6: Load position holds the player on the spot for a few frames
    ((void (__cdecl*)(DWORD))0x406A50)(cam);
    PhotoApplyNow();
}
static void PhotoExit()
{
    __try {
        PhotoPatchApply(false);
        if (g_photoFovChanged) { *(volatile float*)0x67AD64 = g_photoFovSaved; *(volatile float*)0x61F930 = g_photoFovSaved2; }
        volatile float* rot = (volatile float*)0x67AB78;
        Log("CAMERA AT EXIT: pos %.3f %.3f %.3f | right %.4f %.4f %.4f | up %.4f %.4f %.4f | forward %.4f %.4f %.4f | FOV %.1f",
            g_photoPos[0], g_photoPos[1], g_photoPos[2], rot[0], rot[1], rot[2], rot[3], rot[4], rot[5], rot[6], rot[7], rot[8], g_photoFov);
        *(volatile float*)0x6769FC = 1.0f;
        *(volatile BYTE*)0x736738 = 0;
    } __except (1) {}
    g_photo = false;
    for (int i = 0; i < P_COUNT; ++i) if (g_raw[i]) g_padIgnore[i] = true;
    Log("photo mode off");
}
// v0.76 DEV (stump viewer, [Developer] PhotoRemote=1): while photo mode is on, the camera can be set from
// <plugin folder>\photo_cam.txt = "x y z yaw pitch" (radians, the photo camera's own yaw / pitch). Read when the
// file changes; "Done n" is written to photo_cam_done.txt so a script knows the camera moved. Pad input still works.
static int g_devPhotoRemote = 0;
// v0.76 DEV ([Developer] AlwaysGib=1): shooting a CORPSE always passes the 10 / 20 % gib roll (0x410F00, called only
// at 0x412C4E) -> its limb comes off once the hits use up the limb's health (head / arms 15, legs 30). For the stump
// viewer on the laptop. Off = the game's roll.
static int g_devAlwaysGib = 0;
static char __cdecl Hook_GibRoll(int dmgType, int flags)
{
    if (g_devAlwaysGib) return 1;
    return ((char (__cdecl*)(int, int))0x410F00)(dmgType, flags);
}
static void PhotoRemote()
{
    if (!g_devPhotoRemote) return;
    static FILETIME last = {};
    static int n = 0;
    char p[MAX_PATH]; snprintf(p, MAX_PATH, "%s\\photo_cam.txt", g_dir);
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(p, GetFileExInfoStandard, &a)) return;
    if (CompareFileTime(&a.ftLastWriteTime, &last) == 0) return;
    last = a.ftLastWriteTime;
    FILE* f = fopen(p, "r"); if (!f) return;
    float x, y, z, yaw, pitch;
    int k = fscanf(f, "%f %f %f %f %f", &x, &y, &z, &yaw, &pitch);
    fclose(f);
    if (k != 5) return;
    g_photoPos[0] = x; g_photoPos[1] = y; g_photoPos[2] = z; g_photoYaw = yaw; g_photoPitch = pitch;
    PhotoBuildRot();
    ++n;
    char q[MAX_PATH]; snprintf(q, MAX_PATH, "%s\\photo_cam_done.txt", g_dir);
    if (FILE* g = fopen(q, "w")) { fprintf(g, "Done %d\n", n); fclose(g); }
    Log("photo remote #%d: camera %.3f %.3f %.3f yaw %.3f pitch %.3f", n, x, y, z, yaw, pitch);
}
static void PhotoFrame()
{
    static bool comboWas = false, yWas = false;
    static DWORD last = GetTickCount();
    DWORD now = GetTickCount(); float dt = (now - last) / 1000.0f; last = now;
    if (dt > 0.1f) dt = 0.1f;
    bool combo = g_raw[P_L3] && g_raw[P_R3];
    if (combo && !comboWas && !g_enhOpen) { if (g_photo) PhotoExit(); else PhotoEnter(); }
    comboWas = combo;
    if (!g_photo) return;
    if (g_raw[P_EAST] && !combo) { PhotoExit(); return; }
    if (g_raw[P_NORTH] && !yWas) g_photoFreeze = !g_photoFreeze;
    yWas = g_raw[P_NORTH];
    // v0.49: is something else moving the camera (e.g. an interrogation camera)? compare with what we wrote
    __try {
        volatile float* cp = (volatile float*)0x67AB6C;
        float dd = fabsf(cp[0] - g_photoPos[0]) + fabsf(cp[1] - g_photoPos[1]) + fabsf(cp[2] - g_photoPos[2]);
        if (dd > 0.01f) {
            ++g_photoOverrides;
            if (GetTickCount() - g_photoLastOverrideLog > 2000) {
                g_photoLastOverrideLog = GetTickCount();
                Log("camera tool: the game moved the camera (%d times) - now %.2f %.2f %.2f", g_photoOverrides, cp[0], cp[1], cp[2]);
            }
        }
    } __except (1) {}
    // v0.49: X = save this camera to the log; D-pad up / down = zoom (narrower / wider lens)
    static bool xWas = false;
    if (g_raw[P_WEST] && !xWas) {
        __try {
            volatile float* rot = (volatile float*)0x67AB78;
            DWORD pl = *(volatile DWORD*)0x7CF804;
            float px = 0, py = 0, pz = 0;
            if (pl) { px = *(volatile float*)(pl + 0x1C); py = *(volatile float*)(pl + 0x20); pz = *(volatile float*)(pl + 0x24); }
            ++g_photoSaves;
            LogPairPlacement("camera saved");
            Log("CAMERA SAVED #%d: pos %.3f %.3f %.3f | right %.4f %.4f %.4f | up %.4f %.4f %.4f | forward %.4f %.4f %.4f | FOV %.1f | player %.3f %.3f %.3f",
                g_photoSaves, g_photoPos[0], g_photoPos[1], g_photoPos[2], rot[0], rot[1], rot[2], rot[3], rot[4], rot[5],
                rot[6], rot[7], rot[8], g_photoFov, px, py, pz);
            g_padIgnore[P_WEST] = false;
        } __except (1) { Log("camera tool: save failed"); }
        if (g_pad) SDL_RumbleGamepad(g_pad, 0x6000, 0x6000, 120);   // a short buzz = saved
    }
    xWas = g_raw[P_WEST];
    float zoom = (g_raw[P_DUP] ? -1.0f : 0.0f) + (g_raw[P_DDOWN] ? 1.0f : 0.0f);
    if (zoom != 0.0f) {
        g_photoFov += zoom * 20.0f * dt;
        if (g_photoFov < 10.0f) g_photoFov = 10.0f;
        if (g_photoFov > 90.0f) g_photoFov = 90.0f;
        g_photoFovChanged = true;
    }
    if (g_photoFovChanged) {
        __try {
            float was = *(volatile float*)0x67AD64;
            *(volatile float*)0x67AD64 = g_photoFov;
            static DWORD lastFovLog = 0;
            if (fabsf(was - g_photoFov) > 0.5f && GetTickCount() - lastFovLog > 2000) {
                lastFovLog = GetTickCount(); Log("camera tool: the game reset the zoom to %.1f (we set %.1f)", was, g_photoFov);
            }
        } __except (1) {}
    }
    auto dz = [](float v) { return fabsf(v) < 0.15f ? 0.0f : (v - (v > 0 ? 0.15f : -0.15f)) / 0.85f; };
    float lx = dz(Axis(SDL_GAMEPAD_AXIS_LEFTX)), ly = dz(Axis(SDL_GAMEPAD_AXIS_LEFTY));
    float rx = dz(Axis(SDL_GAMEPAD_AXIS_RIGHTX)), ry = dz(Axis(SDL_GAMEPAD_AXIS_RIGHTY));
    float lt = Axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), rt = Axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    float speed = g_photoMoveSpeed * (g_raw[P_LB] ? 0.25f : 1.0f) * (g_raw[P_RB] ? 4.0f : 1.0f);
    float turn = g_photoTurnSpeed * (g_raw[P_LB] ? 0.35f : 1.0f);
    g_photoYaw += rx * 1.2f * turn * dt;
    g_photoPitch += (g_photoInvertY ? -ry : ry) * 0.9f * turn * dt;      // v0.49.1: stick up = look up (v0.49.2: option)
    if (g_photoPitch > 1.5f) g_photoPitch = 1.5f;
    if (g_photoPitch < -1.5f) g_photoPitch = -1.5f;
    PhotoBuildRot();
    const float* r = g_photoRot; const float* f = g_photoRot + 6;
    for (int i = 0; i < 3; ++i) g_photoPos[i] += (r[i] * lx - f[i] * ly) * speed * dt;
    g_photoPos[1] += (rt - lt) * speed * dt;
    PhotoRemote();                                       // v0.76 dev: camera from photo_cam.txt (stump viewer)
    __try {
        volatile float* pos = (volatile float*)0x67AB6C;
        for (int i = 0; i < 3; ++i) pos[i] = g_photoPos[i];
        volatile float* rot = (volatile float*)0x67AB78;      // v0.49.1: interrogation cameras skip the copy
        for (int i = 0; i < 9; ++i) rot[i] = g_photoRot[i];
        *(volatile float*)0x6769FC = g_photoFreeze ? 1000.0f : 1.0f;
        *(volatile BYTE*)0x736738 = 1;
    } __except (1) {}
}

// ---------------------------------------------------------------- hook helpers
static bool PatchPointer(void** slot, void* value, void** original)
{
    DWORD old;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return false;
    if (original) *original = *slot;
    *slot = value;
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}

// replace an import of pun.exe (by dll + function name)
static bool PatchImport(const char* dll, const char* func, void* hook, void** original)
{
    BYTE* base = (BYTE*)GetModuleHandleA(nullptr);
    auto dos = (IMAGE_DOS_HEADER*)base;
    auto nt  = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; ++imp) {
        if (_stricmp((char*)(base + imp->Name), dll)) continue;
        auto names = (IMAGE_THUNK_DATA*)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        auto slots = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (names->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            auto ibn = (IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData);
            if (!strcmp((char*)ibn->Name, func))
                return PatchPointer((void**)&slots->u1.Function, hook, original);
        }
    }
    return false;
}

// ---------------------------------------------------------------- menus: the console pad query
// 0x55CF90 is the console version's "pad direction pressed?" function, gutted on PC to "return 0"
// (xor eax,eax / ret). The menu code still calls it next to every arrow-key check:
//   id 0 = up, 1 = down, 2 = left, 3 = right.   cdecl (player, unknown, id) -> int
// We put it back: it answers from the D-pad / left stick, with key-style auto repeat.
// Only menus call it, so "called in the last 250 ms" also tells us a menu is open.
static DWORD g_lastMenuQuery = 0;
static DWORD g_lastPopup = 0;        // v0.12: last time the pop-up box input handler 0x4768E0 ran
static bool  g_menuActive = false;
static bool  g_dirDown[4], g_dirFire[4];
static bool  g_dirPulse[4];   // v0.50.8: one-update press for the Extended menu (it reads every frame itself)
static DWORD g_dirNext[4];
static bool  g_dirStick[4], g_dirRead[4];   // v0.50.7
static DWORD g_dirSet[4];
static int   g_unknownIdLogs = 0;

static void UpdateMenuDirections()
{
    float lx = Axis(SDL_GAMEPAD_AXIS_LEFTX), ly = Axis(SDL_GAMEPAD_AXIS_LEFTY);
    // v0.50.7: stick with hysteresis (on at 0.5, off below 0.35) - near half tilt it used to flicker on / off
    float th[4];
    for (int i = 0; i < 4; ++i) th[i] = g_dirStick[i] ? 0.35f : 0.5f;
    g_dirStick[0] = ly < -th[0]; g_dirStick[1] = ly > th[1]; g_dirStick[2] = lx < -th[2]; g_dirStick[3] = lx > th[3];
    bool now[4] = {
        g_raw[P_DUP]    || g_dirStick[0],   // v0.20: raw buttons (the enhancements menu masks g_down)
        g_raw[P_DDOWN]  || g_dirStick[1],
        g_raw[P_DLEFT]  || g_dirStick[2],
        g_raw[P_DRIGHT] || g_dirStick[3] };
    DWORD t = GetTickCount();
    for (int i = 0; i < 4; ++i) {
        // v0.50.7: a press stays up until the menu has read it once (or 300 ms passed). Before, it lasted exactly one
        // plugin update - if the menu did not ask during that update the press was lost (D-pad / stick felt dead).
        if (g_dirFire[i] && (g_dirRead[i] || (LONG)(t - g_dirSet[i]) > 300)) g_dirFire[i] = false;
        bool fire = false;
        if (now[i] && !g_dirDown[i]) { fire = true; g_dirNext[i] = t + 400; }      // first press
        else if (now[i] && (LONG)(t - g_dirNext[i]) >= 0) { fire = true; g_dirNext[i] = t + 120; } // repeat
        g_dirPulse[i] = fire;
        if (fire) { g_dirFire[i] = true; g_dirRead[i] = false; g_dirSet[i] = t; }
        g_dirDown[i] = now[i];
    }
    // The pad query is also called during gameplay, so "recently called" alone is not enough:
    // a menu counts as open only while the game is paused (byte 0x736109) or not in a level
    // (bit 0 of 0x759BDC is set while a level is running).
    bool paused  = *(volatile BYTE*)0x736109 != 0;
    bool inLevel = (*(volatile BYTE*)0x759BDC & 1) != 0;
    bool popup = (t - g_lastPopup) < 250;      // v0.12: a pop-up box (OK / CANCEL / Continue) is open
    bool active = ((t - g_lastMenuQuery) < 250 && (paused || !inLevel)) || popup;
    if (active != g_menuActive) { g_menuActive = active; Log("menu %s", active ? "opened" : "closed"); }
}

static int __cdecl Hook_PadQuery(int player, int unk, int id)
{
    g_lastMenuQuery = GetTickCount();
    if (g_enhOpen) return 0;                   // v0.20: the enhancements menu has the pad
    if (id >= 0 && id < 4) {   // v0.50.7: a press counts in the first frame the menu reads it (never twice)
        static DWORD readFrame[4];
        if (!g_dirFire[id]) return 0;
        if (!g_dirRead[id]) { g_dirRead[id] = true; readFrame[id] = g_frameNo; return 1; }
        return readFrame[id] == g_frameNo ? 1 : 0;
    }
    // (one menu passes a pointer here, e.g. 22080776 - not a direction; answered 0 as before)
    if (g_unknownIdLogs < 3) { ++g_unknownIdLogs; Log("menu pad query with unknown id %d (ignored)", id); }
    return 0;
}

// v0.77: PROFILES screen - switching between the New / Delete column and the profile names (menu object 0x74F4B0,
// callback 0x488B10) only checks the KEYBOARD arrows / numpad 4 6 (0x534800 with DIK 0xCD, 0x4D, 0xCB, 0x4B; any one
// toggles the column). The pad never reached it (user, Steam Controller). The first check (0x488B79, DIK right) also
// answers yes when the pad's left or right fired (D-pad or stick), once per press like Hook_PadQuery.
static void PatchCall(DWORD site, void* hook, DWORD expectedTarget);
typedef char (__cdecl* KeyHit_t)(int, int);
static char __cdecl Hook_ProfileColumn(int dik, int flag)
{
    char r = ((KeyHit_t)0x534800)(dik, flag);
    if (r || g_enhOpen) return r;
    for (int i = 2; i <= 3; ++i)
        if (g_dirFire[i] && !g_dirRead[i]) { g_dirRead[i] = true; return 1; }
    return 0;
}
static void InstallMenuHook()
{
    PatchCall(0x488B79, (void*)Hook_ProfileColumn, 0x534800);   // v0.77 profiles screen: pad left / right
    BYTE* p = (BYTE*)0x55CF90;
    if (!(p[0] == 0x33 && p[1] == 0xC0 && p[2] == 0xC3)) { Log("menu hook: 0x55CF90 not the expected stub - skipped"); return; }
    DWORD old; VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE9; *(LONG*)(p + 1) = (LONG)((DWORD)Hook_PadQuery - (0x55CF90 + 5));
    VirtualProtect(p, 5, old, &old);
    Log("menu hook installed (console pad query restored)");
}

// ---------------------------------------------------------------- analog sticks: the console stick reader
// 0x53D880 is the console's "read analog stick" function, gutted on PC to write 0, 0:
//   cdecl (player, stick 0 = left / 1 = right, float* x, float* y, int unknown)
// Player movement, interrogations and other systems still call it. We put it back.
static int g_stickCallerLogs = 0;
static DWORD g_stickCallers[32];

// v0.66 debug (tester: "no full 360 with the stick when walking or crouching"): [Developer] FakeStickTest=1 feeds the
// left stick by itself once a level runs - 8 steps of 3 s: angles 0 / 30 / 60 / 90 deg at tilt 1.0 and 0.4 - and logs
// the player's position at the start and end of each step. Not in the menu. 0 = off.
static int g_fakeStickTest = 0;
static float g_fakeX = 0, g_fakeY = 0;
static bool g_fakeOn = false;
static float g_tpPos[3], g_tpRot[9];   // teleport (v0.49.6; moved up for the v0.66 stick test)
static int   g_tpFrames = 0;
static void FakeStickTick()
{
    static DWORD t0 = 0; static int lastStep = -2; static float sp[3], home[3], homeRot[9]; static bool haveHome = false;
    if (!g_fakeStickTest) return;
    DWORD e = 0; __try { e = *(volatile DWORD*)0x6B70FC; } __except (1) { e = 0; }
    if (!e) { t0 = 0; g_fakeOn = false; return; }
    const int D = (g_fakeStickTest > 1 ? g_fakeStickTest : 8) * 1000;
    if (!t0) { t0 = GetTickCount(); lastStep = -2; haveHome = false; Log("fakestick: level running - test starts in %d s", D / 1000); }
    int step = (int)(GetTickCount() - t0 - D) / 1500;   // 0.5 s back home + 1 s stick per step
    if ((int)(GetTickCount() - t0) < D) step = -1;
    static const float ang[4] = { 0, 20, 40, 60 }, mag[2] = { 1.0f, 0.4f };
    float p[3] = {0};
    __try { memcpy(p, (void*)(e + 0x1C), 12); } __except (1) {}
    if (step != lastStep) {
        if (lastStep >= 0 && lastStep < 8) {
            float dx = p[0] - sp[0], dz = p[2] - sp[2];
            float a = atan2f(dx, dz) * 57.2958f, d = sqrtf(dx * dx + dz * dz);
            Log("fakestick step %d: stick %.0f deg tilt %.1f -> moved %.2f m, world angle %.1f deg (dy %.2f)", lastStep,
                ang[lastStep % 4], mag[lastStep / 4], d, a, p[1] - sp[1]);
        }
        if (step == 0 && !haveHome) { memcpy(home, p, 12); __try { memcpy(homeRot, (void*)(e + 0x28), 36); } __except (1) {} haveHome = true; }
        if (step >= 0 && step < 8 && haveHome) {   // back to the same spot (teleport holds it a few frames)
            memcpy(g_tpPos, home, 12); memcpy(g_tpRot, homeRot, 36); g_tpFrames = 10;
        }
        lastStep = step;
    }
    bool settling = ((GetTickCount() - t0 - D) % 1500) < 500;
    if (step >= 0 && step < 8 && settling) { g_fakeOn = false; memcpy(sp, p, 12); return; }
    if (step >= 0 && step < 8) {
        float a = ang[step % 4] / 57.2958f, m = mag[step / 4];
        g_fakeX = sinf(a) * m; g_fakeY = -cosf(a) * m; g_fakeOn = true;
    } else g_fakeOn = false;
}
static void StickValue(int stick, float* ox, float* oy)
{
    if (stick == 0 && g_fakeOn) { *ox = g_fakeX; *oy = g_fakeY; return; }
    float x = Axis(stick ? SDL_GAMEPAD_AXIS_RIGHTX : SDL_GAMEPAD_AXIS_LEFTX);
    float y = Axis(stick ? SDL_GAMEPAD_AXIS_RIGHTY : SDL_GAMEPAD_AXIS_LEFTY);
    float mag = sqrtf(x * x + y * y);
    if (mag <= g_cfg.nativeDeadzone) { *ox = 0; *oy = 0; return; }
    float m = (fminf(mag, 1.0f) - g_cfg.nativeDeadzone) / (1.0f - g_cfg.nativeDeadzone);
    x = x / mag * m; y = y / mag * m;
    if (g_cfg.invertNativeY) y = -y;       // SDL's direction is what the game expects (tested on Deck)
    *ox = x; *oy = y;
}

static void __cdecl Hook_ReadStick(int player, int stick, float* x, float* y, int unk)
{
    if (x) *x = 0; if (y) *y = 0;
    DWORD caller = (DWORD)_ReturnAddress();
    if (caller == 0x464A23 || caller == 0x464A39) g_lastChoiceRead = GetTickCount();
    bool known = false;
    for (int i = 0; i < g_stickCallerLogs; ++i) if (g_stickCallers[i] == caller) { known = true; break; }
    if (!known && g_stickCallerLogs < 32) { g_stickCallers[g_stickCallerLogs++] = caller; Log("stick reader used by %08X (stick %d)", caller, stick); }
    if (x && y && stick == 0 && g_fakeOn) { StickValue(0, x, y); return; }   // v0.66 debug
    if (!x || !y || !g_pad || g_enhOpen || g_photo) return;
    if (stick == 0 && g_cfg.nativeLeftStick)  StickValue(0, x, y);
    bool interrogating = *(volatile DWORD*)0x7E2DBC != 0;
    if (stick == 1 && (g_cfg.nativeRightStick || (interrogating && g_cfg.interrogationNativeStick))) StickValue(1, x, y);
}

static int PatchCallsTo(DWORD lo, DWORD hi, DWORD target, void* hook, const char* what);
// v0.66: ThirteenAG's Fusion Fix adds its own Options entry in row 4 (only when the list has 3 rows) and takes over
// that row's input - our "Extended Menu" entry would open its menu. With the Fusion Fix present we leave the row to it.
static bool FusionFixPresent()
{
    if (GetModuleHandleA("ThePunisher.FusionFix.asi")) return true;
    char exe[MAX_PATH]; GetModuleFileNameA(NULL, exe, MAX_PATH); if (char* q = strrchr(exe, '\\')) *q = 0;
    const char* dirs[] = { "", "\\scripts", "\\plugins" };
    for (const char* d : dirs) {
        char f[MAX_PATH]; snprintf(f, MAX_PATH, "%s%s\\ThePunisher.FusionFix.asi", exe, d);
        if (GetFileAttributesA(f) != INVALID_FILE_ATTRIBUTES) return true;
    }
    char pat[MAX_PATH]; snprintf(pat, MAX_PATH, "%s\\scripts\\*", exe);
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
    bool found = false;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
            char f[MAX_PATH]; snprintf(f, MAX_PATH, "%s\\scripts\\%s\\ThePunisher.FusionFix.asi", exe, fd.cFileName);
            if (GetFileAttributesA(f) != INVALID_FILE_ATTRIBUTES) { found = true; break; }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    return found;
}
static void InstallStickHook()
{
    // v0.66: the 12 CALLS to 0x53D880 are redirected (was: a jmp written over the function's first bytes). Same
    // result for the game, but the function's own bytes stay as they are - ThirteenAG's Fusion Fix finds its menu
    // code with a byte pattern that runs into 0x53D880 (notes\FUSIONFIX_REVIEW.md). No jmp / pointer to it exists.
    BYTE* p = (BYTE*)0x53D880;
    if (!(p[0] == 0x8B && p[1] == 0x44 && p[2] == 0x24 && p[3] == 0x0C)) { Log("stick hook: 0x53D880 not as expected - skipped"); return; }
    int n = PatchCallsTo(0x401000, 0x5EB000, 0x53D880, (void*)Hook_ReadStick, "console stick reader");
    Log("stick hook installed (console stick reader restored, %d calls)", n);
}

// ---------------------------------------------------------------- keyboard injection
// The game does not read the keyboard with GetKeyboardState for gameplay - its window procedure
// passes every WM_KEYDOWN/WM_KEYUP to an internal key-event function (0x534930) that keeps its own
// key tables indexed by DirectInput scan code. We call that same function on button press/release,
// so the game sees exactly what a real key press would give it.
//   0x534930: eax = scan code, [esp+4] = 1 down / 0 up, [esp+8] = event age in ms (0 = now), caller cleans stack
static const DWORD kGameKeyEvent = 0x534930;
static const DWORD kInputReady   = 0xcc3194;   // byte: game's input system initialised
static const DWORD kCurrentInterrogation = 0x7e2dbc; // pointer to the running interrogation, 0 if none
                                                     // (0x7d112c was wrong: it stays set after melee-item kills)

static int VkToScan(int vk)
{
    switch (vk) {   // extended keys: DirectInput code = scan code + 0x80
    case VK_UP: return 0xC8; case VK_DOWN: return 0xD0; case VK_LEFT: return 0xCB; case VK_RIGHT: return 0xCD;
    case VK_INSERT: return 0xD2; case VK_DELETE: return 0xD3; case VK_HOME: return 0xC7; case VK_END: return 0xCF;
    case VK_PAUSE: return 0x45;   // the game checks 0x45 for its pause key (Pause sends scan 0x45, not extended)
    case VK_PRIOR: return 0xC9; case VK_NEXT: return 0xD1; case VK_RCONTROL: return 0x9D; case VK_RMENU: return 0xB8;
    case VK_SHIFT: case VK_LSHIFT: return 0x2A; case VK_RSHIFT: return 0x36;
    case VK_CONTROL: case VK_LCONTROL: return 0x1D; case VK_MENU: case VK_LMENU: return 0x38;
    }
    return (int)MapVirtualKeyA((UINT)vk, 0 /*MAPVK_VK_TO_VSC*/) & 0xFF;
}

static void GameKeyEvent(int scan, int down)
{
    if (!*(volatile BYTE*)kInputReady || scan <= 0) return;
    DWORD fn = kGameKeyEvent;
    __asm {
        push 0
        push down
        mov  eax, scan
        call fn
        add  esp, 8
    }
}

static void MessageKeyEvent(int vk, int scan, int down)
{
    HWND w = GetActiveWindow();               // the game's window (we run on the game's thread)
    if (!w) w = GetForegroundWindow();
    if (!w) { GameKeyEvent(scan, down); return; }
    LPARAM lp = 1 | ((LPARAM)(scan & 0x7F) << 16);
    if (scan & 0x80) lp |= (LPARAM)1 << 24;   // extended key
    if (!down) lp |= ((LPARAM)1 << 30) | ((LPARAM)1 << 31);
    PostMessageA(w, down ? WM_KEYDOWN : WM_KEYUP, (WPARAM)vk, lp);
}

static int g_keyLogCount = 0;
static int  g_keySentVk[P_COUNT];   // key currently held down for each pad input (0 = none)

static void SendKeyNow(int i, int vk, int down)
{
    int scan = VkToScan(vk);
    if (g_cfg.keyMethod == 1) MessageKeyEvent(vk, scan, down);
    else GameKeyEvent(scan, down);
    if (g_cfg.logKeys && (g_cfg.logKeys > 1 || g_keyLogCount < 300)) {
        ++g_keyLogCount;
        Log("key %s: %s vk=0x%02X scan=0x%02X%s", down ? "down" : "up  ", kPadNames[i], vk, scan, g_menuActive ? " (menu)" : "");
    }
}

// ---------------------------------------------------------------- v0.12: interrogation -> shield
// The game keeps 0x7E2DBC (current interrogation) set while you hold the broken enemy as a shield, so
// the interrogation boost stayed on for the normal camera. The v0.11 log shows the real marker: during
// the interrogation the player's animation set is 8 (or 9 for environment ones); when the enemy breaks
// it changes to 6 = holding a human shield (g_holdingHostage). So: interrogation running + holding a
// shield = shield phase = normal camera, [Hostage] buttons. (v0.11 used the prompts - they flicker.)
static bool g_shieldPhaseLogged = false;
static bool InterrogationShieldMode()
{
    bool on = *(volatile DWORD*)kCurrentInterrogation != 0 && g_holdingHostage;
    if (on != g_shieldPhaseLogged) {
        g_shieldPhaseLogged = on;
        Log(on ? "interrogation: enemy broken, held as shield - boost off" : "interrogation: shield phase over");
    }
    return on;
}
// force phase = interrogation running and the enemy not (yet) a shield: [Interrogation] buttons + boost
static bool InterrogationForcePhase()
{
    return *(volatile DWORD*)kCurrentInterrogation != 0 && !g_holdingHostage;
}
static bool InterrogationActiveForPhoto() { __try { return *(volatile DWORD*)kCurrentInterrogation != 0; } __except (1) { return false; } }

// ---------------------------------------------------------------- v0.28: console-style interrogation stick
// 0x4D2620 (called by the interrogation update 0x4D2B40 every 16 ms) turns mouse up/down movement into a
// PRETEND stick at 0x7E2E98: it adds the movement, lets it drift back to 0 (x 0.95) and clamps it to -1..1.
// That pretend stick is the input: types 0/1 use it directly as the position (like a real stick held still).
//   004D270D  8B 15 98 2E 7E 00   MOV EDX,[0x7E2E98]   <- we jump out here
//   004D2713  89 54 24 08         MOV [ESP+8],EDX      <- the working value (local_14)
// The hook hands back pretend stick + real right stick, clamped. The mouse / trackpad keeps working
// (the pretend stick is left untouched), and the stick behaves like the console: hold it halfway = halfway.
// Flags must survive: the TEST at 0x4D2703 is used by the JZ at 0x4D2717. The x87 stack is empty here.
static DWORD g_consoleStickOut = 0;
static int   g_consoleStickLogs = 0;

// v0.28.1: the stick is read EXACTLY like the Xbox version (default.xbe):
//  - stick reader 0x1890A0: each axis on its own, raw / 32768 (Y flipped so up = negative, same as SDL),
//    deadzone 0.30 per axis, then (|v| - 0.30) x 1.6666667, clamped to 1  -> full force at 90 % tilt.
//  - interrogation input 0x121B80: reads the right AND the left stick; the one moved last (> 0.1) is used.
//  - everything after that (the five interrogation types, the spring) is the same code as the PC's.
static float XboxAxis(float v)
{
    float a = fabsf(v);
    if (a < 0.30f) return 0.0f;
    float r = (a - 0.30f) * 1.6666667f;
    if (r > 1.0f) r = 1.0f;
    return v < 0 ? -r : r;
}

static DWORD __cdecl ConsoleStickValue()
{
    static bool useLeft = false;
    float v = *(volatile float*)0x7E2E98;          // pretend stick from the mouse / trackpad
    if (g_cfg.consoleStick && g_pad && !g_enhOpen && !g_photo && InterrogationForcePhase()) {   // v0.50.1: not in photo mode
        float ry = XboxAxis(Axis(SDL_GAMEPAD_AXIS_RIGHTY));
        float ly = g_cfg.interrogationLeftStick ? XboxAxis(Axis(SDL_GAMEPAD_AXIS_LEFTY)) : 0.0f;
        if (useLeft) { if (fabsf(ry) > 0.1f) useLeft = false; }
        else         { if (fabsf(ly) > 0.1f) useLeft = true; }
        float s = useLeft ? ly : ry;
        v += s;
        if (v < -1.0f) v = -1.0f;
        if (v >  1.0f) v =  1.0f;
        if (g_consoleStickLogs < 40 && fabsf(s) > 0.5f) {
            static DWORD t0 = 0;
            if (GetTickCount() - t0 > 500) { t0 = GetTickCount(); ++g_consoleStickLogs;
                Log("console stick (%s): stick %.2f + mouse %.2f -> %.2f", useLeft ? "left" : "right", s, *(volatile float*)0x7E2E98, v); }
        }
    }
    DWORD out; memcpy(&out, &v, 4);
    return out;
}

static __declspec(naked) void Hook_ConsoleStick()
{
    __asm {
        pushfd
        pushad
        call ConsoleStickValue
        mov  g_consoleStickOut, eax
        popad
        popfd
        mov  edx, g_consoleStickOut
        push 0x4D2713
        ret
    }
}

static void InstallConsoleStick()
{
    BYTE* p = (BYTE*)0x4D270D;
    static const BYTE want[] = { 0x8B, 0x15, 0x98, 0x2E, 0x7E, 0x00, 0x89, 0x54, 0x24, 0x08 };
    if (memcmp(p, want, sizeof(want)) != 0) { Log("console stick: 0x4D270D not as expected - skipped"); return; }
    DWORD old; VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE9; *(LONG*)(p + 1) = (LONG)((DWORD)Hook_ConsoleStick - (0x4D270D + 5)); p[5] = 0x90;
    VirtualProtect(p, 6, old, &old);
    Log("console stick hook installed (interrogations: right stick like the console, %s)", g_cfg.consoleStick ? "on" : "off in the ini");
}
static int VkToScan(int vk);
static void UpdateFooterLabels();
static void UpdateHitRumble();
static void UpdateQuickKillRumble();
static void GorePendingUpdate();   // v0.46.3
static bool ScheduleQuickKillRumble(const char* sound);
static void ImpactRumble(int sound);
static bool EnvKillSound(int sound);
static void UpdateEnvKillPulses();
static void EnhUpdate();              // v0.20: in-game enhancements menu (open / navigate / close)

// v0.11: the game's Help action (0x20, shown during interrogations) is bound to F1, which other
// plugins also use. Move it to [Settings] HelpKey (bindings: player object 0x7CF804 + 0x224, 16 bytes
// per action, key at +0x28). HelpKey=F1 puts it back, HelpKey=NONE leaves the game's binding alone.
static void ApplyHelpKey()
{
    if (!g_cfg.helpKeyScan) return;
    if (g_cfg.helpKeyScan < 0) {
        int vk = -g_cfg.helpKeyScan;
        g_cfg.helpKeyScan = VkToScan(vk);
        Log("help key: game's Help action -> scan code 0x%02X", g_cfg.helpKeyScan);
        if (!g_cfg.helpKeyScan) return;
    }
    __try {
        DWORD so = *(volatile DWORD*)0x7CF804;
        if (!so) return;
        volatile int* k = (volatile int*)(so + 0x224 + 0x28 + 0x20 * 16);
        if (*k != g_cfg.helpKeyScan) {
            Log("help key: binding was 0x%02X, set to 0x%02X", *k, g_cfg.helpKeyScan);
            *k = g_cfg.helpKeyScan;
        }
    } __except (1) {}
}

// ---------------------------------------------------------------- v0.40: Developer page (the console switches)
// The Xbox / PS2 builds have a developer console (228 commands). The PC build dropped the console but KEPT the game code
// behind many switches - the variables are read and never written on PC (found by matching the Xbox code, see
// notes\CUT_CONTENT_CATALOG.md section 24). These are set from here every frame:
//   Big_head_mode         byte 0x7CF81C  (bone 8 = head scaled x2.5, FUN 0x532FF0)
//   Old_style_reticle     byte 0x7D1110  (FUN around 0x4C5CD8)
//   No_respawn            byte 0x7EEE40  (enemy spawner, 0x4DFE22)
//   Unlimited_env_actions byte 0x7D10CC  (0x4C3E30 / 0x4C3E60: env kills never used up)
//   Hud_disabled          byte 0x736738  (HUD main 0x465600, score 0x46B30C)
//   invulnerable          player flags +0x84 bit 4 (Xbox +0x80; 7 damage checks on both)
//   slaughter_cheat       player +0x168C = 20.0 (slaughter meter full, Xbox code 0x101287)
//   slowmo                time scale float 0x6769FC (1 = normal, bigger = slower)
//   show_hud_map          byte 0x7369D0  (HUD map / radar drawer 0x466350, called by the HUD every frame; draws with
//                         lines, no textures; sub-parts gated by 0x61FAD4 / 0x61FAD5 = on by default)
//   display_music_name    byte 0x70FCE0  ("Current song: %s" pickup-style message at 0x4609BB)
//   toggle_crouch_mode    player +0x46F byte (1 = press to toggle) + +0x298 dword (-1 / 0); readers 0x4AD4D0 0x4BE965 0x4C0732
//   toggle_zoom_mode      player +0x470 byte + +0x468 dword (same pattern, Xbox handler 0x12DA00)
int          g_devLogBones = 0;   // v0.50.1: [Developer] LogBones - bone / placement readout + L3+R3 player dumps
static int   g_devBigHead = 0, g_devOldReticle = 0, g_devNoRespawn = 0, g_devUnlimitedEnv = 0, g_devHudOff = 0;
static int   g_devInvuln = 0, g_devSlaughter = 0, g_devHudMap = 0, g_devSongNames = 0, g_devCrouchToggle = 0, g_devZoomToggle = 0;
static float g_devSlowmo = 1.0f;
extern int g_levelPoolMB;        // v0.58: defined with PatchLevelPools
static int   g_bwFilter = 1;     // v0.58: [Display] BlackWhite: environment kills in black & white (v0.63: kills only)
static int   g_interKillScore = 0; // v0.66: [Gameplay] InterrogationKillScore 0 unchanged, 1 penalty (retail), 2 none, 3 bonus
static int   g_achOn = 1;        // v0.68: [Achievements] Enabled
static int   g_bwSlaughter = 1;  // v0.63: [Display] BlackWhiteSlaughter: Slaughter mode in black & white
static int   g_devForceBW = 0;   // v0.63 debug: [Developer] ForceBlackWhite=1 keeps the B&W screen on (seam test; not in the menu)
static int   g_devHudKey = 0;    // v0.50.5: [Developer] HudToggle 0 off, 1-4 = hold LB + D-pad Up / Down / Left / Right
static int   g_devHudVk = 0;     // v0.50.5: [Developer] HudToggleKey = any keyboard key (F8, H, NUMPAD5 ...), empty = none
static void HudToggleFrame()
{
    static bool was = false, keyWas = false;
    if (g_devHudVk && !g_enhOpen) {   // a keyboard key (on the Deck: put it on any button with Steam Input)
        bool k = false;
        HWND w = GetForegroundWindow(); DWORD pid = 0;
        if (w) GetWindowThreadProcessId(w, &pid);
        if (pid == GetCurrentProcessId()) k = (GetAsyncKeyState(g_devHudVk) & 0x8000) != 0;
        if (k && !keyWas) { g_devHudOff = !g_devHudOff; Log("dev: HUD %s (key %02X)", g_devHudOff ? "off" : "on", g_devHudVk); }
        keyWas = k;
    }
    if (g_devHudKey < 1 || g_devHudKey > 4 || g_enhOpen) { was = false; return; }
    static const int dp[4] = { P_DUP, P_DDOWN, P_DLEFT, P_DRIGHT };
    int b = dp[g_devHudKey - 1];
    bool on = g_raw[P_LB] && g_raw[b];
    if (on && !was) {
        g_devHudOff = !g_devHudOff;
        Log("dev: HUD %s (LB + %s)", g_devHudOff ? "off" : "on", kPadNames[b]);
    }
    if (on) { g_padIgnore[b] = true; g_down[b] = false; }   // the D-pad press does not switch weapons
    was = on;
}
// v0.60: the env-kill finish writes 1 to the B&W byte with "mov byte ptr [0x70E231], 1" (0x4D84EB, immediate at
// 0x4D84F1). ermaccer's PunisherPlugin (bDisableBlackWhiteFilterOnKills = true) sets that immediate to 0 at start,
// which beat our "On" (Deck test Nov 14: no B&W on interrogation kills). Our option now owns the immediate.
// v0.63: two switches. The game turns the black & white screen (byte 0x70E231) on in two places, each a
// "mov byte ptr [0x70E231], 1": env-kill finish 0x4D84EB (FUN_004D8150) and Slaughter-mode start 0x4C165B
// (FUN_004C0480). Each option owns the immediate of its own instruction (code bytes checked first). Clearing
// (slaughter end 0x4B84C0 / 0x4B9480, 0x4B7D20) is left to the game.
static void BwImm(DWORD at, int on, const char* what)
{
    BYTE want = (BYTE)(on ? 1 : 0);
    volatile BYTE* p = (volatile BYTE*)(at + 6);
    if (*(volatile WORD*)at != 0x05C6 || *(volatile DWORD*)(at + 2) != 0x0070E231) return;   // not the US exe code
    if (*p == want) return;
    BYTE was = *p;
    DWORD old;
    if (!VirtualProtect((void*)p, 1, PAGE_EXECUTE_READWRITE, &old)) return;
    *p = want;
    VirtualProtect((void*)p, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)at, 7);
    static int logged = 0;
    if (logged++ < 8) Log("black & white %s: %s (byte %08X = %d%s)", what, want ? "on" : "off", at + 6, want,
                          (want && !was) ? ", was 0 - another mod had switched it off" : "");
}
// v0.66 (tester tester): interrogation kills cost points. The score code (FUN_004BA600) uses a multiplier for that
// kill type: "mov dword ptr [esp+0x14], 1.0" at 0x4BA9B2 (immediate 0x4BA9B6) x a negative table value.
// ermaccer's PunisherPlugin bDisableNegativePoints writes -2.0 there (= double the amount as a bonus).
// 0 = leave it (game / other mods), 1 = 1.0 retail penalty, 2 = 0.0 no points, 3 = -1.0 the same amount as a bonus.
static void InterKillScore()
{
    if (g_interKillScore <= 0 || g_interKillScore > 3) return;
    static const float k[4] = { 1.0f, 1.0f, 0.0f, -1.0f };
    if (*(volatile DWORD*)0x4BA9B2 != 0x142444C7) return;   // mov dword ptr [esp+0x14], imm32 - not the US exe code
    volatile float* p = (volatile float*)0x4BA9B6;
    if (*p == k[g_interKillScore]) return;
    DWORD old;
    if (!VirtualProtect((void*)p, 4, PAGE_EXECUTE_READWRITE, &old)) return;
    *p = k[g_interKillScore];
    VirtualProtect((void*)p, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)0x4BA9B2, 8);
    static int logged = 0;
    if (logged++ < 4) Log("interrogation kill score: multiplier %.1f (%s)", k[g_interKillScore],
                          g_interKillScore == 1 ? "penalty" : g_interKillScore == 2 ? "no points" : "bonus");
}
// v0.81 (tester tester): Frank's shadow goes faint with a wide FOV or the camera far back. The shadow draw
// (FUN_0053EC80, called from the "Render shadows" step of FUN_004A0A10) gives each shadow caster a distance from the
// camera (FUN_00512C10: camera distance / 0x913514 * 0x91351C - so a wider FOV counts as further away). Below
// the float at 0x677618 (5.0, only read: 0x53E857 + 0x53EC80) the real shadow (render-to-texture) is drawn; from
// 0.75 x that it fades into the faint round blob (simple_shadow.tga). [Display] ShadowDistance = that float in m.
static int g_shadowDist = 12;
static void ShadowDistFrame()
{
    static int state = 0;                                  // 0 = not checked, 1 = ok, 2 = not the US exe value
    volatile float* p = (volatile float*)0x677618;
    if (state == 0) {
        state = (*p == 5.0f) ? 1 : 2;
        if (state == 2) Log("shadow distance: 0x677618 is %.3f, not the game's 5.0 - left alone", *p);
    }
    if (state != 1) return;
    int d = g_shadowDist < 5 ? 5 : (g_shadowDist > 40 ? 40 : g_shadowDist);
    if (*p != (float)d) {
        *p = (float)d;
        static int logged = 0;
        if (logged++ < 6) Log("shadow distance: %d m (game 5)", d);
    }
}
static void BwKillByte()
{
    static int lastK = -1, lastS = -1;
    BwImm(0x4D84EB, g_bwFilter, "environment kills");
    BwImm(0x4C165B, g_bwSlaughter, "Slaughter mode");
    // switched off while the screen is black & white -> back to colour now (not every frame: the other one may be on)
    if ((lastK == 1 && !g_bwFilter) || (lastS == 1 && !g_bwSlaughter)) *(volatile BYTE*)0x70E231 = 0;
    lastK = g_bwFilter; lastS = g_bwSlaughter;
}
// v0.72: when an interrogation starts, log whether the hit / kill effect files the env kills use are loaded in this
// level (-1 = the level file does not have it -> no blood). Casket hit blood needs env_kill_cskt_hit.rxe in Mort1.vpp.
static void EnvFxCheck()
{
    static DWORD last = 0;
    DWORD grp = *(volatile DWORD*)0x7E2DBC;
#ifdef EC_RELEASE
    grp = 0;                                               // test log - not in the release
#endif
    if (grp && grp != last) {
        static const char* names[] = { "env_kill_cskt_hit", "env_kill_cskt_slm", "env_kill_semi", "blood_hit_0", "blood_hit_2" };
        char b[256]; int o = snprintf(b, sizeof(b), "interrogation start: kill effect id %d; effects loaded:", *(volatile int*)(grp + 0x20));
        for (const char* n : names) o += snprintf(b + o, sizeof(b) - o, " %s=%d", n, ((int (__cdecl*)(const char*))0x40E440)(n));
        Log("%s", b);
    }
    last = grp;
}
static void DevApply()
{
    static int lastInvuln = 0;
    __try { EnvFxCheck(); } __except (1) {}
    static float lastSlow = 1.0f;
    __try {
        *(volatile BYTE*)0x7CF81C = (BYTE)(g_devBigHead ? 1 : 0);
        *(volatile BYTE*)0x7D1110 = (BYTE)(g_devOldReticle ? 1 : 0);
        *(volatile BYTE*)0x7EEE40 = (BYTE)(g_devNoRespawn ? 1 : 0);
        *(volatile BYTE*)0x7D10CC = (BYTE)(g_devUnlimitedEnv ? 1 : 0);
        *(volatile BYTE*)0x736738 = (BYTE)((g_devHudOff || g_photo) ? 1 : 0);
        *(volatile BYTE*)0x7369D0 = (BYTE)(g_devHudMap ? 1 : 0);
        if (g_devForceBW) *(volatile BYTE*)0x70E231 = 1;
        InterKillScore();                                  // v0.66
        ShadowDistFrame();                                 // v0.81
        BwKillByte();                                      // v0.63: kills / Slaughter mode switches (v0.60: beats PunisherPlugin)
        if (g_devHudMap) {
            // v0.50.5: the map's place / size (0x7369AC..0x7369C8) is set by an Xbox init function (0x9ABD0) that the PC
            // build dropped -> all 0 = nothing drawn. Xbox values for 640x480: 100 x 100 px, 30 px from the left,
            // 130 px above the bottom, range 100, dot 10. Scaled to the screen height here.
            int H = *(volatile int*)0x913254;
            static int laidOutFor = 0;
            if (H > 0 && (H != laidOutFor || *(volatile int*)0x7369B4 == 0)) {
                float k = H / 480.0f; int sz = (int)(100 * k + 0.5f);
                *(volatile int*)0x7369AC = 1;    *(volatile int*)0x7369B0 = 0;
                *(volatile int*)0x7369B4 = sz;   *(volatile int*)0x7369B8 = sz;
                *(volatile int*)0x7369BC = H - (int)(130 * k + 0.5f);
                *(volatile int*)0x7369C0 = (int)(30 * k + 0.5f);
                *(volatile int*)0x7369C4 = 100;  *(volatile int*)0x7369C8 = 10;
                laidOutFor = H;
                Log("dev: HUD map laid out %dx%d px at %d,%d (screen height %d)", sz, sz, *(volatile int*)0x7369C0, *(volatile int*)0x7369BC, H);
            }
        }
        *(volatile BYTE*)0x70FCE0 = (BYTE)(g_devSongNames ? 1 : 0);
        {   // v0.50.5: env quick kills (fryer, stove, fuse box ...) use a spot trigger whose use count (+0x58) reaches its
            // limit (+0x54); the spot finder 0x4C1DA0 then skips it (jg at 0x4C216B). Unlimited_env_actions only
            // reset the count after env INTERROGATIONS. With the option on, the "used up" jump is NOPed.
            static int envPatched = 0;
            if (g_devUnlimitedEnv != envPatched) {
                static const BYTE jg[6] = { 0x0F, 0x8F, 0x71, 0x03, 0x00, 0x00 }, nop[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
                BYTE* at = (BYTE*)0x4C216B;
                if (!memcmp(at, g_devUnlimitedEnv ? jg : nop, 6)) {
                    DWORD old; VirtualProtect(at, 6, PAGE_EXECUTE_READWRITE, &old);
                    memcpy(at, g_devUnlimitedEnv ? nop : jg, 6);
                    VirtualProtect(at, 6, old, &old);
                    Log("dev: env quick-kill spots %s", g_devUnlimitedEnv ? "never used up" : "back to normal");
                } else Log("dev: env quick-kill spot check not as expected - skipped");
                envPatched = g_devUnlimitedEnv;
            }
            // v0.50.8: user test - env QUICK-KILL spots still stayed used up (the hostage path does not reach 0x4C216B;
            // the glow / prompt 0x46AFD5 and the event handlers 0x4EA4DA.. check the same count). So: reset the use
            // count (+0x58) of every Environment_Action (event list 0x7EEEDC, count 0x7EEED8, type +0x48 == 0x33)
            // while nothing is running (no interrogation, no env kill group 0x7D112C, player record +0x1600 == -1).
            if (g_devUnlimitedEnv) {
                DWORD rec = *(volatile DWORD*)0x7CF804;
                int n = *(volatile int*)0x7EEED8;
                bool idle = rec && *(volatile int*)(rec + 0x1600) == -1 && !*(volatile DWORD*)kCurrentInterrogation
                            && !*(volatile DWORD*)0x7D112C;
                static int resetLogs = 0;
                if (idle && n > 0 && n < 4096) {
                    for (int i = 0; i < n; ++i) {
                        DWORD ev = ((volatile DWORD*)0x7EEEDC)[i];
                        if (!ev || *(volatile int*)(ev + 0x48) != 0x33) continue;
                        int used = *(volatile int*)(ev + 0x58);
                        if (used > 0) {
                            if (resetLogs < 10) { ++resetLogs; Log("dev: env spot %08X used %d of %d -> reset", ev, used, *(volatile int*)(ev + 0x54)); }
                            *(volatile int*)(ev + 0x58) = 0;
                        }
                    }
                }
                // ... and the trigger box that lists the spot (e.g. zoo3 altar "Stab_Kill_Trigger" 0x800044): trigger
                // +0x24C = times fired, +0x250 = max (-1 = no limit); 0x4F8BD0 ignores a box that reached its max.
                // Triggers = list at sentinel 0x7EFA08 (next +0x238); links +0x26C (handle, type), count +0x270.
                if (idle) {
                    typedef DWORD(__cdecl* EvFromHandle_t)(DWORD);
                    DWORD tr = *(volatile DWORD*)(0x7EFA08 + 0x238);
                    for (int guard = 0; tr && tr != 0x7EFA08 && guard < 8192; ++guard, tr = *(volatile DWORD*)(tr + 0x238)) {
                        int fired = *(volatile int*)(tr + 0x24C), mx = *(volatile int*)(tr + 0x250);
                        if (fired <= 0 || mx <= 0 || fired < mx) continue;
                        int nl = *(volatile int*)(tr + 0x270);
                        DWORD links = *(volatile DWORD*)(tr + 0x26C);
                        bool spot = false;
                        for (int j = 0; j < nl && j < 64 && links && !spot; ++j) {
                            if (((volatile DWORD*)links)[j * 2 + 1] != 0) continue;
                            DWORD ev = ((EvFromHandle_t)0x4EF5C0)(((volatile DWORD*)links)[j * 2]);
                            spot = ev && *(volatile int*)(ev + 0x48) == 0x33;
                        }
                        if (!spot) continue;
                        if (resetLogs < 20) { ++resetLogs; Log("dev: env spot trigger box %08X fired %d of %d -> reset", tr, fired, mx); }
                        *(volatile int*)(tr + 0x24C) = 0;
                    }
                }
            }
        }
        // v0.50.5: invulnerable is a flag of the Punisher's ENTITY (0x6B70FC, +0x84 bit 4 = the flag the level event
        // "Invulnerable" sets; damage checks e.g. 0x42F19D / 0x4AC2EC). v0.40-v0.50.4 wrote it into the player
        // record 0x7CF804 instead (= no effect).
        {
            DWORD ent = *(volatile DWORD*)0x6B70FC;
            if (ent) {
                volatile DWORD* fl = (volatile DWORD*)(ent + 0x84);
                if (g_devInvuln) *fl |= 4;
                else if (lastInvuln) *fl &= ~4u;
            }
        }
        DWORD pl = *(volatile DWORD*)0x7CF804;
        if (pl) {
            if (g_devSlaughter) *(volatile float*)(pl + 0x168C) = 20.0f;
            static int lastCrouch = -1, lastZoom = -1;
            if (g_devCrouchToggle != lastCrouch || *(volatile BYTE*)(pl + 0x46F) != (BYTE)g_devCrouchToggle) {
                *(volatile BYTE*)(pl + 0x46F) = (BYTE)g_devCrouchToggle;
                *(volatile int*)(pl + 0x298) = g_devCrouchToggle ? -1 : 0;
                lastCrouch = g_devCrouchToggle;
            }
            if (g_devZoomToggle != lastZoom || *(volatile BYTE*)(pl + 0x470) != (BYTE)g_devZoomToggle) {
                *(volatile BYTE*)(pl + 0x470) = (BYTE)g_devZoomToggle;
                *(volatile int*)(pl + 0x468) = g_devZoomToggle ? -1 : 0;
                lastZoom = g_devZoomToggle;
            }
        }
        lastInvuln = g_devInvuln;
        if (!g_photo && (g_devSlowmo != 1.0f || lastSlow != 1.0f)) *(volatile float*)0x6769FC = g_devSlowmo;
        lastSlow = g_devSlowmo;
    } __except (1) {}
}
static void SendKeys()
{
    DevApply();
    UpdateMenuDirections();
    EnhUpdate();                          // v0.20 (if the menu is open, g_down is all false -> keys released)
    for (int i = 0; i < P_COUNT; ++i) {
        const Target* t = &g_cfg.map[i];
        if (g_menuActive && g_cfg.menuMap[i].kind != T_NONE) t = &g_cfg.menuMap[i];
        else if (InterrogationForcePhase() && g_cfg.interMap[i].kind != T_NONE) t = &g_cfg.interMap[i];
        else if (g_holdingHostage && g_cfg.hostageMap[i].kind != T_NONE) t = &g_cfg.hostageMap[i];
        int want = (t->kind == T_KEY && g_down[i]) ? t->value : 0;
        // a held button keeps the key it started with (e.g. B pressed in a menu = Esc until released,
        // even if the menu closes meanwhile - otherwise it would turn into a dodge)
        if (g_down[i] && g_keySentVk[i]) want = g_keySentVk[i];
        if (want != g_keySentVk[i]) {
            if (g_keySentVk[i]) SendKeyNow(i, g_keySentVk[i], 0);
            if (want) SendKeyNow(i, want, 1);
            g_keySentVk[i] = want;
        }
    }
}

// ---------------------------------------------------------------- mouse injection (DirectInput)
static const GUID kGUID_SysMouse = { 0x6F1D2B60, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };

typedef HRESULT (WINAPI* DI8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID*, void*);
typedef HRESULT (WINAPI* CreateDevice_t)(void*, REFGUID, void**, void*);
typedef HRESULT (WINAPI* GetDeviceState_t)(void*, DWORD, LPVOID);
typedef HRESULT (WINAPI* GetDeviceData_t)(void*, DWORD, LPVOID, LPDWORD, DWORD);
static DI8Create_t      o_DI8Create = nullptr;
static CreateDevice_t   o_CreateDevice = nullptr;
static GetDeviceState_t o_GetDeviceState = nullptr;
static GetDeviceData_t  o_GetDeviceData = nullptr;
static void* g_mouse = nullptr;

static bool g_wasInterrogating = false;
static float g_accX = 0, g_accY = 0;       // sub-count remainder of stick movement
static DWORD g_seq = 0x40000000;

// ---------------------------------------------------------------- v0.52: console aim assist (Xbox / PS2), PC lost it
// Catalog 27.9 + 30: the PC still picks a target (player record *(0x7CF804) +0x56C = target handle, -1 none; object =
// 0x4A9460(handle), position +0x1C) but never applies the console's
//   (a) aim friction: look speed lowered by 2/30 per frame while on the target, floor 0.9 - (dist - 1) x 0.0571,
//       recovering off target;  (b) magnetism: the heading follows the target, rate 2 - (dist - 1) / 7, none beyond
//       15 m;  (c) turn ramp: stick past 0.99 -> look speed 1x -> 3.25x over 0.4 s (1.67x while friction is on).
// Done here on the right stick. Magnetism needs mouse counts per radian of turn: measured live from the player's own
// turning (yaw change of the camera 0x67AB78 vs counts sent), used only once it is stable.
static int   g_aimAssist = 0;           // [Settings] AimAssist (Controls page)
static float g_aimMagnet = 1.0f;       // [Settings] AimMagnetStrength (ini only)
static float g_aaFriction = 1.0f, g_aaRamp = 0.0f, g_aaCpr = 0.0f, g_aaLastYaw = 0.0f;
static LONG  g_aaLastCounts = 0;
static int   g_aaCprSamples = 0;
static void AimAssist(float& sx, float& sy, float dt)
{
    if (!g_aimAssist || dt <= 0) { g_aaFriction = 1.0f; g_aaRamp = 0; return; }
    float camPos[3], fwd[3];
    DWORD obj = 0; float dist = 0, tp[3] = {0, 0, 0};
    __try {
        volatile float* pos = (volatile float*)0x67AB6C; volatile float* rot = (volatile float*)0x67AB78;
        for (int i = 0; i < 3; ++i) { camPos[i] = pos[i]; fwd[i] = rot[6 + i]; }
        DWORD rec = *(volatile DWORD*)0x7CF804;
        int h = rec ? *(volatile int*)(rec + 0x56C) : -1;
        if (h != -1) obj = ((DWORD(__cdecl*)(int))0x4A9460)(h);
        if (obj) { for (int i = 0; i < 3; ++i) tp[i] = *(volatile float*)(obj + 0x1C + 4 * i); }
    } __except (1) { return; }
    // live calibration: counts sent last frame vs the yaw change it caused (only clear, steady turns)
    float yaw = atan2f(fwd[0], fwd[2]);
    float dyaw = yaw - g_aaLastYaw;
    if (dyaw > 3.14159f) dyaw -= 6.28318f; if (dyaw < -3.14159f) dyaw += 6.28318f;
    if (labs(g_aaLastCounts) >= 3 && fabsf(dyaw) > 0.002f && fabsf(dyaw) < 0.5f) {
        float c = (float)g_aaLastCounts / dyaw;               // signed counts per radian
        g_aaCpr = g_aaCprSamples ? g_aaCpr * 0.95f + c * 0.05f : c;
        if (++g_aaCprSamples == 60) Log("aim assist: calibrated %.0f mouse counts per radian", g_aaCpr);
    }
    g_aaLastYaw = yaw;
    // (c) turn ramp at full tilt
    float mag = sqrtf(g_rxLin * g_rxLin + g_ryLin * g_ryLin);
    g_aaRamp = mag > 0.99f ? fminf(g_aaRamp + dt, 0.4f) : 0.0f;
    float rampMax = g_aaFriction < 0.999f ? 1.67f : 3.25f;
    float ramp = 1.0f + (rampMax - 1.0f) * (g_aaRamp / 0.4f);
    if (!obj) {
        g_aaFriction = fminf(1.0f, g_aaFriction + 2.0f * dt);
        sx *= g_aaFriction * ramp; sy *= g_aaFriction * ramp;
        return;
    }
    float d[3] = { tp[0] - camPos[0], tp[1] - camPos[1], tp[2] - camPos[2] };
    dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    static DWORD lastObj = 0; static int tLogs = 0;
    if (obj != lastObj && tLogs < 15) { ++tLogs; Log("aim assist: target %08X at %.1f m (friction floor %.2f)", obj, dist, fmaxf(0.1f, fminf(0.9f, 0.9f - (dist - 1.0f) * 0.0571f))); }
    lastObj = obj;
    // on target = the target is within ~6 degrees of the view direction
    float dl = dist > 0.01f ? dist : 0.01f;
    float cosA = (d[0] * fwd[0] + d[1] * fwd[1] + d[2] * fwd[2]) / dl;
    bool onTarget = cosA > 0.9945f;
    // (a) friction
    float floorF = 0.9f - (dist - 1.0f) * 0.0571f; if (floorF < 0.1f) floorF = 0.1f; if (floorF > 0.9f) floorF = 0.9f;
    if (onTarget) g_aaFriction = fmaxf(floorF, g_aaFriction - 2.0f * dt);
    else          g_aaFriction = fminf(1.0f, g_aaFriction + 2.0f * dt);
    sx *= g_aaFriction * ramp; sy *= g_aaFriction * ramp;
    // (b) magnetism: gentle heading pull while the stick is (nearly) still, close targets only, within 12 degrees
    if (dist < 15.0f && fabsf(g_rxLin) < 0.25f && g_aaCprSamples >= 60 && fabsf(g_aaCpr) > 10.0f && cosA > 0.978f) {
        float tyaw = atan2f(d[0], d[2]);
        float err = tyaw - yaw; if (err > 3.14159f) err -= 6.28318f; if (err < -3.14159f) err += 6.28318f;
        float rate = 2.0f - (dist - 1.0f) / 7.0f; if (rate < 0) rate = 0;
        float turn = err * fminf(1.0f, rate * dt * 2.0f) * g_aimMagnet;   // fraction of the error per frame
        g_accX += turn * g_aaCpr;
    }
}
static HRESULT WINAPI Hook_GetDeviceState(void* self, DWORD size, LPVOID data)
{
    HRESULT hr = o_GetDeviceState(self, size, data);
    if (self != g_mouse || FAILED(hr) || !data || size < 16) return hr;
    ++g_frameNo;
    PadUpdate();
    SendKeys();

    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    float dt = (float)(now.QuadPart - g_lastUpdate.QuadPart) / (float)g_qpf.QuadPart;
    g_lastUpdate = now;
    if (dt > 0.1f) dt = 0.1f;               // after pauses / loading

    float ry = g_cfg.invertY ? -g_ry : g_ry;
    // during interrogations the game reads mouse movement as "force" - the stick needs much more range
    bool interrogating = *(volatile DWORD*)kCurrentInterrogation != 0;
    if (interrogating != g_wasInterrogating) {
        DWORD g = *(volatile DWORD*)kCurrentInterrogation;
        Log("interrogation %s%s", interrogating ? "started" : "ended",
            interrogating ? ((*(volatile BYTE*)(g + 0xCC) & 0x2) ? " (standard)" : " (environment)") : "");
        g_wasInterrogating = interrogating;
        if (interrogating) g_placeStart = GetTickCount(), g_placeStep = 0;
    }
    if (g_devLogBones && interrogating && g_placeStep < 4 && GetTickCount() - g_placeStart > (DWORD)(1000 + 2000 * g_placeStep)) {
        char why[32]; snprintf(why, sizeof(why), "%.0f s into the interrogation", (GetTickCount() - g_placeStart) / 1000.0);
        LogPairPlacement(why); ++g_placeStep;
    }
    // group+0xCC bit 0x2 = "+Permanent" = one of the 4 standard interrogations that work anywhere
    DWORD grp = *(volatile DWORD*)kCurrentInterrogation;
    ApplyHelpKey();
    UpdateFooterLabels();
    UpdateHitRumble();
    UpdateQuickKillRumble();
    GorePendingUpdate();   // v0.46.3
    UpdateEnvKillPulses();
    if (InterrogationShieldMode()) interrogating = false;   // (called every frame so its log state stays right)   // v0.11: shield after the interrogation = normal camera
    bool standard = interrogating && (*(volatile BYTE*)(grp + 0xCC) & 0x2);
    float boost = !interrogating ? 1.0f : (standard ? g_cfg.interrogationBoost : g_cfg.envInterrogationBoost);
    float sx = g_rx, sy = ry;
    if (!interrogating && !g_photo) AimAssist(sx, sy, dt);   // v0.52: console aim assist (off by default)
    if (interrogating) {           // own curve: a gentle push must give a gentle force (slow push into the chipper)
        float lm = sqrtf(g_rxLin * g_rxLin + g_ryLin * g_ryLin);
        float cm = lm > 0 ? powf(lm, g_cfg.interrogationCurve) / lm : 0;
        sx = g_rxLin * cm; sy = (g_cfg.invertY ? -g_ryLin : g_ryLin) * cm;
    }
    if (interrogating && g_cfg.interrogationNativeStick) { sx = 0; sy = 0; }   // the game reads the stick itself
    if (interrogating && g_cfg.consoleStick && g_pad) { sx = 0; sy = 0; }      // v0.28: the stick goes in at 0x4D270D instead
    g_accX += sx * g_cfg.lookSensX * g_cfg.lookScale * boost * dt;   // v0.29: LookSensitivity multiplier
    g_accY += sy * g_cfg.lookSensY * g_cfg.lookScale * boost * dt;
    LONG ix = (LONG)g_accX, iy = (LONG)g_accY;
    g_aaLastCounts = (!interrogating && !g_photo) ? ix : 0;   // v0.52: aim assist calibration
    g_accX -= ix; g_accY -= iy;

    LONG* m = (LONG*)data;                  // DIMOUSESTATE / DIMOUSESTATE2: lX, lY, lZ, then buttons
    if (interrogating && g_cfg.logInterrogation) {
        static DWORD t0 = 0; static int frames = 0; static LONG mx = 0, my = 0, px = 0, py = 0, sxs = 0, sys = 0;
        frames++; mx += labs(m[0]); my += labs(m[1]); sxs += labs(ix); sys += labs(iy);
        if (labs(m[1]) > py) py = labs(m[1]); if (labs(m[0]) > px) px = labs(m[0]);
        if (GetTickCount() - t0 > 500) {
            if (mx || my || sxs || sys)
                Log("interrogation input / frame: trackpad-mouse x %.1f y %.1f (peak %ld %ld) | stick x %.1f y %.1f",
                    (float)mx / frames, (float)my / frames, px, py, (float)sxs / frames, (float)sys / frames);
            t0 = GetTickCount(); frames = 0; mx = my = px = py = sxs = sys = 0;
        }
    }
    m[0] += ix; m[1] += iy;
    BYTE* buttons = (BYTE*)data + 12;
    int nButtons = (int)size - 12;
    for (int i = 0; i < P_COUNT; ++i) {
        const Target& t = g_cfg.map[i];
        if (t.kind == T_MOUSE_BUTTON && g_down[i] && t.value < nButtons) buttons[t.value] |= 0x80;
        if (t.kind == T_WHEEL) { if (g_down[i] && !g_wheelPrev[i]) m[2] += t.value; g_wheelPrev[i] = g_down[i]; }
    }
    return hr;
}

static HRESULT WINAPI Hook_GetDeviceData(void* self, DWORD cb, LPVOID rgdod, LPDWORD inout, DWORD flags)
{
    DWORD cap = inout ? *inout : 0;
    HRESULT hr = o_GetDeviceData(self, cb, rgdod, inout, flags);
    if (self != g_mouse || FAILED(hr) || !rgdod || !inout || (flags & 1 /*DIGDD_PEEK*/) || cb < 16) return hr;

    DWORD n = *inout;
    auto push = [&](DWORD ofs, DWORD value) {
        if (n >= cap) return;
        BYTE* p = (BYTE*)rgdod + n * cb;
        memset(p, 0, cb);
        DWORD* d = (DWORD*)p;
        d[0] = ofs; d[1] = value; d[2] = GetTickCount(); d[3] = ++g_seq;
        ++n;
    };
    // press/release events for pad buttons mapped to mouse buttons (DIMOFS_BUTTON0 = 12)
    PadUpdate();
    for (int i = 0; i < P_COUNT; ++i) {
        const Target& t = g_cfg.map[i];
        if (t.kind == T_MOUSE_BUTTON && g_down[i] != g_mouseSent[i] && n < cap) {
            push(12 + t.value, g_down[i] ? 0x80 : 0);
            g_mouseSent[i] = g_down[i];
        }
    }
    *inout = n;
    return hr;
}

static HRESULT WINAPI Hook_CreateDevice(void* self, REFGUID guid, void** dev, void* outer)
{
    HRESULT hr = o_CreateDevice(self, guid, dev, outer);
    if (SUCCEEDED(hr) && dev && *dev && IsEqualGUID(guid, kGUID_SysMouse)) {
        g_mouse = *dev;
        void** vt = *(void***)g_mouse;
        if (vt[9] != (void*)Hook_GetDeviceState)  PatchPointer(&vt[9],  (void*)Hook_GetDeviceState, (void**)&o_GetDeviceState);
        if (vt[10] != (void*)Hook_GetDeviceData)  PatchPointer(&vt[10], (void*)Hook_GetDeviceData,  (void**)&o_GetDeviceData);
        Log("mouse device hooked");
    }
    return hr;
}

static HRESULT WINAPI Hook_DI8Create(HINSTANCE inst, DWORD ver, REFIID iid, LPVOID* out, void* outer)
{
    HRESULT hr = o_DI8Create(inst, ver, iid, out, outer);
    if (SUCCEEDED(hr) && out && *out) {
        void** vt = **(void****)out;
        if (vt[3] != (void*)Hook_CreateDevice) PatchPointer(&vt[3], (void*)Hook_CreateDevice, (void**)&o_CreateDevice);
        Log("DirectInput hooked");
    }
    return hr;
}


// ---------------------------------------------------------------- v0.4 diagnostics: quick-kill blood decal
// Three call sites in the quick-kill code (function around 0x4CCCB0) are redirected through logging
// wrappers that call the original functions unchanged, so gameplay is not affected.
static void PatchCall(DWORD site, void* hook, DWORD expectedTarget)
{
    BYTE* p = (BYTE*)site;
    DWORD target = site + 5 + *(LONG*)(p + 1);
    if (p[0] != 0xE8 || target != expectedTarget) { Log("call at %08X not as expected - skipped", site); return; }
    DWORD old; VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    *(LONG*)(p + 1) = (LONG)((DWORD)hook - (site + 5));
    VirtualProtect(p, 5, old, &old);
    Log("hooked call at %08X", site);
}
// ---------------------------------------------------------------- v0.51: windowed mode + slaughter flashback whispers
// Found by comparing the PC demo 1.00 exe (2004-11-30) with retail (notes\CUT_CONTENT_CATALOG.md section 29).
// WINDOWED: 1.00 read a registry value "Windowed"; retail calls the D3D setup 0x511C80(w, h, fullscreen) with a
//   constant fullscreen = 1 (push 1 at 0x4DD761 = 6A 01). The flag goes to 0x913258; the present setup 0x516CC7
//   still turns it into D3D "Windowed". [Display] Windowed=1 -> the push becomes 0. Read at start (restart to apply).
// WHISPERS: slaughter start 0x4B85A0 looks up the foley "SL <level>" (0x461040 at 0x4B8705) and keeps the id in
//   0x666F88; during slaughter a 8-11 s timer plays it at the player (0x4BB385, English / language 2 only). No table
//   has any "SL <level>" -> id -1 -> nothing plays. 1.00 fell back to "SL FLASHBACK" (foley.tbl: SFX_SL_FLSHBCK_1-6,
//   in retail audio.vpp). [Beta] FlashbackWhispers=1 restores that fallback (checked at each slaughter start).
static int g_windowed = 0, g_whispers = 0;
static int g_whisperLogs = 0;
static int __cdecl Hook_SlaughterFoley(const char* name)
{
    typedef int(__cdecl* Foley_t)(const char*);
    int id = ((Foley_t)0x461040)(name);
    if (id == -1 && g_whispers) {
        id = ((Foley_t)0x461040)("SL FLASHBACK");
        if (g_whisperLogs++ < 10) Log("slaughter: '%s' has no sound -> flashback whispers (foley id %d)", name ? name : "?", id);
    }
    return id;
}
static void InstallWindowedAndWhispers(const char* ini)
{
    g_windowed = GetPrivateProfileIntA("Display", "Windowed", 0, ini) ? 1 : 0;
    g_whispers = GetPrivateProfileIntA("Beta", "FlashbackWhispers", 0, ini) ? 1 : 0;
    g_aimAssist = GetPrivateProfileIntA("Settings", "AimAssist", 0, ini) ? 1 : 0;              // v0.52
    { char v[32]; GetPrivateProfileStringA("Settings", "AimMagnetStrength", "1.0", v, sizeof(v), ini); g_aimMagnet = (float)atof(v);
      if (g_aimMagnet < 0) g_aimMagnet = 0; if (g_aimMagnet > 3) g_aimMagnet = 3; }
    PatchCall(0x4B8705, (void*)Hook_SlaughterFoley, 0x461040);   // always hooked; does nothing while the option is off
    if (g_windowed) {
        BYTE* p = (BYTE*)0x4DD761;
        if (p[0] == 0x6A && p[1] == 0x01) {
            DWORD old; VirtualProtect(p, 2, PAGE_EXECUTE_READWRITE, &old);
            p[1] = 0x00;
            VirtualProtect(p, 2, old, &old);
            Log("display: windowed mode on (fullscreen argument at 004DD762 = 0)");
        } else Log("display: windowed mode - code at 004DD761 not as expected (%02X %02X) - skipped", p[0], p[1]);
    }
}
static int g_diagCount = 0;
static int g_decalLayers = 2;     // [Settings] DecalLayers: 1 = normal, 2-4 = bloodier
static bool g_decalDiag = true;   // [Settings] DecalDiagnostics
static bool DiagOk() { return g_diagCount++ < 200; }
static DWORD CurQK() { return *(volatile DWORD*)0x7D1130; }

// 0x4CD62C: "did the grab-finish trigger fire?"  (0x4191D0(entity, trigger, -1) -> al)
typedef char (__cdecl* TrigCheck_t)(DWORD, DWORD, DWORD);
static char __cdecl Diag_GrabFinish(DWORD ent, DWORD trig, DWORD idx)
{
    char r = ((TrigCheck_t)0x4191D0)(ent, trig, idx);
    if (r && g_decalDiag && DiagOk()) {
        DWORD qk = CurQK();
        if (qk) Log("decal-diag: grab-finish fired, quick kill record %08X flags %08X size %.3f pos %.3f %.3f %.3f",
                    qk, *(DWORD*)(qk + 0x7C), *(float*)(qk + 0x24), *(float*)(qk + 4), *(float*)(qk + 8), *(float*)(qk + 0xC));
        else    Log("decal-diag: grab-finish fired, but no quick kill record is active");
    }
    return r;
}
// 0x4CDA26: gate before the decal (0x462170 must return non-zero)
static int __cdecl Diag_Gate()
{
    int r = ((int (__cdecl*)())0x462170)();
    if (g_decalDiag && DiagOk()) Log("decal-diag: gate 0x462170 returned %d (0 = decal skipped)", r);
    return r;
}
// 0x4CDAFB: the decal paint call itself (0x52F820, 6 arguments)
typedef int (__cdecl* Paint_t)(DWORD, DWORD, DWORD, DWORD, DWORD, DWORD);
static int __cdecl Diag_Paint(DWORD a, DWORD b, DWORD c, DWORD d, DWORD e, DWORD f)
{
    int r = ((Paint_t)0x52F820)(a, b, c, d, e, f);
    // bloodier: paint the same decal again on top (each extra layer makes the stain denser)
    for (int i = 1; i < g_decalLayers; ++i) ((Paint_t)0x52F820)(a, b, c, d, e, f);
    if (g_decalDiag && DiagOk()) { float len; memcpy(&len, &e, 4);
        Log("decal-diag: PAINT called (texture %08X mesh %08X length %.3f) -> returned %d", a, b, len, r); }
    return r;
}
static void InstallDecalDiag()
{
    PatchCall(0x4CD62C, (void*)Diag_GrabFinish, 0x4191D0);
    PatchCall(0x4CDA26, (void*)Diag_Gate,       0x462170);
    PatchCall(0x4CDAFB, (void*)Diag_Paint,      0x52F820);
}

// ---------------------------------------------------------------- v0.44: gore options ([Gore], menu page "Gore")
// G1 Exploding heads (Off / Random 50-50 / Always), G4a Blood on clothes (chest + upper back after a head gib),
// G4b Bullet hit blood (Normal / Bigger / Heavy). Defaults = the behaviour before v0.44 (Always / Off / Normal).
// Facts (pun.exe, checked in Ghidra / capstone, see cut_content\scans\overnight2\gore_batch1.md):
//  * Gibs (0x411870, esi = CORPSE) exist only on corpses. A head gib from an animation "gib-head" trigger is applied
//    a) at corpse creation (0x413C50: trigger already passed in the death anim -> entity +0xB04 bit 0x40000 ->
//       gib at 0x415203) and b) in the corpse update (0x411E50: trigger fires on the corpse anim -> gib at 0x41201A).
//  * Both places hold the retail "no head gib while a quick kill plays" check (9 bytes at 0x414F38 / 0x411F9C:
//    mov eax,[0x7D112C]; test eax,eax; jne apply  (EB = always apply on the gore-patched exe), then the player state
//    [0x6B70FC]+0x860 == 4 / 8 -> skip unless the player flag [0x7CF804]+0x1F3 is set).
//  * We replace those 11 bytes with  call stub; test al,al; jne apply; jmp skip  -> our function decides.
//  * Body decal paint 0x52F820(texture, mesh, pos*, dir*, float half-size, flag): pos / dir in the mesh's own space
//    (y up, +z = front). flag 1 = bind pose + a depth clip of +-size along dir (quick-kill decals);
//    flag 0 = current pose, no depth clip (gunfire blood_hit01 mark at 0x41F4C1). Decal records: 256 (free list
//    0xC93610, next +0x18); vertex arena used 0xC93638, limit = imm at 0x52EA25 (4 MB on the patched exe).
static int g_goreHeads   = 2;   // [Gore] ExplodingHeads   0 Off, 1 Random 50-50, 2 Always (gore-patched exe)
static int g_goreClothes = 0;   // [Gore] BloodOnClothes   1 = chest / shoulder + upper back splats after a head gib
static int g_goreBullet  = 0;   // [Gore] BulletHitBlood   0 Normal, 1 Bigger, 2 Heavy
static int g_goreLogCount = 0;
static bool GoreLogOk() { return g_goreLogCount++ < 300; }

static DWORD ArenaLimit() { DWORD v = *(DWORD*)0x52EA25; return (v >= 0x8000 && v <= 0x10000000) ? v : 0x19000; }
static DWORD ArenaUsed()  { return *(volatile DWORD*)0xC93638; }
static int FreeDecalRecords()
{
    __try {
        DWORD h = *(volatile DWORD*)0xC93610; if (!h) return 0;
        int n = 1; DWORD p = *(volatile DWORD*)(h + 0x18);
        while (p && p != h && n < 1200) { ++n; p = *(volatile DWORD*)(p + 0x18); }
        return n;
    } __except (1) { return 0; }
}
// room for extra decals: arena below `share` of its size and at least `records` free decal records
static bool DecalRoom(float share, int records)
{
    __try { return *(BYTE*)0x52EA24 == 0x3D && ArenaUsed() < (DWORD)(ArenaLimit() * share) && FreeDecalRecords() >= records; }
    __except (1) { return false; }
}
static DWORD ShotTex()  { return *(volatile DWORD*)(0x6D8708 + (rand() % 3) * 4); }   // shot_01/02/03_64.tga
static DWORD HitTex()   { return *(volatile DWORD*)0x61F1B8; }                         // blood_hit01.tga
static float Jit(float a) { return ((rand() % 2001) - 1000) / 1000.0f * a; }
// one decal in the mesh's own space; returns true if something was painted (the arena grew)
static bool PaintLocal(DWORD tex, DWORD mesh, float x, float y, float z, float dx, float dy, float dz, float size, int flag)
{
    if (!tex || !mesh || size <= 0) return false;
    float p[3] = { x, y, z }, d[3] = { dx, dy, dz };
    DWORD s; memcpy(&s, &size, 4);
    DWORD before = ArenaUsed();
    __try { ((Paint_t)0x52F820)(tex, mesh, (DWORD)p, (DWORD)d, s, (DWORD)flag); }
    __except (1) { Log("gore: decal paint raised an exception (mesh %08X) - skipped", mesh); return false; }
    return ArenaUsed() != before;
}
static DWORD CorpseMesh(DWORD corpse) { __try { return corpse ? *(volatile DWORD*)(corpse + 0x94) : 0; } __except (1) { return 0; } }

// v0.45: which kill is it? The corpse plays the victim's death anim: index at corpse +0x254 into the anim slot table
// 0xC9C988 (0x24 bytes per slot, 0xC80 slots, name string at +0; see 0x530C10 / 0x531A0B).
static int g_goreVary = 1;      // [Gore] WoundVariation  1 = wound spots shift a little per kill
static bool CorpseAnimName(DWORD corpse, char* out, size_t n)
{
    out[0] = 0;
    __try {
        int idx = *(volatile int*)(corpse + 0x254);
        if (idx < 0 || idx >= 0xC80) return false;
        const char* s = (const char*)(0xC9C988 + idx * 0x24);
        size_t i = 0;
        for (; i + 1 < n && i < 0x19 && s[i]; ++i) out[i] = (char)tolower((unsigned char)s[i]);
        out[i] = 0;
        return i > 0;
    } __except (1) { out[0] = 0; return false; }
}
// Slaughter-mode kills (punch head, uppercut, dive bash) always gib as in the retail game; v0.46: so do the two
// non-gun head gibs (flamethrower in the mouth, grenade in the mouth)
static bool IsSlaughterAnim(const char* a)
{
    return strstr(a, "male_sm_") || strstr(a, "male_qk_flame_mouf") || strstr(a, "male_qk_grenade");
}
// v0.46: the game's 12 always-loaded spatter textures [0x6D8708 + i*4]: 0-2 shot_01/02/03 round, 3 shot_05 DRIP,
// 4 shot_06 ring, 5 shot_07 drops, 6 shot_14 DENSE, 7 shot_15, 8 shot_16, 9 shot_31 SMEAR (64x128),
// 10 shot_35 SPRAY streak, 11 shot_36 splash
enum { TX_DRIP = 3, TX_DENSE = 6, TX_SMEAR = 9, TX_STREAK = 10, TX_SPLASH = 11, TX_HIT = 100, TX_RND = 101 };
static DWORD SpatTex(int i)
{
    if (i == TX_HIT) return HitTex();
    if (i == TX_RND || i < 0 || i > 11) return ShotTex();
    return *(volatile DWORD*)(0x6D8708 + i * 4);
}
// v0.46: a small blood burst attached to the corpse's neck point (same attach as the game's own gib burst):
// effect id 0x40E440(name); spawn 0x40E5C0(id, room, pos*, orient*, 0, parent handle, point, parent handle)
static int g_burstId = -2;
static void NeckBurst(DWORD corpse)
{
    __try {
        if (*(volatile int*)0x666DF8 >= 0x38) return;            // keep 8 of the 64 effect slots free
        if (g_burstId == -2) g_burstId = ((int (__cdecl*)(const char*))0x40E440)("blood_hit_0");
        if (g_burstId < 0) return;
        int skel = *(volatile int*)(corpse + 0x248);
        int slot = *(volatile int*)(0x6B70DC + 5 * 4);            // gib type 5 = head -> limb slot
        if (skel < 0 || slot < 0) return;
        int point = *(volatile int*)(0x6B7244 + (skel * 0xB4 + slot) * 4);
        if (point < 0) return;
        DWORD h = *(volatile DWORD*)(corpse + 0x14);
        int r = ((int (__cdecl*)(int, DWORD, DWORD, DWORD, DWORD, DWORD, int, DWORD))0x40E5C0)(
            g_burstId, *(volatile DWORD*)(corpse + 0x98), corpse + 0x1C, corpse + 0x28, 0, h, point, h);
        if (GoreLogOk()) Log("gore: exit-wound blood burst on corpse %08X (point %d) -> %d", corpse, point, r);
    } __except (1) { Log("gore: exit-wound burst raised an exception - skipped"); }
}

// G1 wound sides (bind pose, mesh space: y up, +z = the thug's front, +x = the thug's RIGHT [left-handed, to check])
enum WoundSide { W_FACE, W_FOREHEAD, W_BETWEEN_EYES, W_BACK, W_LOWER_BACK, W_RIGHT_TEMPLE, W_LEFT_TEMPLE };
struct HeadWoundKill { const char* anim; const char* name; WoundSide entry, exit; float dy; };   // v0.48: dy = height shift (animation check)
static const HeadWoundKill kHeadWoundKills[] = {   // the author's baseline (Nov 8 2026)
    { "male_qk_pstl_head",   "pistol: shot to the back of the head", W_LOWER_BACK,   W_BETWEEN_EYES },
    { "male_qk_pstl_smshst", "pistol: smash over head, shoot face",  W_FACE,         W_BACK, -0.07f },
    { "male_qk_pstl_fakeot", "pistol: fake-out head shot",           W_RIGHT_TEMPLE, W_LEFT_TEMPLE },
    { "male_qk_pstl_tapsht", "pistol: tap on shoulder head shot",    W_FACE,         W_BACK, -0.08f },
    { "male_hs_kill_p",      "pistol: human shield execution",       W_BACK,         W_FACE },
    { "male_qk_hrfl_down",   "rifle: shoulder knockdown and shoot",  W_FACE,         W_BACK },
    { "male_qk_hrfl_pstl",   "rifle: toss rifle, left pistol",       W_BETWEEN_EYES, W_BACK, -0.06f },
    { "male_hs_hrfl_kill",   "rifle: human shield execution",        W_FACE,         W_BACK },
    { "male_qk_sg1_rit",     "shotgun: no-look blast",               W_FACE,         W_BACK },
};
// one wound: entry = small hole + blood round, exit = bigger splat + blood running down. Each start point sits
// outside the head and the ray points INTO it; flag 1 clips +-size along the ray, so a box reaches the near
// surface (face ~ +0.10, back ~ -0.10, temples ~ +-0.08) but not the far side.
static int PaintWound(DWORD mesh, WoundSide s, bool entry, float jx, float jy)
{
    float y; int n = 0;
    switch (s) {
    case W_FACE: case W_FOREHEAD: case W_BETWEEN_EYES:
        y = (s == W_FOREHEAD ? 1.71f : s == W_BETWEEN_EYES ? 1.67f : 1.64f) + jy;
        if (entry) {
            n += PaintLocal(HitTex(),  mesh, jx, y, 0.12f, 0, 0, -1, 0.06f, 1);
            n += PaintLocal(ShotTex(), mesh, jx, y - 0.01f, 0.14f, 0, 0, -1, 0.11f, 1);
        } else {
            n += PaintLocal(ShotTex(), mesh, jx, y, 0.15f, 0, 0, -1, 0.13f, 1);
            n += PaintLocal(ShotTex(), mesh, jx * 2, y - 0.07f, 0.15f, 0, 0, -1, 0.09f, 1);
        }
        break;
    case W_BACK: case W_LOWER_BACK:
        y = (s == W_LOWER_BACK ? 1.60f : 1.66f) + jy;
        if (entry) {
            n += PaintLocal(HitTex(),  mesh, jx, y, -0.12f, 0, 0, 1, 0.06f, 1);
            n += PaintLocal(ShotTex(), mesh, jx, y - 0.01f, -0.14f, 0, 0, 1, 0.11f, 1);
        } else {
            n += PaintLocal(ShotTex(), mesh, jx, y, -0.15f, 0, 0, 1, 0.13f, 1);
            n += PaintLocal(ShotTex(), mesh, jx * 2, y - 0.10f, -0.15f, 0, 0, 1, 0.11f, 1);   // down the neck
        }
        break;
    case W_RIGHT_TEMPLE: case W_LEFT_TEMPLE: {
        float sx = s == W_RIGHT_TEMPLE ? 1.0f : -1.0f;   // outside x, ray towards the middle
        y = 1.68f + jy;
        if (entry) {
            n += PaintLocal(HitTex(),  mesh, sx * 0.12f, y, 0.02f + jx, -sx, 0, 0, 0.06f, 1);
            n += PaintLocal(ShotTex(), mesh, sx * 0.14f, y - 0.01f, 0.02f + jx, -sx, 0, 0, 0.10f, 1);
        } else {
            n += PaintLocal(ShotTex(), mesh, sx * 0.15f, y, 0.02f + jx, -sx, 0, 0, 0.12f, 1);
            n += PaintLocal(ShotTex(), mesh, sx * 0.15f, y - 0.09f, jx * 2, -sx, 0, 0, 0.09f, 1);
        }
        break; }
    }
    return n;
}
// G1: wounds when a quick-kill head does NOT explode: the kill's entry + exit spots (unknown kill = face -> back)
static void PaintHeadWounds(DWORD corpse)
{
    DWORD mesh = CorpseMesh(corpse);
    if (!mesh) { Log("gore: head wounds - corpse %08X has no mesh", corpse); return; }
    if (!DecalRoom(0.9f, 12)) { Log("gore: head wounds skipped (decal memory / records low: %u of %u KB, %d records free)", ArenaUsed() / 1024, ArenaLimit() / 1024, FreeDecalRecords()); return; }
    char anim[32]; CorpseAnimName(corpse, anim, sizeof(anim));
    const HeadWoundKill* k = nullptr;
    for (const auto& w : kHeadWoundKills) if (anim[0] && strstr(anim, w.anim)) { k = &w; break; }
    WoundSide in = k ? k->entry : W_FACE, out = k ? k->exit : W_BACK;
    float dy = k ? k->dy : 0;
    float jx = g_goreVary ? Jit(0.025f) : 0, jy = (g_goreVary ? Jit(0.02f) : 0) + dy;
    int n = PaintWound(mesh, in, true, jx, jy);
    n += PaintWound(mesh, out, false, g_goreVary ? Jit(0.025f) : 0, (g_goreVary ? Jit(0.02f) : 0) + dy);
    if (GoreLogOk()) Log("gore: head wounds on corpse %08X, anim '%s' = %s: %d of 4 decals, decal memory %u KB, %d records free",
                         corpse, anim, k ? k->name : "unknown kill (face -> back of head)", n, ArenaUsed() / 1024, FreeDecalRecords());
    NeckBurst(corpse);   // v0.46
}

// G1 decision. site 0 = corpse update (trigger fires now), 1 = corpse creation (trigger may have passed already).
struct GibChoice { DWORD corpse, handle, tick; bool gib; };
static GibChoice g_gibChoice[32];
static int g_gibChoiceNext = 0;
static int g_gibDecisions = 0;
static bool __cdecl GibHeadDecide(DWORD corpse, int site, int curFrame)
{
    const char* where = site ? "corpse creation" : "corpse update";
    __try {
        if (site == 1) {   // the game only gibs here if the gib-head frame is already behind the current frame
            int trig = ((int (__cdecl*)(int, int))0x5299C0)(*(volatile int*)0x61F0E0, *(volatile int*)(corpse + 0x254));
            double lim = (double)curFrame * *(float*)0x5F5B3C * *(float*)0x5F5B38 * *(float*)0x5F52F0;
            if (!((double)trig < lim)) return true;   // not reached yet: the game's own frame test says no, the corpse update decides later
        }
        DWORD p = *(volatile DWORD*)0x6B70FC;
        DWORD pl = *(volatile DWORD*)0x7CF804;
        bool envKill = *(volatile DWORD*)0x7D112C != 0;
        bool flag = pl && *(volatile BYTE*)(pl + 0x1F3) != 0;
        int state = p ? *(volatile int*)(p + 0x860) : -1;
        DWORD handle = *(volatile DWORD*)(corpse + 0x14);
        bool victim = p && *(volatile DWORD*)(p + 0x87C) == handle;   // the corpse of the last quick-kill / shield victim
        bool retailCancel = !envKill && p && !flag && (state == 4 || state == 8);
        bool qk = retailCancel || (!envKill && p && !flag && victim);
        if (!qk) {
            if (GoreLogOk()) Log("gore: head gib (%s) corpse %08X - not a quick kill (state %d, env %d) -> GIB as usual", where, corpse, state, envKill);
            return true;
        }
        char anim[32];
        bool named = CorpseAnimName(corpse, anim, sizeof(anim));
        // v0.48.3: interrogation deaths (Piranha: gib-head trigger + skull_fish GibProp) also leave the victim's handle
        // at +0x87C - only quick-kill / human-shield death anims are decided here, the rest gib as in retail
        if (named && !strstr(anim, "_qk_") && !strstr(anim, "_hs_") && !strstr(anim, "male_sm_")) {
            if (GoreLogOk()) Log("gore: head gib (%s) corpse %08X - '%s' is not a quick kill (interrogation / scripted) -> GIB as usual", where, corpse, anim);
            return true;
        }
        if (named && IsSlaughterAnim(anim)) {   // v0.45: Slaughter = retail
            if (GoreLogOk()) Log("gore: head gib (%s) corpse %08X - Slaughter kill '%s' -> GIB as usual", where, corpse, anim);
            return true;
        }
        // once per kill: a corpse decided before keeps its answer
        DWORD now = GetTickCount();
        for (auto& c : g_gibChoice)
            if (c.corpse == corpse && c.handle == handle && now - c.tick < 60000) {
                if (GoreLogOk()) Log("gore: head gib (%s) corpse %08X - already decided: %s", where, corpse, c.gib ? "GIB" : "no gib");
                return c.gib;
            }
        bool gib; const char* why;
        if (g_goreHeads >= 2)      { gib = true;  why = "Always"; }
        else if (g_goreHeads <= 0) { gib = false; why = "Off"; }
        else                       { gib = (rand() & 1) != 0; why = "Random 50-50"; }
        GibChoice& c = g_gibChoice[g_gibChoiceNext++ % 32];
        c.corpse = corpse; c.handle = handle; c.tick = now; c.gib = gib;
        ++g_gibDecisions;
        Log("gore: quick-kill head gib #%d (%s) corpse %08X state %d -> %s (%s)", g_gibDecisions, where, corpse, state, gib ? "GIB" : "NO GIB", why);
        bool headThere = *(volatile DWORD*)(corpse + 0x5DC) == 0;
        if (!gib && headThere) PaintHeadWounds(corpse);
        return gib;
    } __except (1) {
        Log("gore: head gib decision raised an exception (corpse %08X) - head gibs as before", corpse);
        return true;
    }
}
static __declspec(naked) void Stub_GibHeadUpdate()
{
    __asm {
        push 0
        push 0
        push ebp                 // ebp = the corpse in 0x411E50
        call GibHeadDecide
        add  esp, 12
        ret
    }
}
static __declspec(naked) void Stub_GibHeadCreate()
{
    __asm {
        mov  eax, [esp + 0x38]   // the corpse's current death-anim frame (the game reads it as [esp+0x3C] after 2 pushes)
        push eax
        push 1
        push ebp                 // ebp = the new corpse in 0x413C50
        call GibHeadDecide
        add  esp, 12
        ret
    }
}

// G4a: wrapper around the gib call (0x411870, esi = corpse, cdecl 9 arguments, type first). After a HEAD gib
// (type 5, the head stub slot +0x5DC filled by this call) the chest / shoulder and upper back get blood splats -
// painted on the corpse AFTER the gib, so they are not lost like the grab-finish decal.
static DWORD g_gibRet = 0, g_gibType = 0, g_gibHeadBefore = 0;
static void __cdecl AfterGib(DWORD corpse)
{
    if (g_gibType != 5) return;
    __try {
        if (g_gibHeadBefore != 0 || *(volatile DWORD*)(corpse + 0x5DC) == 0) return;   // not a new head gib
    } __except (1) { return; }
    if (!g_goreClothes) return;
    DWORD mesh = CorpseMesh(corpse);
    if (!mesh) return;
    if (!DecalRoom(0.9f, 12)) { Log("gore: clothes blood skipped (decal memory / records low: %u of %u KB, %d records free)", ArenaUsed() / 1024, ArenaLimit() / 1024, FreeDecalRecords()); return; }
    float j = Jit(0.03f);
    int n = 0;
    // v0.45: all 8 cm higher (user: the splats sat below the neck line)
    // v0.46: chest = dense splat + splash on top + blood running down from the neck (user: the chest was weak)
    n += PaintLocal(SpatTex(TX_DENSE), mesh, j, 1.40f, 0.30f, 0, 0, -1, 0.28f, 1);
    n += PaintLocal(SpatTex(TX_DENSE), mesh, -j, 1.38f, 0.30f, 0, 0, -1, 0.24f, 1);   // v0.46.2: 2nd layer = more solid
    n += PaintLocal(SpatTex(TX_SPLASH), mesh, -j, 1.42f, 0.30f, 0, 0, -1, 0.22f, 1);
    n += PaintLocal(SpatTex(TX_DRIP), mesh, j * 2, 1.30f, 0.30f, 0, 0, -1, 0.20f, 1);
    // v0.46.1: the band between the neck stump and the chest stayed clean (it slopes up towards the neck, so a
    // straight-on stamp misses it): one stamp on the collar line + one from above-front at 45 degrees
    n += PaintLocal(SpatTex(TX_DENSE), mesh, -j, 1.53f, 0.26f, 0, 0, -1, 0.14f, 1);
    n += PaintLocal(SpatTex(TX_SPLASH), mesh, j, 1.64f, 0.17f, 0, -0.7071f, -0.7071f, 0.15f, 1);
    n += PaintLocal(SpatTex(TX_RND), mesh, -j, 1.64f, -0.17f, 0, -0.7071f, 0.7071f, 0.13f, 1);   // same on the back
    n += PaintLocal(ShotTex(), mesh, (rand() & 1) ? 0.13f : -0.13f, 1.50f, 0.25f, 0, 0, -1, 0.20f, 1);  // shoulder (front)
    n += PaintLocal(ShotTex(), mesh, -j, 1.50f, -0.30f, 0, 0, 1, 0.28f, 1);                      // upper back / neck
    n += PaintLocal(ShotTex(), mesh, j * 2, 1.38f, -0.30f, 0, 0, 1, 0.22f, 1);                   // running down the back
    if (GoreLogOk()) Log("gore: head gib on corpse %08X -> clothes blood %d of 10 decals (chest x4, collar x2, shoulder, upper back x3), decal memory %u KB, %d records free",
                         corpse, n, ArenaUsed() / 1024, FreeDecalRecords());
}
// v0.48.1: normal GUNFIRE head gibs (calls at 0x414A1B / 0x414AF8 = returns 0x414A20 / 0x414AFD, gib type 5 = head):
// [Gore] GunfireHeads 0 Normal (game decides), 1 Random 50-50, 2 Off. Skipping = the gib function is not called
// (the caller only reads its own fields afterwards); the game's bullet mark stays as the wound.
static int g_goreGunHeads = 0;
static int g_gunHeadLogs = 0;
static int __cdecl GunfireHeadSkip(DWORD ret, DWORD type)
{
    if (type != 5 || g_goreGunHeads <= 0 || g_goreGunHeads >= 3 || (ret != 0x414A20 && ret != 0x414AFD)) return 0;
    bool skip = g_goreGunHeads >= 2 || (rand() & 1);
    if (g_gunHeadLogs++ < 60) Log("gore: gunfire head gib -> %s (%s)", skip ? "NO GIB" : "GIB", g_goreGunHeads >= 2 ? "Off" : "Random 50-50");
    return skip ? 1 : 0;
}
// v0.50.8: [Gore] GunfireHeads 3 = "Critical hits": a head-shot KILL with ANY gun gibs the head with CritChance %.
// Corpse creation 0x413C50, block 0x4148D8: ebx = the dying entity, ebp = the new corpse, [ebx+0x9D8] = gib spot of the
// last hit (5 = head), [ebx+0x9D0] bit 2 = the weapon may gib (pistols: no) -> then rand < 0.2 (bit 4) / 0.1 -> gib
// path 0x414989 (gore setting, entity "no gib" flag, then the head goes straight to the gib call 0x414A1B).
// Hooked at 0x4148F7 (mov ecx,[ebx+0x9D0]): 1 = go to the gib path, 2 = no gib (0x41492D), 0 = the game's own test.
static int g_goreCritPct = 25;
static int g_critLogs = 0;
static int __cdecl CritDecide(DWORD ent, DWORD corpse)
{
    if (g_goreGunHeads != 3) return 0;
    __try {
        if (*(volatile int*)(ent + 0x9D8) != 5) return 0;          // not a head hit: arms / legs as in the game
        DWORD pl = *(volatile DWORD*)0x7CF804;
        if (pl && *(volatile BYTE*)(pl + 0x1F3)) return 0;          // gore cheat: the game always gibs anyway
        if (*(volatile DWORD*)0x7D112C) return 0;                   // environment / prop kill
        if (*(volatile DWORD*)kCurrentInterrogation) return 0;      // any interrogation kill (gun-to-head too)
        DWORD p = *(volatile DWORD*)0x6B70FC;
        if (p) {
            int st = *(volatile int*)(p + 0x860);
            if (st == 4 || st == 8) return 0;                       // quick kill running: its own head-gib choice
            if (*(volatile DWORD*)(p + 0x87C) == *(volatile DWORD*)(ent + 0x14)) return 0;   // quick-kill / shield victim
        }
        bool gib = (rand() % 100) < g_goreCritPct;
        if (g_critLogs++ < 60) Log("gore: head-shot kill (corpse %08X) -> %s (critical hit chance %d %%)", corpse, gib ? "CRITICAL - head gib" : "no gib", g_goreCritPct);
        return gib ? 1 : 2;
    } __except (1) { return 0; }
}
static __declspec(naked) void Stub_Crit()
{
    __asm {
        pushad
        push ebp
        push ebx
        call CritDecide
        add  esp, 8
        mov  [esp + 0x1C], eax           // pushad's eax slot
        popad
        cmp  eax, 1
        je   crit_gib
        cmp  eax, 2
        je   crit_none
        mov  eax, [ebx + 0x9D8]          // the game's code below uses eax = gib spot
        mov  ecx, [ebx + 0x9D0]          // the replaced instruction
        push 0x4148FD
        ret
    crit_gib:
        push 0x414989
        ret
    crit_none:
        push 0x41492D
        ret
    }
}
static __declspec(naked) void Wrap_Gib()
{
    __asm {
        pop  dword ptr [g_gibRet]        // the caller's return address (0x411870 is not re-entered)
        mov  eax, [esp]
        mov  dword ptr [g_gibType], eax
        mov  eax, [esi + 0x5DC]
        mov  dword ptr [g_gibHeadBefore], eax
        pushad
        push dword ptr [g_gibType]
        push dword ptr [g_gibRet]
        call GunfireHeadSkip
        add  esp, 8
        mov  [esp + 0x1C], eax           // pushad's eax slot
        popad
        test eax, eax
        jnz  skipgib
        mov  eax, 0x411870
        call eax                         // arguments are where the gib function expects them; esi = corpse
        pushad
        push esi
        call AfterGib
        add  esp, 4
        popad
        push dword ptr [g_gibRet]
        ret
    skipgib:
        xor  eax, eax
        push dword ptr [g_gibRet]
        ret
    }
}

// G4b: the gunfire body mark (0x41F4C1: blood_hit01, 2.5-5 cm, current pose). Bigger = x2.5, Heavy = x3 plus a
// blood splat around the hit. Extra decals only while the decal memory is below 85 % and 64+ records are free
// (so quick-kill / kill decals always find room); otherwise the normal mark.
static int g_bulletLogs = 0;
static int __cdecl Hook_BulletMark(DWORD tex, DWORD mesh, DWORD pos, DWORD dir, DWORD size, DWORD flag)
{
    if (g_goreBullet <= 0) { ((Paint_t)0x52F820)(tex, mesh, pos, dir, size, flag); return 0; }
    float s; memcpy(&s, &size, 4);
    bool room = DecalRoom(0.85f, 64);
    // v0.45: 3 = Normal + splat (the normal mark, plus a 5-9 cm blood splat around it)
    float k = !room ? 1.0f : (g_goreBullet == 1 ? 2.5f : g_goreBullet == 2 ? 3.0f : 1.0f);
    float s2 = s * k; DWORD sz; memcpy(&sz, &s2, 4);
    bool splat = room && g_goreBullet >= 2;
    __try {
        ((Paint_t)0x52F820)(tex, mesh, pos, dir, sz, flag);
        if (splat) {
            float sp = g_goreBullet == 2 ? 0.08f + (rand() % 61) / 1000.0f    // Heavy: 8-14 cm splat
                                         : 0.05f + (rand() % 41) / 1000.0f;   // Normal + splat: 5-9 cm
            DWORD ss; memcpy(&ss, &sp, 4);
            // v0.46: pick from the game's spatter set: splats (most), the spray streak (1 in 4)
            static const int kSplats[] = { 0, 1, 2, 4, 6, 7, 8, 11 };
            int t = (rand() % 4 == 0) ? TX_STREAK : kSplats[rand() % 8];
            ((Paint_t)0x52F820)(SpatTex(t), mesh, pos, dir, ss, flag);
            if (rand() % 5 == 0) {   // now and then blood running down under the hit (mesh space: y up)
                float p2[3]; memcpy(p2, (void*)pos, 12); p2[1] -= sp * 0.6f;
                float sd = sp * 0.8f; DWORD sds; memcpy(&sds, &sd, 4);
                ((Paint_t)0x52F820)(SpatTex(TX_DRIP), mesh, (DWORD)p2, dir, sds, flag);
            }
        }
    } __except (1) { Log("gore: bullet mark paint raised an exception - skipped"); }
    if (g_decalDiag && g_bulletLogs++ < 200)
        Log("gore: bullet mark %.3f -> %.3f%s (decal memory %u of %u KB, %d records free)", s, s2,
            splat ? " + splat" : (room ? "" : " (memory low: normal size)"), ArenaUsed() / 1024, ArenaLimit() / 1024, FreeDecalRecords());
    return 0;
}

// ---------------------------------------------------------------- v0.46: per-kill wound marks (non-gun quick kills)
// 0x4CDA33 (mov eax,[0x7D1130]) starts the quick kill's one table decal (grab-finish, victim entity = ebx, mesh
// ebx+0x94, bind pose flag 1). For the kills below we paint our own marks instead and skip to 0x4CDB03.
// Kill = the victim's current action anim: 0x418D00(entity, [entity+0x828]) -> anim slot (same as corpse creation).
static int g_goreKillWounds = 1;   // [Gore] KillWounds
struct WMark { int tex; float x, y, z, dx, dy, dz, size; int ms; };   // v0.47: ms = delay after the kill's blood moment / start
struct KillMarks { const char* anim; const char* name; WMark m[12]; };
// mesh space, bind pose: y up, +z = the thug's front, +x = his right. Torso front ~ +0.13, back ~ -0.13,
// face ~ +0.10, top of head ~ 1.80, chin underside ~ 1.55. Start points sit outside, the ray points in.
static const KillMarks kKillMarks[] = {
    // v0.48.2: spots back to v0.47.2 (the author's placement), hit times from the v0.48 animation check
    { "melee:quick_kill_1", "knife into lower back (back -> stomach)", {
        { TX_HIT, 0, 1.10f, -0.17f, 0, 0, 1, 0.05f, 433 }, { TX_RND, 0, 1.10f, -0.25f, 0, 0, 1, 0.14f, 433 },
        { TX_DRIP, 0, 1.01f, -0.25f, 0, 0, 1, 0.12f, 700 }, { TX_STREAK, 0, 1.10f, 0.26f, 0, 0, -1, 0.15f, 433 },
        { TX_DRIP, 0, 1.01f, 0.25f, 0, 0, -1, 0.11f, 700 } } },
    { "melee:quick_kill_2", "knife to back of head, out the face (unarmed)", {
        { TX_HIT, 0, 1.66f, -0.12f, 0, 0, 1, 0.05f, 550 }, { TX_RND, 0, 1.65f, -0.14f, 0, 0, 1, 0.10f, 550 },
        { TX_STREAK, 0, 1.65f, 0.15f, 0, 0, -1, 0.11f, 560 }, { TX_DRIP, 0, 1.57f, 0.15f, 0, 0, -1, 0.08f, 800 } } },
    { "rifle:quick_kill_6", "rifle: knife to head from behind (right temple -> left temple)", {
        { TX_HIT, 0.12f, 1.66f, 0.02f, -1, 0, 0, 0.05f, 567 }, { TX_RND, 0.14f, 1.65f, 0.02f, -1, 0, 0, 0.10f, 567 },
        { TX_STREAK, -0.15f, 1.66f, 0.02f, 1, 0, 0, 0.12f, 580 }, { TX_DRIP, -0.15f, 1.58f, 0, 1, 0, 0, 0.09f, 800 } } },
    { "melee:quick_kill_3", "knife into liver (stomach -> back)", {
        { TX_HIT, 0.07f, 1.08f, 0.17f, 0, 0, -1, 0.05f, 1000 }, { TX_RND, 0.07f, 1.08f, 0.25f, 0, 0, -1, 0.14f, 1000 },
        { TX_DRIP, 0.07f, 0.99f, 0.24f, 0, 0, -1, 0.12f, 1300 }, { TX_STREAK, 0.07f, 1.08f, -0.26f, 0, 0, 1, 0.15f, 1000 } } },
    { "melee:quick_kill_4", "slit throat (melee)", {
        { TX_SMEAR, 0, 1.53f, 0.10f, 0, 0, -1, 0.09f, 1400 }, { TX_RND, 0, 1.52f, 0.07f, 0, 0, -1, 0.07f, 1400 },
        { TX_DENSE, 0, 1.50f, 0.09f, 0, 0, -1, 0.06f, 1420 }, { TX_DRIP, 0, 1.45f, 0.13f, 0, 0, -1, 0.10f, 1700 } } },
    { "rifle:quick_kill_4", "slit throat (rifle)", {
        { TX_SMEAR, 0, 1.53f, 0.10f, 0, 0, -1, 0.09f, 1400 }, { TX_RND, 0, 1.52f, 0.07f, 0, 0, -1, 0.07f, 1400 },
        { TX_DENSE, 0, 1.50f, 0.09f, 0, 0, -1, 0.06f, 1420 }, { TX_DRIP, 0, 1.45f, 0.13f, 0, 0, -1, 0.10f, 1700 } } },
    { "melee:quick_kill_5", "jam nose into brain (more blood on the nose)", {
        { TX_HIT, 0, 1.63f, 0.13f, 0, 0, -1, 0.04f, 633 }, { TX_DENSE, 0, 1.63f, 0.14f, 0, 0, -1, 0.07f, 633 },
        { TX_RND, 0, 1.62f, 0.15f, 0, 0, -1, 0.08f, 633 }, { TX_DRIP, 0, 1.57f, 0.14f, 0, 0, -1, 0.08f, 800 },
        { TX_DRIP, 0.02f, 1.55f, 0.14f, 0, 0, -1, 0.07f, 900 } } },
    { "pistol:quick_kill_2", "pistol butt shot (butt -> front)", {
        { TX_HIT, 0, 0.95f, -0.15f, 0, 0, 1, 0.05f, 1267 }, { TX_DENSE, 0, 0.95f, -0.22f, 0, 0, 1, 0.14f, 1267 },
        { TX_DRIP, 0, 0.86f, -0.22f, 0, 0, 1, 0.12f, 1500 }, { TX_STREAK, 0, 0.92f, 0.22f, 0, 0, -1, 0.15f, 1280 },
        { TX_RND, 0, 0.92f, 0.26f, 0, 0, -1, 0.17f, 1280 } } },
    { "pistol:quick_kill_3", "pistol chest shot (chest -> back)", {
        { TX_HIT, 0, 1.28f, 0.17f, 0, 0, -1, 0.05f, 600 }, { TX_DENSE, 0, 1.28f, 0.27f, 0, 0, -1, 0.16f, 600 },
        { TX_DRIP, 0, 1.18f, 0.25f, 0, 0, -1, 0.13f, 900 }, { TX_STREAK, 0, 1.30f, -0.28f, 0, 0, 1, 0.18f, 610 },
        { TX_RND, 0, 1.26f, -0.30f, 0, 0, 1, 0.20f, 610 } } },
    { "pistol:quick_kill_4", "pistol groin shot (more blood)", {
        { TX_HIT, 0, 0.92f, 0.14f, 0, 0, -1, 0.05f, 1400 }, { TX_DENSE, 0, 0.92f, 0.24f, 0, 0, -1, 0.15f, 1400 },
        { TX_SPLASH, 0.02f, 0.95f, 0.26f, 0, 0, -1, 0.17f, 1400 }, { TX_DRIP, 0, 0.84f, 0.22f, 0, 0, -1, 0.12f, 1650 } } },
    { "shotgun:quick_kill_4", "shotgun jab + stomach shot (big, out the back)", {
        { TX_DENSE, 0, 1.10f, 0.32f, 0, 0, -1, 0.26f, 1900 }, { TX_SPLASH, 0, 1.12f, 0.30f, 0, 0, -1, 0.22f, 1900 },
        { TX_DRIP, 0, 1.00f, 0.28f, 0, 0, -1, 0.18f, 1900 }, { TX_STREAK, 0, 1.12f, -0.32f, 0, 0, 1, 0.26f, 1900 },
        { TX_DENSE, 0, 1.10f, -0.30f, 0, 0, 1, 0.22f, 1900 } } },
    { "pistol:quick_kill_7", "pistol overhead smash (face)", {
        { TX_DENSE, 0, 1.65f, 0.15f, 0, 0, -1, 0.13f, 1500 }, { TX_SPLASH, 0.02f, 1.62f, 0.15f, 0, 0, -1, 0.11f, 1500 } } },
    { "rifle:quick_kill_2", "rifle: toss gun, stab face (mouth / chin)", {   // v0.48.2: user - mouth / chin, not the neck
        { TX_HIT, 0, 1.58f, 0.13f, 0, 0, -1, 0.04f, 1800 }, { TX_STREAK, 0, 1.58f, 0.14f, 0, 0, -1, 0.08f, 1800 },
        { TX_RND, 0, 1.555f, 0.13f, 0, 0, -1, 0.06f, 1800 }, { TX_DRIP, 0, 1.52f, 0.14f, 0, 0, -1, 0.08f, 2200 } } },
    { "rifle:quick_kill_5", "rifle: knife to top of head (front)", {
        // v0.48.4: crown is at ~1.77 (animation check); the old 1.88 / 1.90 starts with a short depth box missed it
        { TX_HIT, 0, 1.81f, 0.03f, 0, -1, 0, 0.05f, 867 }, { TX_RND, 0, 1.82f, 0.03f, 0, -1, 0, 0.10f, 867 },
        { TX_DENSE, 0.01f, 1.81f, 0.02f, 0, -1, 0, 0.07f, 867 },
        { TX_RND, 0, 1.73f, 0.14f, 0, 0, -1, 0.08f, 867 }, { TX_RND, 0, 1.48f, 0.07f, 0, 1, 0, 0.07f, 867 } } },
    { "rifle:quick_kill_7", "rifle: knife through gullet (under chin + mouth)", {
        { TX_HIT, 0, 1.50f, 0.07f, 0, 1, 0, 0.05f, 833 }, { TX_RND, 0, 1.49f, 0.07f, 0, 1, 0, 0.08f, 833 },
        { TX_RND, 0, 1.58f, 0.14f, 0, 0, -1, 0.05f, 833 }, { TX_DRIP, 0, 1.53f, 0.14f, 0, 0, -1, 0.07f, 833 } } },
    { "rifle:quick_kill_10", "rifle double bash (face)", {
        { TX_RND, 0, 1.64f, 0.15f, 0, 0, -1, 0.11f, 1950 }, { TX_DENSE, 0.01f, 1.63f, 0.15f, 0, 0, -1, 0.08f, 1950 } } },
    { "rifle:quick_kill_11", "rifle baseball swing (big face)", {
        { TX_DENSE, 0.03f, 1.64f, 0.15f, 0, 0, -1, 0.14f, 800 }, { TX_SPLASH, -0.02f, 1.62f, 0.15f, 0, 0, -1, 0.12f, 800 } } },
    { "shotgun:quick_kill_5", "shotgun: legs + head bash (big face)", {
        { TX_DENSE, 0.02f, 1.64f, 0.15f, 0, 0, -1, 0.14f, 1867 }, { TX_SPLASH, -0.02f, 1.62f, 0.15f, 0, 0, -1, 0.12f, 1867 } } },
    { "flamethrower:quick_kill_1", "flamethrower canister bash (back of head)", {
        { TX_DENSE, 0, 1.68f, -0.16f, 0, 0, 1, 0.12f, 1630 }, { TX_DRIP, 0, 1.58f, -0.15f, 0, 0, 1, 0.09f, 1900 } } },
    // v0.47: Slaughter Mode (times = victim animation frames x 33 ms from the START of the kill; analysis Nov 9)
    { "slaughter mode:quick_kill_1", "Slaughter 1: punch to face (knife tip in the chest; head gibs)", { { TX_HIT, 0, 1.46f, 0.16f, 0, 0, -1, 0.05f, 400 }, { TX_RND, 0, 1.46f, 0.22f, 0, 0, -1, 0.08f, 400 } } },
    { "slaughter mode:quick_kill_2", "Slaughter 2: uppercut (knife tip at the collar; head gibs)", { { TX_HIT, -0.1f, 1.62f, 0.04f, 0, -1, 0, 0.05f, 400 }, { TX_RND, -0.1f, 1.57f, 0.16f, 0, 0, -1, 0.07f, 400 } } },
    { "slaughter mode:quick_kill_3", "Slaughter 3: grab + toss backward (knives in the chest, back hits the ground)", { { TX_HIT, 0.07f, 1.38f, 0.16f, 0, 0, -1, 0.05f, 200 }, { TX_RND, 0.07f, 1.38f, 0.21f, 0, 0, -1, 0.08f, 200 }, { TX_HIT, -0.11f, 1.41f, 0.14f, 0, 0, -1, 0.05f, 800 }, { TX_RND, -0.11f, 1.41f, 0.19f, 0, 0, -1, 0.08f, 800 }, { TX_RND, 0.1f, 1.3f, -0.22f, 0, 0, 1, 0.1f, 1330 } } },
    { "slaughter mode:quick_kill_5", "Slaughter 5: grab right, toss left (knives in the chest)", { { TX_HIT, -0.02f, 1.39f, 0.16f, 0, 0, -1, 0.05f, 200 }, { TX_RND, -0.02f, 1.39f, 0.21f, 0, 0, -1, 0.08f, 200 }, { TX_HIT, -0.12f, 1.29f, 0.11f, 0, 0, -1, 0.05f, 300 }, { TX_RND, -0.12f, 1.29f, 0.17f, 0, 0, -1, 0.08f, 300 } } },
    { "slaughter mode:quick_kill_6", "Slaughter 6: grab left, toss right (knives in the chest)", { { TX_HIT, -0.03f, 1.4f, 0.16f, 0, 0, -1, 0.05f, 100 }, { TX_RND, -0.03f, 1.4f, 0.21f, 0, 0, -1, 0.08f, 100 }, { TX_HIT, 0.07f, 1.4f, 0.15f, 0, 0, -1, 0.05f, 230 }, { TX_RND, 0.07f, 1.4f, 0.2f, 0, 0, -1, 0.08f, 230 } } },
    { "slaughter mode:quick_kill_7", "Slaughter 7: arm flip + kick (back hits the ground, kick in the lower back)", { { TX_RND, -0.09f, 1, -0.22f, 0, 0, 1, 0.1f, 1100 }, { TX_RND, -0.14f, 1.41f, -0.22f, 0, 0, 1, 0.09f, 1170 }, { TX_DENSE, -0.09f, 1.15f, -0.22f, 0, 0, 1, 0.08f, 1670 } } },
    { "slaughter mode:quick_kill_9", "Slaughter 9: diving tackle (knives at chest + shoulder, back slammed)", { { TX_HIT, 0.07f, 1.5f, 0.14f, 0, 0, -1, 0.05f, 230 }, { TX_RND, 0.07f, 1.5f, 0.19f, 0, 0, -1, 0.07f, 230 }, { TX_RND, -0.1f, 1.66f, 0.01f, 0, -1, 0, 0.07f, 230 }, { TX_RND, 0.05f, 1.45f, -0.22f, 0, 0, 1, 0.11f, 400 } } },
    { "slaughter mode:quick_kill_10", "Slaughter 10: lift + throw forward (knife in the chest, then the groin)", { { TX_HIT, -0.05f, 1.32f, 0.17f, 0, 0, -1, 0.05f, 370 }, { TX_RND, -0.05f, 1.32f, 0.22f, 0, 0, -1, 0.09f, 370 }, { TX_HIT, -0.1f, 0.85f, 0.14f, 0, 0, -1, 0.05f, 970 }, { TX_DENSE, -0.1f, 0.85f, 0.2f, 0, 0, -1, 0.09f, 970 }, { TX_DRIP, -0.1f, 0.78f, 0.18f, 0, 0, -1, 0.09f, 1300 } } },
    { "slaughter mode:quick_kill_11", "Slaughter 11: lift by shoulder + slam (knife in the shoulder + chest, back slammed)", { { TX_HIT, -0.12f, 1.66f, -0.01f, 0, -1, 0, 0.05f, 170 }, { TX_RND, -0.12f, 1.68f, -0.01f, 0, -1, 0, 0.09f, 170 }, { TX_HIT, 0.02f, 1.43f, 0.16f, 0, 0, -1, 0.05f, 400 }, { TX_RND, 0.02f, 1.43f, 0.21f, 0, 0, -1, 0.08f, 400 }, { TX_RND, 0.12f, 1.43f, -0.22f, 0, 0, 1, 0.1f, 1900 } } },
    { "slaughter mode:quick_kill_12", "Slaughter 12: lift + throw over the head (knife in the stomach)", { { TX_HIT, -0.03f, 1.16f, 0.18f, 0, 0, -1, 0.05f, 270 }, { TX_RND, -0.03f, 1.16f, 0.24f, 0, 0, -1, 0.1f, 270 } } },
    { "slaughter mode:quick_kill_13", "Slaughter 13: lift + slam (knives in chest + groin, back slammed)", { { TX_HIT, -0.11f, 1.42f, 0.14f, 0, 0, -1, 0.05f, 230 }, { TX_RND, -0.11f, 1.42f, 0.19f, 0, 0, -1, 0.08f, 230 }, { TX_HIT, -0.05f, 0.86f, 0.15f, 0, 0, -1, 0.05f, 500 }, { TX_DENSE, -0.05f, 0.86f, 0.2f, 0, 0, -1, 0.09f, 500 }, { TX_DRIP, -0.05f, 0.79f, 0.18f, 0, 0, -1, 0.09f, 800 }, { TX_RND, -0.09f, 1.4f, -0.22f, 0, 0, 1, 0.1f, 1700 } } },
    { "slaughter mode:quick_kill_14", "Slaughter 14: stab to the stomach, then the spine", { { TX_HIT, 0.03f, 1.25f, 0.18f, 0, 0, -1, 0.05f, 1030 }, { TX_DENSE, 0.03f, 1.25f, 0.26f, 0, 0, -1, 0.12f, 1030 }, { TX_DRIP, 0.03f, 1.15f, 0.24f, 0, 0, -1, 0.11f, 1800 }, { TX_HIT, 0.02f, 1.4f, -0.18f, 0, 0, 1, 0.05f, 2600 }, { TX_DENSE, 0.02f, 1.4f, -0.26f, 0, 0, 1, 0.12f, 2600 }, { TX_DRIP, 0.02f, 1.3f, -0.24f, 0, 0, 1, 0.11f, 3000 } } },
    { "slaughter mode:quick_kill_15", "Slaughter 15: double stab to both ears", { { TX_HIT, 0.12f, 1.6f, -0.03f, -1, 0, 0, 0.05f, 570 }, { TX_RND, 0.14f, 1.6f, -0.03f, -1, 0, 0, 0.09f, 570 }, { TX_HIT, -0.12f, 1.58f, -0.02f, 1, 0, 0, 0.05f, 630 }, { TX_RND, -0.14f, 1.58f, -0.02f, 1, 0, 0, 0.09f, 630 }, { TX_DRIP, 0.14f, 1.52f, -0.03f, -1, 0, 0, 0.07f, 900 }, { TX_DRIP, -0.14f, 1.5f, -0.02f, 1, 0, 0, 0.07f, 900 }, { TX_RND, -0.07f, 1.61f, -0.18f, 0, 0, 1, 0.08f, 1570 } } },
    { "slaughter mode:quick_kill_16", "Slaughter 16: toss two knives, stab with them (neck + chin)", { { TX_HIT, 0.04f, 1.52f, 0.10f, 0, 0, -1, 0.05f, 1700 }, { TX_DENSE, 0.04f, 1.52f, 0.12f, 0, 0, -1, 0.07f, 1700 }, { TX_HIT, -0.03f, 1.56f, 0.13f, 0, 0, -1, 0.05f, 1700 }, { TX_RND, -0.03f, 1.56f, 0.15f, 0, 0, -1, 0.07f, 1700 }, { TX_DRIP, 0, 1.46f, 0.13f, 0, 0, -1, 0.10f, 2100 } } },
    { "pistol:quick_kill_10", "pistol lift + shoot (upper left chest -> back of the neck)", {
        { TX_HIT, -0.09f, 1.42f, 0.16f, 0, 0, -1, 0.05f, 767 }, { TX_DENSE, -0.09f, 1.42f, 0.24f, 0, 0, -1, 0.12f, 767 },
        { TX_DRIP, -0.09f, 1.33f, 0.22f, 0, 0, -1, 0.11f, 1000 }, { TX_STREAK, 0, 1.51f, -0.22f, 0, 0, 1, 0.12f, 780 } } },
    { "rifle:quick_kill_12", "rifle shoulder knockdown 2 (belly -> back, collar -> back of the head)", {
        { TX_HIT, 0.04f, 1.13f, 0.17f, 0, 0, -1, 0.05f, 1530 }, { TX_DENSE, 0.04f, 1.13f, 0.26f, 0, 0, -1, 0.13f, 1530 },
        { TX_RND, 0.04f, 1.37f, -0.26f, 0, 0, 1, 0.12f, 1540 }, { TX_HIT, 0.06f, 1.44f, 0.15f, 0, 0, -1, 0.05f, 1700 },
        { TX_RND, 0.06f, 1.44f, 0.20f, 0, 0, -1, 0.08f, 1700 }, { TX_RND, 0, 1.84f, -0.08f, 0, -1, 0, 0.09f, 1710 } } },
    { "missile:quick_kill_1", "launcher double bash (right back of the head, then top left)", {
        { TX_DENSE, 0.15f, 1.67f, -0.10f, -0.7071f, 0, 0.7071f, 0.10f, 433 }, { TX_DRIP, 0.14f, 1.58f, -0.10f, -0.7071f, 0, 0.7071f, 0.08f, 700 },
        { TX_RND, -0.05f, 1.86f, 0.02f, 0, -1, 0, 0.10f, 2300 }, { TX_SPLASH, -0.06f, 1.84f, 0.03f, 0, -1, 0, 0.08f, 2300 } } },
    { "default:quick_kill_3", "grab and throw down (top of head)", {
        { TX_RND, 0, 1.88f, 0, 0, -1, 0, 0.12f, 1400 }, { TX_SPLASH, 0.02f, 1.90f, 0.02f, 0, -1, 0, 0.09f, 1400 } } },
    { "default:quick_kill_4", "grab and throw down 2 (top of head)", {
        { TX_RND, 0, 1.88f, 0, 0, -1, 0, 0.12f, 1400 }, { TX_SPLASH, 0.02f, 1.90f, 0.02f, 0, -1, 0, 0.09f, 1400 } } },
};
struct PendingMarks { bool active, useTimer; const KillMarks* k; DWORD handle, mesh, tick; int maxMs; char anim[64]; };
static PendingMarks g_pend = {};
// v0.47: marks with a delay (multi-hit kills) wait in a small queue, painted on the victim found by its handle
struct SchedMark { DWORD due, handle; WMark m; float jx, jy; const char* name; };
static SchedMark g_sched[48]; static int g_schedN = 0;
static DWORD VictimMesh(DWORD handle)
{
    DWORD mesh = 0;
    __try {
        DWORD obj = handle ? ((DWORD (__cdecl*)(DWORD))0x4A8D20)(handle) : 0;
        if (obj) mesh = *(volatile DWORD*)(obj + 0x94);
        if (!mesh) {
            DWORD p = *(volatile DWORD*)0x6B70FC;
            DWORD c = p ? ((DWORD (__cdecl*)(DWORD))0x4A8D20)(*(volatile DWORD*)(p + 0x87C)) : 0;
            if (c) mesh = *(volatile DWORD*)(c + 0x94);
        }
    } __except (1) { mesh = 0; }
    return mesh;
}
static int PaintMark(DWORD mesh, const WMark& m, float jx, float jy)
{
    return PaintLocal(SpatTex(m.tex), mesh, m.x + (m.dx == 0 ? jx : 0), m.y + (m.dy == 0 ? jy : 0), m.z + (m.dz == 0 ? jx : 0),
                      m.dx, m.dy, m.dz, m.size, 1);
}
static void RunSchedule()
{
    DWORD now = GetTickCount();
    for (int i = 0; i < g_schedN; ) {
        SchedMark& e = g_sched[i];
        if ((int)(now - e.due) < 0) { ++i; continue; }
        DWORD mesh = VictimMesh(e.handle);
        int ok = (mesh && DecalRoom(0.9f, 8)) ? PaintMark(mesh, e.m, e.jx, e.jy) : 0;
        if (GoreLogOk()) Log("gore: '%s' timed mark at +%d ms -> %s", e.name, e.m.ms, ok ? "painted" : (mesh ? "missed the body" : "victim gone"));
        g_sched[i] = g_sched[--g_schedN];
    }
}
static void PaintKillMarks(DWORD mesh, DWORD handle, const KillMarks* k, const char* anim, DWORD waited)
{
    if (!DecalRoom(0.9f, 12)) { Log("gore: quick kill '%s' - decal memory low, marks skipped", anim); return; }
    float jx = g_goreVary ? Jit(0.015f) : 0, jy = g_goreVary ? Jit(0.015f) : 0;
    int n = 0, want = 0, later = 0; char miss[16] = ""; int mi = 0;
    DWORD now = GetTickCount();
    for (const WMark& m : k->m) {
        if (m.size <= 0) continue;
        if (m.ms > 0 && g_schedN < 48) { g_sched[g_schedN++] = { now + (DWORD)m.ms, handle, m, jx, jy, k->name }; ++later; continue; }
        ++want;
        int before = n;
        n += PaintMark(mesh, m, jx, jy);
        if (n == before && mi < 14) { miss[mi++] = (char)('0' + want); miss[mi] = 0; }
    }
    if (GoreLogOk()) Log("gore: '%s' = %s: %d of %d wound marks after %u ms, %d more timed%s%s", anim, k->name, n, want, waited, later,
                         miss[0] ? ", missed mark #" : "", miss);
}
// called every frame (mouse poll) and right after the grab: paint once the game's blood-drop timer has run out
static void PropStartPoll();
static void GorePendingUpdate()
{
    PropStartPoll();
    RunSchedule();
    if (!g_pend.active) return;
    DWORD waited = GetTickCount() - g_pend.tick;
    bool go = !g_pend.useTimer || (int)waited >= g_pend.maxMs;
    if (!go) {
        __try {
            bool set = ((char (__fastcall*)(void*, void*))0x525CC0)((void*)0x7D3AF4, nullptr) != 0;
            bool done = set && ((char (__fastcall*)(void*, void*))0x525C00)((void*)0x7D3AF4, nullptr) != 0;
            go = done || !set;
        } __except (1) { go = true; }
    }
    if (!go) return;
    g_pend.active = false;
    // the victim may be a corpse by now: same mesh object if the entity still exists, else the QK corpse
    DWORD mesh = 0;
    __try {
        DWORD obj = ((DWORD (__cdecl*)(DWORD))0x4A8D20)(g_pend.handle);
        if (obj) mesh = *(volatile DWORD*)(obj + 0x94);
        if (!mesh) {
            DWORD p = *(volatile DWORD*)0x6B70FC;
            DWORD c = p ? ((DWORD (__cdecl*)(DWORD))0x4A8D20)(*(volatile DWORD*)(p + 0x87C)) : 0;
            if (c) mesh = *(volatile DWORD*)(c + 0x94);
        }
    } __except (1) { mesh = 0; }
    if (!mesh) { Log("gore: quick kill '%s' - victim gone before the blood, marks skipped", g_pend.anim); return; }
    PaintKillMarks(mesh, g_pend.handle, g_pend.k, g_pend.anim, waited);
}
// v0.47: PROP quick kills (bottle, cleaver, wrench, crowbar, nightstick, katana, TV) are env-kill groups. The running
// group = [0x7E2DBC]; its +EffectDeath id is at group +0x20 (parser 0x4D609C) -> identifies the kill. Hit times come
// from the animations (30 fps, frame * 33 ms after the group starts). Bat: the head gibs (clothes blood does it).
// Victim = the quick-kill victim at grab-finish. The group's own table
// mark (+0x44..+0x50, size at +0x50) is switched off (size 0) for these kills: the animation check showed it is off.
static const KillMarks kPropMarks[] = {
    { "qk_bottle", "bottle (face)", {
        { TX_DENSE, -0.02f, 1.61f, 0.15f, 0, 0, -1, 0.09f, 1230 }, { TX_SPLASH, 0, 1.62f, 0.16f, 0, 0, -1, 0.11f, 1230 },
        { TX_DRIP, -0.02f, 1.55f, 0.14f, 0, 0, -1, 0.08f, 1250 } } },
    { "qk_cleaver", "cleaver (top front right of the head)", {
        { TX_HIT, 0.07f, 1.82f, 0.05f, 0, -1, 0, 0.06f, 700 }, { TX_RND, 0.07f, 1.84f, 0.05f, 0, -1, 0, 0.09f, 700 },
        { TX_DENSE, 0.06f, 1.70f, 0.15f, 0, 0, -1, 0.08f, 720 }, { TX_DRIP, 0.05f, 1.62f, 0.14f, 0, 0, -1, 0.08f, 900 } } },
    { "qk_wrench", "wrench (stomach hit, then impaled in the back)", {
        { TX_HIT, 0, 1.11f, 0.17f, 0, 0, -1, 0.05f, 770 }, { TX_DENSE, 0, 1.11f, 0.26f, 0, 0, -1, 0.14f, 770 },
        { TX_HIT, 0.02f, 1.30f, -0.17f, 0, 0, 1, 0.05f, 2130 }, { TX_DENSE, 0.02f, 1.30f, -0.26f, 0, 0, 1, 0.13f, 2130 },
        { TX_SPLASH, 0.02f, 1.32f, -0.28f, 0, 0, 1, 0.12f, 2150 }, { TX_DRIP, 0.02f, 1.20f, -0.25f, 0, 0, 1, 0.12f, 2600 } } },
    { "qk_crowbar", "crowbar (stabbed into the upper right side of the head)", {
        { TX_HIT, 0.12f, 1.68f, 0.02f, -1, 0, 0, 0.05f, 670 }, { TX_DENSE, 0.14f, 1.68f, 0.02f, -1, 0, 0, 0.08f, 670 },
        { TX_SPLASH, 0.14f, 1.70f, 0.01f, -1, 0, 0, 0.09f, 690 }, { TX_DRIP, 0.14f, 1.60f, 0.02f, -1, 0, 0, 0.08f, 900 } } },
    { "qk_tonfa", "nightstick (left neck, left shoulder, back of neck, left head)", {
        { TX_DENSE, -0.12f, 1.50f, 0.03f, 1, 0, 0, 0.08f, 470 }, { TX_RND, -0.18f, 1.52f, 0, 0, -1, 0, 0.09f, 1000 },
        { TX_DENSE, 0, 1.50f, -0.16f, 0, 0, 1, 0.09f, 2170 }, { TX_SPLASH, -0.14f, 1.62f, 0, 1, 0, 0, 0.08f, 4170 } } },
    { "qk_katana", "katana (thrust through the chest, out the back)", {
        { TX_HIT, -0.02f, 1.24f, 0.17f, 0, 0, -1, 0.05f, 430 }, { TX_DENSE, -0.02f, 1.24f, 0.27f, 0, 0, -1, 0.15f, 430 },
        { TX_STREAK, -0.02f, 1.24f, -0.28f, 0, 0, 1, 0.16f, 450 }, { TX_DRIP, -0.02f, 1.14f, 0.25f, 0, 0, -1, 0.12f, 600 } } },
    { "qk_tv", "TV (chest + back)", {
        { TX_DENSE, 0, 1.30f, 0.30f, 0, 0, -1, 0.22f, 1270 }, { TX_SPLASH, 0, 1.33f, 0.30f, 0, 0, -1, 0.18f, 1270 },
        { TX_RND, 0, 1.35f, -0.30f, 0, 0, 1, 0.22f, 1270 }, { TX_DENSE, 0, 1.28f, -0.30f, 0, 0, 1, 0.18f, 1300 } } },
};
// "group:quick_kill_N" of the active quick-kill record [0x7D1130] (+0 group index -> 0x61F040, +0x54 tag -> 0x61EB08)
static void QkKeyNow(char* anim, size_t n, DWORD* recOut)
{
    anim[0] = 0; if (recOut) *recOut = 0;
    __try {
        DWORD rec = *(volatile DWORD*)0x7D1130;
        if (recOut) *recOut = rec;
        if (rec) {
            int g = *(volatile int*)rec, t = *(volatile int*)(rec + 0x54);
            const char* gs = (g >= 0 && g < 0x20) ? *(const char**)(0x61F040 + g * 4) : nullptr;
            const char* ts = (t >= 0x101 && t < 0x119) ? *(const char**)(0x61EB08 + t * 4) : nullptr;
            if (gs && ts) _snprintf(anim, n - 1, "%s:%s", gs, ts);
            for (char* c = anim; *c; ++c) *c = (char)tolower((unsigned char)*c);
        }
    } __except (1) { anim[0] = 0; }
}
static const KillMarks* KillMarksOf(const char* key)
{
    for (const auto& km : kKillMarks) if (key[0] && !strcmp(key, km.anim)) return &km;
    return nullptr;
}
static int g_propIds[8]; static bool g_propIdsDone = false;
static DWORD g_propLastG = 0, g_propLastV = 0, g_propDoneV = 0;
static const KillMarks* PropKillOf(DWORD g, int* idOut)
{
    const int nProps = (int)(sizeof(kPropMarks) / sizeof(kPropMarks[0]));
    if (!g_propIdsDone) {
        g_propIdsDone = true;
        for (int i = 0; i < nProps; ++i) {
            __try { g_propIds[i] = ((int (__cdecl*)(const char*))0x40E440)(kPropMarks[i].anim); } __except (1) { g_propIds[i] = -1; }
        }
    }
    int id = -1; __try { id = *(volatile int*)(g + 0x20); } __except (1) { return nullptr; }
    if (idOut) *idOut = id;
    for (int i = 0; i < nProps; ++i) if (id >= 0 && g_propIds[i] == id) return &kPropMarks[i];
    return nullptr;
}
static void PropSchedule(DWORD g, const KillMarks* k, int id, DWORD handle, const char* when)
{
    __try { *(volatile float*)(g + 0x50) = 0.0f; } __except (1) {}   // the table's own (misplaced) mark off
    Log("gore: prop kill %s (effect id %d), victim handle %08X - marks timed from %s", k->name, id, handle, when);
    float jx = g_goreVary ? Jit(0.015f) : 0, jy = g_goreVary ? Jit(0.015f) : 0;
    DWORD now = GetTickCount();
    for (const WMark& m : k->m)
        if (m.size > 0 && g_schedN < 48) g_sched[g_schedN++] = { now + (DWORD)m.ms, handle, m, jx, jy, k->name };
    g_propDoneV = handle;
}
// every frame: a melee-item quick kill STARTS when [0x7D112C] (its env-kill group) is set with a new victim in the
// Punisher's quick-kill victim handle (+0x878). The hit times are frames from the start of the animation.
static void PropStartPoll()
{
    if (!g_goreKillWounds) return;
    DWORD g = 0, v = 0, ph = 0;
    __try {
        g = *(volatile DWORD*)0x7D112C;
        DWORD p = *(volatile DWORD*)0x6B70FC;
        if (p) { v = *(volatile DWORD*)(p + 0x878); ph = *(volatile DWORD*)(p + 0x14); }
    } __except (1) { return; }
    if (g == g_propLastG && v == g_propLastV) return;
    g_propLastG = g; g_propLastV = v;
    if (!v || v == ph || v == g_propDoneV) return;
    bool alive = false; __try { alive = ((DWORD (__cdecl*)(DWORD))0x4A8D20)(v) != 0; } __except (1) {}
    if (!alive) return;
    if (g) {
        int id = -1; const KillMarks* k = PropKillOf(g, &id);
        if (k) PropSchedule(g, k, id, v, "the start");
        return;
    }
    // v0.47: Slaughter Mode kills are timed from their start too (their grab-finish comes late, e.g. frame 108)
    char key[64]; DWORD rec = 0; QkKeyNow(key, sizeof(key), &rec);
    const KillMarks* k = KillMarksOf(key);
    if (!k) return;
    bool timed = false;
    for (const WMark& m : k->m) if (m.size > 0 && m.ms > 0) timed = true;
    if (!timed) return;   // v0.48: kills with measured hit times start here; the rest wait for the blood timer
    Log("gore: %s - marks timed from the start (victim %08X)", k->name, v);
    float jx = g_goreVary ? Jit(0.015f) : 0, jy = g_goreVary ? Jit(0.015f) : 0;
    DWORD now = GetTickCount();
    for (const WMark& m : k->m)
        if (m.size > 0 && g_schedN < 48) g_sched[g_schedN++] = { now + (DWORD)m.ms, v, m, jx, jy, k->name };
    g_propDoneV = v;
}
// at grab-finish (Stub_QkDecal; for prop kills that is the END of the animation): if the start was missed, paint now
static bool PropKillCheck(DWORD victim)
{
    DWORD g = 0;
    __try { g = *(volatile DWORD*)0x7D112C; } __except (1) { g = 0; }
    if (!g || !g_goreKillWounds) return false;
    int id = -1; const KillMarks* k = PropKillOf(g, &id);
    if (!k) { if (GoreLogOk()) Log("gore: item / env quick kill, death effect id %d - no wound list", id); return false; }
    DWORD handle = 0; __try { handle = *(volatile DWORD*)(victim + 0x14); } __except (1) {}
    if (handle && handle != g_propDoneV) {   // start not caught: all marks now
        __try { *(volatile float*)(g + 0x50) = 0.0f; } __except (1) {}
        Log("gore: prop kill %s - start not caught, marks now", k->name);
        float jx = g_goreVary ? Jit(0.015f) : 0, jy = g_goreVary ? Jit(0.015f) : 0;
        DWORD now = GetTickCount();
        for (const WMark& m : k->m)
            if (m.size > 0 && g_schedN < 48) g_sched[g_schedN++] = { now, handle, m, jx, jy, k->name };
        g_propDoneV = handle;
    }
    return true;
}
static bool __cdecl QkKillMarks(DWORD victim)
{
    if (!g_goreKillWounds || !victim) return false;
    if (PropKillCheck(victim)) return false;   // v0.47: prop kill scheduled; the record / env table marks are off or size 0
    char anim[64]; QkKeyNow(anim, sizeof(anim), nullptr);
    DWORD vh = 0; __try { vh = *(volatile DWORD*)(victim + 0x14); } __except (1) {}
    const KillMarks* k = KillMarksOf(anim);
    if (k && vh && vh == g_propDoneV) return true;   // v0.47: already scheduled from the kill's start
    if (!k) { if (GoreLogOk()) Log("gore: quick kill '%s' - no wound list, table decal as usual", anim); return false; }
    DWORD mesh = 0, handle = 0;
    __try { mesh = *(volatile DWORD*)(victim + 0x94); handle = *(volatile DWORD*)(victim + 0x14); } __except (1) {}
    if (!mesh || !DecalRoom(0.9f, 12)) { Log("gore: quick kill '%s' - no mesh / decal memory low, table decal as usual", anim); return false; }
    // v0.46.3: paint when the kill's blood starts, not at the grab: the game starts its blood-drop timer 0x7D3AF4
    // at the quick-kill start with the table's "first drop" ms (+Blood_particles 2nd value, record +0x60).
    int drops = 0, firstMs = 0;
    __try { DWORD rec = *(volatile DWORD*)0x7D1130; drops = *(volatile int*)(rec + 0x5C); firstMs = *(volatile int*)(rec + 0x60); } __except (1) {}
    g_pend.k = k; g_pend.handle = handle; g_pend.mesh = mesh; g_pend.tick = GetTickCount();
    g_pend.useTimer = drops > 0; g_pend.maxMs = (firstMs > 0 && firstMs < 6000) ? firstMs + 300 : 2500;
    strncpy(g_pend.anim, anim, sizeof(g_pend.anim) - 1);
    g_pend.active = true;
    if (GoreLogOk()) Log("gore: quick kill '%s' = %s: marks wait for the blood (%d drops, first at %d ms)", anim, k->name, drops, firstMs);
    GorePendingUpdate();   // the blood may already have started
    return true;
}
static __declspec(naked) void Stub_QkDecal()
{
    __asm {
        pushad
        push ebx
        call QkKillMarks
        add  esp, 4
        mov  [esp + 0x1C], eax           // pushad's eax slot
        popad
        test al, al
        jnz  skip
        mov  eax, dword ptr ds:[0x7D1130]   // the replaced instruction
        ret
    skip:
        add  esp, 4                      // drop our return address
        push 0x4CDB03                    // continue after the table decal
        ret
    }
}

// ---------------------------------------------------------------- v0.46: limits
// (1) body decal records 256 -> 1024: the array 0xC91610 is only used by the init 0x52EA60 (imm start 0x52EA73,
//     end 0x52EB09). (2) the decal vertex arena (bump allocator, 4 MB) was never reset: reset it whenever the
//     records are rebuilt (level load: jmp 0x52EA60 at 0x530911) and make it 16 MB if not allocated yet.
// (3) gib bursts / pieces per moment 4 -> 16 (counter 0x6B6A60; cmp imm8 at 0x411992, 0x4119F1, 0x411B37).
// (4) world (floor / wall) decals: one pool for bullet holes, wall spatter and floor drops (125 in .decals).
//     Bigger pool + "keep blood": bullet holes are removed / faded first.
static const int kBodyRecords = 1024;
static const DWORD kArenaBytes = 0x1000000;
static int g_goreWorld = 1;                     // [Gore] WorldBlood 0 Normal 125, 1 Large 300, 2 Huge 600
static const int kWorldCaps[3] = { 125, 300, 600 };
static int g_worldCapLive = 125, g_worldCapLess3 = 122;
static BYTE* g_worldArr = nullptr;
static int g_arenaResets = 0;
static int g_vbReleased = 0;   // (6) leak fix count

static __declspec(naked) void Stub_ArenaReset()
{
    __asm {
        mov  dword ptr ds:[0xC93638], 0
        inc  dword ptr [g_arenaResets]
        mov  eax, 0x52EA60
        jmp  eax
    }
}
static bool PatchImm32(DWORD at, DWORD expect, DWORD val, const char* what)
{
    if (*(DWORD*)at != expect) { Log("limits: %s at %08X not as expected (%08X) - skipped", what, at, *(DWORD*)at); return false; }
    DWORD old; VirtualProtect((void*)at, 4, PAGE_EXECUTE_READWRITE, &old);
    *(DWORD*)at = val;
    VirtualProtect((void*)at, 4, old, &old);
    return true;
}
static void PatchByte(DWORD at, BYTE expect, BYTE val, const char* what)
{
    if (*(BYTE*)at != expect) { Log("limits: %s at %08X not as expected (%02X) - skipped", what, at, *(BYTE*)at); return; }
    DWORD old; VirtualProtect((void*)at, 1, PAGE_EXECUTE_READWRITE, &old);
    *(BYTE*)at = val;
    VirtualProtect((void*)at, 1, old, &old);
}
static void PatchJmpOrCall(DWORD at, BYTE op, void* to, int total)   // op E8 / E9, rest NOPs
{
    DWORD old; VirtualProtect((void*)at, total, PAGE_EXECUTE_READWRITE, &old);
    BYTE* p = (BYTE*)at; p[0] = op; *(LONG*)(p + 1) = (LONG)((DWORD)to - (at + 5));
    for (int i = 5; i < total; ++i) p[i] = 0x90;
    VirtualProtect((void*)at, total, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, total);
}

// ---------------------------------------------------------------- v0.58: always play flashbacks (notes\RESEARCH_FLASHBACKS.md)
// Flashbacks start DURING an interrogation (0x4D04D0): once per level, Flashbacks_on 0x667415, not Punishment rules
// ([0x70E22C] != 2), a 50 % roll, then the level's 2 flashback ids (0x7421B0 + level * 0x3B8) are checked: the first
// one the player has NOT seen (player + 0x3C + id; the same 24 bytes = the Extras gallery unlocks) plays. When both are
// seen the loop at 0x4D09C1 falls through to the normal interrogation. With [Beta] AlwaysFlashbacks the end of that loop
// picks a seen one anyway (slot 0 / 1 alternating). Unseen ones still come first; the save / gallery is not touched; the
// gang check of the play path (0x4D0ACB) still applies (a Gnucci flashback needs a Gnucci thug).
int g_alwaysFlashbacks = 0;
static volatile int g_fbToggle = 0;
static DWORD kFbLoopTop = 0x4D09B0, kFbNone = 0x4D09CA, kFbPlay = 0x4D0ACB;
__declspec(naked) static void FbLoopStub()
{
    __asm {
        inc  ecx
        add  eax, 4
        cmp  ecx, 2
        jge  all_seen
        jmp  dword ptr [kFbLoopTop]
    all_seen:
        cmp  dword ptr [g_alwaysFlashbacks], 0
        je   none
        xor  dword ptr [g_fbToggle], 1
        mov  ecx, dword ptr [g_fbToggle]
        cmp  dword ptr [eax - 8 + ecx*4], 0
        jge  play
        xor  ecx, ecx
    play:
        jmp  dword ptr [kFbPlay]
    none:
        jmp  dword ptr [kFbNone]
    }
}
static void InstallAlwaysFlashbacks(const char* ini)
{
    g_alwaysFlashbacks = GetPrivateProfileIntA("Beta", "AlwaysFlashbacks", 0, ini) ? 1 : 0;
    static const BYTE orig[9] = { 0x41, 0x83, 0xC0, 0x04, 0x83, 0xF9, 0x02, 0x7C, 0xE6 };
    static const BYTE play[6] = { 0x0F, 0x84, 0x0A, 0x01, 0x00, 0x00 };
    if (memcmp((void*)0x4D09C1, orig, 9) == 0 && memcmp((void*)0x4D09BB, play, 6) == 0) {
        PatchJmpOrCall(0x4D09C1, 0xE9, (void*)FbLoopStub, 9);
        Log("flashbacks: replay hook at 004D09C1 installed (AlwaysFlashbacks=%d)", g_alwaysFlashbacks);
    } else Log("flashbacks: code at 004D09C1 not as expected - replay option off");
}

// world decal record (0xD8): +0x14 created time (float), +0x1C flags (0x40000000 = fading), +0x20 texture,
// list next +0xD0 / prev +0xD4, head = oldest [0x68AA38]
static DWORD g_bloodTex[16]; static int g_bloodTexN = 0; static bool g_bloodTexDone = false;
static bool IsBloodTex(DWORD t)
{
    if (!g_bloodTexDone) {
        g_bloodTexDone = true;
        __try {
            for (int i = 0; i < 12; ++i) g_bloodTex[g_bloodTexN++] = *(volatile DWORD*)(0x6D8708 + i * 4);
            g_bloodTex[g_bloodTexN++] = *(volatile DWORD*)0x61F1B8;
            g_bloodTex[g_bloodTexN++] = (DWORD)((int (__cdecl*)(const char*, int))0x50C750)("bloodsplat-drop.tga", -1);
            g_bloodTex[g_bloodTexN++] = (DWORD)((int (__cdecl*)(const char*, int))0x50C750)("all_blood_01.tga", -1);
        } __except (1) {}
        Log("limits: keep-blood rule knows %d blood textures", g_bloodTexN);
    }
    for (int i = 0; i < g_bloodTexN; ++i) if (g_bloodTex[i] == t) return true;
    return false;
}
static int g_keepBloodLogs = 0;
static DWORD __cdecl WorldPickRemove()   // replaces "mov ebx,[0x68AA38]" (the oldest) when the pool is full
{
    DWORD head = *(volatile DWORD*)0x68AA38;
    __try {
        DWORD p = head; int n = 0;
        while (p && n++ < 2000) {
            if (!IsBloodTex(*(volatile DWORD*)(p + 0x20))) {
                if (p != head && g_decalDiag && g_keepBloodLogs++ < 30) Log("limits: world decals full - removed a bullet hole, kept older blood");
                return p;
            }
            p = *(volatile DWORD*)(p + 0xD0);
            if (p == head) break;
        }
    } __except (1) {}
    return head;
}
static __declspec(naked) void Stub_WorldPickRemove()
{
    __asm {
        pushad
        call WorldPickRemove
        mov  [esp + 0x10], eax           // pushad's ebx slot
        popad
        ret
    }
}
static void __cdecl WorldFadeOne()   // replaces 0x40A1B0: start fading the oldest decal, bullet holes first
{
    DWORD head = *(volatile DWORD*)0x68AA38;
    if (!head) return;
    DWORD best[2] = { 0, 0 }; float bt[2] = { 3.0e38f, 3.0e38f };
    __try {
        DWORD p = head; int n = 0;
        do {
            DWORD fl = *(volatile DWORD*)(p + 0x1C);
            if (!(fl & 0x40000000)) {
                int b = IsBloodTex(*(volatile DWORD*)(p + 0x20)) ? 1 : 0;
                float t = *(volatile float*)(p + 0x14);
                if (t < bt[b]) { bt[b] = t; best[b] = p; }
            }
            p = *(volatile DWORD*)(p + 0xD0);
        } while (p && p != head && ++n < 2000);
        DWORD pick = best[0] ? best[0] : best[1];
        if (pick) *(volatile DWORD*)(pick + 0x1C) |= 0x40000000;
    } __except (1) {}
}
static __declspec(naked) void Stub_WorldCapFull()   // replaces "cmp [count],125" (then jne): equal or more = full
{
    __asm {
        push eax
        mov  eax, dword ptr ds:[0x68AA30]
        cmp  eax, dword ptr [g_worldCapLive]
        jl   notfull
        cmp  eax, eax                    // ZF = 1
        pop  eax
        ret
    notfull:
        test esp, esp                    // ZF = 0
        pop  eax
        ret
    }
}
static __declspec(naked) void Stub_WorldCapNear()   // replaces "cmp [count],122" (then jl)
{
    __asm {
        push eax
        mov  eax, dword ptr ds:[0x68AA30]
        cmp  eax, dword ptr [g_worldCapLess3]
        pop  eax
        ret
    }
}
static void __cdecl WorldInit()   // the pool is rebuilt (level load): apply the wanted size now
{
    int cap = kWorldCaps[(g_goreWorld >= 0 && g_goreWorld <= 2) ? g_goreWorld : 1];
    DWORD old; VirtualProtect((void*)0x40A376, 4, PAGE_EXECUTE_READWRITE, &old);
    *(DWORD*)0x40A376 = (DWORD)g_worldArr + cap * 0xD8;
    VirtualProtect((void*)0x40A376, 4, old, &old);
    g_worldCapLive = cap; g_worldCapLess3 = cap - 3;
    Log("limits: floor / wall decal pool rebuilt with %d slots (keep blood on); body decal arena resets so far: %d, decal buffers released: %d", cap, g_arenaResets, g_vbReleased);
}
static __declspec(naked) void Stub_WorldInit()   // replaces "mov eax, 0x1106000" (array start) in 0x40A200
{
    __asm {
        pushad
        call WorldInit
        popad
        mov  eax, dword ptr [g_worldArr]
        ret
    }
}
// (6) LEAK FIX: every body decal makes a D3D vertex buffer (device->CreateVertexBuffer at 0x52F8CE, stored in
//     record +8) and NO code ever releases it - a reused record just overwrites the pointer. Release the old
//     buffer when a record is reused (the record sat on the free list, nothing draws it).
static void __cdecl ReleaseOldDecalVB(DWORD rec)
{
    __try {
        DWORD vb = *(DWORD*)(rec + 8);   // COM object: vtable slot 2 = Release (stdcall, this)
        if (vb) { *(DWORD*)(rec + 8) = 0; ((ULONG (__stdcall*)(DWORD))(*(DWORD**)vb)[2])(vb); ++g_vbReleased; }
    } __except (1) {}
}
static __declspec(naked) void Stub_DecalVB()   // replaces "mov eax,[0x913C48]" at 0x52F8C5 (ebx = record)
{
    __asm {
        pushad
        push ebx
        call ReleaseOldDecalVB
        add  esp, 4
        popad
        mov  eax, dword ptr ds:[0x913C48]
        ret
    }
}
// v0.50: the gun-interrogation camera from the old exe patch, now done at run time (no patched pun.exe needed).
// Camera table 0x667418..0x6675EC, 0x24-byte entries (key, pos xyz, dir xyz, float, probe); slot 1 of key 1 (gun)
// at 0x667484 becomes "E2" (over the shoulder), and the pick at 0x4D8C08 becomes random between the gun cameras
// (if the picked camera is blocked the game still falls back to the other one).
static const BYTE kCamE2[36] = { 0x01,0x00,0x00,0x00, 0x00,0x00,0x80,0xbe, 0xcd,0xcc,0xec,0x3f, 0x00,0x00,0x40,0xbf,
    0x9c,0x9c,0x97,0xbe, 0x9c,0x9c,0x97,0xbe, 0xac,0x78,0x68,0x3f, 0x00,0x00,0x80,0x3f, 0x00,0x00,0x40,0x3f };
static const BYTE kCamRetail[36] = { 0x01,0x00,0x00,0x00, 0x37,0x89,0x81,0x3e, 0x02,0x2b,0x07,0x40, 0x58,0x39,0x14,0xbf,
    0x85,0xeb,0x11,0xbf, 0x9e,0xef,0x07,0xbf, 0x12,0x83,0x20,0x3f, 0x00,0x00,0x80,0x3f, 0x00,0x00,0x40,0x3f };
// v0.54: the Oct 2004 gun camera = entry 3 of the same table in the Oct 2004 PS2 demo (SLUS_291.38 + OPM 90 disc);
// every later build (Jan 2005 beta, Xbox, PS2 / PC retail) has kCamRetail. Above / behind the LEFT shoulder, 42 deg down.
static const BYTE kCamOct2004[36] = { 0x01,0x00,0x00,0x00, 0x9c,0xc4,0x20,0xbe, 0x19,0x04,0x16,0x40, 0x68,0x91,0xed,0xbe,
    0x8b,0x6c,0x67,0xbe, 0x1f,0x85,0x2b,0xbf, 0x7d,0x3f,0x35,0x3f, 0x00,0x00,0x80,0x3f, 0x00,0x00,0x40,0x3f };
// [Settings] GunInterrogationCamera: 0 = retail, 1 = over-the-shoulder (E2) + front at random, 2 = Oct 2004 + front at
// random, 3 = Oct 2004 only (the front camera only if the first one is blocked). Menu: Camera & Photo page.
static int g_gunCam = 1;
static int g_gunCamApplied = -1;
static const char* kGunCamNames[4] = { "Retail", "Over the shoulder + front", "Oct 2004 + front", "Oct 2004 only" };
static void GunCamApply()      // writes gun camera 1 for the current setting (cheap: only when it changed)
{
    if (g_gunCam == g_gunCamApplied) return;
    const BYTE* src = g_gunCam == 1 ? kCamE2 : (g_gunCam >= 2 ? kCamOct2004 : kCamRetail);
    DWORD old; VirtualProtect((void*)0x667484, 36, PAGE_READWRITE, &old);
    memcpy((void*)0x667484, src, 36);
    VirtualProtect((void*)0x667484, 36, old, &old);
    g_gunCamApplied = g_gunCam;
    Log("gun interrogation camera: %s", kGunCamNames[g_gunCam & 3]);
}
static int __cdecl CamPickC(int count)   // returns the game's loop start (0 = first camera, -n = camera n)
{
    GunCamApply();                        // menu changes take effect at the next interrogation
    if ((g_gunCam != 1 && g_gunCam != 2) || count <= 0) return 0;
    return -(rand() % count);
}
static __declspec(naked) void Stub_CamPick()   // replaces "xor edx,edx / xor eax,eax / mov ecx,0x667418" (esi = key, ecx = count)
{
    __asm {
        xor  edx, edx
        cmp  esi, 1               // gun interrogation cameras only
        jne  done
        push ecx
        push ecx
        call CamPickC
        add  esp, 4
        pop  ecx
        mov  edx, eax             // the game's loop counts edx up to 0 = picks that entry
    done:
        xor  eax, eax
        mov  ecx, 0x667418
        ret
    }
}
static void InstallGunCamera()
{
    char ini[MAX_PATH]; snprintf(ini, sizeof(ini), "%s\\ExtendedCut.ini", g_dir);
    g_gunCam = GetPrivateProfileIntA("Settings", "GunInterrogationCamera", 1, ini);
    if (g_gunCam < 0 || g_gunCam > 3) g_gunCam = 1;
    bool tableOk = !memcmp((void*)0x667484, kCamRetail, 36) || !memcmp((void*)0x667484, kCamE2, 36);
    bool siteOk = !memcmp((void*)0x4D8C08, "\x33\xD2\x33\xC0\xB9\x18\x74\x66\x00", 9);
    // the old patched exe ("call 0x5F4C00" + 4 nops, its random pick in a code cave) - same 9 bytes, so take it over
    bool oldExe = *(BYTE*)0x4D8C08 == 0xE8 && *(DWORD*)0x4D8C09 == 0x5F4C00 - (0x4D8C08 + 5);
    if (oldExe) Log("gun interrogation camera: old exe patch found - replaced by the menu choice");
    if (!tableOk || !(siteOk || oldExe)) { Log("gun interrogation camera: game code not as expected - unchanged"); return; }
    PatchJmpOrCall(0x4D8C08, 0xE8, (void*)Stub_CamPick, 9);
    GunCamApply();
}
// v0.50.1 (bug hunt B1): the floor / wall decal add path 0x40A4E0 zeroes the record's vertex buffer at 0x40A500
// without Release -> one leaked buffer per reused slot. Release it first (0x40A460 zeroes +0xC4 after its own
// Release, so nothing is released twice). ebp = the record at 0x40A4F2 ("lea ebx,[ebp+0xC4]").
static int g_worldVbReleased = 0;
static void __cdecl ReleaseOldWorldVB(DWORD rec)
{
    __try {
        DWORD vb = *(DWORD*)(rec + 0xC4);
        if (vb) { *(DWORD*)(rec + 0xC4) = 0; ((ULONG (__stdcall*)(DWORD))(*(DWORD**)vb)[2])(vb); ++g_worldVbReleased; }
    } __except (1) {}
}
static __declspec(naked) void Stub_WorldVB()
{
    __asm {
        pushad
        push ebp
        call ReleaseOldWorldVB
        add  esp, 4
        popad
        lea  ebx, [ebp + 0xC4]
        ret
    }
}
static void InstallGoreLimits()
{
    // v0.50.4: the byte check here was damaged in v0.50.1-3 (so the fix never switched on). Now behind
    // [Settings] WorldDecalLeakFix, default 0 until it has been tested in game.
    char iniL[MAX_PATH]; snprintf(iniL, sizeof(iniL), "%s\\ExtendedCut.ini", g_dir);
    if (!GetPrivateProfileIntA("Settings", "WorldDecalLeakFix", 0, iniL))
        Log("limits: floor / wall decal buffer leak fix off (WorldDecalLeakFix=0)");
    else if (!memcmp((void*)0x40A4F2, "\x8D\x9D\xC4\x00\x00\x00", 6)) {   // lea ebx,[ebp+0xC4]
        PatchJmpOrCall(0x40A4F2, 0xE8, (void*)Stub_WorldVB, 6);
        Log("limits: floor / wall decal graphics-buffer leak fix on");
    } else Log("limits: floor / wall decal buffer site not as expected - leak fix off");
    if (!memcmp((void*)0x52F8C5, "\xA1\x48\x3C\x91\x00", 5) && !memcmp((void*)0x52F8BB, "\x89\x0B", 2)) {
        PatchJmpOrCall(0x52F8C5, 0xE8, (void*)Stub_DecalVB, 5);
        Log("limits: body decal graphics-buffer leak fix on (old buffer released when a decal slot is reused)");
    } else Log("limits: decal buffer site not as expected - leak fix off");
    // (1) body decal records
    BYTE* rec = (BYTE*)VirtualAlloc(nullptr, kBodyRecords * 0x20, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (rec && *(BYTE*)0x52EA72 == 0xB8 && *(BYTE*)0x52EB08 == 0x3D &&
        PatchImm32(0x52EA73, 0xC91610, (DWORD)rec, "body decal records start")) {
        PatchImm32(0x52EB09, 0xC93610, (DWORD)rec + kBodyRecords * 0x20, "body decal records end");
        Log("limits: body decal records 256 -> %d", kBodyRecords);
    }
    // (2) arena: bigger (only if not allocated yet) + reset on every record rebuild
    if (*(volatile DWORD*)0xC93634 == 0 && *(BYTE*)0x52EA40 == 0x68 && *(BYTE*)0x52EA24 == 0x3D) {
        DWORD cur = *(DWORD*)0x52EA41;
        if (cur == *(DWORD*)0x52EA25 && cur >= 0x19000 && cur < kArenaBytes &&
            PatchImm32(0x52EA41, cur, kArenaBytes, "decal arena size")) {
            PatchImm32(0x52EA25, cur, kArenaBytes, "decal arena limit");
            Log("limits: body decal memory %u KB -> %u KB", cur / 1024, kArenaBytes / 1024);
        }
    } else Log("limits: decal arena already allocated (%08X) - size unchanged", *(volatile DWORD*)0xC93634);
    if (*(BYTE*)0x530911 == 0xE9 && 0x530911 + 5 + *(LONG*)0x530912 == 0x52EA60) {
        PatchJmpOrCall(0x530911, 0xE9, (void*)Stub_ArenaReset, 5);
        Log("limits: body decal memory is now cleared at every level load");
    } else Log("limits: level-load decal reset site not as expected - skipped");
    // (3) gib bursts per moment
    PatchByte(0x411992, 4, 16, "gib cap (head)");
    PatchByte(0x4119F1, 4, 16, "gib cap (limb)");
    PatchByte(0x411B37, 4, 16, "gib cap (piece)");
    Log("limits: gib bursts / pieces per moment 4 -> 16");
    // (4) world decals
    // v0.50: works on the retail exe too (caps 20 / 17 = 0x14 / 0x11) - the 125-decal exe (.decals section) is no
    // longer needed: the pool lives in plugin memory either way.
    static const BYTE capFull[6] = { 0x83, 0x3D, 0x30, 0xAA, 0x68, 0x00 };
    static const BYTE headLd[6] = { 0x8B, 0x1D, 0x38, 0xAA, 0x68, 0x00 }, fade[6] = { 0x8B, 0x15, 0x38, 0xAA, 0x68, 0x00 };
    BYTE cf = *(BYTE*)0x40B07E, cn = *(BYTE*)0x40B099;
    bool capsKnown = (cf == 0x7D && cn == 0x7A) || (cf == 0x14 && cn == 0x11);
    g_worldArr = (BYTE*)VirtualAlloc(nullptr, 600 * 0xD8, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_worldArr && capsKnown && !memcmp((void*)0x40B078, capFull, 6) && !memcmp((void*)0x40B093, capFull, 6) &&
        !memcmp((void*)0x40B082, headLd, 6) && !memcmp((void*)0x40A1B0, fade, 6) &&
        *(BYTE*)0x40A226 == 0xB8 && *(BYTE*)0x40A375 == 0x3D) {
        PatchJmpOrCall(0x40A226, 0xE8, (void*)Stub_WorldInit, 5);
        PatchJmpOrCall(0x40B078, 0xE8, (void*)Stub_WorldCapFull, 7);
        PatchJmpOrCall(0x40B093, 0xE8, (void*)Stub_WorldCapNear, 7);
        PatchJmpOrCall(0x40B082, 0xE8, (void*)Stub_WorldPickRemove, 6);
        PatchJmpOrCall(0x40A1B0, 0xE9, (void*)WorldFadeOne, 5);
        Log("limits: floor / wall decals: pool %d at the next level load, keep-blood rule on", kWorldCaps[g_goreWorld]);
    } else Log("limits: floor / wall decal code not as expected - unchanged");
    // v0.50: physical gib pieces last 60 s instead of 10 (the old exe patch; longer crashes at 0x5792F3)
    {   // v0.50.1: option, default 10 s = retail (60 s pieces are linked to the zoo crash at 0x5792F3)
        char ini[MAX_PATH]; snprintf(ini, sizeof(ini), "%s\\ExtendedCut.ini", g_dir);
        int sec = GetPrivateProfileIntA("Gore", "GibPieceSeconds", 10, ini);
        if (sec > 60) sec = 60;
        float f = (float)sec; DWORD v; memcpy(&v, &f, 4);
        if (sec > 10 && *(DWORD*)0x411BFE == 0x41200000) PatchImm32(0x411BFE, 0x41200000, v, "gib piece lifetime");
        Log("limits: gib pieces last %.0f s (GibPieceSeconds=%d)", *(float*)0x411BFE, sec);
    }
    // (5) per-kill wound marks
    if (!memcmp((void*)0x4CDA33, "\xA1\x30\x11\x7D\x00", 5)) {
        PatchJmpOrCall(0x4CDA33, 0xE8, (void*)Stub_QkDecal, 5);
        Log("gore: per-kill wound marks hooked at 004CDA33");
    } else Log("gore: quick-kill decal start not as expected - per-kill wound marks off");
}

// the 11-byte decision patch: call stub / test al,al / jne apply (same place as the retail jne) / jmp skip
static bool PatchGibDecision(DWORD at, void* stub, BYTE skipRel, const char* name)
{
    BYTE* p = (BYTE*)at;
    static const BYTE head[7] = { 0xA1, 0x2C, 0x11, 0x7D, 0x00, 0x85, 0xC0 };
    if (memcmp(p, head, 7) != 0 || !(p[7] == 0x75 || p[7] == 0xEB) || p[8] != 0x29 || p[9] != 0xA1 || p[10] != 0xFC) {
        Log("gore: %s head-gib check at %08X not as expected (%02X %02X %02X %02X %02X %02X %02X %02X %02X) - Exploding heads not installed there",
            name, at, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8]);
        return false;
    }
    BYTE was = p[7];
    DWORD old; VirtualProtect(p, 11, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE8; *(LONG*)(p + 1) = (LONG)((DWORD)stub - (at + 5));
    p[5] = 0x84; p[6] = 0xC0;          // test al, al
    p[7] = 0x75; p[8] = 0x29;          // jne apply (the retail target)
    p[9] = 0xEB; p[10] = skipRel;      // jmp skip (the retail "cancel" target)
    VirtualProtect(p, 11, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 11);
    Log("gore: %s head-gib decision hooked at %08X (exe had %s = %s)", name, at, was == 0xEB ? "EB 29" : "75 29",
        was == 0xEB ? "head-gib unlock, gore-patched" : "retail check");
    return true;
}
// v0.77: GIB STUMPS - Original / Restored / Random variation ([Gore] Stumps 0 / 1 / 2, Gore page).
// The game's own stump models ship pointing INWARD (only a flat cone shows); flipped outward with their own textures
// they show torn meat + bone (the neck matches the store-kiosk attract video). tools\5 Cut hunt\stumps_restored.py
// ADDS variant files to mini.vpp next to the untouched originals: <stub>_u (restored), <stub>_b (restored, bloodier
// texture), hips also <stub>_s (slim, for thin bodies). The game loads a gib's stump by the name in the enemy CLASS
// record (+0x150 = 5 x 24-byte names, base 0x6B7258 + class * 0x2D0, slot order neck / l arm / r arm / l leg / r leg)
// inside the gib function 0x411870 (esi = corpse, 1st arg = gib type, slot = 0x6B70DC[type], class = corpse +0x248).
// An entry hook on 0x411870 writes the chosen file name there just before. Without the variant files in mini.vpp the
// option stays on Original (a missing file would break the gib).
static int   g_goreStumps = 1;
static bool  g_stumpFiles = false;
static bool  g_stumpTucked = false;   // v0.78: arms <stub>_t (tucked) in the Random pool
static bool  g_stumpCone = false;     // v0.79: legs <stub>_c (cone + piece v2) in the Random pool
static void* g_trGibEntry = nullptr;
static int   g_stumpLogs = 0;
static const char* const kStubBase[5] = { "stub-neck", "stub-l_shldr", "stub-r_shoulder", "stub-l_hip", "stub-r_hip" };
static void* EntryHook(DWORD at, int len, const BYTE* expect, void* hook, const char* what);
static void __cdecl StumpPick(DWORD corpse, int type)
{
    __try {
        if (type < 0 || type > 7) return;
        int slot = *(volatile int*)(0x6B70DC + type * 4);
        if (slot < 0 || slot > 4) return;
        int cls = *(volatile int*)(corpse + 0x248);
        if (cls < 0 || cls > 400) return;
        char* nm = (char*)(0x6B7258 + cls * 0x2D0 + slot * 24);
        if (strncmp(nm, kStubBase[slot], strlen(kStubBase[slot])) != 0) return;     // not a name we know - leave it
        int mode = g_stumpFiles ? g_goreStumps : 0;
        const char* suf = "";
        if (mode == 1) suf = "_u";
        else if (mode == 2) {                       // the restored one is part of the random pool
            static const char* hip[4] = { "_u", "_b", "_s", "_c" }, *neck[2] = { "_u", "_b" }, *arm[3] = { "_u", "_b", "_t" };
            if (slot >= 3) suf = hip[rand() % (g_stumpCone ? 4 : 3)];
            else if (slot >= 1) suf = arm[rand() % (g_stumpTucked ? 3 : 2)];
            else suf = neck[rand() % 2];
        }
        char want[24];
        snprintf(want, sizeof(want), "%s%s.rfm", kStubBase[slot], suf);
        if (strcmp(nm, want) != 0) memcpy(nm, want, strlen(want) + 1);
        if (g_stumpLogs < 12) { ++g_stumpLogs; Log("gore: gib slot %d (class %d) -> %s", slot, cls, want); }
    } __except (1) {}
}
static __declspec(naked) void EH_GibEntry()
{
    __asm {
        pushad
        mov  eax, [esp + 36]          // 1st argument = gib type (after the 8 pushad registers + return address)
        push eax
        push esi                      // the corpse
        call StumpPick
        add  esp, 8
        popad
        jmp  dword ptr [g_trGibEntry]
    }
}
static void InstallStumps(const char* ini)
{
    g_goreStumps = (int)GetPrivateProfileIntA("Gore", "Stumps", 1, ini);
    if (g_goreStumps < 0) g_goreStumps = 0; if (g_goreStumps > 2) g_goreStumps = 2;
    char exe[MAX_PATH]; GetModuleFileNameA(NULL, exe, MAX_PATH); if (char* q = strrchr(exe, '\\')) *q = 0;
    char mv[MAX_PATH]; snprintf(mv, MAX_PATH, "%s\\dvd_pc\\data\\mini.vpp", exe);
    g_stumpFiles = false;
    if (FILE* f = fopen(mv, "rb")) {                                  // the directory is at the start of the archive
        static char buf[0x10000]; size_t n = fread(buf, 1, sizeof(buf), f); fclose(f);
        int found = 0;
        for (const char* want : { "stub-neck_u.rxm", "stub-l_hip_b.rxm", "stub-r_hip_s.rxm", "stub-r_shoulder_b.rxm" })
            for (size_t i = 0; i + strlen(want) <= n; ++i) if (!_strnicmp(buf + i, want, strlen(want))) { ++found; break; }
        g_stumpFiles = found == 4;
        for (const char* want : { "stub-l_shldr_t.rxm" })
            for (size_t i = 0; i + strlen(want) <= n; ++i) if (!_strnicmp(buf + i, want, strlen(want))) { g_stumpTucked = true; break; }
        for (const char* want : { "stub-l_hip_c.rxm" })
            for (size_t i = 0; i + strlen(want) <= n; ++i) if (!_strnicmp(buf + i, want, strlen(want))) { g_stumpCone = true; break; }
        if (!g_stumpFiles) g_stumpTucked = g_stumpCone = false;
    }
    static const BYTE want[5] = { 0x83, 0xEC, 0x58, 0x53, 0x55 };     // sub esp,58h / push ebx / push ebp
    g_trGibEntry = EntryHook(0x411870, 5, want, (void*)EH_GibEntry, "gib stumps");
    static const char* nm[3] = { "Original", "Restored", "Random variation" };
    Log("gore: gib stumps %s%s%s%s", nm[g_goreStumps], g_stumpFiles ? "" : " - the restored stump files are not in mini.vpp, Original is used",
        g_stumpTucked ? " (random pool has the tucked arms)" : "", g_stumpCone ? " (and the cone + piece legs)" : "");
}
static void InstallGore(const char* ini)
{
    InstallStumps(ini);   // v0.77
    g_goreHeads   = (int)GetPrivateProfileIntA("Gore", "ExplodingHeads", 2, ini);
    g_goreClothes = GetPrivateProfileIntA("Gore", "BloodOnClothes", 0, ini) ? 1 : 0;
    g_goreBullet  = (int)GetPrivateProfileIntA("Gore", "BulletHitBlood", 0, ini);
    g_goreVary    = GetPrivateProfileIntA("Gore", "WoundVariation", 1, ini) ? 1 : 0;
    g_goreKillWounds = GetPrivateProfileIntA("Gore", "KillWounds", 1, ini) ? 1 : 0;           // v0.46
    g_goreGunHeads = (int)GetPrivateProfileIntA("Gore", "GunfireHeads", 0, ini);              // v0.48.1
    if (g_goreGunHeads < 0) g_goreGunHeads = 0; if (g_goreGunHeads > 3) g_goreGunHeads = 3;
    g_goreCritPct = (int)GetPrivateProfileIntA("Gore", "CritChance", 25, ini);                // v0.50.8
    if (g_goreCritPct < 5) g_goreCritPct = 5; if (g_goreCritPct > 100) g_goreCritPct = 100;
    {   // v0.50.8: critical head-shot hook (always installed; does nothing unless GunfireHeads = 3)
        static const BYTE want[6] = { 0x8B, 0x8B, 0xD0, 0x09, 0x00, 0x00 };
        if (!memcmp((void*)0x4148F7, want, 6)) { PatchJmpOrCall(0x4148F7, 0xE9, (void*)Stub_Crit, 6); Log("gore: critical head-shot hook at 004148F7"); }
        else Log("gore: critical head-shot site not as expected - skipped");
    }
    g_goreWorld   = (int)GetPrivateProfileIntA("Gore", "WorldBlood", 1, ini);                 // v0.46
    if (g_goreWorld < 0) g_goreWorld = 0; if (g_goreWorld > 2) g_goreWorld = 2;
    if (g_goreHeads < 0) g_goreHeads = 0; if (g_goreHeads > 2) g_goreHeads = 2;
    if (g_goreBullet < 0) g_goreBullet = 0; if (g_goreBullet > 3) g_goreBullet = 3;
    srand(GetTickCount());
    // the two stubs and their skip targets: 0x411FA5 + 2 + 0x7B = 0x412022, 0x414F41 + 2 + 0x69 = 0x414FAC
    bool a = PatchGibDecision(0x411F9C, (void*)Stub_GibHeadUpdate, 0x7B, "corpse update");
    bool b = PatchGibDecision(0x414F38, (void*)Stub_GibHeadCreate, 0x69, "corpse creation");
    if (a != b) Log("gore: only one of the two head-gib checks could be hooked - the other keeps the exe's behaviour");
    // G4a: every gib call that can be a head gib (anim triggers on the corpse / at creation, gunfire, explosions)
    for (DWORD site : { 0x41201Au, 0x415203u, 0x414A1Bu, 0x414AF8u, 0x414886u }) PatchCall(site, (void*)Wrap_Gib, 0x411870);
    // G4b
    PatchCall(0x41F4C1, (void*)Hook_BulletMark, 0x52F820);
    InstallGoreLimits();   // v0.46
    InstallGunCamera();    // v0.50
    if (*(BYTE*)0x52EA24 == 0x3D) Log("gore: character decal memory %u KB (retail 100 KB), %d decal records free", ArenaLimit() / 1024, FreeDecalRecords());
    else Log("gore: decal memory check not as expected at 0052EA24 - extra decals stay off");
    static const char* hn[3] = { "Off", "Random 50-50", "Always" }, *bn[4] = { "Normal", "Bigger", "Heavy", "Normal + splat" };
    Log("gore: Exploding heads %s, Wound variation %s, Blood on clothes %s, Bullet hit blood %s", hn[g_goreHeads], g_goreVary ? "on" : "off",
        g_goreClothes ? "on" : "off", bn[g_goreBullet]);
    Log("gore: head gib timing 'on the shot' is not available: pun.exe gibs only corpses (0x411870); the living victim has no gib code");
}

// ---------------------------------------------------------------- v0.10: button prompts
// (a) Hint text. Every "%s grab", "%s Quick Kill", "Press %s" ... prompt gets its key name from
//     0x46D3F0 (bindings, action) -> name. We replace that function: it looks up which pad button
//     sends the bound key (or mouse button) in the active layer and returns that button's icon
//     character from the game's own fonts (0x82 = Xbox A ... 0xB3 = PlayStation Cross ...).
//     No match, no controller or Style=Keyboard -> the normal key name, exactly like before.
// (b) Interrogation choice. The PC HUD (0x464990) prints the four key letters W/A/S/D in the middle.
//     With a controller we skip those four text calls and draw the console stick picture instead
//     (int_analog_center/up/down/left/right.tga, which the PC port still loads - as "wasd.tga").
enum { ST_KEYBOARD = 0, ST_XBOX, ST_PLAYSTATION, ST_DECK, ST_AUTO };
static const char* kStyleNames[] = { "Keyboard", "Xbox", "PlayStation", "SteamDeck", "Auto" };

static int PromptStyleNow()
{
    if (!g_pad) return ST_KEYBOARD;
    if (g_cfg.promptStyle != ST_AUTO) return g_cfg.promptStyle;
    if (g_padType == SDL_GAMEPAD_TYPE_PS3 || g_padType == SDL_GAMEPAD_TYPE_PS4 || g_padType == SDL_GAMEPAD_TYPE_PS5)
        return ST_PLAYSTATION;
    return ST_XBOX;
}

// icon character per pad input and style (0 = no icon: use kTextName)
//                                   A     B     X     Y     LB    RB    LT    RT    L3    R3   Back Start  DUp  DDown DLeft DRight LSup LSdn LSlt LSrt
static const BYTE kIconXbox[P_COUNT] = { 0x82, 0x85, 0x83, 0x84, 0x87, 0x86, 0x88, 0x8A, 0x89, 0x8B, 0,   0,   0x8D, 0x8E, 0x90, 0x8F, 0x89, 0x89, 0x89, 0x89 };
static const BYTE kIconPS[P_COUNT]   = { 0xB3, 0xB6, 0xB4, 0xB5, 0xB7, 0xBA, 0xB8, 0xBB, 0xB9, 0xBC, 0,   0,   0xAF, 0xB0, 0xB2, 0xB1, 0xB9, 0xB9, 0xB9, 0xB9 };
// v0.21 three-style font (misc.vpp from the v0.21 package): the Xbox (0x82-0x90) and PlayStation (0xAF-0xBC)
// icons are the game's originals again; the Steam Deck icons have their own codes (accented capitals that
// English text never uses): A C3, X C5, Y C6, B CC, L1 CD, R1 CE, L2 CF, R2 D0, L stick D2, R stick D3,
// L3 D4, R3 D5, D-pad up D8 / down D9 / right DA / left DB; Menu 0x80, View 0x81 (were blank).
static const BYTE kIconDeck[P_COUNT] = { 0xC3, 0xCC, 0xC5, 0xC6, 0xCD, 0xCE, 0xCF, 0xD0, 0xD4, 0xD5, 0x81, 0x80, 0xD8, 0xD9, 0xDB, 0xDA, 0xD2, 0xD2, 0xD2, 0xD2 };
// which button wins when several send the same key (crouch C is on LB and L3 -> LB)
static const int kPromptOrder[] = { P_SOUTH, P_WEST, P_NORTH, P_EAST, P_RB, P_LB, P_RT, P_LT, P_R3, P_L3,
                                    P_DUP, P_DDOWN, P_DLEFT, P_DRIGHT, P_START, P_BACK,
                                    P_LS_UP, P_LS_DOWN, P_LS_LEFT, P_LS_RIGHT };
static char g_iconStr[256][2];
static int  g_promptLogs = 0;

static const char* PromptFor(int key, int mouse, bool keyNamed, int* which)
{
    int style = PromptStyleNow();
    if (style == ST_KEYBOARD) return nullptr;
    const BYTE* icons = style == ST_PLAYSTATION ? kIconPS : style == ST_DECK ? kIconDeck : kIconXbox;
    for (int k = 0; k < P_COUNT; ++k) {
        int i = kPromptOrder[k];
        const Target* t = &g_cfg.map[i];
        if (InterrogationForcePhase() && g_cfg.interMap[i].kind != T_NONE) t = &g_cfg.interMap[i];
        else if (g_holdingHostage && g_cfg.hostageMap[i].kind != T_NONE) t = &g_cfg.hostageMap[i];
        bool match;
        if (keyNamed) match = t->kind == T_KEY && VkToScan(t->value) == key;
        else match = (t->kind == T_MOUSE_BUTTON && t->value == mouse) ||
                     (t->kind == T_WHEEL && ((t->value > 0 && mouse == 8) || (t->value < 0 && mouse == 9)));
        if (!match) continue;
        *which = i;
        if (icons[i]) return g_iconStr[icons[i]];
        if (i == P_BACK)  return style == ST_PLAYSTATION ? "Select" : "Back";
        if (i == P_START) return "Start";
        return nullptr;
    }
    return nullptr;
}

typedef const char* (__cdecl* NameFn_t)(int);
// replaces 0x46D3F0: cdecl (bindings, action) -> name.  Bindings: 16 bytes per action,
// +0x28 key (DirectInput scan code), +0x30 mouse button (0 L, 1 R, 2 M, 8 wheel up, 9 wheel down)
static const char* __cdecl Hook_ActionName(int binds, int action)
{
    int key = 0, mouse = 0;
    __try { key = *(int*)(binds + action * 16 + 0x28); mouse = *(int*)(binds + action * 16 + 0x30); }
    __except (1) { return "?"; }
    const char* name = ((NameFn_t)0x46CE30)(key);
    bool keyNamed = name && *name;
    if (!keyNamed) name = ((NameFn_t)0x46D2F0)(mouse);
    int which = -1;
    const char* icon = PromptFor(key, mouse, keyNamed, &which);
    // v0.13.1: many actions have a key AND a mouse button (fire = LCtrl + LClick, left-hand fire / throw
    // held = Z + RClick). The game names the key, but the pad sends the mouse button -> try that too.
    if (!icon && keyNamed && mouse >= 0 && mouse <= 9) {
        const char* mn = ((NameFn_t)0x46D2F0)(mouse);
        if (mn && *mn) icon = PromptFor(key, mouse, false, &which);
    }
    // v0.12.1: the help prompt stays on screen for a moment after the break, when [Interrogation] no longer
    // applies - keep showing the help button's icon instead of the key name (J)
    if (!icon && keyNamed && g_cfg.helpKeyScan > 0 && key == g_cfg.helpKeyScan && PromptStyleNow() != ST_KEYBOARD) {
        int style = PromptStyleNow();
        const BYTE* icons = style == ST_PLAYSTATION ? kIconPS : style == ST_DECK ? kIconDeck : kIconXbox;
        for (int k = 0; k < P_COUNT && !icon; ++k) {
            int i = kPromptOrder[k];
            if (g_cfg.interMap[i].kind == T_KEY && VkToScan(g_cfg.interMap[i].value) == key && icons[i]) {
                icon = g_iconStr[icons[i]]; which = i;
            }
        }
    }
    static DWORD seen[64]; static int nSeen = 0;
    DWORD sig = (DWORD)action << 16 | (DWORD)(which + 1) << 8 | (InterrogationForcePhase() ? 1 : 0) | (g_holdingHostage ? 2 : 0);
    bool fresh = true;
    for (int k = 0; k < nSeen; ++k) if (seen[k] == sig) { fresh = false; break; }
    if (fresh && nSeen < 64) seen[nSeen++] = sig;
    if (g_cfg.logPrompts && fresh && g_promptLogs < 64) {
        ++g_promptLogs;
        Log("prompt: action %d (%s) -> %s%s", action, name && *name ? name : "?",
            which >= 0 ? kPadNames[which] : "no pad button", icon ? "" : " (text)");
    }
    if (icon) return icon;
    return (name && *name) ? name : "?";
}

static void InstallPromptHook()
{
    for (int c = 0; c < 256; ++c) { g_iconStr[c][0] = (char)c; g_iconStr[c][1] = 0; }
    BYTE* p = (BYTE*)0x46D3F0;
    static const BYTE expect[6] = { 0x81, 0xEC, 0x84, 0x00, 0x00, 0x00 };
    if (memcmp(p, expect, 6)) { Log("prompt hook: 0x46D3F0 not as expected - skipped"); return; }
    DWORD old; VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE9; *(LONG*)(p + 1) = (LONG)((DWORD)Hook_ActionName - (0x46D3F0 + 5)); p[5] = 0x90;
    VirtualProtect(p, 6, old, &old);
    Log("prompt hook installed (style %s)", kStyleNames[g_cfg.promptStyle]);
}

// (b) the stick picture
static const char* kStickTex[5] = { "int_analog_center.tga", "int_analog_up.tga", "int_analog_down.tga",
                                    "int_analog_left.tga", "int_analog_right.tga" };
static const DWORD kStickHandles = 0x61FA7C;   // 5 texture handles the HUD init fills (center, up, down, left, right)
static bool g_stickTexOk = false;

static void InstallStickTextures()
{
    // the HUD init (0x465360) loads 5 textures, all "wasd.tga" on PC: give them their console names back
    static const DWORD push[5] = { 0x4653C5, 0x4653D1, 0x4653E2, 0x4653F3, 0x465404 };
    for (int i = 0; i < 5; ++i) {
        BYTE* p = (BYTE*)push[i];
        if (p[0] != 0x68 || *(DWORD*)(p + 1) != 0x5FA544) { Log("stick pictures: 0x%06X not as expected - skipped", push[i]); return; }
    }
    for (int i = 0; i < 5; ++i) {
        BYTE* p = (BYTE*)push[i]; DWORD old;
        VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
        *(const char**)(p + 1) = kStickTex[i];
        VirtualProtect(p, 5, old, &old);
    }
    g_stickTexOk = true;
    Log("stick pictures: HUD loads int_analog_*.tga again");
}

// ---------------------------------------------------------------- v0.42: HUD size (Display page)
// The game draws HUD pictures at their texture size and HUD text at font size, at pixel positions from hud.tbl
// (blocks #640x480..#1280x960, picked by 0x463CA0; the widescreen fix re-anchors them to the real screen). So the
// HUD gets small at high resolutions. While the whole HUD draws (0x465530, one call at 0x45DA12) every picture
// (0x510C80 / 0x510C20 / 0x510BE0) and text (0x5186B0 -> scaled text 0x518800) is enlarged by k around the
// nearest screen anchor (left / centre / right x top / middle / bottom, from the piece's centre). Pieces that touch
// an earlier piece of this frame use that piece's anchor, so a box, its icon and its text stay together.
// Positions the game computes from native text / picture sizes stay right, because the mapping is a plain
// enlargement around one point per group. Full-screen pieces (wider / taller than 60 % of the screen) stay as is.
// v0.43: 0 = Auto = the HUD keeps the same share of the screen as at 800 lines (Deck handheld = 100 %, 1080p TV
// = 135 %, 4K = 270 %), so handheld / docked need no setting change. Never smaller than the game size, max 300 %.
static const float kHudScales[] = { 0.0f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.25f, 2.5f, 3.0f };
static const char* kHudScaleNames[] = { "Auto", "Off (game size)", "125 %", "150 %", "175 %", "200 %", "225 %", "250 %", "300 %" };
static const int kHudScaleCount = 9;
static const float kHudAutoRefH = 800.0f;
static int   g_hudScaleIdx = 0;              // [Display] HudSize (v0.43; the v0.42 key HudScale is not read)
static float HudScaleNow()
{
    if (g_hudScaleIdx > 0 && g_hudScaleIdx < kHudScaleCount) return kHudScales[g_hudScaleIdx];
    int h = *(volatile int*)0x913254;
    float k = (h > 0) ? (float)h / kHudAutoRefH : 1.0f;
    if (k < 1.0f) k = 1.0f;
    if (k > 3.0f) k = 3.0f;
    return (int)(k * 20.0f + 0.5f) / 20.0f;   // steps of 5 %
}
static volatile BYTE g_hudScaling = 0;       // 1 while the HUD draws with k != 1
static float g_hudK = 1.0f;
struct HudGroup { float x0, y0, x1, y1, ax, ay; };
// v0.55.1: was 64 - the HUD draws ~80 pieces a frame (more with the early development HUD); pieces past the limit
// were scaled around their own anchor and drifted away from their box / icon / numbers
static const int kHudGroupMax = 1024;
static HudGroup g_hudGroups[kHudGroupMax]; static int g_hudGroupN = 0;
static DWORD g_hudPieces = 0, g_hudFrames = 0;

static bool HudAnchor(float x, float y, float w, float h, float* ax, float* ay)
{
    float W = (float)*(volatile int*)0x913250, H = (float)*(volatile int*)0x913254;
    if (W < 16 || H < 16 || w <= 0 || h <= 0) return false;
    if (w > W * 0.6f || h > H * 0.6f) return false;
    const float m = 3.0f;
    for (int i = 0; i < g_hudGroupN; ++i) {
        const HudGroup& g = g_hudGroups[i];
        if (x < g.x1 + m && x + w > g.x0 - m && y < g.y1 + m && y + h > g.y0 - m) {
            *ax = g.ax; *ay = g.ay;
            if (g_hudGroupN < kHudGroupMax) g_hudGroups[g_hudGroupN++] = { x, y, x + w, y + h, g.ax, g.ay };
            return true;
        }
    }
    float cx = x + w * 0.5f, cy = y + h * 0.5f;
    *ax = cx < W / 3 ? 0.0f : cx > W * 2 / 3 ? W : W * 0.5f;
    *ay = cy < H / 3 ? 0.0f : cy > H * 2 / 3 ? H : H * 0.5f;
    if (g_hudGroupN < kHudGroupMax) g_hudGroups[g_hudGroupN++] = { x, y, x + w, y + h, *ax, *ay };
    return true;
}
static int HudRound(float v) { return (int)floorf(v + 0.5f); }

typedef int (__cdecl* Pic80_t)(int, int, int, DWORD, DWORD);
typedef int (__cdecl* Pic20_t)(int, int, int, int, int, int, int, int, int, int, int, DWORD, DWORD, int);
typedef int (__cdecl* Quad_t)(int, float*, float*, float*, float*, DWORD, DWORD);
typedef int (__cdecl* Text_t)(int, int, const char*, int, DWORD, DWORD);
typedef int (__cdecl* TextK_t)(int, int, DWORD, DWORD, const char*, int, DWORD, DWORD);
static DWORD FBits(float f) { DWORD d; memcpy(&d, &f, 4); return d; }

// v0.65 diag: while the black & white screen is drawn (g_fxBwAt = last b&w draw), log every picture / rectangle that
// covers at least half the screen, with the game code that drew it (Deck line = something squeezed to 4:3 by the
// Widescreen Fix before the b&w copy). At most 40 lines per game run.
static DWORD g_fxBwAt = 0;
static int g_fxBigLogs = 0;
static void FxBigLog(const char* what, int tex, int x, int y, int w, int h, void* ret)
{
    if (!g_fxBwAt || GetTickCount() - g_fxBwAt > 300 || g_fxBigLogs >= 40) return;
    int W = *(volatile int*)0x913250, H = *(volatile int*)0x913254;
    if (w < W / 2 && h < H / 2) return;
    ++g_fxBigLogs;
    Log("fx big %s: tex %d at %d,%d %dx%d (screen %dx%d) from %08X", what, tex, x, y, w, h, W, H, (DWORD)(size_t)ret);
}
static int __cdecl HudPic20(int tex, int x, int y, int w, int h, int u0, int v0, int u1, int v1, int fx, int fy,
                            DWORD a, DWORD b, int z)
{
    FxBigLog("picture", tex, x, y, w, h, _ReturnAddress());
    float ax, ay;
    if (g_hudScaling && tex != -1 && HudAnchor((float)x, (float)y, (float)w, (float)h, &ax, &ay)) {
        ++g_hudPieces;
        float k = g_hudK;
        int nx = HudRound(ax + (x - ax) * k), ny = HudRound(ay + (y - ay) * k);
        int nx1 = HudRound(ax + (x + w - ax) * k), ny1 = HudRound(ay + (y + h - ay) * k);
        x = nx; y = ny; w = nx1 - nx; h = ny1 - ny;
        if (w < 1) w = 1; if (h < 1) h = 1;
    }
    return ((Pic20_t)0x510C20)(tex, x, y, w, h, u0, v0, u1, v1, fx, fy, a, b, z);
}
static int __cdecl HudPic80(int tex, int x, int y, DWORD a, DWORD b)
{
    if (g_fxBwAt && GetTickCount() - g_fxBwAt < 300) {
        int w0 = 0, h0 = 0;
        __try { ((void (__cdecl*)(int, int*, int*))0x50BEB0)(tex, &w0, &h0); } __except (1) { w0 = h0 = 0; }
        FxBigLog("picture80", tex, x, y, w0, h0, _ReturnAddress());
    }
    if (g_hudScaling && tex != -1) {
        int w = 0, h = 0;
        __try { ((void (__cdecl*)(int, int*, int*))0x50BEB0)(tex, &w, &h); } __except (1) { w = h = 0; }
        if (w > 0 && h > 0 && !(w > *(volatile int*)0x913250 * 0.6f || h > *(volatile int*)0x913254 * 0.6f))
            return HudPic20(tex, x, y, w, h, 0, 0, w, h, 0, 0, a, b, 0);
    }
    return ((Pic80_t)0x510C80)(tex, x, y, a, b);
}
static int __cdecl HudQuad(int tex, float* xs, float* ys, float* us, float* vs, DWORD a, DWORD b)
{
    if (g_hudScaling && tex != -1 && xs && ys) {
        float x0 = xs[0], x1 = xs[0], y0 = ys[0], y1 = ys[0];
        for (int i = 1; i < 4; ++i) { x0 = min(x0, xs[i]); x1 = max(x1, xs[i]); y0 = min(y0, ys[i]); y1 = max(y1, ys[i]); }
        float ax, ay;
        if (HudAnchor(x0, y0, x1 - x0, y1 - y0, &ax, &ay)) {
            ++g_hudPieces;
            float nx[4], ny[4];
            for (int i = 0; i < 4; ++i) { nx[i] = ax + (xs[i] - ax) * g_hudK; ny[i] = ay + (ys[i] - ay) * g_hudK; }
            return ((Quad_t)0x510BE0)(tex, nx, ny, us, vs, a, b);
        }
    }
    return ((Quad_t)0x510BE0)(tex, xs, ys, us, vs, a, b);
}
static int __cdecl HudText(int x, int y, const char* text, int font, DWORD sx, DWORD sy)
{
    if (g_hudScaling && text && *text) {
        int w = 0, h = 0;
        __try { ((void (__cdecl*)(int*, int*, const char*, int, int))0x5185E0)(&w, &h, text, (int)strlen(text), font); }
        __except (1) { w = h = 0; }
        float ax, ay;
        if (w > 0 && h > 0 && HudAnchor((float)x, (float)y, (float)w, (float)h, &ax, &ay)) {
            ++g_hudPieces;
            float k = g_hudK;
            return ((TextK_t)0x518800)(HudRound(ax + (x - ax) * k), HudRound(ay + (y - ay) * k), FBits(k), FBits(k),
                                       text, font, sx, sy);
        }
    }
    return ((Text_t)0x5186B0)(x, y, text, font, sx, sy);
}
// v0.82 (tester, 1080p+): captions / subtitles were cut off with HUD size above 100 %. The HUD clips some pieces
// with the viewport set by 0x510870(x, y, w, h) (speaker caption box 0x466AD0, typed-out subtitles 0x46C210 /
// 0x46C7B0, bar fills 0x46A4D0, map 0x466350 ...): the text was enlarged, the clip area was not. While the HUD
// scales, a clip area smaller than 60 % of the screen is enlarged with the same anchor as its pieces. Full-screen
// resets (wider / taller than 60 %) pass unchanged.
typedef void (__cdecl* Viewport_t)(int, int, int, int);
static DWORD g_hudClips = 0;
static void __cdecl HudViewport(int x, int y, int w, int h)
{
    float ax, ay;
    if (g_hudScaling && w > 0 && h > 0 && HudAnchor((float)x, (float)y, (float)w, (float)h, &ax, &ay)) {
        ++g_hudClips;
        float k = g_hudK;
        int nx = HudRound(ax + (x - ax) * k), ny = HudRound(ay + (y - ay) * k);
        int nx1 = HudRound(ax + (x + w - ax) * k), ny1 = HudRound(ay + (y + h - ay) * k);
        x = nx; y = ny; w = nx1 - nx; h = ny1 - ny;
        if (w < 1) w = 1; if (h < 1) h = 1;
    }
    ((Viewport_t)0x510870)(x, y, w, h);
}
static void AchFrame();
static int __cdecl Hook_HudRender(DWORD player)          // 0x45DA12 -> 0x465530 (the whole HUD)
{
    float k = HudScaleNow();
    g_hudK = k; g_hudGroupN = 0;
    g_hudScaling = (k != 1.0f && !g_enhOpen && !g_photo) ? 1 : 0;
    int r = ((int (__cdecl*)(DWORD))0x465530)(player);
    g_hudScaling = 0;
    if (!g_photo) { __try { AchFrame(); } __except (1) {} }   // v0.68: achievements in levels
    if (k != 1.0f && ++g_hudFrames == 300) Log("hud size: x%.2f, %lu pieces + %lu clip areas scaled in 300 frames, %d groups last frame",
                                              k, g_hudPieces, g_hudClips, g_hudGroupN);
    return r;
}

typedef void (__cdecl* DrawText_t)(int, int, const char*, int, DWORD, DWORD);
typedef void (__cdecl* DrawPic_t)(int, int, int, DWORD, DWORD);
static void DrawChoiceStick(int cx, int cy)
{
    float lx = Axis(SDL_GAMEPAD_AXIS_LEFTX), ly = Axis(SDL_GAMEPAD_AXIS_LEFTY);
    float dz = g_cfg.moveDeadzone;
    int idx = 0;                                       // 0 center 1 up 2 down 3 left 4 right
    if (fabsf(lx) > dz || fabsf(ly) > dz) {
        if (fabsf(lx) > fabsf(ly)) idx = lx > 0 ? 4 : 3;
        else                       idx = ly > 0 ? 2 : 1;
    }
    // v0.26: the stick picture follows the prompt style - mini.vpp (Extended Cut) carries three sets:
    //   int_analog_<dir>_x.tga Xbox (PC original), _p.tga PlayStation (PS2), _d.tga Steam Deck.
    // Not there (older mini.vpp) -> the picture the HUD loaded (int_analog_<dir>.tga), as before.
    static const char* kSets[3] = { "_x", "_p", "_d" };
    static int styled[3][5]; static bool looked[3] = { false, false, false };
    int style = PromptStyleNow(), set = style == ST_PLAYSTATION ? 1 : style == ST_DECK ? 2 : 0;
    if (!looked[set]) {
        looked[set] = true;
        int found = 0;
        for (int i = 0; i < 5; ++i) {
            char nm[40]; const char* base = kStickTex[i];
            size_t n = strlen(base) - 4;                                      // without ".tga"
            snprintf(nm, sizeof(nm), "%.*s%s.tga", (int)n, base, kSets[set]);
            int h = -1;
            __try { h = ((int (__cdecl*)(const char*, int))0x50C750)(nm, -1); } __except (1) { h = -1; }
            styled[set][i] = h; if (h >= 0) ++found;
        }
        Log("stick pictures (%s): %d of 5 found%s", kSets[set], found, found == 5 ? "" : " - using the HUD's own picture");
    }
    int tex = styled[set][idx] >= 0 ? styled[set][idx] : *(volatile int*)(kStickHandles + idx * 4);
    if (tex < 0) return;
    static int logged = 0;
    if (logged < 5) { ++logged; Log("stick picture: %s (texture %d) at %d,%d", kStickTex[idx], tex, cx, cy); }
    volatile DWORD* color = (volatile DWORD*)0x91327C;         // r,g,b,a set by 0x510B60
    DWORD saved = *color;
    *color = 0x00FFFFFF | (saved & 0xFF000000);                // full brightness, keep the HUD's fade
    HudPic80(tex, cx - 32, cy - 32, *(volatile DWORD*)0x6766D4, *(volatile DWORD*)0x6766D8);
    *color = saved;
}

// the four letter draws of the choice HUD: left 0x4651A0, up 0x465239, right 0x4652C0, down 0x46534D
static void __cdecl Hook_ChoiceLetter(int x, int y, const char* text, int font, DWORD sx, DWORD sy)
{
    if (!g_stickTexOk || PromptStyleNow() == ST_KEYBOARD) {   // v0.29: the stick picture follows the prompt style
        HudText(x, y, text, font, sx, sy);
        return;
    }
    // the "right" letter is drawn at (centre x + 5, centre y - font height / 2)
    if ((DWORD)_ReturnAddress() == 0x4652C5) {
        int h = ((int (__cdecl*)(int))0x5185B0)(-1);
        DrawChoiceStick(x - 5, y + h / 2);
    }
}

static void PatchCall(DWORD site, void* hook, DWORD expectedTarget);
static void InstallChoiceStick()
{
    InstallStickTextures();
    if (!g_stickTexOk) return;
    PatchCall(0x4651A0, (void*)Hook_ChoiceLetter, 0x5186B0);
    PatchCall(0x465239, (void*)Hook_ChoiceLetter, 0x5186B0);
    PatchCall(0x4652C0, (void*)Hook_ChoiceLetter, 0x5186B0);
    PatchCall(0x46534D, (void*)Hook_ChoiceLetter, 0x5186B0);
}

// ---------------------------------------------------------------- v0.13: tutorial texts for the pad
// Level / tutorial text tokens are expanded by 0x4670C0 (token string in EBX, returns the text), called
// only from 0x4678E0 at 0x46796A (LEA EBX,[ESP+0x10]; CALL 0x4670C0). With a controller we answer a few
// tokens ourselves; everything else goes to the game's function unchanged (EBX untouched).
//   STICK_CONFIG_MSG            "Use W, A, S, D to move around. Use the mouse to look around"
//                               -> "Use [L stick] to move around. Use [R stick] to look around"
//   INTERROGATION_INSTRUCTIONS  "Move the mouse"  -> "Move [L stick]"
//   LEFT_STICK / STICKS         console tokens    -> [L stick] / [L stick] [R stick]
static char g_tokenText[160];
static const char* __cdecl TokenOverride(const char* token)
{
    int style = PromptStyleNow();
    if (style == ST_KEYBOARD || !token) return nullptr;
    char l = (char)(style == ST_PLAYSTATION ? 0xB9 : style == ST_DECK ? 0xD2 : 0x89);     // left stick (PS: L3)
    char r = (char)(style == ST_PLAYSTATION ? 0xBC : style == ST_DECK ? 0xD3 : 0x8B);     // right stick (PS: R3)
    const char* out = nullptr;
    __try {
        if (!strncmp(token, "STICK_CONFIG_MSG", 16) && (token[16] == 0 || token[16] == '$')) {
            snprintf(g_tokenText, sizeof(g_tokenText), "Use %c to move around. Use %c to look around", l, r);
            out = g_tokenText;
        } else if (!strncmp(token, "INTERROGATION_INSTRUCTIONS", 26) && (token[26] == 0 || token[26] == '$')) {
            snprintf(g_tokenText, sizeof(g_tokenText), "Move %c", l);
            out = g_tokenText;
        } else if (!strncmp(token, "LEFT_STICK", 10) && (token[10] == 0 || token[10] == '$')) {
            snprintf(g_tokenText, sizeof(g_tokenText), "%c", l);
            out = g_tokenText;
        } else if (!strncmp(token, "STICKS", 6) && (token[6] == 0 || token[6] == '$')) {
            snprintf(g_tokenText, sizeof(g_tokenText), "%c %c", l, r);
            out = g_tokenText;
        }
    } __except (1) { out = nullptr; }
    if (out) {
        static int logged = 0;
        if (logged < 10) { ++logged; Log("tutorial text: %.40s -> pad version", token); }
    }
    return out;
}
static __declspec(naked) void Hook_Token()
{
    __asm {
        push ebx                // the token
        call TokenOverride
        add  esp, 4
        test eax, eax
        jz   game_token
        ret                     // our text (EAX), back to 0x46796F
    game_token:
        mov  eax, 0x4670C0
        jmp  eax                // the game's own function, same return address, EBX unchanged
    }
}

// ---------------------------------------------------------------- v0.13.1: quotes around icons
// Tutorial texts put some key names in quotes ("PRESS '$ALT_FIRE$' TO ..." -> "PRESS 'Z' ..."). With an
// icon that reads 'icon'. 0x4678E0 (src, dst, cdecl) expands those texts; we wrap its 4 calls and remove
// a pair of quotes around a single icon character.
static bool IsDeckIcon(BYTE c)
{
    static const BYTE k[] = { 0xC3, 0xC5, 0xC6, 0xCC, 0xCD, 0xCE, 0xCF, 0xD0, 0xD2, 0xD3, 0xD4, 0xD5, 0xD8, 0xD9, 0xDA, 0xDB };
    for (BYTE x : k) if (c == x) return true;
    return false;
}
static bool IsIconChar(BYTE c)
{
    if ((c >= 0x80 && c <= 0x90) || (c >= 0xAF && c <= 0xBC)) return true;
    return PromptStyleNow() == ST_DECK && IsDeckIcon(c);    // only then are these icons (else accented letters)
}
static bool IsQuoteChar(BYTE c) { return c == '\'' || c == '`' || c == '"' || c == 0x91 || c == 0x92 || c == 0x93 || c == 0x94; }
static void __cdecl Hook_ExpandText(const char* src, char* dst)
{
    ((void (__cdecl*)(const char*, char*))0x4678E0)(src, dst);
    if (PromptStyleNow() == ST_KEYBOARD || !dst) return;
    __try {
        // v0.21: the game's own D-pad tokens give the PlayStation D-pad (AF up, B0 down, B1 right, B2 left);
        // with the Xbox or Steam Deck style they become that style's D-pad
        int style = PromptStyleNow();
        if (style == ST_XBOX || style == ST_DECK) {
            static const BYTE xb[4] = { 0x8D, 0x8E, 0x8F, 0x90 }, dk[4] = { 0xD8, 0xD9, 0xDA, 0xDB };
            for (BYTE* q = (BYTE*)dst; *q; ++q)
                if (*q >= 0xAF && *q <= 0xB2) *q = (style == ST_DECK ? dk : xb)[*q - 0xAF];
        }
        BYTE* r = (BYTE*)dst; BYTE* w = (BYTE*)dst;
        while (*r) {
            if (IsQuoteChar(r[0]) && IsIconChar(r[1]) && r[2] && IsQuoteChar(r[2])) { *w++ = r[1]; r += 3; continue; }
            *w++ = *r++;
        }
        *w = 0;
    } __except (1) {}
}

// ---------------------------------------------------------------- v0.14: rumble
// The PC port removed the console force feedback (weapons.tbl still names an effect per weapon, the game
// reads it and throws it away; "Force Feedback Process" in the main loop is empty). We rebuild it:
//  - your shots: the weapon fire code plays the player's own fire sound (weapon def +0xBC) through
//    0x4613B0 only when the shooter is the player (enemies use 0x4FF320 instead). We wrap those calls,
//    find the weapon by its sound, and rumble with that weapon's console effect.
//  - explosions and other hits the game shakes the camera for: 0x404A90 (camera, amount, duration, freq).
struct RumbleFx { const char* name; float lo, hi; int ms; };
static const RumbleFx kFx[] = {      // weapons.tbl "$Force Feedback:" names (spelling as in the table)
    { "Pistol",            0.30f, 0.55f,  90 },
    { "Shotgun",           0.75f, 1.00f, 170 },
    { "Grendade Launcher", 0.60f, 0.90f, 220 },
    { "Auto Light",        0.20f, 0.35f,  55 },
    { "Auto Medium",       0.30f, 0.50f,  65 },
    { "Auto Strong",       0.45f, 0.70f,  75 },
    { "Melee",             0.35f, 0.60f, 100 },
};
struct WeaponFx { const char* weapon; int fx; };
static const WeaponFx kWeaponFx[] = {   // from the PS2 / PC weapons.tbl (identical)
    { "Colt 1911", 0 }, { "Remington 870", 1 }, { "Daewoo USAS12", 1 }, { "Grenade Launcher", 2 },
    { "LAW", 2 }, { "Ingram Mac 10", 3 }, { "OA-93", 3 }, { "Barret M-95", 1 }, { "Colt M16A4", 4 },
    { "AK103", 4 }, { "M-60", 5 }, { "HK MP5/40", 4 }, { "XM-29", 4 }, { "FlameThrower", 4 },
    { "Hunter", 0 }, { "357", 0 }, { "P90", 4 }, { "Desert Eagle", 0 }, { "melee weapon", 6 },
    { "BushwackerRifle", 4 }, { "BushwackerShotgun", 1 }, { "BushwackerRocket", 2 }, { "Turret", 5 },
    { "Jungle Turret", 5 }, { "Automated Turret", 5 }, { "Tank Main Gun", 5 }, { "Tank Machinegun", 5 },
};
static const DWORD kWeaponTable = 0x8017C8, kWeaponCount = 0x8017B8, kWeaponStride = 0x1D0;
static int g_rumbleLogs = 0;

static void Rumble(float lo, float hi, int ms, const char* why)
{
    if (!g_cfg.rumble || !g_pad || g_cfg.rumbleStrength <= 0) return;
    float k = g_cfg.rumbleStrength;
    auto u16 = [](float v) { if (v < 0) v = 0; if (v > 1) v = 1; return (Uint16)(v * 65535.0f); };
    SDL_RumbleGamepad(g_pad, u16(lo * k), u16(hi * k), (Uint32)ms);
    if (g_cfg.logRumble && g_rumbleLogs < 100) { ++g_rumbleLogs; Log("rumble: %s (%.2f / %.2f, %d ms)", why, lo * k, hi * k, ms); }
}

static int WeaponForFireSound(int sound)
{
    __try {
        int n = *(volatile int*)kWeaponCount;
        if (n < 0 || n > 64) return -1;
        for (int i = 0; i < n; ++i)
            if (*(volatile int*)(kWeaponTable + i * kWeaponStride + 0xBC) == sound) return i;
    } __except (1) {}
    return -1;
}

typedef int (__cdecl* PlaySound_t)(int, void*, int, int, DWORD, int);

// v0.14.1: quick kills, interrogations, punches and kicks have no rumble source left, but they all play
// sounds (foley names from foley.tbx, triggered by the animations in skeleton.tbl or by the kill code).
// Every sound goes through 0x4613B0 (sound id, pos, ...), so we watch all its calls and rumble for the
// impact sounds below. Ids are looked up by name with the game's own 0x461040 (name) -> id.
//   class: 0 light (choke, force)   1 medium (punch, kick, stab, knife)   2 heavy (finishers, deaths)
//          3 long (wood chipper / grinder / torch loops)   4 heartbeat (very light thump, console-style)
struct ImpactSound { const char* name; int cls; };
static const ImpactSound kImpact[] = {
    { "HEARTBEAT 1", 4 }, { "HEARTBEAT 2", 5 }, { "HEARTBEAT SLOW", 4 }, { "HEARTBEAT FAST", 4 },
    { "INT CHOKE 1", 0 }, { "INT CHOKE 2", 0 }, { "INT CHOKE 3", 0 }, { "INT CHOKE 4", 0 },
    { "INT CHOKE START", 0 }, { "INT CHOKE HIT 1", 1 }, { "INT CHOKE END", 1 }, { "INT CHOKE DIE", 2 },
    { "QK Force Down", 0 }, { "QK Force Up", 0 }, { "QK Push down", 0 }, { "QK PFake", 0 }, { "QK PTap", 0 },
    { "PUNCH HIGH", 1 }, { "PUNCH MID", 1 }, { "PUNCH LOW", 1 }, { "PUNCH END", 2 },
    { "MOV KICK NPC", 1 }, { "MOV KICK METAL", 1 }, { "DOOR KICK", 1 }, { "DOOR KICK METAL", 1 }, { "DOOR KICK WOOD", 1 },
    { "Body Drop", 1 }, { "BB SLAM", 2 },
    { "EK STAB START", 1 }, { "EK STAB 1", 1 }, { "EK STAB 2", 1 }, { "EK STAB HIT 1", 1 }, { "EK STAB HIT 2", 1 }, { "EK STAB END", 2 },
    { "EK_FACE_SMASH", 2 }, { "EK_CHOP_SLAM", 2 }, { "EK_CRUSH_DIE", 2 }, { "EK_SACRA_STAB", 2 }, { "EK_LICENSE_SLAM", 2 },
    { "WOOD CHIPPER START", 1 }, { "WOOD CHIPPER LOOP", 3 }, { "WOOD CHIPPER END", 2 },
    { "EK WOOD CHIPPER START", 1 }, { "EK WOOD CHIPPER DIE", 2 }, { "EK WOOD CHIPPER DIE 2", 2 }, { "EK WOOD CHIPPER END", 2 },
    { "EK GRIND START", 1 }, { "EK GRIND LOOP1", 3 }, { "EK GRIND IMPACT", 2 }, { "EK GRIND DIE", 2 }, { "EK GRIND END", 2 },
    { "EK TORCH START", 1 }, { "EK TORCH LOOP 2", 3 }, { "EK TORCH BURN", 1 }, { "EK TORCH DIE", 2 }, { "EK TORCH END", 1 },
    { "QK PSMSHOT", 2 }, { "QK Break Neck", 2 }, { "QK Break Neck 2", 2 }, { "QK HBack", 1 }, { "QK HChoke", 1 },
    { "QK HNutz", 1 }, { "QK HStab", 2 }, { "QK Jam Nose", 2 }, { "QK Knife 1", 1 }, { "QK Knife 2", 1 },
    { "QK Knife 3", 1 }, { "QK Knife 4", 1 }, { "QK Knife 5", 1 }, { "QK Knife 6", 1 }, { "QK Knife 7", 1 },
    { "QK PChest", 1 }, { "QK PHead", 2 }, { "QK PNutz", 1 }, { "QK PStom", 1 }, { "QK Knockout", 2 },
    { "QK Grenade", 2 }, { "QK Cleaver", 2 }, { "QK Bat Swing", 0 }, { "QK Bat Hit", 2 }, { "QK Flame Hit", 1 },
    { "QK Flame Mouth", 2 }, { "QK HBash", 2 }, { "QK HPstl", 2 }, { "QK HSwing", 1 }, { "QK LSwing", 1 },
    { "QK PSmash", 2 }, { "QK PLift", 1 }, { "QK SG Bash", 2 }, { "QK SG Jam", 2 }, { "QK TV MD", 2 },
    { "QK MACHINE TIP", 2 }, { "QK Crowbar", 2 }, { "QK Bottle", 2 }, { "QK TONFA EYE", 2 }, { "QK TONFA", 1 },
    { "QK KATANA", 2 }, { "QK Wrench", 2 }, { "QK Bear Trap", 2 }, { "QK_CASKET_GRENADE", 2 }, { "QK JEEP IMPALE", 2 },
    { "SL PUNCHHEAD", 1 }, { "SL FLIPKICK", 1 }, { "SL BACKSTAB", 2 }, { "SL HEADSTAB", 2 }, { "SL TOSSSTAB", 2 },
    { "SL UPPERCUT", 1 }, { "SL SHOULDER", 1 }, { "SL DIVETACKLE", 1 }, { "SL DIVEBASH", 2 },
    { "SL KNIFE R", 1 }, { "SL KNIFE L", 1 }, { "SL KNIFE B", 1 },
    { "RUSSIAN SMASH GROUND", 2 }, { "RUSSIAN PUNCH R HEAD", 1 }, { "RUSSIAN PUNCH R STOM", 1 }, { "RUSSIAN PUNCH R GRN", 1 },
    { "RUSSIAN PUNCH L GRN", 1 }, { "RUSSIAN PUNCH L HEAD", 1 }, { "RUSSIAN PUNCH L STOM", 1 },
};
static const RumbleFx kImpactFx[6] = {
    { "light",  0.25f, 0.35f,  90 },
    { "medium", 0.50f, 0.70f, 130 },
    { "heavy",  0.85f, 1.00f, 230 },
    { "long",   0.40f, 0.50f, 700 },
    { "heartbeat", 0.30f, 0.00f, 80 },   // "lub" - low motor only
    { "heartbeat", 0.18f, 0.00f, 60 },   // "dub"
};
static const int kMaxImpactIds = 160;
static int  g_impactId[kMaxImpactIds], g_impactIdx[kMaxImpactIds], g_impactCount = 0;
static int  g_impactTries = 0;
static void ResolveImpactSounds()
{
    if (g_impactCount > 0 || g_impactTries > 20) return;
    ++g_impactTries;
    typedef int (__cdecl* FoleyId_t)(const char*);
    for (int i = 0; i < (int)(sizeof(kImpact) / sizeof(kImpact[0])) && g_impactCount < kMaxImpactIds; ++i) {
        int id = -1;
        __try { id = ((FoleyId_t)0x461040)(kImpact[i].name); } __except (1) { id = -1; }
        if (id < 0) continue;
        g_impactId[g_impactCount] = id; g_impactIdx[g_impactCount] = i; ++g_impactCount;
    }
    Log("rumble: %d of %d impact sounds found", g_impactCount, (int)(sizeof(kImpact) / sizeof(kImpact[0])));
}

// v0.68: semi trailer KICKS bleed (Deck tests Nov 15). The interrogation's damage is the time-based neck press, so its
// +EffectDamage never fires on a kick (kicks do no damage there). The kick animation (pnsh_semi_hit) plays the foley
// "EK_SEMI_HIT" at the impact -> while the running group ([0x7E2DBC]) is "Harbor Semi Trailer" (its +EffectDeath id at
// group +0x20 = env_kill_semi), the silent blood_hit_0 burst is attached to the victim's head (NeckBurst; victim =
// handle at Punisher +0x878 / +0x87C -> 0x4A8D20). [Gore] SemiKickBlood=0 = off.
static int g_semiKickBlood = 1;
static void NeckBurst(DWORD corpse);
static void SemiKickBlood(int sound)
{
    static int kick = -2, semi = -2;
    __try {
        if (kick == -2) kick = ((int (__cdecl*)(const char*))0x461040)("EK_SEMI_HIT");
        if (sound != kick || kick < 0) return;
        DWORD grp = *(volatile DWORD*)0x7E2DBC;
        if (!grp) return;
        if (semi == -2) semi = ((int (__cdecl*)(const char*))0x40E440)("env_kill_semi");
        if (semi < 0 || *(volatile int*)(grp + 0x20) != semi) return;
        DWORD p = *(volatile DWORD*)0x6B70FC;
        if (!p) return;
        for (int k = 0; k < 2; ++k) {
            DWORD h = *(volatile DWORD*)(p + 0x878 + 4 * k);
            DWORD o = h ? ((DWORD (__cdecl*)(DWORD))0x4A8D20)(h) : 0;
            if (o && o != p) {
                static int logged = 0;
                if (logged++ < 6) Log("semi trailer: kick blood on victim %08X", o);
                NeckBurst(o);
                return;
            }
        }
    } __except (1) {}
}
static int __cdecl Hook_PlaySound(int sound, void* pos, int a3, int a4, DWORD a5, int a6)
{
    int r = ((PlaySound_t)0x4613B0)(sound, pos, a3, a4, a5, a6);
    if (g_semiKickBlood && sound >= 0) SemiKickBlood(sound);   // v0.68
    if (!g_cfg.rumble || !g_pad || sound < 0) return r;
    // your shots: the player's own fire sound (weapon def +0xBC), played as a 2D sound (3rd argument 1)
    // only for the player's shots; enemy shots go through 0x4FF320, which plays its sounds in 3D (0) -
    // and some weapons use the same sound for both, which is why enemy fire rumbled in v0.14.1
    if (g_cfg.rumbleWeapons && a3 == 1) {
        int w = WeaponForFireSound(sound);
        if (w >= 0) {
            const char* name = (const char*)(kWeaponTable + w * kWeaponStride);   // $Name, 32 chars
            int fx = 4;                                                          // unknown weapon: Auto Medium
            __try {
                for (const WeaponFx& e : kWeaponFx) if (!_stricmp(name, e.weapon)) { fx = e.fx; break; }
            } __except (1) {}
            char why[64]; snprintf(why, sizeof(why), "shot, %.24s = %s", name, kFx[fx].name);
            Rumble(kFx[fx].lo, kFx[fx].hi, kFx[fx].ms, why);
            return r;
        }
    }
    ImpactRumble(sound);
    return r;
}

static void ImpactRumble(int sound)
{
    if (g_cfg.rumbleMelee && *(volatile int*)kWeaponCount > 0 && EnvKillSound(sound)) return;   // v0.19
    if (g_cfg.rumbleMelee) {
        if (*(volatile int*)kWeaponCount > 0) ResolveImpactSounds();      // tables loaded
        for (int k = 0; k < g_impactCount; ++k) {
            if (g_impactId[k] != sound) continue;
            const ImpactSound& e = kImpact[g_impactIdx[k]];
            // a quick kill picked by the chooser: light pulse now, heavy one at its DeathFrame.
            // Quick kills with props from the level (bat, crowbar, bottle, wrench ...) don't use the
            // chooser - they play a sound per swing / hit, so those rumble on their own sound.
            // Prop / environment quick kills (env_kills.tbl: bat, bottle, crowbar, TV, jeep ...) play one
            // sound at the start too; their kill moment comes from the death effect (OnEffect below).
            // Only a separate hit sound (QK Bat Hit) keeps its own strength.
            bool qk = !_strnicmp(e.name, "QK", 2);
            if (qk) ScheduleQuickKillRumble(e.name);
            if (qk && strstr(e.name, "Hit")) qk = false;
            const RumbleFx& f = kImpactFx[qk ? 0 : e.cls];
            char why[64]; snprintf(why, sizeof(why), "%s (%s)", e.name, f.name);
            Rumble(f.lo, f.hi, f.ms, why);
            break;
        }
    }
}

// v0.17: 2D sounds go through 0x461300 (sound, 0, -1, 1.0 - cdecl, 4 arguments) - the interrogation
// heartbeat ("Heartbeat 1" at 0x4D9C6B, "Heartbeat 2" at 0x4D9CFB, from 0x4D9490) is played this way.
typedef int (__cdecl* PlaySound2D_t)(int, int, int, DWORD);
static int __cdecl Hook_PlaySound2D(int sound, int a2, int a3, DWORD a4)
{
    int r = ((PlaySound2D_t)0x461300)(sound, a2, a3, a4);
    if (g_cfg.rumble && g_pad && sound >= 0) ImpactRumble(sound);
    return r;
}

// v0.16: the kill moment of a quick kill. Each quick kill has one sound, started at frame 2 of the
// animation, so the sound rumble came at the very start. quick_kills.tbl gives each kill a DeathFrame
// (table 0x7D11B0, 0x84 bytes per kill, DeathFrame at +0x74). The kill chooser 0x4C83F0 returns the
// chosen kill's entry; we catch that return value at its two calls (0x4CAA8F, 0x4CDF87) with a
// pass-through wrapper (works whatever its calling convention), and when the quick kill sound starts we
// schedule a heavy rumble (DeathFrame - 2) frames later.
static volatile DWORD g_qkEntry = 0, g_qkEntryAt = 0, g_qkRet = 0, g_qkCalls = 0;
static DWORD g_qkRumbleAt = 0, g_qkLastCalls = 0;
static void NoteChooserCalls()
{
    if (g_qkCalls != g_qkLastCalls) { g_qkLastCalls = g_qkCalls; g_qkEntryAt = GetTickCount(); }   // chooser ran
}
static __declspec(naked) void Hook_QuickKillChooser()
{
    __asm {
        pop  dword ptr [g_qkRet]       // caller's return address (the chooser is not recursive)
        mov  eax, 0x4C83F0
        call eax                       // arguments are where the chooser expects them
        mov  dword ptr [g_qkEntry], eax
        inc  dword ptr [g_qkCalls]
        push dword ptr [g_qkRet]
        ret
    }
}
static bool ScheduleQuickKillRumble(const char* sound)
{
    NoteChooserCalls();
    DWORD e = g_qkEntry;
    if (e < 0x7D11B0 || e >= 0x7D11B0 + 0x84 * 128 || (e - 0x7D11B0) % 0x84) return false;
    if (GetTickCount() - g_qkEntryAt > 3000) return false;                 // chosen too long ago
    int death = 0;
    __try { death = *(volatile int*)(e + 0x74); } __except (1) { return false; }
    if (death <= 2 || death > 400) return false;
    int ms = (int)((death - 2) * 1000.0f / g_cfg.qkFrameRate);
    g_qkRumbleAt = GetTickCount() + (DWORD)ms;
    if (!g_qkRumbleAt) g_qkRumbleAt = 1;
    g_qkEntryAt = GetTickCount() - 100000;     // used: later sounds of the same kill don't reschedule
    if (g_cfg.logRumble) Log("rumble: quick kill %s, kill moment at frame %d -> in %d ms", sound, death, ms);
    return true;
}
static void UpdateQuickKillRumble()
{
    NoteChooserCalls();
    if (g_qkRumbleAt && (LONG)(GetTickCount() - g_qkRumbleAt) >= 0) {
        g_qkRumbleAt = 0;
        Rumble(0.9f, 1.0f, 260, "quick kill - kill moment");
    }
}

// ---------------------------------------------------------------- v0.19: prop / environment quick kills
// env_kills.tbl "+Quickkill" groups: the Punisher's animation plays one sound at frame `sfx` (the start);
// the victim's animation has persona triggers at fixed frames - Pain_Blunt / Pain_Panic / Thrown follow
// a hit, Death_* is the kill (ragdoll / gib-head where there is no Death). Both animations start
// together, so when the start sound plays we schedule the hits (frame - sfx) / QuickKillFPS later.
// v0.18 rumbled on the death effect - but the game spawns it at the START of the kill, so now it is
// only used to tell apart kills that share a sound ("QK Bear Trap": bear trap, wall zap, missile).
struct EnvKill { const char* group; const char* sound; const char* effect; int sfx; int n; int ev[6][2]; };
static const EnvKill kEnvKills[] = {   // frame, class (1 medium hit, 2 heavy kill) - from env_kills.tbl
    { "Jungle Jeep Impale", "QK JEEP IMPALE", "env_kill_jp", 5, 2, { { 32, 1 }, { 72, 2 } } },
    { "Jungle Missile Exhaust", "QK Bear Trap", "", 1, 0, { { 0, 0 } } },
    { "Zoo Tiki Impale", "EK TIKI IMPALE", "env_kill_tiki", 2, 2, { { 40, 1 }, { 80, 2 } } },
    { "Zoo Sacrificial Kill", "EK_SACRA_STAB", "env_kill_sacr", 10, 4, { { 20, 1 }, { 68, 1 }, { 88, 1 }, { 112, 2 } } },
    { "Zoo Snake Kill", "EK_SNAKE_KILL", "env_kill_snake", 9, 3, { { 38, 1 }, { 108, 1 }, { 148, 1 } } },
    { "Mansion Bear Trap", "QK Bear Trap", "env_kill_bear_trp", 2, 1, { { 59, 2 } } },
    { "Mansion Elephant Tusk", "EK ELEPHANT IMPALE", "env_kill_tusk", 2, 2, { { 70, 1 }, { 93, 2 } } },
    { "Ma Through Window", "EK_THROUGH_WIN", "env_kill_ma_gcci", 1, 4, { { 36, 1 }, { 86, 1 }, { 172, 1 }, { 277, 2 } } },
    { "Mortuary Casket Grenade", "QK_CASKET_GRENADE", "env_kill_grn_cskt", 2, 3, { { 22, 1 }, { 46, 1 }, { 130, 1 } } },
    { "Fisk Weight Bench", "EK_MBENCH", "", 2, 3, { { 46, 1 }, { 71, 1 }, { 118, 2 } } },
    { "NYCrack Through Window", "THROUGH_WINDOW", "env_kill_truwin", 2, 2, { { 40, 2 }, { 54, 1 } } },
    { "Prison Transformer", "EK_TRANS_KILL", "env_kill_trans", 1, 2, { { 70, 1 }, { 93, 2 } } },
    { "Stark Wall Zap", "QK Bear Trap", "env_kill_wzap", 2, 1, { { 59, 2 } } },
    { "Bottle quick kill", "QK Bottle", "qk_bottle", 7, 2, { { 39, 2 }, { 53, 1 } } },
    { "Bat quick kill", "QK Bat Swing", "qk_bat", 7, 0, { { 0, 0 } } },          // "QK Bat Hit" sound does it
    { "Cleaver quick kill", "QK Cleaver", "qk_cleaver", 7, 2, { { 25, 2 }, { 110, 1 } } },
    { "Wrench quick kill", "QK Wrench", "qk_wrench", 7, 3, { { 28, 1 }, { 66, 2 }, { 83, 1 } } },
    { "Crowbar quick kill", "QK Crowbar", "qk_crowbar", 7, 1, { { 25, 2 } } },
    { "Tonfa quick kill", "QK TONFA", "qk_tonfa", 7, 4, { { 15, 1 }, { 45, 1 }, { 81, 1 }, { 123, 2 } } },
    { "Katana quick kill", "QK KATANA", "qk_katana", 7, 2, { { 14, 1 }, { 99, 2 } } },
    { "TV_MD quick kill", "QK TV MD", "qk_tv", 7, 1, { { 43, 2 } } },
};
static const int kEnvKillCount = (int)(sizeof(kEnvKills) / sizeof(kEnvKills[0]));
static int   g_envSoundId[kEnvKillCount], g_envResolved = 0;
static const char* g_lastDeathFx = nullptr;
static DWORD g_lastDeathFxAt = 0;
struct Pulse { DWORD at; int cls; const char* group; };
static Pulse g_pulse[16];

static void ResolveEnvKills()
{
    if (g_envResolved) return;
    typedef int (__cdecl* FoleyId_t)(const char*);
    int n = 0;
    for (int i = 0; i < kEnvKillCount; ++i) {
        int id = -1;
        __try { id = ((FoleyId_t)0x461040)(kEnvKills[i].sound); } __except (1) { id = -1; }
        g_envSoundId[i] = id;
        if (id >= 0) ++n;
    }
    g_envResolved = 1;
    Log("rumble: %d of %d prop / environment quick kills found", n, kEnvKillCount);
}
// true if `sound` starts a prop / environment quick kill (pulses scheduled)
static bool EnvKillSound(int sound)
{
    ResolveEnvKills();
    int pick = -1, candidates = 0;
    bool fresh = g_lastDeathFx && GetTickCount() - g_lastDeathFxAt < 1500;
    for (int i = 0; i < kEnvKillCount; ++i) {
        if (g_envSoundId[i] != sound) continue;
        ++candidates;
        if (pick < 0) pick = i;
        if (fresh && kEnvKills[i].effect[0] && !strcmp(kEnvKills[i].effect, g_lastDeathFx)) { pick = i; break; }
        if (!fresh && !kEnvKills[i].effect[0]) pick = i;               // shared sound, no effect: the one without
    }
    if (pick < 0) return false;
    const EnvKill& k = kEnvKills[pick];
    DWORD now = GetTickCount();
    int q = 0;
    for (int e = 0; e < k.n; ++e) {
        while (q < 16 && g_pulse[q].at) ++q;
        if (q >= 16) break;
        int ms = (int)((k.ev[e][0] - k.sfx) * 1000.0f / g_cfg.qkFrameRate);
        g_pulse[q].at = now + (DWORD)(ms > 0 ? ms : 1);
        g_pulse[q].cls = k.ev[e][1];
        g_pulse[q].group = k.group;
    }
    g_lastDeathFx = nullptr;
    if (g_cfg.logRumble) Log("rumble: %s - %d hit(s) scheduled", k.group, k.n);
    Rumble(kImpactFx[0].lo, kImpactFx[0].hi, kImpactFx[0].ms, k.group);   // light: the grab
    return true;
}
static void UpdateEnvKillPulses()
{
    DWORD now = GetTickCount();
    for (int q = 0; q < 16; ++q) {
        if (!g_pulse[q].at || (LONG)(now - g_pulse[q].at) < 0) continue;
        const RumbleFx& f = kImpactFx[g_pulse[q].cls];
        char why[64]; snprintf(why, sizeof(why), "%s - %s", g_pulse[q].group, f.name);
        Rumble(f.lo, f.hi, g_pulse[q].cls == 2 ? 280 : f.ms, why);
        g_pulse[q].at = 0;
    }
}

// v0.18: death effects of environment kills and prop quick kills. env_kills.tbl gives every kill an
// +EffectDeath (qk_bottle, qk_crowbar, qk_tv, env_kill_jp, env_kill_chipper ...) that the game spawns at
// the kill. Effects are spawned through 0x40E5C0 (effect id first - the muzzle flash uses it too); ids
// come from the game's own name lookup 0x40E440. We watch every call of 0x40E5C0 and rumble heavy for
// these effects.
static const char* kDeathFx[] = {
    "qk_bat", "qk_bottle", "qk_cleaver", "qk_crowbar", "qk_katana", "qk_tonfa", "qk_tv", "qk_wrench",
    "env_kill_bear_trp", "env_kill_grn_cskt", "env_kill_jp", "env_kill_ma_gcci", "env_kill_sacr",
    "env_kill_snake", "env_kill_tiki", "env_kill_trans", "env_kill_truwin", "env_kill_tusk", "env_kill_wzap",
    "env_kill_blwtrch", "env_kill_celldoor", "env_kill_chipper", "env_kill_chrme",
    "env_kill_cskt_slm", "env_kill_d_case", "env_kill_dangle02", "env_kill_drgbt_saw", "env_kill_eel",
    "env_kill_elec_c", "env_kill_ewire02", "env_kill_fireplace", "env_kill_forklift", "env_kill_fryer_dth",
    "env_kill_gate_dth", "env_kill_grndr", "env_kill_heater", "env_kill_jeep", "env_kill_jfan",
    "env_kill_kniferack", "env_kill_m5_spear", "env_kill_mgrind", "env_kill_mtl_chp", "env_kill_nailg",
    "env_kill_prnha", "env_kill_rhino", "env_kill_sausg", "env_kill_semi", "env_kill_shark", "env_kill_stamp",
    "env_kill_steam", "env_kill_turret", "env_kill_window", "env_kill_zoo_curb", "env_zoo_curb_hit",
};
static const int kDeathFxCount = (int)(sizeof(kDeathFx) / sizeof(kDeathFx[0]));
static int g_deathFxId[64], g_deathFxCount = 0, g_deathFxTries = 0, g_effectLogs = 0;

// call a one-argument game function whatever its calling convention (the stack is restored after)
static int CallGame1(DWORD fn, const char* arg)
{
    int r = -1;
    __asm {
        push esi
        mov  esi, esp
        push arg
        call fn
        mov  esp, esi
        pop  esi
        mov  r, eax
    }
    return r;
}
static void ResolveDeathEffects()
{
    if (g_deathFxCount > 0 || g_deathFxTries > 20) return;
    ++g_deathFxTries;
    int n = 0;
    for (int i = 0; i < kDeathFxCount; ++i) {
        int id = -1;
        __try { id = CallGame1(0x40E440, kDeathFx[i]); } __except (1) { id = -1; }
        g_deathFxId[i] = id;
        if (id >= 0) ++n;
    }
    if (n) g_deathFxCount = kDeathFxCount;
    Log("rumble: %d of %d death effects found", n, kDeathFxCount);
}
static void __cdecl OnEffect(int id)
{
    if (!g_cfg.rumble || !g_cfg.rumbleMelee || !g_pad || id < 0) return;
    if (*(volatile int*)kWeaponCount <= 0) return;                    // tables not loaded yet
    ResolveDeathEffects();
    for (int i = 0; i < g_deathFxCount; ++i) {
        if (g_deathFxId[i] != id) continue;
        bool qkFx = false;
        for (int k = 0; k < kEnvKillCount; ++k) if (!strcmp(kEnvKills[k].effect, kDeathFx[i])) { qkFx = true; break; }
        if (qkFx) {                       // spawned at the start of a quick kill: just remember it (v0.19)
            g_lastDeathFx = kDeathFx[i]; g_lastDeathFxAt = GetTickCount();
            if (g_cfg.logRumble && g_effectLogs < 20) { ++g_effectLogs; Log("rumble: effect %s (quick kill start)", kDeathFx[i]); }
            return;
        }
        char why[64]; snprintf(why, sizeof(why), "kill effect %s", kDeathFx[i]);
        Rumble(0.90f, 1.00f, 280, why);
        return;
    }
}
// v0.73 diag: during an interrogation, log every effect the game spawns (id, position, caller) - casket hit blood test
static int g_envFxLogs = 0;
static void __cdecl EnvFxSpawnLog(const DWORD* f)        // f[0] return address, f[1] id, f[2] room, f[3] pos*
{
    __try {
#ifdef EC_RELEASE
        return;                                            // test log - not in the release
#endif
        if (!*(volatile DWORD*)0x7E2DBC || g_envFxLogs >= 80) return;
        ++g_envFxLogs;
        const float* p = (const float*)f[3];
        DWORD pl = *(volatile DWORD*)0x6B70FC;
        float hp = 0; DWORD v = 0;
        if (pl) { DWORD h = *(volatile DWORD*)(pl + 0x878); v = h; }
        Log("interrogation effect: id %d at %.2f %.2f %.2f from %08X (point %d)", (int)f[1], p ? p[0] : 0.f, p ? p[1] : 0.f, p ? p[2] : 0.f, f[0], (int)f[7]);
        (void)hp; (void)v;
    } __except (1) {}
}
static __declspec(naked) void Hook_SpawnEffect()
{
    __asm {
        pushad
        lea  eax, [esp + 32]
        push eax
        call EnvFxSpawnLog
        add  esp, 4
        mov  eax, [esp + 32 + 4]     // effect id
        push eax
        call OnEffect
        add  esp, 4
        popad
        mov  eax, 0x40E5C0
        jmp  eax
    }
}

// v0.15/v0.16: hits taken. The HUD health bar (0x46A4D0) eases its bar towards the Punisher's health,
// a float at entity +0x288 (v0.15 used +0xD8 - that is the "flash the bar until" time, wrong); the
// player entity is 0x6B70FC (what handle -999 resolves to in 0x4A9460). Health going down between two
// frames = a hit; the bigger the drop, the stronger the rumble. Regeneration is ignored.
static DWORD g_hpEntity = 0;
static float g_hp = 0;
static int   g_hitLogs = 0;
static void UpdateHitRumble()
{
    if (!g_cfg.rumble || !g_cfg.rumbleHits || !g_pad) return;
    __try {
        DWORD e = *(volatile DWORD*)0x6B70FC;
        if (!e) { g_hpEntity = 0; return; }
        float h = *(volatile float*)(e + 0x288);
        if (e != g_hpEntity || !(h == h)) { g_hpEntity = e; g_hp = h; return; }   // new level / player
        float d = g_hp - h;
        g_hp = h;
        if (d >= g_cfg.hitMinimum && d < 100000.0f) {     // (most hits also take a ~0.2 nibble first)
            float k = d / 20.0f;
            if (k < 0.25f) k = 0.25f;
            if (k > 1.0f)  k = 1.0f;
            char why[64]; snprintf(why, sizeof(why), "hit: health %.1f -> %.1f", h + d, h);
            if (g_hitLogs < 30) ++g_hitLogs; else why[0] = 0;
            Rumble(k, k * 0.6f, 110 + (int)(k * 140), why[0] ? why : "hit");
        }
    } __except (1) {}
}

static void __cdecl OnCameraShake(float amount, float seconds)
{
    if (!g_cfg.rumbleShake) return;
    if (!(amount > 0) || !(seconds > 0)) return;
    float s = amount / 0.15f;                          // the LAW's own kick is 0.05 -> about a third
    if (s > 1) s = 1;
    if (s < 0.15f) s = 0.15f;
    int ms = (int)(seconds * 1000.0f);
    if (ms < 80) ms = 80;
    if (ms > 900) ms = 900;
    char why[48]; snprintf(why, sizeof(why), "camera shake %.3f for %.2f s", amount, seconds);
    Rumble(s, s * 0.8f, ms, why);
}
static __declspec(naked) void Hook_CameraShake()
{
    __asm {
        pushad
        mov  eax, [esp + 32 + 8]     // amount
        mov  edx, [esp + 32 + 12]    // duration
        push edx
        push eax
        call OnCameraShake
        add  esp, 8
        popad
        mov  eax, 0x404A90
        jmp  eax
    }
}

// CALL rel32 sites in [lo, hi) that go to target
static int PatchCallsTo(DWORD lo, DWORD hi, DWORD target, void* hook, const char* what)
{
    int n = 0;
    for (DWORD a = lo; a + 5 <= hi; ++a) {
        BYTE* p = (BYTE*)a;
        if (p[0] != 0xE8) continue;
        if (a + 5 + *(LONG*)(p + 1) != target) continue;
        DWORD old; VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
        *(LONG*)(p + 1) = (LONG)((DWORD)hook - (a + 5));
        VirtualProtect(p, 5, old, &old);
        ++n;
    }
    Log("hooks: %d %s call(s) hooked", n, what);
    return n;
}

static void InstallRumble()
{
    // every sound (your shots, quick kills, interrogations ... - see Hook_PlaySound)
    PatchCallsTo(0x401000, 0x5EB000, 0x4613B0, (void*)Hook_PlaySound, "sound");
    PatchCallsTo(0x401000, 0x5EB000, 0x404A90, (void*)Hook_CameraShake, "camera shake");
    PatchCallsTo(0x401000, 0x5EB000, 0x461300, (void*)Hook_PlaySound2D, "2D sound");   // v0.17: heartbeat
    PatchCallsTo(0x401000, 0x5EB000, 0x40E5C0, (void*)Hook_SpawnEffect, "effect");     // v0.18: kill effects
    PatchCall(0x4CAA8F, (void*)Hook_QuickKillChooser, 0x4C83F0);   // v0.16: which quick kill was chosen
    PatchCall(0x4CDF87, (void*)Hook_QuickKillChooser, 0x4C83F0);
}

// ---------------------------------------------------------------- v0.12: pop-up boxes and menu footers
// Pop-up boxes (cheat warning, mission complete, OK / CANCEL, "Enter: Continue") are run by 0x477470;
// it calls the box's input handler 0x4768E0 (at 0x477545) only while a box is open and accepting input.
// That handler takes Enter (accept), Esc (cancel) and left/right, but in a level the box did not count as
// a menu, so A sent E instead of Enter. We wrap that call: a box open = menu buttons ([Menu] A=Enter, B=Esc).
static void __cdecl Hook_PopupInput()
{
    static bool logged = false;
    if (!logged) { logged = true; Log("pop-up box seen (A/B work as Enter/Esc while it is open)"); }
    g_lastPopup = GetTickCount();
    ((void (__cdecl*)())0x4768E0)();
}

// Menu footers ("Enter: Select   Esc: Back", built by 0x479170 from key codes) and the "Enter: Continue"
// box use the literal strings "Enter:" (0x5FB770) and "Esc:" (0x5FBB64). We point those code references
// at two buffers of our own and fill them with the pad's icon (the button that sends Enter / Esc in
// [Menu], else in [Buttons]) while a controller is connected, or the original text without one.
static char g_lblEnter[8] = "Enter:", g_lblEsc[8] = "Esc:";
static int PadButtonForVk(int vk)
{
    for (int k = 0; k < P_COUNT; ++k) { int i = kPromptOrder[k];
        if (g_cfg.menuMap[i].kind == T_KEY && g_cfg.menuMap[i].value == vk) return i; }
    for (int k = 0; k < P_COUNT; ++k) { int i = kPromptOrder[k];
        if (g_cfg.menuMap[i].kind == T_NONE && g_cfg.map[i].kind == T_KEY && g_cfg.map[i].value == vk) return i; }
    return -1;
}
static void UpdateFooterLabels()
{
    int style = PromptStyleNow();
    const BYTE* icons = style == ST_PLAYSTATION ? kIconPS : style == ST_DECK ? kIconDeck : kIconXbox;
    int a = PadButtonForVk(VK_RETURN), b = PadButtonForVk(VK_ESCAPE);
    if (style != ST_KEYBOARD && a >= 0 && icons[a]) { g_lblEnter[0] = (char)icons[a]; g_lblEnter[1] = 0; }
    else strcpy(g_lblEnter, "Enter:");
    if (style != ST_KEYBOARD && b >= 0 && icons[b]) { g_lblEsc[0] = (char)icons[b]; g_lblEsc[1] = 0; }
    else strcpy(g_lblEsc, "Esc:");
    // the localized copy of "Enter:" (string table entry 0xC0) used by the "Continue" box
    __try { if (*(volatile DWORD*)0x7EDDC4) *(char* volatile*)0x7EDDC4 = g_lblEnter; } __except (1) {}
}
// replace the 32-bit constant `from` inside the instruction at `site` (searched in its first 12 bytes)
static bool PatchImm(DWORD site, DWORD from, DWORD to)
{
    BYTE* p = (BYTE*)site;
    for (int o = 1; o <= 8; ++o) {
        if (*(DWORD*)(p + o) != from) continue;
        DWORD old; VirtualProtect(p + o, 4, PAGE_EXECUTE_READWRITE, &old);
        *(DWORD*)(p + o) = to;
        VirtualProtect(p + o, 4, old, &old);
        return true;
    }
    Log("footer: no reference at %08X - skipped", site);
    return false;
}
// v0.34: 2004 score display (GameSpy press shots, Nov 2004: plain white "TOTAL:3014 / COMBO:1500 / BODY COUNT:1" in
// the top right, left-aligned, no skull). Retail still has it, stripped: the HUD score function 0x46B300 (cdecl,
// player; called once at 0x46BABB) draws the blood splat + skull (0x510C20 at 0x46B437 / 0x46B489), the score with
// string-table slot 0x7EDB40 ("%.7d", text call 0x46B549), and while a combo runs the combo under it (slots 0x7EDB48
// "%d" / 0x7EDB44 "%dX%d"; calls 0x46B743 animated, 0x46B806, 0x46B832, 0x46B936) and the "+N" points pop-up
// (0x46B9FF). Player fields: +0x1724 combo points, +0x171C multiplier (= kills / 5 + 1), +0x1720 kills in this
// combo = the BODY COUNT (AddScore 0x4BA600 counts, 0x4B8100 banks the combo into the total and resets all three).
// Strings #33 "Combo:%d" / #34 "Kills:%d" survive unused.
// v0.34.1 (user: "doesn't match the beta"): ON = the game still runs the function (timers) but every draw inside it
// is skipped; the plugin draws the three lines itself, left-aligned, block right edge on the score's right edge.
// v0.34.2: full white. v0.34.3: interface font [0x676054] - side by side with every font texture, the 2004 text is
// the hand-drawn interface font (score font = narrow stencil, HUD font = plain).
typedef int(__cdecl* HudText_t)(int, int, const char*, int, DWORD, DWORD);
static bool  g_scoreHudOn = false;               // inside 0x46B300 with the 2004 display on -> its draws are skipped
static int   g_scoreX = 0, g_scoreY = 0, g_scoreW = 0, g_scoreH = 0;
static DWORD g_scoreSx = 0, g_scoreSy = 0;
static char  g_scoreText[32] = "";
static bool  g_scoreSeen = false;
static int __cdecl Hook_ScoreText(int x, int y, const char* text, int font, DWORD sx, DWORD sy)
{
    if (g_scoreHudOn && text) {
        g_scoreX = x; g_scoreY = y; g_scoreSx = sx; g_scoreSy = sy; g_scoreW = g_scoreH = 0;
        __try {
            ((void(__cdecl*)(int*, int*, const char*, int, int))0x5185E0)(&g_scoreW, &g_scoreH, text, (int)strlen(text), font);
            const char* d = text; while (*d && (*d < '0' || *d > '9')) ++d;   // digits of the total
            snprintf(g_scoreText, sizeof(g_scoreText), "%d", atoi(d));
            g_scoreSeen = true;
        } __except (1) {}
        return 0;
    }
    return ((HudText_t)0x46F7C0)(x, y, text, font, sx, sy);
}
static int __cdecl Hook_ScoreHudText(int x, int y, const char* text, int font, DWORD sx, DWORD sy)
{
    if (g_scoreHudOn) return 0;
    return ((HudText_t)0x46F7C0)(x, y, text, font, sx, sy);
}
static int __cdecl Hook_ScoreHudText2(DWORD a, DWORD b, DWORD c, DWORD d, DWORD e, DWORD f)   // 0x51E8E0 (scaled)
{
    if (g_scoreHudOn) return 0;
    return ((int(__cdecl*)(DWORD, DWORD, DWORD, DWORD, DWORD, DWORD))0x51E8E0)(a, b, c, d, e, f);
}
static int __cdecl Hook_ScoreSprite(DWORD a1, DWORD a2, DWORD a3, DWORD a4, DWORD a5, DWORD a6, DWORD a7,
                                    DWORD a8, DWORD a9, DWORD a10, DWORD a11, DWORD a12, DWORD a13, DWORD a14)
{
    if (g_scoreHudOn) return 0;
    return HudPic20((int)a1, (int)a2, (int)a3, (int)a4, (int)a5, (int)a6, (int)a7, (int)a8, (int)a9, (int)a10, (int)a11,
                    a12, a13, (int)a14);   // v0.42: HUD size
}
static void __cdecl Hook_ScoreHud(int player)
{
    g_scoreHudOn = g_betaComboHud != 0;
    g_scoreSeen = false;
    ((void(__cdecl*)(int))0x46B300)(player);
    bool on = g_scoreHudOn; g_scoreHudOn = false;
    if (!on || !g_scoreSeen || !player) return;
    static int fails = 0;
    __try {
        int combo = *(volatile int*)(player + 0x1724), mult = *(volatile int*)(player + 0x171C);
        int kills = *(volatile int*)(player + 0x1720);
        int font = *(volatile int*)0x676054;   // v0.34.3: interface font = the hand-drawn font of the 2004 shots
        char l[3][48];
        snprintf(l[0], 48, "TOTAL:%s", g_scoreText);
        if (mult >= 2) snprintf(l[1], 48, "COMBO:%d x%d", combo < 0 ? 0 : combo, mult);
        else snprintf(l[1], 48, "COMBO:%d", combo < 0 ? 0 : combo);
        snprintf(l[2], 48, "BODY COUNT:%d", kills < 0 ? 0 : kills);
        typedef void(__cdecl* Measure_t)(int*, int*, const char*, int, int);
        auto W = [&](const char* t) { int w = 0, hh = 0; ((Measure_t)0x5185E0)(&w, &hh, t, (int)strlen(t), font); return w; };
        int h = 0, wMax = 0;
        ((Measure_t)0x5185E0)(&wMax, &h, "BODY COUNT:00 ", 14, font);
        // v0.34.7, measured on the press shots (glyph columns): line pitch = 2.0 x cap height; cap height ~ 0.6 x the
        // font height here (v0.34.5 pitch h * 7/6 matched 1.9 x; v0.34.6 doubled it - wrong)
        int lineH = h > 0 ? h * 6 / 5 : 18;
        int y = g_scoreY - lineH / 2;
        int xs[3];
        int xCombo = g_scoreX + g_scoreW - W("COMBO:0000");
        if (g_betaComboHud == 1) {
            // 2004 misaligned, by letters: the T of TOTAL centred over the first O of COMBO; the C of COUNT starts
            // half an O-width left of that O (shots: O 69-79 / 70-79, T 72-78, C 64-72 / 66-71)
            int oLeft = xCombo + W("C"), wO = W("O"), wT = W("T");
            xs[1] = xCombo;
            xs[0] = oLeft + (wO - wT) / 2;
            xs[2] = oLeft - wO / 2 - W("BODY ");
        } else {
            xs[0] = xs[1] = xs[2] = g_scoreX + g_scoreW - wMax;
        }
        BYTE a = 255;   // v0.34.2: full white like 2004 (the HUD fade byte made it grey)
        if (g_betaComboSplash) {
            // v0.34.6: the faint grey smoke the retail HUD has behind the weapon / ammo counter = meters_weaponM.tga
            // (loaded at 0x46BB52, stored in [0x61FBE0]; S / L in [0x61FBDC] / [0x61FBD8]). v0.34.5 used the black
            // ink bar display_background.tga ([0x61FBD0], message boxes). Same sprite call / space as the score splat
            // (0x510C20, scale [0x6766D4] / [0x6766D8]).
            int tex = *(volatile int*)0x61FBE0;
            if (tex > 0) {
                int left = xs[0], right = 0;
                for (int i = 0; i < 3; ++i) {
                    int lw = 0, lh = 0;
                    ((Measure_t)0x5185E0)(&lw, &lh, l[i], (int)strlen(l[i]), font);
                    if (xs[i] < left) left = xs[i];
                    if (xs[i] + lw > right) right = xs[i] + lw;
                }
                int padX = lineH, padY = lineH / 2;
                int tw = 0, th = 0;
                ((void(__cdecl*)(int, int*, int*))0x50BEB0)(tex, &tw, &th);
                ((void(__cdecl*)(int, int, int, int))0x510B60)(255, 255, 255, 255);
                HudPic20(
                    tex, left - padX, y - padY, (right - left) + 2 * padX, 3 * lineH + 2 * padY, 0, 0, tw, th, 0, 0,
                    *(volatile DWORD*)0x6766D4, *(volatile DWORD*)0x6766D8, 0);
            }
        }
        for (int i = 0; i < 3; ++i) {
            ((void(__cdecl*)(int, int, int, int))0x510B60)(255, 255, 255, a);
            ((HudText_t)0x46F7C0)(xs[i], y + i * lineH, l[i], font, g_scoreSx, g_scoreSy);
        }
    } __except (1) { if (fails++ < 3) Log("combo hud: draw failed (exception)"); }
}
// v0.35: weapon add-ons. 0x5053F0 (weapon given to an entity) checks the owner's upgrades and calls the attach
// function 0x4FDD50 (cdecl weapon, add-on id: 0 Scope, 1 Silencer, 2 Grenade Launcher, 3 Extended Magazine; returns
// al = attached) at 0x5054BD / D7 / F1 / 0x50550B; 0x4D11DF / 0x4D11FF attach silencer / magazine in a special case.
// A weapon instance: +8 weapon type, +0x34..+0x3C three add-on slots (-1 = empty). Weapon table 0x8017C8, 0x1D0 per
// weapon (name first), +0x6C = HUD icon texture. Only the player owns upgrades, so an M16 that gets the launcher here
// is the player's.
typedef int(__cdecl* Attach_t)(DWORD, int);
static bool g_m16HasGL = false;
static int  g_m16Index = -2;
static int M16Index()
{
    if (g_m16Index != -2) return g_m16Index;
    g_m16Index = -1;
    __try {
        for (int i = 0; i < 27; ++i)
            if (!_stricmp((const char*)(0x8017C8 + i * 0x1D0), "Colt M16A4")) { g_m16Index = i; break; }
    } __except (1) {}
    Log("weapons: Colt M16A4 = weapon %d", g_m16Index);
    return g_m16Index;
}
static int __cdecl Hook_Attach(DWORD weapon, int mod)
{
    bool allow = mod < 0 || mod > 3 || g_modAllow[mod];
    int type = -1;
    __try { if (weapon) type = *(volatile int*)(weapon + 8); } __except (1) {}
    if (mod == 2 && type >= 0 && type == M16Index()) g_m16HasGL = allow;
    if (!allow) {
        static int n = 0;
        if (n++ < 20) Log("weapons: add-on %d not attached to weapon type %d (switched off)", mod, type);
        return 0;
    }
    return ((Attach_t)0x4FDD50)(weapon, mod);
}
typedef int(__cdecl* Sprite_t)(int, int, int, DWORD, DWORD);
static bool H04Icon(int tex, int x, int y);   // v0.53 2004 HUD
static int __cdecl Hook_HudWeaponIcon(int tex, int x, int y, DWORD sx, DWORD sy)
{
    if (H04Icon(tex, x, y)) return 0;      // v0.53: 2004 HUD weapon icon
    // v0.36: draws ws_m16_gl.tga (the HUD-style M16 + M203 rebuilt from the 2004 beta HUD; added to mini.vpp
    // always_loaded.ceg by the HUD icons build) instead of ws_m16 when the player's M16 carries the launcher.
    // Same size as ws_m16 (95x33) -> same position / scale. Missing texture (retail mini.vpp) -> normal icon.
    if (g_hudM16GL && g_m16HasGL) {
        static int glTex = -2;
        __try {
            int m = M16Index();
            if (m >= 0 && tex == *(volatile int*)(0x801834 + m * 0x1D0)) {
                if (glTex == -2) {
                    glTex = ((int (__cdecl*)(const char*, int))0x50C750)("ws_m16_gl.tga", -1);
                    Log("weapons: ws_m16_gl.tga texture %d (needs the HUD icons mini.vpp)", glTex);
                }
                if (glTex >= 0) return HudPic80(glTex, x, y, sx, sy);
            }
        } __except (1) {}
    }
    return HudPic80(tex, x, y, sx, sy);
}
typedef int(__cdecl* PickupMsg_t)(DWORD, DWORD, DWORD, DWORD, int, DWORD, DWORD);
static int __cdecl Hook_PickupCard(DWORD text, DWORD a1, DWORD a2, DWORD a3, int tex, DWORD a5, DWORD a6)
{
    // v0.37.1: the ammo / weapon pickup card ("+30 ROUNDS" + weapon picture) = 0x4682D0(text, .., .., -1, icon, 0, -1),
    // icon = weapon table +0x6C (ws_m16 for the M16). Same swap as the HUD icon: M16 with the launcher -> ws_m16_gl.
    if (g_hudM16GL && g_m16HasGL) {
        __try {
            int m = M16Index();
            if (m >= 0 && tex == *(volatile int*)(0x801834 + m * 0x1D0)) {
                int gl = ((int (__cdecl*)(const char*, int))0x50C750)("ws_m16_gl.tga", -1);
                if (gl >= 0) tex = gl;
            }
        } __except (1) {}
    }
    return ((PickupMsg_t)0x4682D0)(text, a1, a2, a3, tex, a5, a6);
}
// v0.38: enemy lifebar (HS_LifeBar.tga, handle [0x61FBB4], loaded at 0x469794). Drawn by 0x510BE0 (call 0x46A276)
// and 0x5131B0 (world billboard, call 0x46A4C0), texture = first argument. With [Beta] Lifebar2004 on, the texture
// becomes hs_lifebar_2004.tga (same 8x64 size, added to mini.vpp by the HUD icons build).
static int __cdecl LifebarTex(int tex)
{
    if (!g_betaLifebar) return tex;
    static int t2004 = -2;
    __try {
        if (tex != *(volatile int*)0x61FBB4) return tex;
        if (t2004 == -2) {
            t2004 = ((int (__cdecl*)(const char*, int))0x50C750)("hs_lifebar_2004.tga", -1);
            Log("beta: hs_lifebar_2004.tga texture %d (needs the HUD icons mini.vpp)", t2004);
        }
        if (t2004 >= 0) return t2004;
    } __except (1) {}
    return tex;
}
static DWORD g_lifeTgtA = (DWORD)HudQuad, g_lifeTgtB = 0x5131B0;   // v0.42: 2D bar through the HUD size
static __declspec(naked) void Hook_LifebarA()
{
    __asm {
        push ecx
        push edx
        push dword ptr [esp + 12]
        call LifebarTex
        add esp, 4
        pop edx
        pop ecx
        mov dword ptr [esp + 4], eax
        jmp dword ptr [g_lifeTgtA]
    }
}
// v0.38.4: free enemy's bar = world quad 0x5131B0(tex, &pos, &axes, halfWidth 0.05, halfLength, c1, c2, &colour)
// (8 args): corners = pos +- axes[0..2]*halfWidth +- axes[3..5]*halfLength (caller esi = the enemy; frame HS_Bkgrnd
// at call 0x46A41D with halfLength 0.3, bar at 0x46A4C0 with halfLength 0.3 * health). Retail: long side = row 1 =
// across the screen, over the head. 2004 style ([Beta] Lifebar2004):
//  - frame: rows 0 / 1 swapped (long side upright), anchored beside the body (H + row1dir*LifebarSide, - LifebarDrop);
//  - bar (v0.38.5): solid retail-red fill from the bottom up to the health level + the notch overlay
//    hs_lifeseg_2004.tga over the full length (never resized) -> notches stay put, the colour drains from the top.
// The held hostage (state 0x13) keeps the game's placement and the striped texture.
static const int kLbSegs = 14;
static float g_lbSide = -0.45f, g_lbDrop = 0.5f;
static float s_lbPos[3], s_lbAxes[12], s_lbHalf = 0.3f;
static int s_lbEnt = 0;
typedef void(__cdecl* WorldQuad_t)(int, const float*, const float*, float, float, DWORD, DWORD, DWORD);
static int __cdecl LifebarWorld(DWORD* a, int ent, int slot)
{
    a[0] = (DWORD)LifebarTex((int)a[0]);
    if (!g_betaLifebar || !ent) return 0;
    __try {
        if (*(volatile int*)(ent + 0x268) == 0x13) return 0;
        const float* P = (const float*)a[1];
        const float* X = (const float*)a[2];
        if (slot == 0) {
            float* nx = s_lbAxes;
            for (int i = 0; i < 12; ++i) nx[i] = X[i];
            const float* A = X;            // row 0: short side
            const float* B = X + 3;        // row 1: long side (across the screen)
            float bb = B[0] * B[0] + B[1] * B[1] + B[2] * B[2];
            if (bb < 1e-8f) return 0;
            float bl = sqrtf(bb);
            float H[3] = { *(volatile float*)(ent + 0x1C), *(volatile float*)(ent + 0x20) + *(volatile float*)(ent + 0x6FC),
                           *(volatile float*)(ent + 0x24) };
            for (int i = 0; i < 3; ++i) s_lbPos[i] = H[i] + B[i] / bl * g_lbSide;
            s_lbPos[1] -= g_lbDrop;
            for (int i = 0; i < 3; ++i) { nx[i] = B[i]; nx[3 + i] = A[i]; }
            s_lbHalf = *(float*)&a[4];
            s_lbEnt = ent;
            a[1] = (DWORD)s_lbPos;
            a[2] = (DWORD)nx;
            return 0;
        }
        // slot 1 = the health part: fixed segments
        if (s_lbEnt != ent || s_lbHalf <= 0.0f) return 0;
        static int seg = -2;
        if (seg == -2) {
            seg = ((int (__cdecl*)(const char*, int))0x50C750)("hs_lifeseg_2004.tga", -1);
            Log("beta: hs_lifeseg_2004.tga texture %d (needs the HUD icons mini.vpp)", seg);
        }
        if (seg < 0) return 0;
        float frac = *(float*)&a[4] / 0.3f;
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;
        const float* U = s_lbAxes + 3;     // long side, now upright
        float up = U[1] >= 0.0f ? 1.0f : -1.0f;
        float hw = *(float*)&a[3];
        // v0.38.5: 1) solid fill (retail HS_LifeBar red) from the bottom up to the health level - one plain colour,
        // so shrinking it does not squash anything; 2) the notch overlay (hs_lifeseg_2004) over the full length,
        // never resized, so the notches stay put while the colour drains.
        if (frac > 0.0f) {
            float fh = s_lbHalf * frac;
            float t = -s_lbHalf + fh;
            float fp[3];
            for (int i = 0; i < 3; ++i) fp[i] = s_lbPos[i] + U[i] * up * t;
            static int fill = -2;          // v0.38.6: salmon 2004 fill, retail red if missing
            if (fill == -2) {
                fill = ((int (__cdecl*)(const char*, int))0x50C750)("hs_lifefill_2004.tga", -1);
                Log("beta: hs_lifefill_2004.tga texture %d (needs the HUD icons mini.vpp)", fill);
            }
            ((WorldQuad_t)0x5131B0)(fill >= 0 ? fill : *(volatile int*)0x61FBB4, fp, s_lbAxes, hw, fh, a[5], a[6], a[7]);
        }
        ((WorldQuad_t)0x5131B0)(seg, s_lbPos, s_lbAxes, hw, s_lbHalf, a[5], a[6], a[7]);
        return 1;                          // the game's own (squashing) bar is not drawn
    } __except (1) {}
    return 0;
}
static __declspec(naked) void Hook_LifebarFrame()
{
    __asm {
        push ecx
        push edx
        push 0
        push esi
        lea eax, [esp + 20]
        push eax
        call LifebarWorld
        add esp, 12
        pop edx
        pop ecx
        jmp dword ptr [g_lifeTgtB]
    }
}
static __declspec(naked) void Hook_LifebarB()
{
    __asm {
        push ecx
        push edx
        push 1
        push esi
        lea eax, [esp + 20]
        push eax
        call LifebarWorld
        add esp, 12
        pop edx
        pop ecx
        test eax, eax
        jnz skip
        jmp dword ptr [g_lifeTgtB]
    skip:
        ret
    }
}
static int __cdecl Hook_HostageIcon(int tex, int x, int y, DWORD sx, DWORD sy)
{
    // v0.37: the hostage (human shield) HUD icon - its only draw is this sprite call (texture handle [0x61FBFC] =
    // ws_human_shield.tga, loaded at 0x46BC12). With [Beta] TargetHostageIcon on it draws ws_target_cutout.tga
    // (the 2004 target cutout, added to mini.vpp always_loaded.ceg by the HUD icons build, same 63x52 size).
    if (g_betaTargetIcon) {
        static int tTex = -2;
        __try {
            if (tex == *(volatile int*)0x61FBFC) {
                if (tTex == -2) {
                    tTex = ((int (__cdecl*)(const char*, int))0x50C750)("ws_target_cutout.tga", -1);
                    Log("beta: ws_target_cutout.tga texture %d (needs the HUD icons mini.vpp)", tTex);
                }
                if (tTex >= 0) return HudPic80(tTex, x, y, sx, sy);
            }
        } __except (1) {}
    }
    return HudPic80(tex, x, y, sx, sy);
}
// v0.38.2: crash guard for the physics compound-shape walk at 0x5792C8..0x57932E (known crash 0x5792F3 / 0x5792F7:
// the child returned by vtable +0x34 is NULL or freed memory - long-lived gib pieces outlive what they touch; the zoo
// hits it). At 0x5792EB the child is checked: readable, vtable inside pun.exe .rdata, vtable +0x14 inside .text.
// A bad child is skipped (jump to 0x579321 = next child) instead of crashing.
static int g_ccSkips = 0;
static int __cdecl ValidShapeChild(DWORD p)
{
    __try {
        if (p < 0x10000) return 0;
        DWORD vt = *(volatile DWORD*)p;
        if (vt < 0x5F5000 || vt >= 0x61E000) return 0;
        DWORD fn = *(volatile DWORD*)(vt + 0x14);
        if (fn < 0x401000 || fn >= 0x5F5000) return 0;
        return 1;
    } __except (1) { return 0; }
}
static void __cdecl LogShapeSkip(DWORD p)
{
    if (g_ccSkips++ < 20) Log("crash guard: bad physics shape child %08X skipped (0x5792F3 crash avoided, %d so far)", p, g_ccSkips);
}
static DWORD g_ccOk = 0x5792F3, g_ccNext = 0x579321;
static __declspec(naked) void Hook_CompoundChild()
{
    __asm {
        mov dword ptr [esp + 0x10], eax
        mov dword ptr [esp + 0x14], edi
        push eax
        push ecx
        push edx
        push eax
        call ValidShapeChild
        add esp, 4
        test eax, eax
        pop edx
        pop ecx
        pop eax
        jz bad
        jmp dword ptr [g_ccOk]
    bad:
        push eax
        push ecx
        push edx
        push eax
        call LogShapeSkip
        add esp, 4
        pop edx
        pop ecx
        pop eax
        jmp dword ptr [g_ccNext]
    }
}
static void InstallShapeGuard()
{
    static const BYTE expect[8] = { 0x89, 0x44, 0x24, 0x10, 0x89, 0x7C, 0x24, 0x14 };
    BYTE* p = (BYTE*)0x5792EB;
    if (memcmp(p, expect, 8) != 0) { Log("crash guard: bytes at 005792EB not as expected - skipped"); return; }
    DWORD old; VirtualProtect(p, 8, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE9; *(LONG*)(p + 1) = (LONG)((DWORD)Hook_CompoundChild - (0x5792EB + 5));
    p[5] = 0x90; p[6] = 0x90; p[7] = 0x90;
    VirtualProtect(p, 8, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 8);
    Log("crash guard: physics shape walk at 005792EB guarded");
}
// ---------------------------------------------------------------- v0.53: 2004 HUD ([Beta] Hud2004)
// The HUD of Josh Nizzi's 2004 PS2 dev grabs: cracked skull, slate-blue health bar + grey bar in metal frames,
// sketchy corner brackets round the weapon, the 2004 M16 (+ M203) / grey-grunge weapon icons, 7-segment clip number
// + small RESERVE number. Positions measured from the grabs (640x480) and placed RELATIVE to the retail HUD pieces, so
// the hud.tbl resolution blocks, HUD size, the second gun box and the weapon-switch slide all still apply.
// Retail pieces are skipped at their draw calls; the 2004 textures (hud2004_*, ws_*_2004) come from the 2004 HUD
// mini.vpp (tools\5 Cut hunt\build_hud2004.py). Missing textures -> the retail HUD draws as before.
// Values: health = entity +0x288 / +0x4A0, slaughter = record +0x168C / [0x666E98], reserve = 0x4FDF40(entity, type).
static int  g_hud2004 = 0;        // [Beta] Hud2004: early dev HUD health + slaughter cluster
static int  g_hud2004Wep = 0;     // v0.57: [Beta] Hud2004Weapons: early dev HUD weapon box (brackets, icons, ammo)
static BYTE g_skipOnce = 0;                    // one game text call to hide (read by Hook_TextWatch)
static int  g_h04Skull = -2, g_h04Frame = -2, g_h04Blue = -2, g_h04Grey = -2, g_h04Brk = -2, g_h04M16 = -2, g_h04M16GL = -2, g_h04Red = -1;
static int  g_h04Orig[19], g_h04New[19];
static int  g_h04BoxX = -99999, g_h04BoxY = -99999;   // the weapon box being drawn (set by the frame hook)
static int  g_h04Font = -2;
static const char* kH04Weapons[19] = { "357", "ak103", "colt", "deserteagle", "flamethrower", "glauncher", "hunter",
    "law", "m16", "m60", "mac10", "mp5", "oa93", "p90", "python", "remington870", "sniper", "usas12", "xm29" };
static int H04Find(const char* n)
{
    int t = -1;
    __try { t = ((int (__cdecl*)(const char*))0x50BE30)(n); } __except (1) { t = -1; }
    return t;
}
static bool H04Ready()
{
    if ((!g_hud2004 && !g_hud2004Wep) || g_enhOpen || g_photo) return false;
    if (g_h04Skull == -2) {
        g_h04Skull = H04Find("hud2004_skull.tga"); g_h04Frame = H04Find("hud2004_frame.tga");
        g_h04Blue = H04Find("hud2004_blue.tga"); g_h04Grey = H04Find("hud2004_grey.tga");
        g_h04Brk = H04Find("hud2004_brackets.tga"); g_h04Red = H04Find("hud2004_red.tga"); g_h04M16 = H04Find("ws_m16_2004.tga"); g_h04M16GL = H04Find("ws_m16_gl_2004.tga");
        char n[48];
        for (int i = 0; i < 19; ++i) {
            snprintf(n, sizeof(n), "ws_%s.tga", kH04Weapons[i]); g_h04Orig[i] = H04Find(n);
            snprintf(n, sizeof(n), "w4_%s.tga", kH04Weapons[i]); g_h04New[i] = H04Find(n);   // v0.54.1: names <= 23 chars (longer = start-up crash)
        }
        Log("2004 hud: textures skull %d frame %d blue %d grey %d brackets %d m16 %d / %d%s", g_h04Skull, g_h04Frame,
            g_h04Blue, g_h04Grey, g_h04Brk, g_h04M16, g_h04M16GL,
            (g_h04Skull < 0 || g_h04Frame < 0) ? " - NOT INSTALLED (needs the 2004 HUD mini.vpp), retail HUD stays" : "");
    }
    return g_h04Skull >= 0 && g_h04Frame >= 0 && g_h04Blue >= 0 && g_h04Grey >= 0 && g_h04Brk >= 0;
}
static bool H04Health()  { return g_hud2004 && H04Ready(); }      // v0.57: health / slaughter half on
static bool H04Weapons() { return g_hud2004Wep && H04Ready(); }   // v0.57: weapon box half on
static void H04Colour(int r, int g, int b)
{
    int a = *(volatile BYTE*)0x7367AB;
    ((void (__cdecl*)(int, int, int, int))0x510B60)(r, g, b, a);
}
// draw texture tex (2x supersampled) at x, y with the given on-screen size, optionally only the left part (frac)
static void H04Draw(int tex, int x, int y, int w, int h, float frac = 1.0f)
{
    if (tex < 0 || w <= 0 || h <= 0) return;
    int tw = 0, th = 0;
    __try { ((void (__cdecl*)(int, int*, int*))0x50BEB0)(tex, &tw, &th); } __except (1) { return; }
    if (tw <= 0 || th <= 0) return;
    if (frac < 1.0f) {
        if (frac <= 0.0f) return;
        int fw = (int)(w * frac + 0.5f); if (fw < 1) return;
        HudPic20(tex, x, y, fw, h, 0, 0, (int)(tw * frac + 0.5f), th, 0, 0, *(volatile DWORD*)0x6766D4, *(volatile DWORD*)0x6766D8, 0);
        return;
    }
    HudPic20(tex, x, y, w, h, 0, 0, tw, th, 0, 0, *(volatile DWORD*)0x6766D4, *(volatile DWORD*)0x6766D8, 0);
}
// health cluster, drawn in place of meters_bg (bx, by = the retail cluster background position)
static int __cdecl Hook_H04MetersBg(int tex, int x, int y, DWORD sx, DWORD sy)
{
    if (!H04Health()) return HudPic80(tex, x, y, sx, sy);
    __try {
        float hp = 1.0f, sl = 0.0f;
        DWORD ent = *(volatile DWORD*)0x6B70FC, rec = *(volatile DWORD*)0x7CF804;
        if (ent) { float h = *(volatile float*)(ent + 0x288), m = *(volatile float*)(ent + 0x4A0); hp = m > 0 ? h / m : 0; }
        if (rec) { float m = *(volatile float*)0x666E98; sl = m > 0 ? *(volatile float*)(rec + 0x168C) / m : 0; }
        if (hp < 0) hp = 0; if (hp > 1) hp = 1; if (sl < 0) sl = 0; if (sl > 1) sl = 1;
        // v0.55: every piece taken from the dev grabs, placed where it is in them, relative to the retail background
        // (27, 370 at 640x480): faint frame lines +29,+57 (137 x 31), blue +29,+61 (134 x 14), solid grey bar
        // +29,+79 (134 x 6) with the slaughter amount in the final game's red (hud2004_red = health.vbm frame) over it,
        // skull -11,+4 (59 x 85)
        H04Colour(255, 255, 255);
        H04Draw(g_h04Blue, x + 29, y + 61, 134, 14, hp);
        H04Draw(g_h04Grey, x + 29, y + 79, 134, 6);
        if (g_h04Red >= 0) H04Draw(g_h04Red, x + 29, y + 79, 134, 6, sl);
        H04Draw(g_h04Frame, x + 29, y + 57, 137, 31);
        if (hp < 0.25f) {                      // low health: the skull pulses red like the retail one
            float p = 0.5f + 0.5f * sinf(GetTickCount() * 0.008f);
            H04Colour(255, (int)(255 - 170 * p), (int)(255 - 170 * p));
        }
        H04Draw(g_h04Skull, x - 11, y + 4, 59, 85);
        H04Colour(255, 255, 255);
    } __except (1) {}
    return 0;
}
static int __cdecl Hook_H04Skip80(int tex, int x, int y, DWORD sx, DWORD sy)
{
    if (H04Health()) return 0;
    return HudPic80(tex, x, y, sx, sy);
}
static int __cdecl Hook_H04SkipReserve(int tex, int x, int y, DWORD sx, DWORD sy)   // v0.57: reserve bar = weapon half
{
    if (H04Weapons()) return 0;
    return HudPic80(tex, x, y, sx, sy);
}
static int __cdecl Hook_H04Skip20(int tex, int x, int y, int w, int h, int u0, int v0, int u1, int v1, int fx, int fy,
                                  DWORD a, DWORD b, int z)
{
    if (H04Health()) return 0;
    return HudPic20(tex, x, y, w, h, u0, v0, u1, v1, fx, fy, a, b, z);
}
// weapon box frame -> sketch brackets (box x, y = the retail box position incl. slide)
static bool  g_h04FrameSeen = false;   // v0.54.5: the frame hook ran for the box whose icon comes next
static DWORD g_h04DualAt = 0;          // v0.54.5: last time a SECOND, different box was drawn (= dual wield)
static int   g_h04LastFrameX = -99999; static DWORD g_h04LastFrameT = 0;
static int __cdecl Hook_H04WeaponFrame(int tex, int x, int y, DWORD sx, DWORD sy)
{
    if (!H04Weapons()) { g_h04BoxX = -99999; return HudPic80(tex, x, y, sx, sy); }
    DWORD now = GetTickCount();
    if (g_h04LastFrameX != -99999 && x != g_h04LastFrameX && now - g_h04LastFrameT < 60) g_h04DualAt = now;
    g_h04LastFrameX = x; g_h04LastFrameT = now;
    g_h04BoxX = x; g_h04BoxY = y; g_h04FrameSeen = true;
    H04Colour(255, 255, 255);
    H04Draw(g_h04Brk, x + 20, y + 23, 127, 62);     // v0.55: the dev brackets, grab 505,399 - 632,461 (box 485,376)
    return 0;
}
static bool H04Handgun(int tex)       // v0.55: .357, Colt, Desert Eagle, Python
{
    static const int idx[4] = { 0, 2, 3, 14 };
    for (int i : idx) if (g_h04Orig[i] >= 0 && g_h04Orig[i] == tex) return true;
    return false;
}
static int H04NewFor(int tex, bool* isM16)
{
    *isM16 = false;
    int m = M16Index();
    if (m >= 0 && tex == *(volatile int*)(0x801834 + m * 0x1D0)) { *isM16 = true; return g_m16HasGL ? g_h04M16GL : g_h04M16; }
    for (int i = 0; i < 19; ++i) if (g_h04Orig[i] >= 0 && g_h04Orig[i] == tex) return g_h04New[i];
    return -1;
}
// v0.54.5: the small holstered-weapon picture (0x46BDE0, half size, one per hand) - hidden with the 2004 HUD
// (the 2004 grabs show one weapon only; it was also the retail picture that flashed during a weapon switch)
static int __cdecl Hook_H04SmallIcon(int tex, int x, int y, int w, int h, int u0, int v0, int u1, int v1, int fx, int fy,
                                     DWORD a, DWORD b, int z)
{
    bool m16 = false;
    if (H04Weapons() && H04NewFor(tex, &m16) >= 0) return 0;   // weapon pictures only
    return HudPic20(tex, x, y, w, h, u0, v0, u1, v1, fx, fy, a, b, z);
}
// weapon icon (called by Hook_HudWeaponIcon): true = drawn here
static bool H04Icon(int tex, int x, int y)
{
    bool frame = g_h04FrameSeen; g_h04FrameSeen = false;
    if (!H04Weapons() || g_h04BoxX == -99999) return false;
    bool m16 = false;
    int nt = H04NewFor(tex, &m16);
    if (nt < 0) return false;
    int tw = 0, th = 0;
    __try { ((void (__cdecl*)(int, int*, int*))0x50BEB0)(nt, &tw, &th); } __except (1) { return false; }
    if (tw <= 0 || th <= 0) return false;
    float w = tw * 0.5f, h = th * 0.5f;                // 2x textures
    H04Colour(255, 255, 255);
    if (!frame) {
        // v0.54.5: no frame = the pickup preview (the weapon on the floor, drawn above the box): the 2004 picture at
        // the game's own place and the retail picture's size, so it lines up like the retail icons
        int ow = 0, oh = 0;
        __try { ((void (__cdecl*)(int, int*, int*))0x50BEB0)(tex, &ow, &oh); } __except (1) { return false; }
        if (ow <= 0 || oh <= 0) return false;
        float k = fminf(ow / w, oh / h);
        int dw = (int)(w * k), dh = (int)(h * k);
        H04Draw(nt, x + ow - dw, y + (oh - dh) / 2, dw, dh);
        return true;
    }
    // the box (the slide while switching is vertical - the 2004 picture just stays in place)
    int bx = x - *(volatile int*)0x736650, by = g_h04BoxY;
    if (bx != g_h04BoxX) return false;
    bool dual = GetTickCount() - g_h04DualAt < 500;
    if (m16 && !dual) H04Draw(nt, bx + 4, by + 31, 135, 40);   // v0.55: the dev M16 where it is in the grab
    else if (H04Handgun(tex)) {
        // v0.55: handguns centred between the brackets, max 70 x 30
        float k = fminf(70.0f / w, 30.0f / h);
        int dw = (int)(w * k), dh = (int)(h * k);
        H04Draw(nt, bx + 84 - dw / 2, by + 47 - dh / 2, dw, dh);
    }
    else {
        // inside the brackets (x +22 .. +146): right-aligned, max 118 x 34, centred on the gun line
        float k = fminf(118.0f / w, 34.0f / h);
        int dw = (int)(w * k), dh = (int)(h * k);
        H04Draw(nt, bx + 143 - dw, by + 47 - dh / 2, dw, dh);
    }
    // brackets again ON TOP of the gun, as in the 2004 grabs
    H04Draw(g_h04Brk, g_h04BoxX + 20, g_h04BoxY + 23, 127, 62);
    return true;
}
// one text in the 7-segment font at height hgt, with a thin dark edge (readable over the light gun)
static void H04Text(int x, int y, const char* s, int font, float hgt, DWORD sx, DWORD sy)
{
    int fh = 0;
    __try { fh = ((int (__cdecl*)(int))0x5185B0)(font); } __except (1) { fh = 0; }
    if (fh <= 0) fh = 14;
    float k = hgt / fh;
    if (g_hudScaling) {
        int w = 0, h = 0;
        __try { ((void (__cdecl*)(int*, int*, const char*, int, int))0x5185E0)(&w, &h, s, (int)strlen(s), font); } __except (1) {}
        float ax, ay;
        if (w > 0 && HudAnchor((float)x, (float)y, w * k, h * k, &ax, &ay)) {
            x = HudRound(ax + (x - ax) * g_hudK); y = HudRound(ay + (y - ay) * g_hudK); k *= g_hudK;
        }
    }
    DWORD col = *(volatile DWORD*)0x91327C;           // the colour the game chose (orange when the clip is low)
    int a = *(volatile BYTE*)0x7367AB;
    ((void (__cdecl*)(int, int, int, int))0x510B60)(30, 30, 32, a);
    for (int d = 0; d < 4; ++d)
        ((TextK_t)0x518800)(x + (d == 0 ? 1 : d == 1 ? -1 : 0), y + (d == 2 ? 1 : d == 3 ? -1 : 0), FBits(k), FBits(k), s, font, sx, sy);
    *(volatile DWORD*)0x91327C = col;
    ((TextK_t)0x518800)(x, y, FBits(k), FBits(k), s, font, sx, sy);
}
// the ammo text (OnTextDrawn, return address 0x46C5C7): f[15] entity, f[35] weapon type, f[38] pickup-card flag
static void H04Ammo(DWORD* f)
{
    if (!H04Weapons() || (BYTE)f[38] != 0 || g_h04BoxX == -99999) return;
    if (g_h04Font == -2) {
        __try { g_h04Font = ((int (__cdecl*)(const char*, int, int))0x518DC0)("digital_small.vf2", -1, 0); } __except (1) { g_h04Font = -1; }
        Log("2004 hud: digital_small.vf2 font %d", g_h04Font);
    }
    if (g_h04Font < 0) return;
    int bx = (int)f[1] - *(volatile int*)0x736658, by = (int)f[2] - *(volatile int*)0x73665C;
    if (bx != g_h04BoxX) return;
    const char* clip = (const char*)f[3];
    int reserve = -1, mx = 0;
    __try {
        mx = ((int (__cdecl*)(DWORD, DWORD))0x4FDF80)(f[15], f[35]);
        if (mx > 0) reserve = ((int (__cdecl*)(DWORD, DWORD))0x4FDF40)(f[15], f[35]);
    } __except (1) { reserve = -1; }
    char cs[16]; snprintf(cs, sizeof(cs), "%03d", clip ? atoi(clip) : 0);
    H04Text(bx + 37, by + 53, cs, g_h04Font, 21.0f, f[5], f[6]);       // "030" at 522,429 (21 px)
    if (reserve >= 0) {
        int w = 0, h = 0;
        __try { ((void (__cdecl*)(int*, int*, const char*, int, int))0x5185E0)(&w, &h, cs, (int)strlen(cs), g_h04Font); } __except (1) {}
        int fh = 14; __try { fh = ((int (__cdecl*)(int))0x5185B0)(g_h04Font); } __except (1) {}
        int cw = fh > 0 ? (int)(w * 21.0f / fh) : 44;
        char rs[16]; snprintf(rs, sizeof(rs), "%03d", reserve);
        H04Text(bx + 37 + cw + 2, by + 53 + 21 - 13, rs, g_h04Font, 13.0f, f[5], f[6]);   // "396" 13 px, same baseline
    }
    g_skipOnce = 1;
}
static void InstallHud2004()
{
    PatchCall(0x46A712, (void*)Hook_H04MetersBg, 0x510C80);    // meters_bg -> the 2004 health cluster
    PatchCall(0x46A98F, (void*)Hook_H04Skip20, 0x510C20);      // health damage-drain bar
    PatchCall(0x46AA76, (void*)Hook_H04Skip20, 0x510C20);      // health bar
    PatchCall(0x46AB36, (void*)Hook_H04Skip80, 0x510C80);      // slaughter meter
    PatchCall(0x46AB87, (void*)Hook_H04Skip80, 0x510C80);      // meters outline
    PatchCall(0x46ACA0, (void*)Hook_H04Skip80, 0x510C80);      // skull: hit shake
    PatchCall(0x46AD0E, (void*)Hook_H04Skip80, 0x510C80);      // skull: ghosts
    PatchCall(0x46AE36, (void*)Hook_H04Skip80, 0x510C80);      // skull: low-health pulse
    PatchCall(0x46AE87, (void*)Hook_H04Skip80, 0x510C80);      // skull: normal
    PatchCall(0x46C3FB, (void*)Hook_H04WeaponFrame, 0x510C80); // weapon box frame -> brackets
    PatchCall(0x46C05A, (void*)Hook_H04SmallIcon, 0x510C20);   // v0.54.5: small holstered-weapon picture (hidden)
    PatchCall(0x46C76F, (void*)Hook_H04SkipReserve, 0x510C80); // reserve bar (the 2004 HUD shows the number)
}
// v0.63: full-screen effect quads at exact pixel centres. The black & white screen copies the scene into a texture
// (0x519710 -> quad 0x9131D0 via 0x519B80 at 0x519821) and draws it back (0x514BC0 -> 0x519B80 at 0x514FB3), both
// with corners on pixel EDGES (0..W, uv 0..1) and no D3D9 half-pixel shift -> every pixel samples exactly between
// two texels. Native D3D9 rounds that the same way everywhere; DXVK (Steam Deck) rounded one column differently =
// a thin dark vertical line at x ~1178 of 1280 in Slaughter mode / env-kill B&W (Deck screens Nov 14). The wrapper
// passes a copy of the corners moved by -0.5 px (exact 1:1 copy everywhere). [Display] EffectPixelFix=0 = off.
static int g_fxHalfPixel = 1;
typedef void (__cdecl* FxQuad_t)(int, int, const float*, int, int, int);
// v0.73 diag: sizes of the screen textures / back buffer / depth buffer and the target + viewport while the screen
// copy is drawn (the Deck line could be a size mismatch: Vulkan draws only where every attachment exists).
static void FxSizeLog(const char* when)
{
#ifdef EC_RELEASE
    return;                                                // test log - not in the release
#endif
    static int left = 3; if (left <= 0) return; --left;
    __try {
        void* dev = *(void**)0x913C48; if (!dev) return;
        typedef long (__stdcall* Desc_t)(void*, int, DWORD*);
        typedef long (__stdcall* SDesc_t)(void*, DWORD*);
        typedef long (__stdcall* Get_t)(void*, void**);
        typedef unsigned long (__stdcall* Rel_t)(void*);
        char b[600]; int o = snprintf(b, sizeof(b), "fx sizes (%s):", when);
        static const DWORD texs[3] = { 0x913C5C, 0x913C54, 0x913C64 };
        for (DWORD g : texs) {
            void* tx = *(void**)g; DWORD d[8] = {};
            if (tx && ((Desc_t)(*(void***)tx)[14])(tx, 0, d) >= 0) o += snprintf(b + o, sizeof(b) - o, " tex[%X] %ux%u fmt %u;", g, d[6], d[7], d[0]);
        }
        static const DWORD surfs[2] = { 0x913C70, 0x913C74 };
        for (DWORD g : surfs) {
            void* s = *(void**)g; DWORD d[8] = {};
            if (s && ((SDesc_t)(*(void***)s)[8])(s, d) >= 0) o += snprintf(b + o, sizeof(b) - o, " %s %ux%u fmt %u ms %u;", g == 0x913C70 ? "backbuf" : "depth", d[6], d[7], d[0], d[5]);
        }
        void* rt = nullptr; void* ds = nullptr;
        if (((Get_t)(*(void***)dev)[32])(dev, &rt) >= 0 && rt) {
            DWORD d[8] = {}; ((SDesc_t)(*(void***)rt)[8])(rt, d);
            o += snprintf(b + o, sizeof(b) - o, " current target %ux%u;", d[6], d[7]); ((Rel_t)(*(void***)rt)[2])(rt);
        }
        if (((Get_t)(*(void***)dev)[33])(dev, &ds) >= 0 && ds) {
            DWORD d[8] = {}; ((SDesc_t)(*(void***)ds)[8])(ds, d);
            o += snprintf(b + o, sizeof(b) - o, " current depth %ux%u;", d[6], d[7]); ((Rel_t)(*(void***)ds)[2])(ds);
        }
        DWORD vp[6] = {};
        if (((SDesc_t)(*(void***)dev)[41])(dev, vp) >= 0) o += snprintf(b + o, sizeof(b) - o, " viewport %u,%u %ux%u", vp[0], vp[1], vp[2], vp[3]);
        Log("%s", b);
    } __except (1) { Log("fx sizes: failed"); }
}
static void __cdecl Hook_FxCopy(int tex, int n, const float* v, int a, int b, int c);
static void __cdecl Hook_FxQuad(int tex, int n, const float* v, int a, int b, int c)
{
    float tmp[8 * 8];
    if (g_fxHalfPixel && v && n > 0 && n <= 8) {
        memcpy(tmp, v, n * 32);
        for (int i = 0; i < n; ++i) { tmp[i * 8] -= 0.5f; tmp[i * 8 + 1] -= 0.5f; }
        v = tmp;
    }
    ((FxQuad_t)0x519B80)(tex, n, v, a, b, c);
}
// v0.65 diagnostic (B&W line on the Deck only, gone without the Widescreen Fix): log what the black & white frame
// draws - the two full-screen tint rectangles (0x5110B0 at 0x45D632 / 0x45D875: x, y, w, h = viewport size
// [0x913264] x [0x913268]), the viewport / clip rectangle and the screen size, and the Slaughter zoom quad (0x5144FE).
// A few lines per black & white start, nothing changes on screen.
static int g_fxLogLeft = 0;
static DWORD g_fxLogLast = 0;
static void FxLogArm()
{
    if (GetTickCount() - g_fxLogLast > 20000) g_fxLogLeft = 6;   // new burst at most every 20 s
    g_fxLogLast = GetTickCount();
}
static void FxLogScreen(const char* what)
{
    Log("fx %s: screen %dx%d, viewport %d,%d %dx%d, clip x %d..%d y %d..%d", what,
        *(volatile int*)0x913250, *(volatile int*)0x913254, *(volatile int*)0x91325C, *(volatile int*)0x913260,
        *(volatile int*)0x913264, *(volatile int*)0x913268, *(volatile int*)0x91326C, *(volatile int*)0x913270,
        *(volatile int*)0x913274, *(volatile int*)0x913278);
}
typedef void (__cdecl* FxRect_t)(int, int, int, int, int, int);
static void __cdecl Hook_FxRect(int x, int y, int w, int h, int a, int b)
{
    FxBigLog("rect", -1, x, y, w, h, _ReturnAddress());
    if (g_fxLogLeft > 0) { --g_fxLogLeft; Log("fx tint rect %d,%d %dx%d", x, y, w, h); FxLogScreen("at tint"); }
    ((FxRect_t)0x5110B0)(x, y, w, h, a, b);
}
static void __cdecl Hook_FxZoom(int tex, int n, const float* v, int a, int b, int c)
{
    if (g_fxLogLeft > 0 && v && n == 4) {
        --g_fxLogLeft;
        Log("fx zoom quad x %.1f..%.1f y %.1f..%.1f uv %.3f..%.3f", v[0], v[8], v[1], v[17], v[4], v[12]);
    }
    Hook_FxQuad(tex, n, v, a, b, c);
}
static void __cdecl Hook_FxBW(int tex, int n, const float* v, int a, int b, int c)
{
    g_fxBwAt = GetTickCount() | 1;
    FxLogArm();
    if (g_fxLogLeft > 0 && v && n == 4) {
        --g_fxLogLeft;
        Log("fx b&w quad x %.1f..%.1f y %.1f..%.1f uv %.3f..%.3f", v[0], v[8], v[1], v[17], v[4], v[12]);
        FxLogScreen("at b&w");
    }
    Hook_FxQuad(tex, n, v, a, b, c);
}
// v0.66: THE B&W LINE (Deck). Slaughter mode (FUN_00463490, player +0x1F3) draws texture [0x61FAAC] as 4 mirrored
// screen-quarter tiles, twice, the 2nd time with a random split (+-4 px) - through 0x510C20 -> 0x519CA0. The
// Widescreen Fix hooks that inner call (0x510C68) and reshapes some quarter-screen pictures as if they were 4:3
// screens; at 1280x800 the right tiles end near x 1173 (+ the jitter) -> a thin edge that the b&w copy + Slaughter
// zoom then shows as a moving dark line (Deck test 2: no line without the fix). The 8 tile calls now go straight to
// 0x519CA0 (exactly what 0x510C20 does, minus the fix's reshaping). [Display] SlaughterTileFix=0 = old way.
static int g_slTileFix = 1;
typedef int (__cdecl* PicRaw_t)(int, int, int, int, int, int, int, int, int, int, int, DWORD, DWORD, int);
static int __cdecl Hook_SlTile(int tex, int x, int y, int w, int h, int u0, int v0, int u1, int v1, int fx, int fy,
                               DWORD a, DWORD b, int z)
{
    FxBigLog("slaughter tile", tex, x, y, w, h, _ReturnAddress());
    if (!g_slTileFix) return ((PicRaw_t)0x510C20)(tex, x, y, w, h, u0, v0, u1, v1, fx, fy, a, b, z);
    if (tex == -1) return 0;
    return ((PicRaw_t)0x519CA0)(tex, x, y, w, h, u0, v0, u1, v1, fx, fy, a, b, 0);
}
// v0.71 DIAGNOSTIC (B&W line, Deck only): while the black & white screen is on, every 2D draw that has an edge near
// x 1160..1190 is counted per caller and written to the log (what draws the line at ~1173 = right edge of a centred
// 4:3 area at 1280x800). Entry hooks on the three lowest 2D draws, so draws coming from other plugins (Widescreen
// Fix) are seen too: 0x519B80 (vertex quad), 0x519CA0 (picture), 0x5110B0 (filled rectangle).
// [Developer] LineDiag=0 = off. Nothing changes on screen.
#ifdef EC_RELEASE
static const char* kBuildKind = " (release)";
#else
static const char* kBuildKind = "";
#endif
static int g_lineDiag = 1;
static int g_devMenuPreview = -99;   // v0.72 [Developer] MenuPreview (laptop screenshots, no pad): -3 list, -1..15 level page
static void* EntryHook(DWORD at, int len, const BYTE* expect, void* hook, const char* what)
{
    if (memcmp((void*)at, expect, len) != 0) {
        Log("entry hook: %s at %08X has other bytes (%02X %02X %02X ...) - not hooked", what, at,
            ((BYTE*)at)[0], ((BYTE*)at)[1], ((BYTE*)at)[2]);
        return nullptr;
    }
    BYTE* tr = (BYTE*)VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!tr) return nullptr;
    memcpy(tr, (void*)at, len);
    tr[len] = 0xE9; *(DWORD*)(tr + len + 1) = (at + len) - (DWORD)(tr + len + 5);
    DWORD old; VirtualProtect((void*)at, len, PAGE_EXECUTE_READWRITE, &old);
    *(BYTE*)at = 0xE9; *(DWORD*)(at + 1) = (DWORD)hook - (at + 5);
    for (int i = 5; i < len; ++i) ((BYTE*)at)[i] = 0x90;
    VirtualProtect((void*)at, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)at, len);
    FlushInstructionCache(GetCurrentProcess(), tr, 32);
    Log("entry hook: %s hooked at %08X", what, at);
    return tr;
}
static void DiagWhere(DWORD a, char* out, int sz)
{
    HMODULE m = nullptr; char nm[MAX_PATH] = "?";
    if (a && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)a, &m) && m) {
        GetModuleFileNameA(m, nm, MAX_PATH);
        const char* b = strrchr(nm, '\\'); b = b ? b + 1 : nm;
        if ((DWORD)m == 0x400000) snprintf(out, sz, "%08X", a); else snprintf(out, sz, "%s+%X", b, a - (DWORD)m);
    } else snprintf(out, sz, "%08X", a);
}
struct DiagRow { DWORD ret, game; char kind; int tex; float x0, x1, y0, y1, u0, u1; int n; };
static DiagRow g_diagRows[48]; static int g_diagN = 0, g_diagLines = 0; static DWORD g_diagDump = 0;
static bool DiagActive() { return g_lineDiag && g_fxBwAt && GetTickCount() - g_fxBwAt < 500; }
static bool Near(float x)                               // the right edge of a centred 4:3 area (1173.3 at 1280x800) +-14 px
{
    float W = (float)*(volatile int*)0x913250, H = (float)*(volatile int*)0x913254, e = (W + H * 4.f / 3.f) * 0.5f;
    return x >= e - 14.f && x <= e + 14.f;
}
static void DiagNote(char kind, int tex, float x0, float x1, float y0, float y1, float u0, float u1, DWORD ret, const DWORD* stack)
{
    DWORD game = 0;                                        // first game return address on the stack (caller of a plugin)
    if (ret < 0x401000 || ret > 0x5EB000)
        for (int i = 0; i < 96 && !game; ++i) {
            DWORD r = 0;
            __try { r = stack[i]; if (r > 0x401005 && r < 0x5EB000 && *(BYTE*)(r - 5) == 0xE8) game = r; } __except (1) { break; }
        }
    for (int i = 0; i < g_diagN; ++i) {
        DiagRow& d = g_diagRows[i];
        if (d.ret == ret && d.kind == kind && d.tex == tex) { ++d.n; return; }
    }
    if (g_diagN < 48) g_diagRows[g_diagN++] = { ret, game, kind, tex, x0, x1, y0, y1, u0, u1, 1 };
}
static void DiagDump()
{
    if (!g_diagN || g_diagLines > 400 || GetTickCount() - g_diagDump < 4000) return;
    g_diagDump = GetTickCount();
    int W = *(volatile int*)0x913250, H = *(volatile int*)0x913254;
    Log("line diag: draws with an edge at the 4:3 right edge x %.1f +-14 during black & white (screen %dx%d):", (W + H * 4.f / 3.f) * 0.5f, W, H);
    for (int i = 0; i < g_diagN; ++i) {
        DiagRow& d = g_diagRows[i]; char a[96], b[96]; DiagWhere(d.ret, a, sizeof(a)); DiagWhere(d.game, b, sizeof(b));
        Log("  %c tex %d x %.2f..%.2f y %.2f..%.2f u %.3f..%.3f  x%d  from %s%s%s", d.kind, d.tex, d.x0, d.x1, d.y0, d.y1,
            d.u0, d.u1, d.n, a, d.game ? "  game caller " : "", d.game ? b : "");
        ++g_diagLines;
    }
    g_diagN = 0;
}
typedef void (__cdecl* DQuad_t)(int, int, const float*, int, int, int);
typedef int (__cdecl* DPic_t)(int, int, int, int, int, int, int, int, int, int, int, DWORD, DWORD, int);
typedef void (__cdecl* DRect_t)(int, int, int, int, int, int);
static DQuad_t g_trQuad; static DPic_t g_trPic; static DRect_t g_trRect;
static void __cdecl EH_Quad(int tex, int n, const float* v, int a, int b, int c)
{
    if (DiagActive() && v && n > 0 && n <= 64) {
        __try {
            float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f, u0 = 1e9f, u1 = -1e9f;
            for (int i = 0; i < n; ++i) {
                const float* p = v + i * 8;
                if (p[0] < x0) x0 = p[0]; if (p[0] > x1) x1 = p[0]; if (p[1] < y0) y0 = p[1]; if (p[1] > y1) y1 = p[1];
                if (p[4] < u0) u0 = p[4]; if (p[4] > u1) u1 = p[4];
            }
            if (Near(x0) || Near(x1)) DiagNote('Q', tex, x0, x1, y0, y1, u0, u1, (DWORD)(size_t)_ReturnAddress(), (const DWORD*)&tex);
        } __except (1) {}
        DiagDump();
    }
    g_trQuad(tex, n, v, a, b, c);
}
static int __cdecl EH_Pic(int tex, int x, int y, int w, int h, int u0, int v0, int u1, int v1, int fx, int fy, DWORD a, DWORD b, int z)
{
    if (DiagActive()) {
        if (Near((float)x) || Near((float)(x + w)))
            DiagNote('P', tex, (float)x, (float)(x + w), (float)y, (float)(y + h), (float)u0, (float)u1, (DWORD)(size_t)_ReturnAddress(), (const DWORD*)&tex);
        DiagDump();
    }
    return g_trPic(tex, x, y, w, h, u0, v0, u1, v1, fx, fy, a, b, z);
}
static void __cdecl EH_Rect(int x, int y, int w, int h, int a, int b)
{
    if (DiagActive()) {
        if (Near((float)x) || Near((float)(x + w)))
            DiagNote('R', -1, (float)x, (float)(x + w), (float)y, (float)(y + h), 0, 0, (DWORD)(size_t)_ReturnAddress(), (const DWORD*)&x);
        DiagDump();
    }
    g_trRect(x, y, w, h, a, b);
}
static bool FusionFixPresent();
static void InstallLineDiag()
{
    if (!g_lineDiag) return;
    if (FusionFixPresent()) { Log("line diag: skipped (Fusion Fix present - it scans these functions)"); return; }
    static const BYTE q[] = { 0x53, 0x8B, 0x5C, 0x24, 0x1C };                                  // push ebx; mov ebx,[esp+1C]
    static const BYTE p[] = { 0x8B, 0x4C, 0x24, 0x10, 0xB8, 0x01, 0x00, 0x00, 0x00 };          // mov ecx,[esp+10]; mov eax,1
    static const BYTE r[] = { 0x8B, 0x4C, 0x24, 0x0C, 0x8B, 0x44, 0x24, 0x04 };                // mov ecx,[esp+C]; mov eax,[esp+4]
    g_trQuad = (DQuad_t)EntryHook(0x519B80, 5, q, (void*)EH_Quad, "quad draw");
    g_trPic  = (DPic_t)EntryHook(0x519CA0, 9, p, (void*)EH_Pic, "picture draw");
    g_trRect = (DRect_t)EntryHook(0x5110B0, 8, r, (void*)EH_Rect, "rectangle draw");
}
static void __cdecl Hook_FxCopy(int tex, int n, const float* v, int a, int b, int c)
{
    if (g_fxBwAt && GetTickCount() - g_fxBwAt < 500) FxSizeLog("b&w screen copy");
    Hook_FxQuad(tex, n, v, a, b, c);
}
static void InstallFxHalfPixel()
{
    InstallLineDiag();   // v0.71 diag
    PatchCall(0x412C4E, (void*)Hook_GibRoll, 0x410F00);   // v0.76 dev: [Developer] AlwaysGib
    for (DWORD s : { 0x4636A0u, 0x46372Eu, 0x4637BEu, 0x46386Eu, 0x463942u, 0x4639D2u, 0x463A61u, 0x463B10u })
        PatchCall(s, (void*)Hook_SlTile, 0x510C20);       // v0.66: Slaughter tiles past the Widescreen Fix
    PatchCall(0x519821, (void*)Hook_FxCopy, 0x519B80);   // screen copy (all screen effects) - v0.73: + size log
    PatchCall(0x514FB3, (void*)Hook_FxBW, 0x519B80);     // black & white draw (v0.65: + log)
    PatchCall(0x5144FE, (void*)Hook_FxZoom, 0x519B80);   // v0.65: Slaughter zoom overlay (half-pixel + log)
    PatchCallsTo(0x401000, 0x5EB000, 0x5110B0, (void*)Hook_FxRect, "rectangle draw (b&w diag)");   // v0.65 (log only)
}
static void InstallWeaponMods()
{
    InstallFxHalfPixel();
    InstallHud2004();   // v0.53 (must run before InstallHudScale, like the other HUD PatchCalls here)
    PatchCall(0x5054BD, (void*)Hook_Attach, 0x4FDD50);
    PatchCall(0x5054D7, (void*)Hook_Attach, 0x4FDD50);
    PatchCall(0x5054F1, (void*)Hook_Attach, 0x4FDD50);
    PatchCall(0x50550B, (void*)Hook_Attach, 0x4FDD50);
    PatchCall(0x4D11DF, (void*)Hook_Attach, 0x4FDD50);
    PatchCall(0x4D11FF, (void*)Hook_Attach, 0x4FDD50);
    PatchCall(0x46C4A3, (void*)Hook_HudWeaponIcon, 0x510C80);   // v0.36: M16 launcher icon (beta HUD style)
    PatchCall(0x46CBEC, (void*)Hook_HostageIcon, 0x510C80);     // v0.37: 2004 target-cutout hostage icon
    PatchCall(0x46A276, (void*)Hook_LifebarA, 0x510BE0);        // v0.38: 2004 striped enemy lifebar
    PatchCall(0x46A41D, (void*)Hook_LifebarFrame, 0x5131B0);    // v0.38.1: frame, placed beside the enemy too
    PatchCall(0x46A4C0, (void*)Hook_LifebarB, 0x5131B0);
    PatchCall(0x4C008E, (void*)Hook_PickupCard, 0x4682D0);      // v0.37.1: pickup card picture (4 callers)
    PatchCall(0x4C01AA, (void*)Hook_PickupCard, 0x4682D0);
    PatchCall(0x4E3AC3, (void*)Hook_PickupCard, 0x4682D0);
    PatchCall(0x4E3BD5, (void*)Hook_PickupCard, 0x4682D0);
}
static void InstallComboHud()
{
    PatchCall(0x46BABB, (void*)Hook_ScoreHud, 0x46B300);
    PatchCall(0x46B549, (void*)Hook_ScoreText, 0x46F7C0);
    PatchCall(0x46B832, (void*)Hook_ScoreHudText, 0x46F7C0);
    PatchCall(0x46B936, (void*)Hook_ScoreHudText, 0x46F7C0);
    PatchCall(0x46B9FF, (void*)Hook_ScoreHudText, 0x46F7C0);
    PatchCall(0x46B806, (void*)Hook_ScoreHudText2, 0x51E8E0);
    PatchCall(0x46B437, (void*)Hook_ScoreSprite, 0x510C20);
    PatchCall(0x46B489, (void*)Hook_ScoreSprite, 0x510C20);
    // 0x46B743 (scaled combo, 0x518800) is already Hook_TitleText - it checks g_scoreHudOn
}
static void InstallPopupAndFooter()
{
    PatchCall(0x477545, (void*)Hook_PopupInput, 0x4768E0);
    int n = 0;
    n += PatchImm(0x479217, 0x5FB770, (DWORD)g_lblEnter);     // footer "Enter:"
    n += PatchImm(0x479221, 0x5FBB64, (DWORD)g_lblEsc);       // footer "Esc:"
    static const DWORD box[4] = { 0x474A3F, 0x474A4D, 0x474A6E, 0x474A8C };   // "Enter: Continue" box
    for (int i = 0; i < 4; ++i) n += PatchImm(box[i], 0x5FB770, (DWORD)g_lblEnter);
    Log("footer: %d of 6 \"Enter:\"/\"Esc:\" references redirected", n);
}

// ---------------------------------------------------------------- v0.20: in-game enhancements menu
// Pause with Start (the plain console-style pause screen), press [Enhancements] OpenButton (Y): a list of
// the plugin's settings is drawn on top of the pause screen with the game's own text function.
// D-pad / left stick: up/down = choose, left/right = change; A = toggle / change; LB/RB = page;
// X = all settings back to your defaults (ExtendedCut_defaults.ini); B (or Y / Start) = close. Changes work at once and are written
// to ExtendedCut.ini when the menu closes (only the value is replaced - your comments stay; the ini as it
// was before the first save of a session is kept as ExtendedCut.ini.bak).
//
// Draw points (all naked pass-throughs: the game's function runs unchanged, then we draw on top):
//   0x461D50  plain pause screen (pause_bg.tga + paused.tga)   - preferred, used if it runs every frame
//   0x467D00  hint messages                                    - fallback
// 0x5186B0 (DrawGameText) is watched (not changed) to learn which fonts / text scales the game uses, and
// which functions draw text while paused (logged) - in case the draw point needs to move.
static const DWORD kPaused = 0x736109;     // byte: game paused
static const DWORD kLevelFlags = 0x759BDC; // bit 0: a level is running
static DWORD g_fnPause = 0x461D50, g_fnHint = 0x467D00, g_fnText = 0x5186B0;
static DWORD g_retPause = 0, g_retHint = 0;
static int   g_hookedPause = 0, g_hookedHint = 0, g_hookedText = 0;
static DWORD g_lastPauseDraw = 0, g_pauseCalls = 0, g_hintCalls = 0, g_textCalls = 0;
static int   g_pauseStreak = 0;
static bool  g_pauseEveryFrame = false;   // 0x461D50 seen running every frame while paused
static DWORD g_drawnFrame = 0xFFFFFFFF;

// [Enhancements] ini settings
static int   g_enhOpenBtn = P_NORTH;
static int   g_enhPauseHint = 1;
static float g_enhX = 0.15f, g_enhY = 0.14f, g_enhValCol = 0.0f, g_enhCharW = 0.8f;
static int   g_enhFontSel = 0;           // 0 interface, 1 title, 2 hud, 3 current (-1)

// ---- what the game passes to DrawGameText (we copy its font numbers and scale arguments)
struct ScaleSeen { DWORD sx, sy; int n; };
static ScaleSeen g_scales[8];
static int  g_fontsSeen[12], g_fontsN = 0;
static DWORD g_pausedCallers[24]; static int g_pausedCallerN = 0;
static int  g_tminX = 1 << 30, g_tmaxX = -(1 << 30), g_tminY = 1 << 30, g_tmaxY = -(1 << 30);

extern const char* g_optText;
static void __cdecl OnTextDrawn(DWORD* f)    // f[0] return address, f[1..6] x, y, text, font, sx, sy
{
    ++g_textCalls;
    // v0.33: the HUD ammo counter is drawn by the call at 0x46C5C2 (returns to 0x46C5C7) with the score font
    // [0x676058]. digital_small.vf2 (7-segment digits 0-9) ships in every build but no exe loads it - the beta HUD's
    // counter font. Loaded once with the game's font loader 0x518DC0 (name, -1, flag) and swapped in here.
    if (f[0] == 0x46C5C7) H04Ammo(f);   // v0.53: 2004 HUD clip + reserve numbers (sets g_skipOnce)
    if (g_betaDigitalAmmo && f[0] == 0x46C5C7) {
        static int font = -2;
        if (font == -2) {
            __try { font = ((int (__cdecl*)(const char*, int, int))0x518DC0)("digital_small.vf2", -1, 0); } __except (1) { font = -1; }
            Log("digital ammo: digital_small.vf2 loaded as font %d (score font %d)", font, *(volatile int*)0x676058);
        }
        if (font >= 0) f[4] = (DWORD)font;
    }
    {   // v0.31.2 diagnostics: where (and if) the Options entry "Extended Cut" is drawn
        static int n = 0; static int lx = -99999, ly = -99999;
        if (f[3] == (DWORD)g_optText && n < 12 && ((int)f[1] != lx || (int)f[2] != ly)) {
            ++n; lx = (int)f[1]; ly = (int)f[2];
            Log("options diag: \"Extended Cut\" drawn at %d,%d by %08X (list count %d, screen fields %d %d, selected %d)",
                lx, ly, f[0], *(volatile int*)(0x74B528 + 0x168), *(volatile int*)(0x74B938 + 0x54),
                *(volatile int*)(0x74B938 + 0x58), *(volatile int*)(0x74B938 + 0x60));
        }
    }
    int x = (int)f[1], y = (int)f[2], font = (int)f[4];
    DWORD sx = f[5], sy = f[6];
    int slot = -1, weakest = 0;
    for (int i = 0; i < 8; ++i) {
        if (g_scales[i].n && g_scales[i].sx == sx && g_scales[i].sy == sy) { slot = i; break; }
        if (g_scales[i].n < g_scales[weakest].n) weakest = i;
    }
    if (slot < 0) { slot = weakest; g_scales[slot].sx = sx; g_scales[slot].sy = sy; g_scales[slot].n = 0; }
    ++g_scales[slot].n;
    bool known = false;
    for (int i = 0; i < g_fontsN; ++i) if (g_fontsSeen[i] == font) { known = true; break; }
    if (!known && g_fontsN < 12) g_fontsSeen[g_fontsN++] = font;
    if (g_textCalls < 4000) {
        if (x < g_tminX) g_tminX = x; if (x > g_tmaxX) g_tmaxX = x;
        if (y < g_tminY) g_tminY = y; if (y > g_tmaxY) g_tmaxY = y;
    } else if (g_textCalls == 4000) {
        float a, b; memcpy(&a, &g_scales[0].sx, 4); memcpy(&b, &g_scales[0].sy, 4);
        Log("text: first 4000 game texts at x %d..%d, y %d..%d; %d fonts seen", g_tminX, g_tmaxX, g_tminY, g_tmaxY, g_fontsN);
        for (int i = 0; i < 8; ++i) if (g_scales[i].n) {
            memcpy(&a, &g_scales[i].sx, 4); memcpy(&b, &g_scales[i].sy, 4);
            Log("text: scale args %08X %08X (%.3f %.3f) used %d times", g_scales[i].sx, g_scales[i].sy, a, b, g_scales[i].n);
        }
        for (int i = 0; i < g_fontsN; ++i) Log("text: font %d used", g_fontsSeen[i]);
        Log("text: font globals interface %d, title %d, hud %d", *(volatile int*)0x676054, *(volatile int*)0x67604C, *(volatile int*)0x676050);
    }
    if (*(volatile BYTE*)kPaused && (*(volatile BYTE*)kLevelFlags & 1) && g_pausedCallerN < 24) {
        DWORD r = f[0]; bool seen = false;
        for (int i = 0; i < g_pausedCallerN; ++i) if (g_pausedCallers[i] == r) { seen = true; break; }
        if (!seen) {
            g_pausedCallers[g_pausedCallerN++] = r;
            char t[40] = "";
            __try { strncpy(t, (const char*)f[3], 39); t[39] = 0; for (char* c = t; *c; ++c) if ((BYTE)*c < 32) *c = '.'; } __except (1) {}
            Log("text while paused: caller %08X font %d at %d,%d \"%s\"", r, font, x, y, t);
        }
    }
}
static bool PickScale(DWORD* sx, DWORD* sy)
{
    int best = -1;
    for (int i = 0; i < 8; ++i) if (g_scales[i].n && (best < 0 || g_scales[i].n > g_scales[best].n)) best = i;
    if (best < 0) return false;
    *sx = g_scales[best].sx; *sy = g_scales[best].sy;
    return true;
}
static bool FontSeen(int font) { for (int i = 0; i < g_fontsN; ++i) if (g_fontsSeen[i] == font) return true; return false; }
static int EnhFont()
{
    static const DWORD glob[3] = { 0x676054, 0x67604C, 0x676050 };   // interface, title, hud
    if (g_enhFontSel >= 0 && g_enhFontSel < 3) {
        int f = *(volatile int*)glob[g_enhFontSel];
        if (f >= 0 && FontSeen(f)) return f;          // only a font the game itself has drawn with
    }
    return -1;
}

// v0.29.1: "Log my position" (Extended Cut page). Player entity = *(0x6B70FC); every game object keeps its
// position at +0x1C (x y z) and its rotation at +0x28 (3 rows of 3 floats) - from the object creator 0x4ADAA0.
// ---- v0.30: "Beta content" page. Restored cut animations live in perm_mot.vpp NEXT TO the final ones (old files
// renamed pnsh_ -> pnsb_, + pnsh_hrfl_kickdoor.rfa). The game reads skeleton.tbl into a buffer (FUN_00526F90,
// __thiscall, reader object: [0] buffer, [1] cursor, [9] size) from 3 places; the plugin edits the Punisher block of
// that text before it is parsed. Values are read once at startup (anims load at startup), menu changes need a restart.
static int g_betaDives = 0;      // [Beta] Dives: 0 = final (roll), 1 = 2004 PS2-demo dives (dive to slide)
static int g_betaKick = 0;       // [Beta] RifleDoorKick: 1 = cut rifle-holding door kick
static int g_betaDivesOn = 0, g_betaKickOn = 0;   // latched at startup
static std::string g_skelText;

static std::string EditSkeleton(const char* src, int len, int* nDive, int* nKick)
{
    std::string out; out.reserve(len + 256);
    bool inPun = false; std::string grp;
    bool kickPending = false;   // v0.50.6
    int i = 0;
    while (i < len) {
        int j = i; while (j < len && src[j] != '\n') ++j;
        int e = (j < len) ? j + 1 : j;
        std::string line(src + i, e - i);
        i = e;
        const char* t = line.c_str(); while (*t == ' ' || *t == '\t') ++t;
        bool comment = t[0] == '/' && t[1] == '/';
        if (kickPending && strncmp(t, "+Trigger", 8)) {   // first line after shoot_grenade and its +Trigger lines
            out += "\t+Action:\t\t\t\t\t\t\"kick_door\"\t\t\t\t\t\"pnsh_hrfl_kickdoor.rfa\"\r\n";
            ++*nKick; kickPending = false;
        }
        if (!strncmp(t, "$Name:", 6)) inPun = strstr(t, "\"punisher\"") != nullptr;
        if (inPun && !strncmp(t, "$Group:", 7)) {
            const char* q = strchr(t, '"'); const char* q2 = q ? strchr(q + 1, '"') : nullptr;
            grp = (q && q2) ? std::string(q + 1, q2) : std::string();
        }
        if (inPun && !comment && g_betaDivesOn) {
            const char* from = nullptr; const char* to = nullptr;
            if (grp == "pistol")                              { from = "pnsh_pstl_dive"; to = "pnsb_dpst_dive"; }
            else if (grp == "pistol dual" || grp == "rifle dual") { from = "pnsh_dpst_dive"; to = "pnsb_dpst_dive"; }
            else if (grp == "rifle")                          { from = "pnsh_hrfl_dive"; to = "pnsb_hrfl_dive"; }
            size_t p;
            if (from && (p = line.find(from)) != std::string::npos) { line.replace(p, strlen(from), to); ++*nDive; }
        }
        // v0.50.3: the v0.39 "Side dives slide" test (enemy sliding dive) was removed - too glitchy (see RESUME 2026-11-07 m).
        out += line;
        // v0.50.6 FIX: v0.30-v0.50.5 put kick_door straight after the shoot_grenade line, so shoot_grenade's next line
        // '+Trigger: "release" 3' (the frame the grenade leaves) belonged to kick_door -> the M16 launcher played its
        // sound, never released the grenade and the Punisher stayed in the fire action (only move / look worked).
        if (inPun && !comment && g_betaKickOn && grp == "rifle" && strstr(t, "\"shoot_grenade\"")) kickPending = true;
    }
    return out;
}
typedef int(__thiscall* TableOpen_t)(void* self, const char* name);
static void EditSkeletonBuffer(int* o)
{
    const char* buf = (const char*)o[0]; int len = o[9];
    if (!buf || len <= 0) return;
    int nd = 0, nk = 0;
    g_skelText = EditSkeleton(buf, len, &nd, &nk);
    o[0] = o[1] = (int)g_skelText.c_str();
    o[9] = (int)g_skelText.size();
    Log("beta: skeleton.tbl edited (old dives %d lines, rifle door kick %d)", nd, nk);
}
// v0.31.3: gui.tbl sets the Options paper ("op_menu_bg") and the list area ("op_action_list", rows outside it are
// clipped) per screen size: both get one row (20) taller when the Extended Cut entry is in the list.
static bool g_optAdded = false;
static std::string g_guiText;
static void EditGuiBuffer(int* o)
{
    const char* buf = (const char*)o[0]; int len = o[9];
    if (!buf || len <= 0) return;
    std::string out; out.reserve(len + 64);
    int n = 0, i = 0;
    while (i < len) {
        int j = i; while (j < len && buf[j] != '\n') ++j;
        int e = (j < len) ? j + 1 : j;
        std::string line(buf + i, e - i);
        i = e;
        if (line.find("\"op_menu_bg\"") != std::string::npos || line.find("\"op_action_list\"") != std::string::npos) {
            size_t cut = line.find("//"); if (cut == std::string::npos) cut = line.size();
            size_t k = cut; while (k > 0 && !isdigit((unsigned char)line[k - 1])) --k;     // end of the last number
            size_t b = k; while (b > 0 && isdigit((unsigned char)line[b - 1])) --b;
            if (b < k) { line.replace(b, k - b, std::to_string(atoi(line.substr(b, k - b).c_str()) + 20)); ++n; }
        }
        out += line;
    }
    g_guiText.swap(out);
    o[0] = o[1] = (int)g_guiText.c_str();
    o[9] = (int)g_guiText.size();
    Log("options: gui.tbl edited (%d Options panel lines one row taller)", n);
}
static int __fastcall Hook_TableOpen(void* self, void*, const char* name)
{
    int r = ((TableOpen_t)0x526F90)(self, name);
    if (r == 0 && name && !_stricmp(name, "skeleton.tbl") && (g_betaDivesOn || g_betaKickOn)) {
        __try { EditSkeletonBuffer((int*)self); } __except (1) { Log("beta: skeleton.tbl edit failed"); }
    }
    if (r == 0 && name && !_stricmp(name, "gui.tbl") && g_optAdded) {
        __try { EditGuiBuffer((int*)self); } __except (1) { Log("options: gui.tbl edit failed"); }
    }
    return r;
}
// v0.64: the two Beta animation switches need the PS2-demo clips in data\perm_mot.vpp (pnsb_* dives,
// pnsh_hrfl_kickdoor.rfa). The tester build does not install them (they come from the player's PS2 demo disc
// later); without them skeleton.tbl would name clips that do not exist. Checked once at start (vpp directory names).
static int g_betaDivesHave = -1, g_betaKickHave = -1;
static void PermMotCheck()
{
    char exe[MAX_PATH]; GetModuleFileNameA(NULL, exe, MAX_PATH); if (char* q = strrchr(exe, '\\')) *q = 0;
    char path[MAX_PATH]; snprintf(path, MAX_PATH, "%s\\dvd_pc\\data\\perm_mot.vpp", exe);
    g_betaDivesHave = g_betaKickHave = 0;
    FILE* f = fopen(path, "rb");
    if (!f) { Log("beta: %s not found - Beta animation switches off", path); return; }
    std::string d; char buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && d.size() < 64u * 1024 * 1024) d.append(buf, n);
    fclose(f);
    for (char& c : d) c = (char)tolower((unsigned char)c);
    g_betaDivesHave = d.find("pnsb_dpst_dive") != std::string::npos;
    g_betaKickHave  = d.find("pnsh_hrfl_kickdoor") != std::string::npos;
    Log("beta: PS2-demo animations in perm_mot.vpp: dives %s, rifle door kick %s", g_betaDivesHave ? "yes" : "NO",
        g_betaKickHave ? "yes" : "NO");
}
static void InstallBeta(const char* ini)
{
    g_betaDives = g_betaDivesOn = GetPrivateProfileIntA("Beta", "Dives", 0, ini) ? 1 : 0;
    g_betaKick  = g_betaKickOn  = GetPrivateProfileIntA("Beta", "RifleDoorKick", 0, ini) ? 1 : 0;
    PermMotCheck();
    if (g_betaDivesOn && !g_betaDivesHave) { g_betaDivesOn = 0; Log("beta: 2004 dives switched on but not installed - left off"); }
    if (g_betaKickOn && !g_betaKickHave)   { g_betaKickOn = 0;  Log("beta: rifle door kick switched on but not installed - left off"); }
    g_betaDigitalAmmo = GetPrivateProfileIntA("Beta", "DigitalAmmo", 0, ini) ? 1 : 0;   // v0.33: live, no restart
    g_hud2004 = GetPrivateProfileIntA("Beta", "Hud2004", 0, ini) ? 1 : 0;   // v0.53
    // v0.57: the weapon half has its own switch; an older ini without the key keeps both halves together
    g_hud2004Wep = GetPrivateProfileIntA("Beta", "Hud2004Weapons", g_hud2004, ini) ? 1 : 0;
    g_betaPausePic = (int)GetPrivateProfileIntA("Beta", "PauseControllerPicture", 0, ini);   // v0.50.9 (v0.50.10: 2 = PS2)
    if (g_betaPausePic < 0 || g_betaPausePic > 2) g_betaPausePic = 0;
    {   // v0.34.4: 0 off, 1 misaligned, 2 aligned. v0.35.2: also accept the names older builds wrote ("Misaligned")
        char v[32]; GetPrivateProfileStringA("Beta", "ComboHud", "0", v, sizeof(v), ini);
        for (char* c = v; *c; ++c) if (*c == ';' || *c == ' ' || *c == '	') { *c = 0; break; }
        g_betaComboHud = !_stricmp(v, "Misaligned") ? 1 : !_stricmp(v, "Aligned") ? 2 : atoi(v);
    }
    if (g_betaComboHud < 0 || g_betaComboHud > 2) g_betaComboHud = 0;
    g_betaComboSplash = GetPrivateProfileIntA("Beta", "ComboHudSplash", 1, ini) ? 1 : 0;
    {
        static const char* k[4] = { "AttachScope", "AttachSilencer", "AttachGrenadeLauncher", "AttachExtendedMagazine" };
        for (int i = 0; i < 4; ++i) g_modAllow[i] = GetPrivateProfileIntA("Weapons", k[i], 1, ini) ? 1 : 0;
        int old = GetPrivateProfileIntA("Weapons", "M16LauncherIcon", 1, ini);   // v0.36 key, still read as the default
        g_hudM16GL = GetPrivateProfileIntA("Beta", "M16LauncherIcon", old, ini) ? 1 : 0;
        g_betaTargetIcon = GetPrivateProfileIntA("Beta", "TargetHostageIcon", 0, ini) ? 1 : 0;
        g_betaLifebar = GetPrivateProfileIntA("Beta", "Lifebar2004", 0, ini) ? 1 : 0;
        // v0.40: Developer page (console switches)
        g_devBigHead      = GetPrivateProfileIntA("Developer", "BigHead", 0, ini) ? 1 : 0;
        g_devForceBW      = GetPrivateProfileIntA("Developer", "ForceBlackWhite", 0, ini) ? 1 : 0;
        g_fakeStickTest   = (int)GetPrivateProfileIntA("Developer", "FakeStickTest", 0, ini);   // 1 = start after 8 s, N > 1 = after N s
        g_bwFilter        = GetPrivateProfileIntA("Display", "BlackWhite", 1, ini) ? 1 : 0;
        g_bwSlaughter     = GetPrivateProfileIntA("Display", "BlackWhiteSlaughter", 1, ini) ? 1 : 0;
        g_shadowDist      = (int)GetPrivateProfileIntA("Display", "ShadowDistance", 12, ini);   // v0.81
#ifdef EC_RELEASE
        g_achOn           = 0;                                  // release: achievements are not part of this release
#else
        g_achOn           = GetPrivateProfileIntA("Achievements", "Enabled", 1, ini) ? 1 : 0;
#endif
        g_semiKickBlood   = GetPrivateProfileIntA("Gore", "SemiKickBlood", 1, ini) ? 1 : 0;
        g_interKillScore  = (int)GetPrivateProfileIntA("Gameplay", "InterrogationKillScore", 0, ini); if (g_interKillScore < 0 || g_interKillScore > 3) g_interKillScore = 0;
        g_fxHalfPixel     = GetPrivateProfileIntA("Display", "EffectPixelFix", 1, ini) ? 1 : 0;
        g_slTileFix       = GetPrivateProfileIntA("Display", "SlaughterTileFix", 1, ini) ? 1 : 0;
        g_lineDiag        = GetPrivateProfileIntA("Developer", "LineDiag", 0, ini) ? 1 : 0;
        g_devPhotoRemote  = GetPrivateProfileIntA("Developer", "PhotoRemote", 0, ini) ? 1 : 0;
        g_devAlwaysGib    = GetPrivateProfileIntA("Developer", "AlwaysGib", 0, ini) ? 1 : 0;   // v0.74: off by default - its entry hooks break the Fusion Fix pattern scan (crash at start)
        g_devMenuPreview  = (int)GetPrivateProfileIntA("Developer", "MenuPreview", -99, ini);
        g_devInvuln       = GetPrivateProfileIntA("Developer", "Invulnerable", 0, ini) ? 1 : 0;
        g_devSlaughter    = GetPrivateProfileIntA("Developer", "SlaughterCheat", 0, ini) ? 1 : 0;
        g_devNoRespawn    = GetPrivateProfileIntA("Developer", "NoRespawn", 0, ini) ? 1 : 0;
        g_devUnlimitedEnv = GetPrivateProfileIntA("Developer", "UnlimitedEnvActions", 0, ini) ? 1 : 0;
        g_devOldReticle   = GetPrivateProfileIntA("Developer", "OldReticle", 0, ini) ? 1 : 0;
        g_devHudOff       = GetPrivateProfileIntA("Developer", "HudOff", 0, ini) ? 1 : 0;
        g_devLogBones     = GetPrivateProfileIntA("Developer", "LogBones", 0, ini) ? 1 : 0;
        g_devHudMap       = GetPrivateProfileIntA("Developer", "HudMap", 0, ini) ? 1 : 0;
        g_devHudKey       = (int)GetPrivateProfileIntA("Developer", "HudToggle", 0, ini);
        if (g_devHudKey < 0 || g_devHudKey > 4) g_devHudKey = 0;
        { char hk[32]; GetPrivateProfileStringA("Developer", "HudToggleKey", "", hk, sizeof(hk), ini);
          Target t = ParseTarget(hk); g_devHudVk = t.kind == T_KEY ? t.value : 0; }
        g_devSongNames    = GetPrivateProfileIntA("Developer", "SongNames", 0, ini) ? 1 : 0;
        g_devCrouchToggle = GetPrivateProfileIntA("Developer", "CrouchToggle", 0, ini) ? 1 : 0;
        g_devZoomToggle   = GetPrivateProfileIntA("Developer", "ZoomToggle", 0, ini) ? 1 : 0;
        g_hudScaleIdx = (int)GetPrivateProfileIntA("Display", "HudSize", 0, ini);            // v0.43 (0 = Auto)
        if (g_hudScaleIdx < 0 || g_hudScaleIdx >= kHudScaleCount) g_hudScaleIdx = 0;
        g_resIdx = (int)GetPrivateProfileIntA("Display", "Resolution", 0, ini);
        if (g_resIdx < 0 || g_resIdx >= kResCount) g_resIdx = 0;
        g_resSaved = g_resIdx;
        ResolutionStartupCheck(ini);   // v0.43.1: a TV resolution must not stop the next handheld start
        { char pv[32];   // v0.49.2 photo mode options
          GetPrivateProfileStringA("Photo", "MoveSpeed", "1.2", pv, sizeof(pv), ini); g_photoMoveSpeed = (float)atof(pv);
          if (g_photoMoveSpeed < 0.2f || g_photoMoveSpeed > 6.0f) g_photoMoveSpeed = 1.2f;
          GetPrivateProfileStringA("Photo", "TurnSpeed", "1.0", pv, sizeof(pv), ini); g_photoTurnSpeed = (float)atof(pv);
          if (g_photoTurnSpeed < 0.2f || g_photoTurnSpeed > 3.0f) g_photoTurnSpeed = 1.0f;
          g_photoInvertY = (int)GetPrivateProfileIntA("Photo", "InvertY", 0, ini) ? 1 : 0; }
        { char sv[32]; GetPrivateProfileStringA("Developer", "Slowmo", "1.0", sv, sizeof(sv), ini);
          g_devSlowmo = (float)atof(sv); if (g_devSlowmo < 1.0f || g_devSlowmo > 4.0f) g_devSlowmo = 1.0f; }
        char v[32];
        GetPrivateProfileStringA("Beta", "LifebarSide", "-0.45", v, sizeof(v), ini); g_lbSide = (float)atof(v);
        GetPrivateProfileStringA("Beta", "LifebarDrop", "0.5", v, sizeof(v), ini); g_lbDrop = (float)atof(v);
    }
    Log("beta: dives %s, rifle door kick %s", g_betaDivesOn ? "2004" : "final", g_betaKickOn ? "on" : "off");
    if (!g_betaDivesOn && !g_betaKickOn) return;
    PatchCall(0x4198C4, (void*)Hook_TableOpen, 0x526F90);
    PatchCall(0x45FE35, (void*)Hook_TableOpen, 0x526F90);
    PatchCall(0x53282A, (void*)Hook_TableOpen, 0x526F90);
}

// Output matches the level-file coordinates, so the line can go straight into a level edit.
static int g_posLogCount = 0;
// v0.33: the controller layout the PC demo showed on its pause screen (pause_controller.tga, int-pause bank - retail
// ships it but never draws it). Menu action: shown full screen until A / B / Start.
// v0.33.2: retail never loads the bank that holds the picture (the PC demo exe loaded "int-pause.peg"), so the
// lookup gave the game's white stand-in texture. Load it with the game's bank loader 0x50C910 (name, 0, 0, -1, 1, 1, 0,
// as the main menu loads its banks) and unload it again with 0x50C8A0 (name, -1) when the picture closes.
static int g_layoutTex = -2;
static void ShowLayout()
{
    int ok = 0;
    __try { ok = ((int (__cdecl*)(const char*, int, int, int, int, int, int))0x50C910)("int-pause.peg", 0, 0, -1, 1, 1, 0); }
    __except (1) { ok = -1; }
    g_layoutTex = -2;                                     // look the picture up again after the load
    Log("layout: int-pause.peg load returned %d", ok);
    g_showLayout = true;
}
static void HideLayout()
{
    g_showLayout = false;
    if (g_pausePicLoaded) return;                         // v0.50.9: the pause screen still shows it
    g_layoutTex = -2;
    __try { ((void (__cdecl*)(const char*, int))0x50C8A0)("int-pause.peg", -1); } __except (1) {}
}
// v0.50.9: keep int-pause.peg loaded while paused in a level with the pause-screen picture on
static void PausePicBank(bool want)
{
    if (want == g_pausePicLoaded) return;
    g_pausePicLoaded = want;
    if (want) {
        int ok = 0;
        __try { ok = ((int (__cdecl*)(const char*, int, int, int, int, int, int))0x50C910)("int-pause.peg", 0, 0, -1, 1, 1, 0); }
        __except (1) { ok = -1; }
        g_layoutTex = -2;
        Log("pause screen picture: int-pause.peg load returned %d", ok);
    } else if (!g_showLayout) {
        g_layoutTex = -2;
        __try { ((void (__cdecl*)(const char*, int))0x50C8A0)("int-pause.peg", -1); } __except (1) {}
    }
}
static void LogPlayerPosition()
{
    float p[3] = {0, 0, 0}, r[9] = {0};
    DWORD e = 0;
    __try {
        e = *(volatile DWORD*)0x6B70FC;
        if (e) { memcpy(p, (const void*)(e + 0x1C), sizeof(p)); memcpy(r, (const void*)(e + 0x28), sizeof(r)); }
    } __except (1) { e = 0; }
    if (!e) { Log("position: player not found"); return; }
    ++g_posLogCount;
    Log("position #%d: pos %.3f %.3f %.3f | rot %.3f %.3f %.3f  %.3f %.3f %.3f  %.3f %.3f %.3f",
        g_posLogCount, p[0], p[1], p[2], r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8]);
    // v0.80: also list the kill spots (Environment_Action, event type 0x33) the game created in this level, nearest
    // first - shows whether a spot added to a level file was loaded at all (Meat1 hook test)
    __try {
        int n = *(volatile int*)0x7EEED8;
        if (n < 0 || n > 8192) { Log("  kill spots: event list not readable"); return; }
        struct S { float d; DWORD ev; float x, y, z; int used, mx; } best[8]; int nb = 0, total = 0;
        for (int i = 0; i < n; ++i) {
            DWORD ev = ((volatile DWORD*)0x7EEEDC)[i];
            if (!ev || *(volatile int*)(ev + 0x48) != 0x33) continue;
            ++total;
            // the spot's position is at +0x7C (glow code 0x46AEA0 compares it with the player's +0x1C)
            float x = *(volatile float*)(ev + 0x7C), y = *(volatile float*)(ev + 0x80), z = *(volatile float*)(ev + 0x84);
            float d = sqrtf((x - p[0]) * (x - p[0]) + (y - p[1]) * (y - p[1]) + (z - p[2]) * (z - p[2]));
            S s = { d, ev, x, y, z, *(volatile int*)(ev + 0x58), *(volatile int*)(ev + 0x54) };
            int k = nb < 8 ? nb++ : 7;
            if (k == 7 && nb == 8 && best[7].d <= d) continue;
            best[k] = s;
            for (; k > 0 && best[k].d < best[k - 1].d; --k) { S t = best[k]; best[k] = best[k - 1]; best[k - 1] = t; }
        }
        Log("  kill spots in this level: %d%s", total, total ? " - nearest:" : "");
        typedef DWORD(__cdecl* EvFromHandle_t)(DWORD);
        for (int i = 0; i < nb; ++i) {
            // trigger zones that list this spot (glow 0x46AEA0 needs one that is on (+0x258 bit 0) with the player in it)
            int zones = 0, on = 0;
            DWORD tr = *(volatile DWORD*)(0x7EFA08 + 0x238);
            for (int guard = 0; tr && tr != 0x7EFA08 && guard < 8192; ++guard, tr = *(volatile DWORD*)(tr + 0x238)) {
                int nl = *(volatile int*)(tr + 0x270); DWORD links = *(volatile DWORD*)(tr + 0x26C);
                for (int j = 0; j < nl && j < 64 && links; ++j) {
                    if (((volatile DWORD*)links)[j * 2 + 1] != 0) continue;
                    if (((EvFromHandle_t)0x4EF5C0)(((volatile DWORD*)links)[j * 2]) == best[i].ev) {
                        ++zones; if (*(volatile BYTE*)(tr + 0x258) & 1) ++on; break;
                    }
                }
            }
            Log("    %.1f m  at %.2f %.2f %.2f  (used %d of %d)  trigger zones %d, on %d", best[i].d, best[i].x, best[i].y,
                best[i].z, best[i].used, best[i].mx, zones, on);
        }
    } __except (1) { Log("  kill spots: could not be read"); }
}

// v0.49.6: "Save position" / "Load position" (Developer page) - skip the walk to a test spot. Same level only.
// The engine keeps 3 copies of an object's position: +0x1C (pos), +0xA8 (last frame's pos), +0x19C (physics pos; the
// mover 0x545820 writes +0x1C and +0x19C together); +0x28 = rotation 3x3; +0x3AC = velocity. Load writes all of them
// for the next frames (camera tick) so the move code cannot pull the player back. Saved in ExtendedCut.ini [Teleport].
static bool  g_tpHave = false;   // g_tpPos / g_tpRot / g_tpFrames: defined above FakeStickTick (v0.66)
static void TpIni(char* ini, size_t n) { snprintf(ini, n, "%s\\ExtendedCut.ini", g_dir); }
static void TeleportWrite()
{
    DWORD e = 0;
    __try {
        e = *(volatile DWORD*)0x6B70FC;
        if (!e) return;
        memcpy((void*)(e + 0x1C), g_tpPos, 12);
        memcpy((void*)(e + 0xA8), g_tpPos, 12);
        memcpy((void*)(e + 0x19C), g_tpPos, 12);
        memcpy((void*)(e + 0x28), g_tpRot, 36);
        memset((void*)(e + 0x3AC), 0, 12);
    } __except (1) { g_tpFrames = 0; Log("teleport: write failed"); }
}
static void TeleportTick()
{
    if (g_tpFrames <= 0) return;
    TeleportWrite();
    if (--g_tpFrames == 0) {
        float p[3] = {0}; DWORD room = 0;
        __try { DWORD e = *(volatile DWORD*)0x6B70FC; if (e) { memcpy(p, (void*)(e + 0x1C), 12); room = *(DWORD*)(e + 0x98); } } __except (1) {}
        Log("teleport: done, now at %.3f %.3f %.3f (room %08X)", p[0], p[1], p[2], room);
    }
}
static void SavePosition()
{
    DWORD e = 0;
    __try {
        e = *(volatile DWORD*)0x6B70FC;
        if (e) { memcpy(g_tpPos, (void*)(e + 0x1C), 12); memcpy(g_tpRot, (void*)(e + 0x28), 36); }
    } __except (1) { e = 0; }
    if (!e) { Log("save position: player not found"); return; }
    g_tpHave = true;
    char ini[MAX_PATH], v[256]; TpIni(ini, sizeof(ini));
    snprintf(v, sizeof(v), "%.4f %.4f %.4f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f %.5f", g_tpPos[0], g_tpPos[1], g_tpPos[2],
             g_tpRot[0], g_tpRot[1], g_tpRot[2], g_tpRot[3], g_tpRot[4], g_tpRot[5], g_tpRot[6], g_tpRot[7], g_tpRot[8]);
    WritePrivateProfileStringA("Teleport", "Saved", v, ini);
    Log("save position: %s", v);
    Rumble(0.3f, 0.3f, 120, "position saved");
}
static void LoadPosition()
{
    if (!g_tpHave) {
        char ini[MAX_PATH], v[256]; TpIni(ini, sizeof(ini));
        GetPrivateProfileStringA("Teleport", "Saved", "", v, sizeof(v), ini);
        float a[12];
        if (sscanf(v, "%f %f %f %f %f %f %f %f %f %f %f %f", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7], &a[8],
                   &a[9], &a[10], &a[11]) == 12) { memcpy(g_tpPos, a, 12); memcpy(g_tpRot, a + 3, 36); g_tpHave = true; }
    }
    if (!g_tpHave) { Log("load position: nothing saved yet"); return; }
    Log("load position: moving to %.3f %.3f %.3f", g_tpPos[0], g_tpPos[1], g_tpPos[2]);
    TeleportWrite();
    g_tpFrames = 5;
    Rumble(0.5f, 0.5f, 150, "position loaded");
}

// ---- items
// ---------------------------------------------------------------- v0.68: achievements (notes\ACHIEVEMENTS_PLAN.md)
// 166 achievements from the PS2 RetroAchievements set (used with the set author's permission - credits). Unlocks are kept
// in ExtendedCut_achievements.ini next to the plugin ([Unlocked] A001 = date); the game save is never written.
// Tracked now: S story (level done) + G gold medal, read from the game's profile in memory, and K "all flashbacks".
// Profile = [0x667618]; per level (levels.tbl order) a 0x1C-byte record at +0x6F44: +0x6F44 times completed,
// +0x6F48 / +0x6F4C / +0x6F50 best score Easy / Normal / Hard (the "new medal" code 0x471AC0 compares these with the
// level's $Medal 1-3 at 0x742174 / 0x742178 / 0x74217C + level * 0x3B8; gold = Hard score >= $Medal 3, Hard only).
// The rest (E / C / P / H / X, comic covers) is "not tracked yet".
#include "achievements_table.h"
static void EnhStatus(const char* s);
static void AchRefreshRows();
static bool g_achGot[kAchCount];
static bool g_achLoaded = false, g_achFirstRun = false;
static int  g_achQueue[16], g_achQN = 0;
static DWORD g_achShowAt = 0;
static int  g_achShowing = -1, g_achSummary = 0;
static void AchPath(char* p) { snprintf(p, MAX_PATH, "%s\\ExtendedCut_achievements.ini", g_dir); }
static bool AchTracked(const AchDef& a) { return a.kind == 'S' || a.kind == 'G' || (a.kind == 'K' && strstr(a.desc, "flashback")); }
static void AchLoad()
{
    char p[MAX_PATH]; AchPath(p);
    g_achFirstRun = GetFileAttributesA(p) == INVALID_FILE_ATTRIBUTES;
    int n = 0;
    for (int i = 0; i < kAchCount; ++i) {
        char k[8], v[40]; snprintf(k, sizeof(k), "A%03d", i + 1);
        GetPrivateProfileStringA("Unlocked", k, "", v, sizeof(v), p);
        g_achGot[i] = v[0] != 0; if (g_achGot[i]) ++n;
    }
    g_achLoaded = true;
    Log("achievements: %d of %d unlocked%s", n, kAchCount, g_achFirstRun ? " (first run - earlier progress unlocks quietly)" : "");
}
static void AchUnlock(int i, bool quiet)
{
    if (i < 0 || i >= kAchCount || g_achGot[i]) return;
    g_achGot[i] = true;
    char p[MAX_PATH]; AchPath(p);
    SYSTEMTIME t; GetLocalTime(&t);
    char k[8], v[48]; snprintf(k, sizeof(k), "A%03d", i + 1);
    snprintf(v, sizeof(v), "%04d-%02d-%02d %02d:%02d%s", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, quiet ? " (earlier progress)" : "");
    WritePrivateProfileStringA("Unlocked", k, v, p);
    Log("achievement %s: \"%s\" (%d) - %s%s", k, kAch[i].title, kAch[i].points, kAch[i].desc, quiet ? " [earlier progress]" : "");
    if (quiet) { ++g_achSummary; return; }
    if (g_achQN < 16) g_achQueue[g_achQN++] = i;
}
static int AchFind(char kind, int level)
{
    for (int i = 0; i < kAchCount; ++i) if (kAch[i].kind == kind && kAch[i].level == level) return i;
    return -1;
}
// Profile name: the .pun file starts "01 00 00 00 | f0 74 00 00 | 20 00 02 00 | name"; in memory the profile struct
// is the file from offset 8 on (the medal code's +0x6F44 = file 0x6F4C), so the name is at +4.
static bool AchProfileName(DWORD prof, char* out, int n)
{
    out[0] = 0;
    const char* s = (const char*)(prof + 4);
    int i = 0;
    for (; i < n - 1 && i < 31 && s[i]; ++i) { if ((BYTE)s[i] < 0x20 || (BYTE)s[i] > 0x7E) { out[0] = 0; return false; } out[i] = s[i]; }
    out[i] = 0;
    return i > 0;
}
static void AchTick()
{
    static DWORD last = 0;
    static char lastProf[40] = "";
    if (!g_achOn) return;
    if (!g_achLoaded) AchLoad();
    if (GetTickCount() - last < 1000) return;
    last = GetTickCount();
    bool quiet = false;
    char ini[MAX_PATH]; AchPath(ini);
    char name[40] = "";
    __try {
        DWORD prof = *(volatile DWORD*)0x667618;
        if (prof && AchProfileName(prof, name, sizeof(name))) {
            // first time this profile is seen: what it already earned unlocks quietly (one summary pop-up)
            char seen[8]; GetPrivateProfileStringA("Profiles", name, "", seen, sizeof(seen), ini);
            quiet = seen[0] == 0;
            if (strcmp(name, lastProf)) { strncpy(lastProf, name, sizeof(lastProf) - 1); Log("achievements: profile \"%s\"%s", name, quiet ? " - first time, earlier progress unlocks quietly" : ""); }
            for (int L = 0; L < 16; ++L) {
                const volatile int* r = (const volatile int*)(prof + 0x6F44 + L * 0x1C);   // L = levels.tbl index
                int done = r[0], hard = r[3];
                if (done < 0 || done > 100000) continue;               // not a profile (yet)
                if (done > 0) AchUnlock(AchFind('S', L), quiet);
                int gold = *(volatile int*)(0x74217C + L * 0x3B8);
                if (done > 0 && gold > 0 && hard >= gold) AchUnlock(AchFind('G', L), quiet);
            }
        }
        DWORD pl = *(volatile DWORD*)0x7CF804;
        if (pl && name[0]) {                                          // all flashbacks: every id the levels use is seen
            bool all = true; int need = 0;
            for (int L = 0; L < 16 && all; ++L)
                for (int s = 0; s < 2; ++s) {
                    int id = *(volatile int*)(0x7421B0 + L * 0x3B8 + s * 4);
                    if (id < 0 || id > 23) continue;
                    ++need;
                    if (*(volatile BYTE*)(pl + 0x3C + id) == 0) { all = false; break; }
                }
            if (all && need > 0)
                for (int i = 0; i < kAchCount; ++i) if (kAch[i].kind == 'K' && strstr(kAch[i].desc, "flashback")) AchUnlock(i, quiet);
        }
    } __except (1) {}
    if (quiet && name[0]) {                                           // this profile is checked: from now on, pop-ups
        WritePrivateProfileStringA("Profiles", name, "1", ini);
        if (g_achSummary > 0) Log("achievements: %d unlocked from earlier progress", g_achSummary);
    }
    AchRefreshRows();
}
// menu page "Achievements": one row per level (+ whole game) - label = counts, help = the next one to get
static char g_achRowLabel[18][72], g_achRowHelp[18][220];
static void AchRefreshRows()
{
    int tot = 0, totGot = 0, pts = 0, ptsGot = 0;
    for (int i = 0; i < kAchCount; ++i) { ++tot; pts += kAch[i].points; if (g_achGot[i]) { ++totGot; ptsGot += kAch[i].points; } }
    snprintf(g_achRowLabel[0], 72, "Total %d / %d  (%d / %d points)", totGot, tot, ptsGot, pts);
    snprintf(g_achRowHelp[0], 220, "Go down to a level, then left / right to see each of its achievements. Tracked so far: level done, gold medal, all flashbacks - the rest come in later versions");
    for (int L = -1; L < 16; ++L) {
        int row = L < 0 ? 17 : L + 1, n = 0, got = 0, nextI = -1;
        for (int i = 0; i < kAchCount; ++i) if (kAch[i].level == L) {
            ++n; if (g_achGot[i]) ++got;
            else if (nextI < 0 || (AchTracked(kAch[i]) && !AchTracked(kAch[nextI]))) nextI = i;
        }
        snprintf(g_achRowLabel[row], 72, "%s  %d / %d", L < 0 ? "Whole game" : kAchLevelNames[L], got, n);
        if (nextI < 0) snprintf(g_achRowHelp[row], 220, "All done!");
        else snprintf(g_achRowHelp[row], 220, "Next: %s - %s%s", kAch[nextI].title, kAch[nextI].desc, AchTracked(kAch[nextI]) ? "" : " (not tracked yet)");
    }
}
template <int L> static void AchRowAct()
{
    int got = 0, n = 0; char m[96];
    for (int i = 0; i < kAchCount; ++i) if (kAch[i].level == L) { ++n; if (g_achGot[i]) ++got; }
    snprintf(m, sizeof(m), "%s: %d of %d unlocked", L < 0 ? "Whole game" : kAchLevelNames[L], got, n);
    EnhStatus(m);
}
static void AchRowTotal() { AchRefreshRows(); EnhStatus("Unlocks are saved in ExtendedCut_achievements.ini (next to the plugin)"); }
// v0.72: two screens. The Achievements tab = one row per level with its count; A on a level opens that LEVEL PAGE
// (g_achDetail = level): every achievement with a status column, up / down picks one, its description is shown under
// the list, left / right = previous / next level, A or B = back to the level list.
static int g_achPick[18];
static int g_achDetail = -2;                              // -2 = level list, -1..15 = that level's page
static int AchRowLevel(const char* label)               // -1..15 = level row, -2 = not an achievement level row
{
    for (int r = 1; r < 18; ++r) if (label == g_achRowLabel[r]) return r == 17 ? -1 : r - 1;
    return -2;
}
static int AchOfLevel(int L, int* out, int max)
{
    int n = 0;
    for (int i = 0; i < kAchCount && n < max; ++i) if (kAch[i].level == L) out[n++] = i;
    return n;
}
static int AchRowOf(int L) { return L < 0 ? 17 : L + 1; }
static void AchPickMove(int L, int d)
{
    int ids[40]; int n = AchOfLevel(L, ids, 40); if (n <= 0) return;
    int& k = g_achPick[AchRowOf(L)];
    k = ((k + d) % n + n) % n;
}
static void AchHelpFor(int L, char* buf, int sz)
{
    int ids[40]; int n = AchOfLevel(L, ids, 40);
    if (n <= 0) { snprintf(buf, sz, "No achievements here"); return; }
    int k = g_achPick[AchRowOf(L)]; if (k < 0 || k >= n) k = 0;
    const AchDef& a = kAch[ids[k]];
    snprintf(buf, sz, "%d of %d: %s (%d points) - %s. %s%s   Left / right: next one", k + 1, n, a.title, a.points, a.desc,
             g_achGot[ids[k]] ? "GOT IT" : "Not yet",
             (!g_achGot[ids[k]] && !AchTracked(a)) ? " (not tracked yet - comes in a later version)" : "");
}
enum { EK_FLOAT, EK_INT, EK_BOOL, EK_TOGGLE, EK_STYLE, EK_ACTION, EK_BIND, EK_BIND_HOSTAGE, EK_HEADER };
struct EnhItem {
    int page; const char* label; int kind; void* ptr;
    float lo, hi, step; int dec;
    const char* section; const char* key; const char* help;
    int pad = -1;                  // v0.22: the pad button of a binding row (its icon is drawn before the name)
};
// v0.56: 5 tabs (+ Cheats & Debug with Developer mode on); sections fold open / closed
static const char* kEnhPages[] = { "Controls", "Gameplay", "Gore", "HUD & Display", "Restored content", "Achievements", "Cheats & Debug" };
static const int kEnhPageCount = 7;
static int g_devMode = 0;                 // [Enhancements] DeveloperMode: shows the Cheats & Debug tab

// v0.22 in-game rebinding: what a pad button can do = which PC key / mouse input it sends. The names are the
// game's default PC bindings (as in the ini comments); if you changed a key in the game's own controls menu
// the name can be wrong, but the button still sends the key shown in the help line.
struct PadAction { const char* label; const char* token; const char* keyName; Target t; };
static PadAction kActions[] = {
    { "Nothing",                     "NONE",      "nothing" },
    { "Fire",                        "MOUSE1",    "left mouse button" },
    { "Alt fire / throw",            "MOUSE2",    "right mouse button" },
    { "Left-hand fire / throw held", "Z",         "key Z" },
    { "Use / grab / interrogate",    "E",         "key E" },
    { "Quick kill",                  "Q",         "key Q" },
    { "Pick up weapon / item",       "G",         "key G" },
    { "Slaughter mode",              "TAB",       "Tab" },
    { "Crouch",                      "C",         "key C" },
    { "Fine aim (zoom)",             "F",         "key F" },
    { "Jump / dodge",                "SPACE",     "Space" },
    { "Reload",                      "R",         "key R" },
    { "Knock out / toss shield",     "N",         "key N" },
    { "Pistol",                      "1",         "key 1" },
    { "Rifle",                       "2",         "key 2" },
    { "Holster",                     "H",         "key H" },
    { "Next weapon",                 "T",         "key T" },
    { "Help",                        "J",         "key J (HelpKey)" },
    { "Mouse wheel up",              "WHEELUP",   "mouse wheel up" },
    { "Mouse wheel down",            "WHEELDOWN", "mouse wheel down" },
    { "Middle mouse button",         "MOUSE3",    "middle mouse button" },
    { "In-game menu",                "ESCAPE",    "Escape" },
    { "Move forward",                "W",         "key W" },
    { "Move back",                   "S",         "key S" },
    { "Step left",                   "A",         "key A" },
    { "Step right",                  "D",         "key D" },
};
static const int kActionCount = (int)(sizeof(kActions) / sizeof(kActions[0]));
static void InitActions() { for (auto& a : kActions) a.t = ParseTarget(a.token); }
static int ActionOf(const Target& t)
{
    for (int i = 0; i < kActionCount; ++i)
        if (kActions[i].t.kind == t.kind && (t.kind == T_NONE || kActions[i].t.value == t.value)) return i;
    return -1;                                     // something else from the ini - shown as "Other", kept
}
static bool SameTarget(const Target& a, const Target& b) { return a.kind == b.kind && (a.kind == T_NONE || a.value == b.value); }
static const char* kStyleShow[] = { "Keyboard", "Xbox", "PlayStation", "Steam Deck", "Auto" };
static void GorePresetOriginal(); static void GorePresetRecommended(); static void GorePresetMaximum();   // v0.52
static const EnhItem kEnh[] = {
    { 0, "Camera", EK_HEADER, nullptr, 0, 0, 0, 0, nullptr, nullptr, nullptr },
    { 0, "Look sensitivity",      EK_FLOAT, &g_cfg.lookScale, 0.10f, 3.0f, 0.01f, 2, "Settings", "LookSensitivity", "Right-stick camera speed (1.00 = your LookSensitivityX / Y)" },
    { 0, "Invert up/down",        EK_BOOL,  &g_cfg.invertY, 0, 1, 1, 0, "Settings", "InvertY", "Right stick up looks down" },
    { 0, "Triggers and buttons", EK_HEADER, nullptr, 0, 0, 0, 0, nullptr, nullptr, nullptr },
    { 0, "Trigger threshold",     EK_FLOAT, &g_cfg.triggerThreshold, 0.05f, 0.95f, 0.05f, 2, "Settings", "TriggerThreshold", "How far a trigger must be pulled to count" },
    { 0, "Crouch: press to toggle", EK_TOGGLE, &g_devCrouchToggle, 0, 1, 1, 0, "Developer", "CrouchToggle", "On = crouch button toggles crouching instead of holding (console toggle_crouch_mode)" },
    { 0, "Aim zoom: press to toggle", EK_TOGGLE, &g_devZoomToggle, 0, 1, 1, 0, "Developer", "ZoomToggle", "On = zoom button toggles instead of holding (console toggle_zoom_mode)" },
    { 0, "Console aim assist",    EK_TOGGLE, &g_aimAssist, 0, 1, 1, 0, "Settings", "AimAssist", "Like the Xbox / PS2: aim slows on an enemy, follows him a little, faster turning at full tilt. Works right away" },
    { 0, "Interrogation", EK_HEADER, nullptr, 0, 0, 0, 0, nullptr, nullptr, nullptr },
    { 0, "Console stick (like PS2)", EK_TOGGLE, &g_cfg.consoleStick, 0, 1, 1, 0, "Settings", "InterrogationConsoleStick", "Stick position = force, like the Xbox / PS2. Off = old mouse-style force" },
    { 0, "Left stick too",        EK_TOGGLE, &g_cfg.interrogationLeftStick, 0, 1, 1, 0, "Settings", "InterrogationLeftStick", "Like the Xbox: either stick works, the one moved last wins" },
    { 0, "Rumble", EK_HEADER, nullptr, 0, 0, 0, 0, nullptr, nullptr, nullptr },
    { 0, "Rumble",                EK_TOGGLE, &g_cfg.rumble, 0, 1, 1, 0, "Rumble", "Enabled", "All rumble on / off (details in ExtendedCut.ini [Rumble])" },
    { 0, "Strength",              EK_FLOAT, &g_cfg.rumbleStrength, 0, 2, 0.1f, 1, "Rumble", "Strength", "Scales every rumble (try 0.6 if it is too strong)" },
    { 0, "Button prompts", EK_HEADER, nullptr, 0, 0, 0, 0, nullptr, nullptr, nullptr },
    { 0, "Button prompts",        EK_STYLE, &g_cfg.promptStyle, 0, 4, 1, 0, "Prompts", "Style", "Icons and the matching stick picture; Keyboard = key names and W/A/S/D" },
    { 0, "Button layout", EK_HEADER, nullptr, 0, 0, 0, 0, nullptr, nullptr, nullptr },
    { 0, "A", EK_BIND, &g_cfg.map[P_SOUTH], 0, 0, 0, 0, "Buttons", "A", nullptr, P_SOUTH },
    { 0, "B", EK_BIND, &g_cfg.map[P_EAST], 0, 0, 0, 0, "Buttons", "B", nullptr, P_EAST },
    { 0, "X", EK_BIND, &g_cfg.map[P_WEST], 0, 0, 0, 0, "Buttons", "X", nullptr, P_WEST },
    { 0, "Y", EK_BIND, &g_cfg.map[P_NORTH], 0, 0, 0, 0, "Buttons", "Y", nullptr, P_NORTH },
    { 0, "LB", EK_BIND, &g_cfg.map[P_LB], 0, 0, 0, 0, "Buttons", "LB", nullptr, P_LB },
    { 0, "RB", EK_BIND, &g_cfg.map[P_RB], 0, 0, 0, 0, "Buttons", "RB", nullptr, P_RB },
    { 0, "LT", EK_BIND, &g_cfg.map[P_LT], 0, 0, 0, 0, "Buttons", "LT", nullptr, P_LT },
    { 0, "RT", EK_BIND, &g_cfg.map[P_RT], 0, 0, 0, 0, "Buttons", "RT", nullptr, P_RT },
    { 0, "L3", EK_BIND, &g_cfg.map[P_L3], 0, 0, 0, 0, "Buttons", "L3", nullptr, P_L3 },
    { 0, "R3", EK_BIND, &g_cfg.map[P_R3], 0, 0, 0, 0, "Buttons", "R3", nullptr, P_R3 },
    { 0, "Back", EK_BIND, &g_cfg.map[P_BACK], 0, 0, 0, 0, "Buttons", "Back", nullptr, P_BACK },
    { 0, "D-pad up", EK_BIND, &g_cfg.map[P_DUP], 0, 0, 0, 0, "Buttons", "DpadUp", nullptr, P_DUP },
    { 0, "D-pad down", EK_BIND, &g_cfg.map[P_DDOWN], 0, 0, 0, 0, "Buttons", "DpadDown", nullptr, P_DDOWN },
    { 0, "D-pad left", EK_BIND, &g_cfg.map[P_DLEFT], 0, 0, 0, 0, "Buttons", "DpadLeft", nullptr, P_DLEFT },
    { 0, "D-pad right", EK_BIND, &g_cfg.map[P_DRIGHT], 0, 0, 0, 0, "Buttons", "DpadRight", nullptr, P_DRIGHT },
    { 0, "While holding a hostage", EK_HEADER, nullptr, 0, 0, 0, 0, nullptr, nullptr, nullptr },
    { 0, "LB", EK_BIND_HOSTAGE, &g_cfg.hostageMap[P_LB], 0, 0, 0, 0, "Hostage", "LB", nullptr, P_LB },   // console: Reload
    { 0, "LT", EK_BIND_HOSTAGE, &g_cfg.hostageMap[P_LT], 0, 0, 0, 0, "Hostage", "LT", nullptr, P_LT },   // console: Toss the enemy
    { 0, "RT", EK_BIND_HOSTAGE, &g_cfg.hostageMap[P_RT], 0, 0, 0, 0, "Hostage", "RT", nullptr, P_RT },   // console: Fire
    { 0, "A", EK_BIND_HOSTAGE, &g_cfg.hostageMap[P_SOUTH], 0, 0, 0, 0, "Hostage", "A", nullptr, P_SOUTH },   // console: Interrogate
    { 0, "X", EK_BIND_HOSTAGE, &g_cfg.hostageMap[P_WEST], 0, 0, 0, 0, "Hostage", "X", nullptr, P_WEST },   // console: Quick kill
    { 0, "Y", EK_BIND_HOSTAGE, &g_cfg.hostageMap[P_NORTH], 0, 0, 0, 0, "Hostage", "Y", nullptr, P_NORTH },   // console: Knock out
    { 1, "Score", EK_HEADER, nullptr, 0, 0, 0, 0, nullptr, nullptr, nullptr },
    { 1, "Interrogation kill score", EK_INT, &g_interKillScore, 0, 3, 1, 0, "Gameplay", "InterrogationKillScore", "Killing during an interrogation: Unchanged (game / other mods decide), Penalty (original), No penalty, or Bonus (same points as a gain)" },
    { 1, "Weapon upgrades (next level / new weapon)", EK_HEADER, nullptr, 0, 0, 0, 0, nullptr, nullptr, nullptr },
    { 1, "Scope",                 EK_TOGGLE, &g_modAllow[0], 0, 1, 1, 0, "Weapons", "AttachScope", "Off = the bought scope upgrade is not attached (M16, P90, .357). Applies when a weapon is given" },
    { 1, "Silencer",              EK_TOGGLE, &g_modAllow[1], 0, 1, 1, 0, "Weapons", "AttachSilencer", "Off = no silencer on the Colt 1911 / Mac 10 / P90 even when bought" },
    { 1, "Grenade launcher",      EK_TOGGLE, &g_modAllow[2], 0, 1, 1, 0, "Weapons", "AttachGrenadeLauncher", "Off = the M16 stays a plain M16 even when the launcher upgrade is bought" },
    { 1, "Extended magazine",     EK_TOGGLE, &g_modAllow[3], 0, 1, 1, 0, "Weapons", "AttachExtendedMagazine", "Off = normal magazines on the AK103 / OA-93 / USAS12 / MP5" },
    { 1, "Sound", EK_HEADER, nullptr, 0, 0, 0, 0, nullptr, nullptr, nullptr },
    { 1, "Show song titles",      EK_TOGGLE, &g_devSongNames, 0, 1, 1, 0, "Developer", "SongNames", "'Current song: ...' message when the music changes (console display_music_name)" },
    { 1, "Slaughter flashback whispers", EK_TOGGLE, &g_whispers, 0, 1, 1, 0, "Beta", "FlashbackWhispers", "Restored: the whispered flashback lines play during Slaughter mode (in the game files, never played by retail). From the next Slaughter mode" },
    { 1, "Interrogation camera", EK_HEADER, nullptr, 0, 0, 0, 0, nullptr, nullptr, nullptr },
    { 1, "Gun tension camera",    EK_INT,   &g_gunCam, 0, 3, 1, 0, "Settings", "GunInterrogationCamera", "Retail / your over-the-shoulder + front / Oct 2004 PS2-demo original + front / Oct 2004 only. From the next gun interrogation" },
    { 1, "Photo mode: L3 + R3 in game (X saves the camera to the log)", EK_HEADER, nullptr, 0, 0, 0, 0, nullptr, nullptr, nullptr },
    { 1, "Photo move speed",      EK_FLOAT, &g_photoMoveSpeed, 0.2f, 6.0f, 0.2f, 1, "Photo", "MoveSpeed", "Free camera move speed in metres a second (LB = slow, RB = fast)" },
    { 1, "Photo turn speed",      EK_FLOAT, &g_photoTurnSpeed, 0.2f, 3.0f, 0.1f, 1, "Photo", "TurnSpeed", "Free camera look speed (LB = fine)" },
    { 1, "Photo invert up/down",  EK_TOGGLE, &g_photoInvertY, 0, 1, 1, 0, "Photo", "InvertY", "On = right stick up looks down" },
    { 1, "Developer", EK_HEADER, nullptr, 0, 0, 0, 0, nullptr, nullptr, nullptr },
    { 1, "Developer mode",        EK_TOGGLE, &g_devMode, 0, 1, 1, 0, "Enhancements", "DeveloperMode", "On = the Cheats & Debug tab (console cheats, position save / load, logging) is shown" },
    { 2, "Presets", EK_HEADER, nullptr, 0, 0, 0, 0, nullptr, nullptr, nullptr },
    { 2, "Preset: Original gore",  EK_ACTION, (void*)GorePresetOriginal, 0, 0, 0, 0, nullptr, nullptr, "Sets every row below to the game's own behaviour" },
    { 2, "Preset: Recommended",    EK_ACTION, (void*)GorePresetRecommended, 0, 0, 0, 0, nullptr, nullptr, "Extended Cut defaults: more blood, head wounds, critical hits 25 %" },
    { 2, "Preset: Maximum gore",   EK_ACTION, (void*)GorePresetMaximum, 0, 0, 0, 0, nullptr, nullptr, "Everything at its bloodiest" },
    { 2, "Heads", EK_HEADER, nullptr, 0, 0, 0, 0, nullptr, nullptr, nullptr },
    { 2, "Exploding heads",       EK_INT, &g_goreHeads, 0, 2, 1, 0, "Gore", "ExplodingHeads", "Gun quick kills. No gib = entry + exit wound. Slaughter kills always gib" },
    { 2, "Gib stumps",            EK_INT, &g_goreStumps, 0, 2, 1, 0, "Gore", "Stumps", "Original = the game's flat cones. Restored = the developers' torn stumps with bone (they ship turned inward). Random = restored + bloodier + slim / cone legs + tucked arms, a different one each gib" },
    { 2, "Wound variation",       EK_TOGGLE, &g_goreVary, 0, 1, 1, 0, "Gore", "WoundVariation", "On = head wounds shift a little each kill. Off = fixed spots" },
    { 2, "Gunfire head gibs",     EK_INT, &g_goreGunHeads, 0, 3, 1, 0, "Gore", "GunfireHeads", "Normal (game: only some guns), Random 50-50, Off, or Critical hits = a head-shot kill with ANY gun can explode the head" },
    { 2, "Critical hit chance %", EK_INT, &g_goreCritPct, 5, 100, 5, 0, "Gore", "CritChance", "With Critical hits: the chance that a head-shot kill explodes the head" },
    { 2, "Blood", EK_HEADER, nullptr, 0, 0, 0, 0, nullptr, nullptr, nullptr },
    { 2, "Blood on clothes",      EK_TOGGLE, &g_goreClothes, 0, 1, 1, 0, "Gore", "BloodOnClothes", "After a head gib: blood on the chest, a shoulder and the upper back" },
    { 2, "Quick-kill wounds",     EK_TOGGLE, &g_goreKillWounds, 0, 1, 1, 0, "Gore", "KillWounds", "Knife / bash quick kills: 2+ blood marks at the right spots" },
    { 2, "Quick-kill blood layers", EK_INT, &g_decalLayers, 1, 4, 1, 0, "Settings", "DecalLayers", "1 = normal, 2-4 = bloodier (needs the 4 MB decal exe)" },
    { 2, "Floor / wall blood",    EK_INT, &g_goreWorld, 0, 2, 1, 0, "Gore", "WorldBlood", "How much blood stays on floors and walls. Next level load" },
    { 2, "Bullet hit blood",      EK_INT, &g_goreBullet, 0, 3, 1, 0, "Gore", "BulletHitBlood", "Mark at each bullet hit: Normal, Normal + splat, Bigger 2.5x, Heavy 3x + splat" },
    { 3, "HUD", EK_HEADER, nullptr, 0, 0, 0, 0, nullptr, nullptr, nullptr },
    { 3, "HUD size",              EK_INT, &g_hudScaleIdx, 0, 8, 1, 0, "Display", "HudSize", "Auto = same HUD share of the screen in handheld and on a TV. Works right away" },
    { 3, "HUD off",               EK_TOGGLE, &g_devHudOff, 0, 1, 1, 0, "Developer", "HudOff", "Hide the whole HUD - for screenshots (console hud_off)" },
    { 3, "HUD on / off button",   EK_INT, &g_devHudKey, 0, 4, 1, 0, "Developer", "HudToggle", "Hold LB + this D-pad direction to hide / show the HUD. Or any keyboard key: ExtendedCut.ini [Developer] HudToggleKey=F8" },
    { 3, "Reticle only when aiming", EK_TOGGLE, &g_devOldReticle, 0, 1, 1, 0, "Developer", "OldReticle", "The reticle shows only while you zoom-aim (console Old_style_reticle - same picture, no other style)" },
    { 3, "Screen", EK_HEADER, nullptr, 0, 0, 0, 0, nullptr, nullptr, nullptr },
    { 3, "Black & white kill effects", EK_TOGGLE, &g_bwFilter, 0, 1, 1, 0, "Display", "BlackWhite", "On = the game's black & white screen when an environment interrogation kills. Off = in colour. Works right away" },
    { 3, "Black & white Slaughter mode", EK_TOGGLE, &g_bwSlaughter, 0, 1, 1, 0, "Display", "BlackWhiteSlaughter", "On = Slaughter mode turns the screen black & white (the game's look). Off = in colour. Works right away" },
    { 3, "Shadow distance (m)",   EK_INT, &g_shadowDist, 5, 40, 1, 0, "Display", "ShadowDistance", "How far from the camera Frank and others get a real shadow (further = the faint round one). Game = 5. Raise it for a wide FOV or a far camera. Works right away" },
    { 3, "Level memory MB (restart)", EK_INT, &g_levelPoolMB, 0, 256, 32, 0, "Settings", "LevelPoolMB", "0 = the game's 32.5 MB per level. More = room for smooth blacks / HD textures (a level that does not fit hangs on loading). Default 128. Used the next time the game starts" },
    { 3, "Windowed (restart)",    EK_TOGGLE, &g_windowed, 0, 1, 1, 0, "Display", "Windowed", "Run in a window instead of full screen (the PC demo 1.00 had it). Not for the Steam Deck. Used the next time the game starts" },
    { 3, "Resolution (restart)",  EK_INT, &g_resIdx, 0, 10, 1, 0, "Display", "Resolution", "Saved to the Widescreen Fix ini. Used the next time the game starts" },
    { 4, "Restart the game after a change", EK_HEADER, nullptr, 0, 0, 0, 0, nullptr, nullptr, nullptr },
    { 4, "2004 dives (dive to slide)", EK_TOGGLE, &g_betaDives, 0, 1, 1, 0, "Beta", "Dives", "On = PS2-demo dives for all weapons. Off = final dives (roll). Restart the game to apply" },
    { 4, "Rifle door kick",       EK_TOGGLE, &g_betaKick, 0, 1, 1, 0, "Beta", "RifleDoorKick", "On = cut kick while holding a rifle (PS2 demo). Restart the game to apply" },
    { 4, "Works right away", EK_HEADER, nullptr, 0, 0, 0, 0, nullptr, nullptr, nullptr },
    { 4, "Always play flashbacks", EK_TOGGLE, &g_alwaysFlashbacks, 0, 1, 1, 0, "Beta", "AlwaysFlashbacks", "Flashbacks you already unlocked play again (during an interrogation of a thug from that flashback's gang, once per level, like the first time). Your Extras gallery is not changed" },
    { 4, "Early dev HUD: health + slaughter", EK_TOGGLE, &g_hud2004, 0, 1, 1, 0, "Beta", "Hud2004", "Josh Nizzi dev grabs: the dev skull, faint-framed dingy blue health + solid grey bar (slaughter fills red). Needs the early development HUD mini.vpp. Works right away" },
    { 4, "Early dev HUD: weapons", EK_TOGGLE, &g_hud2004Wep, 0, 1, 1, 0, "Beta", "Hud2004Weapons", "Dev brackets, the dev M16, every weapon in the dev surface, 7-segment clip + reserve numbers, no small holstered picture. Needs the early development HUD mini.vpp. Works right away" },
    { 4, "Controller pause screen", EK_INT, &g_betaPausePic, 0, 2, 1, 0, "Beta", "PauseControllerPicture", "The pause screen (Start) becomes the console controller picture: Xbox (in the PC files, unused) or PS2 (needs the PS2 misc.vpp). Works right away" },
    { 4, "Digital ammo counter",  EK_TOGGLE, &g_betaDigitalAmmo, 0, 1, 1, 0, "Beta", "DigitalAmmo", "On = ammo counter in the unused 7-segment font (beta HUD). Works right away" },
    { 4, "2004 score display",    EK_INT, &g_betaComboHud, 0, 2, 1, 0, "Beta", "ComboHud", "TOTAL / COMBO / BODY COUNT instead of the skull score. Misaligned = like the 2004 shots, Aligned = tidy" },
    { 4, "  Smoke behind it", EK_TOGGLE, &g_betaComboSplash, 0, 1, 1, 0, "Beta", "ComboHudSplash", "Faint grey smoke behind the 2004 score - the same backing the weapon / ammo counter has" },
    { 4, "2004 M16 launcher icon", EK_TOGGLE, &g_hudM16GL, 0, 1, 1, 0, "Beta", "M16LauncherIcon", "HUD icon and ammo pickup card show the M16 with the M203 (2004 beta HUD) when the launcher is attached. Needs the HUD icons mini.vpp. Works right away" },
    { 4, "2004 hostage icon (target)", EK_TOGGLE, &g_betaTargetIcon, 0, 1, 1, 0, "Beta", "TargetHostageIcon", "Human-shield HUD icon becomes the 2004 target cutout. Needs the HUD icons mini.vpp. Works right away" },
    { 4, "2004 enemy health bar", EK_TOGGLE, &g_betaLifebar, 0, 1, 1, 0, "Beta", "Lifebar2004", "Enemy health bar in the striped 2004 style, standing upright beside the enemy. Needs the HUD icons mini.vpp. Works right away" },
    { 5, "Achievements",          EK_TOGGLE, &g_achOn, 0, 1, 1, 0, "Achievements", "Enabled", "On = unlock achievements while you play (pop-up + this page). Your game save is not changed" },
    { 5, g_achRowLabel[0],        EK_ACTION, (void*)AchRowTotal, 0, 0, 0, 0, nullptr, nullptr, g_achRowHelp[0] },
    { 5, g_achRowLabel[1], EK_ACTION, (void*)AchRowAct<0>, 0, 0, 0, 0, nullptr, nullptr, g_achRowHelp[1] },
    { 5, g_achRowLabel[2], EK_ACTION, (void*)AchRowAct<1>, 0, 0, 0, 0, nullptr, nullptr, g_achRowHelp[2] },
    { 5, g_achRowLabel[3], EK_ACTION, (void*)AchRowAct<2>, 0, 0, 0, 0, nullptr, nullptr, g_achRowHelp[3] },
    { 5, g_achRowLabel[4], EK_ACTION, (void*)AchRowAct<3>, 0, 0, 0, 0, nullptr, nullptr, g_achRowHelp[4] },
    { 5, g_achRowLabel[5], EK_ACTION, (void*)AchRowAct<4>, 0, 0, 0, 0, nullptr, nullptr, g_achRowHelp[5] },
    { 5, g_achRowLabel[6], EK_ACTION, (void*)AchRowAct<5>, 0, 0, 0, 0, nullptr, nullptr, g_achRowHelp[6] },
    { 5, g_achRowLabel[7], EK_ACTION, (void*)AchRowAct<6>, 0, 0, 0, 0, nullptr, nullptr, g_achRowHelp[7] },
    { 5, g_achRowLabel[8], EK_ACTION, (void*)AchRowAct<7>, 0, 0, 0, 0, nullptr, nullptr, g_achRowHelp[8] },
    { 5, g_achRowLabel[9], EK_ACTION, (void*)AchRowAct<8>, 0, 0, 0, 0, nullptr, nullptr, g_achRowHelp[9] },
    { 5, g_achRowLabel[10], EK_ACTION, (void*)AchRowAct<9>, 0, 0, 0, 0, nullptr, nullptr, g_achRowHelp[10] },
    { 5, g_achRowLabel[11], EK_ACTION, (void*)AchRowAct<10>, 0, 0, 0, 0, nullptr, nullptr, g_achRowHelp[11] },
    { 5, g_achRowLabel[12], EK_ACTION, (void*)AchRowAct<11>, 0, 0, 0, 0, nullptr, nullptr, g_achRowHelp[12] },
    { 5, g_achRowLabel[13], EK_ACTION, (void*)AchRowAct<12>, 0, 0, 0, 0, nullptr, nullptr, g_achRowHelp[13] },
    { 5, g_achRowLabel[14], EK_ACTION, (void*)AchRowAct<13>, 0, 0, 0, 0, nullptr, nullptr, g_achRowHelp[14] },
    { 5, g_achRowLabel[15], EK_ACTION, (void*)AchRowAct<14>, 0, 0, 0, 0, nullptr, nullptr, g_achRowHelp[15] },
    { 5, g_achRowLabel[16], EK_ACTION, (void*)AchRowAct<15>, 0, 0, 0, 0, nullptr, nullptr, g_achRowHelp[16] },
    { 5, g_achRowLabel[17], EK_ACTION, (void*)AchRowAct<-1>, 0, 0, 0, 0, nullptr, nullptr, g_achRowHelp[17] },
    { 6, "Cheats (developer console switches)", EK_HEADER, nullptr, 0, 0, 0, 0, nullptr, nullptr, nullptr },
    { 6, "Invulnerable",          EK_TOGGLE, &g_devInvuln, 0, 1, 1, 0, "Developer", "Invulnerable", "The Punisher takes no damage (console invulnerable)" },
    { 6, "Slaughter meter full",  EK_TOGGLE, &g_devSlaughter, 0, 1, 1, 0, "Developer", "SlaughterCheat", "Slaughter Mode meter always full (console slaughter_cheat)" },
    { 6, "Unlimited env. kills",  EK_TOGGLE, &g_devUnlimitedEnv, 0, 1, 1, 0, "Developer", "UnlimitedEnvActions", "Environment interrogations AND environment quick kills (fryer, stove, fuse box ...) can be used again" },
    { 6, "No enemy respawn",      EK_TOGGLE, &g_devNoRespawn, 0, 1, 1, 0, "Developer", "NoRespawn", "Enemy spawners stop making new enemies (console no_respawn)" },
    { 6, "Big head mode",         EK_TOGGLE, &g_devBigHead, 0, 1, 1, 0, "Developer", "BigHead", "Everyone's head x2.5 (console Big_head_mode). Works right away" },
    { 6, "Slow motion",           EK_FLOAT, &g_devSlowmo, 1.0f, 4.0f, 0.25f, 2, "Developer", "Slowmo", "1.00 = normal speed, bigger = slower (console slowmo). Quick kills reset it" },
    { 6, "Testing", EK_HEADER, nullptr, 0, 0, 0, 0, nullptr, nullptr, nullptr },
    { 6, "Save position",         EK_ACTION, (void*)SavePosition, 0, 0, 0, 0, nullptr, nullptr, "Remembers where you stand (kept after a restart). For testing a spot again without walking there" },
    { 6, "Load position",         EK_ACTION, (void*)LoadPosition, 0, 0, 0, 0, nullptr, nullptr, "Moves you to the saved spot. SAME LEVEL ONLY. Not a checkpoint: killed enemies stay dead (restart the section for fresh ones)" },
    { 6, "Log my position",       EK_ACTION, (void*)LogPlayerPosition, 0, 0, 0, 0, nullptr, nullptr, "Writes where you stand (x y z, facing) to ExtendedCut.log - for placing new hotspots" },
    { 6, "Log bones (animation tests)", EK_TOGGLE, &g_devLogBones, 0, 1, 1, 0, "Developer", "LogBones", "Writes every bone of both actors to the log 1-7 s into each interrogation (+ player dumps on L3 + R3). Off for normal play" },
};
static const int kEnhCount = (int)(sizeof(kEnh) / sizeof(kEnh[0]));
static int   g_enhPage = 0, g_enhSel = 0;     // v0.56: g_enhSel = position in the VISIBLE rows of the page
// ---- v0.56: folding sections, visible rows, remembered place, rows that need a missing file
static bool  g_secOpen[kEnhCount];
static bool  g_enhStateLoaded = false;
#ifdef EC_RELEASE
// public release (build.bat release): achievements come in a later release - their tab (page 5) and the Cheats tab
// (page 6, developer mode only) come after the last visible page, so the release shows pages 0-4.
static int   EnhPagesVisible() { return g_devMode ? kEnhPageCount : kEnhPageCount - 2; }
#else
static int   EnhPagesVisible() { return g_devMode ? kEnhPageCount : kEnhPageCount - 1; }
#endif
static int   EnhHeaderOf(int i)
{
    for (int j = i; j >= 0 && kEnh[j].page == kEnh[i].page; --j) if (kEnh[j].kind == EK_HEADER) return j;
    return -1;
}
static int   EnhVisible(int page, int* out)
{
    int n = 0;
    for (int i = 0; i < kEnhCount; ++i) {
        if (kEnh[i].page != page) continue;
        if (kEnh[i].kind == EK_HEADER) { out[n++] = i; continue; }
        int h = EnhHeaderOf(i);
        if (h < 0 || g_secOpen[h]) out[n++] = i;
    }
    return n;
}
static int   EnhSectionSize(int h)          // settings under header h
{
    int c = 0;
    for (int i = h + 1; i < kEnhCount && kEnh[i].page == kEnh[h].page && kEnh[i].kind != EK_HEADER; ++i) ++c;
    return c;
}
static void  EnhStateLoad()
{
    char ini[MAX_PATH]; snprintf(ini, sizeof(ini), "%s\\ExtendedCut.ini", g_dir);
    char buf[128];
    int last = -1;                                  // default: the first section of each tab open, the rest closed
    for (int i = 0; i < kEnhCount; ++i) if (kEnh[i].kind == EK_HEADER) { g_secOpen[i] = kEnh[i].page != last; last = kEnh[i].page; }
    GetPrivateProfileStringA("Enhancements", "OpenSections", "", buf, sizeof(buf), ini);
    if (buf[0]) {                                   // one 0 / 1 per header, in table order
        int k = 0;
        for (int i = 0; i < kEnhCount; ++i) if (kEnh[i].kind == EK_HEADER) {
            if (buf[k] == '0' || buf[k] == '1') g_secOpen[i] = buf[k] == '1';
            if (buf[k]) ++k;
        }
    }
    g_enhPage = GetPrivateProfileIntA("Enhancements", "MenuTab", 0, ini);
    if (g_enhPage < 0 || g_enhPage >= EnhPagesVisible()) g_enhPage = 0;
    int item = GetPrivateProfileIntA("Enhancements", "MenuRow", -1, ini);
    g_enhSel = 0;
    if (item >= 0 && item < kEnhCount && kEnh[item].page == g_enhPage) {
        static int vis[kEnhCount];
        int n = EnhVisible(g_enhPage, vis), h = EnhHeaderOf(item);
        for (int k = 0; k < n; ++k) if (vis[k] == h) g_enhSel = k;
        for (int k = 0; k < n; ++k) if (vis[k] == item) g_enhSel = k;
    }
    g_enhStateLoaded = true;
}
static void  EnhStateSave()
{
    char ini[MAX_PATH]; snprintf(ini, sizeof(ini), "%s\\ExtendedCut.ini", g_dir);
    char buf[128]; int k = 0;
    for (int i = 0; i < kEnhCount && k < 120; ++i) if (kEnh[i].kind == EK_HEADER) buf[k++] = g_secOpen[i] ? '1' : '0';
    buf[k] = 0;
    WritePrivateProfileStringA("Enhancements", "OpenSections", buf, ini);
    char v[16]; snprintf(v, sizeof(v), "%d", g_enhPage);
    WritePrivateProfileStringA("Enhancements", "MenuTab", v, ini);
    static int vis[kEnhCount];
    int n = EnhVisible(g_enhPage, vis);
    snprintf(v, sizeof(v), "%d", (g_enhSel >= 0 && g_enhSel < n) ? vis[g_enhSel] : -1);
    WritePrivateProfileStringA("Enhancements", "MenuRow", v, ini);
}
static const char* EnhNeeds(const EnhItem& it)    // a row whose file is missing: what it needs (else nullptr)
{
    static DWORD at = 0; static bool h04 = true, life = true, gl = true, tgt = true;
    if (!at || GetTickCount() - at > 2000) {        // texture lookups at most every 2 s
        at = GetTickCount() | 1;
        h04 = H04Find("hud2004_skull.tga") >= 0; life = H04Find("hs_lifebar_2004.tga") >= 0;
        gl = H04Find("ws_m16_gl.tga") >= 0;       tgt = H04Find("ws_target_cutout.tga") >= 0;
    }
    if ((it.ptr == &g_hud2004 || it.ptr == &g_hud2004Wep) && !h04) return "Needs the early development HUD mini.vpp - not installed";
    if (it.ptr == &g_betaLifebar && !life)    return "Needs the HUD icons mini.vpp - not installed";
    if (it.ptr == &g_hudM16GL && !gl)         return "Needs the HUD icons mini.vpp - not installed";
    if (it.ptr == &g_betaTargetIcon && !tgt)  return "Needs the HUD icons mini.vpp - not installed";
    if ((it.ptr == &g_betaDives && g_betaDivesHave == 0) || (it.ptr == &g_betaKick && g_betaKickHave == 0))
        return "Needs the PS2 demo animations (perm_mot.vpp) - not in this build yet";
    return nullptr;
}
static bool  g_enhDirty = false;
static char  g_enhStatus[96] = "";
static DWORD g_enhStatusAt = 0;
static bool  g_enhBackupDone = false;

static void EnhStatus(const char* s) { strncpy(g_enhStatus, s, sizeof(g_enhStatus) - 1); g_enhStatusAt = GetTickCount(); }
// v0.52: gore presets (Gore page). Values: heads 0 off / 1 random / 2 always; gunfire 0 normal .. 3 critical hits;
// world blood 0 normal / 1 large / 2 huge; bullet 0 normal / 3 normal + splat / 1 bigger / 2 heavy; layers 1-4.
static void GorePreset(int heads, int vary, int gun, int crit, int clothes, int kw, int layers, int world, int bullet, const char* name)
{
    g_goreHeads = heads; g_goreVary = vary; g_goreGunHeads = gun; g_goreCritPct = crit; g_goreClothes = clothes;
    g_goreKillWounds = kw; g_decalLayers = layers; g_goreWorld = world; g_goreBullet = bullet;
    g_enhDirty = true;
    char m[96]; snprintf(m, sizeof(m), "Gore preset: %s", name); EnhStatus(m); Log("menu: %s", m);
}
static void GorePresetOriginal()    { g_goreStumps = 0; GorePreset(2, 0, 0, 25, 0, 0, 1, 0, 0, "Original"); }
static void GorePresetRecommended() { g_goreStumps = 1; GorePreset(1, 1, 3, 25, 1, 1, 2, 1, 3, "Recommended"); }
static void GorePresetMaximum()     { g_goreStumps = 2; GorePreset(2, 1, 3, 60, 1, 1, 4, 2, 2, "Maximum gore"); }
static int EnhPageItems(int page, int* first)
{
    int n = 0; *first = -1;
    for (int i = 0; i < kEnhCount; ++i) if (kEnh[i].page == page) { if (*first < 0) *first = i; ++n; }
    return n;
}
static void EnhValueText(const EnhItem& it, char* out, size_t n)
{
    if (it.kind == EK_HEADER) { if (n) out[0] = 0; return; }
    switch (it.kind) {
    case EK_FLOAT:  snprintf(out, n, "%.*f", it.dec, *(float*)it.ptr); break;
    case EK_INT:
        if (it.ptr == &g_hudScaleIdx) {   // v0.42
            int v = *(int*)it.ptr;
            if (v == 0) snprintf(out, n, "Auto  (now %d %%)", (int)(HudScaleNow() * 100.0f + 0.5f));
            else snprintf(out, n, "%s", (v > 0 && v < kHudScaleCount) ? kHudScaleNames[v] : "?");
            break;
        }
        if (it.ptr == &g_resIdx) {
            int v = *(int*)it.ptr;
            snprintf(out, n, "%s  (now %dx%d)", (v >= 0 && v < kResCount) ? kRes[v].name : "?",
                     *(volatile int*)0x913250, *(volatile int*)0x913254); break;
        }
        if (it.ptr == &g_betaComboHud) {   // v0.34.4: named values
            static const char* k[3] = { "Off", "Misaligned", "Aligned" };
            int v = *(int*)it.ptr; snprintf(out, n, "%s", (v >= 0 && v <= 2) ? k[v] : "?"); break;
        }
        if (it.ptr == &g_devHudKey) {   // v0.50.5
            static const char* hk[5] = { "Off", "LB + D-pad Up", "LB + D-pad Down", "LB + D-pad Left", "LB + D-pad Right" };
            int x = *(int*)it.ptr; snprintf(out, n, "%s", (x >= 0 && x <= 4) ? hk[x] : "?"); break;
        }
        if (it.ptr == &g_gunCam) {   // v0.54
            int x = *(int*)it.ptr; snprintf(out, n, "%s", (x >= 0 && x <= 3) ? kGunCamNames[x] : "?"); break;
        }
        if (it.ptr == &g_betaPausePic) {   // v0.50.10
            static const char* pp[3] = { "Off", "Xbox", "PS2" };
            int x = *(int*)it.ptr; snprintf(out, n, "%s", (x >= 0 && x <= 2) ? pp[x] : "?"); break;
        }
        if (it.ptr == &g_interKillScore) {   // v0.66
            static const char* w[4] = { "Unchanged", "Penalty", "No penalty", "Bonus" };
            int x = *(int*)it.ptr; snprintf(out, n, "%s", (x >= 0 && x <= 3) ? w[x] : "?"); break;
        }
        if (it.ptr == &g_goreGunHeads) {   // v0.48.1
            static const char* w[4] = { "Normal", "Random 50-50", "Off", "Critical hits" };   // v0.50.8: + Critical
            int x = *(int*)it.ptr; snprintf(out, n, "%s", (x >= 0 && x <= 3) ? w[x] : "?"); break;
        }
        if (it.ptr == &g_goreWorld) {   // v0.46
            static const char* w[3] = { "Normal (125)", "Large (300)", "Huge (600)" };
            int v = *(int*)it.ptr; snprintf(out, n, "%s", (v >= 0 && v <= 2) ? w[v] : "?"); break;
        }
        if (it.ptr == &g_goreStumps) {   // v0.77
            static const char* s[3] = { "Original", "Restored", "Random variation" };
            int v = *(int*)it.ptr; snprintf(out, n, "%s", (v >= 0 && v <= 2) ? s[v] : "?"); break;
        }
        if (it.ptr == &g_goreHeads || it.ptr == &g_goreBullet) {   // v0.44: named values
            static const char* h[3] = { "Off", "Random 50-50", "Always" }, *b[4] = { "Normal", "Bigger", "Heavy", "Normal + splat" };
            int v = *(int*)it.ptr; bool hd = it.ptr == &g_goreHeads; const char** k = hd ? h : b;
            snprintf(out, n, "%s", (v >= 0 && v <= (hd ? 2 : 3)) ? k[v] : "?"); break;
        }
        snprintf(out, n, "%d", *(int*)it.ptr); break;
    case EK_BOOL:   snprintf(out, n, "%s", *(bool*)it.ptr ? "On" : "Off"); break;
    case EK_TOGGLE: snprintf(out, n, "%s", *(int*)it.ptr ? "On" : "Off"); break;
    case EK_STYLE:  { int v = *(int*)it.ptr; snprintf(out, n, "%s", (v >= 0 && v <= 4) ? kStyleShow[v] : "?"); } break;
    case EK_BIND: case EK_BIND_HOSTAGE: {
        int a = ActionOf(*(Target*)it.ptr);
        if (a == 0 && it.kind == EK_BIND_HOSTAGE) snprintf(out, n, "Same as normal");
        else snprintf(out, n, "%s", a >= 0 ? kActions[a].label : "Other (from the ini)");
    } break;
    default: out[0] = 0;
    }
}
static void EnhIniValue(const EnhItem& it, char* out, size_t n)     // as written to the ini
{
    switch (it.kind) {
    case EK_BOOL:   snprintf(out, n, "%d", *(bool*)it.ptr ? 1 : 0); break;
    case EK_INT:    snprintf(out, n, "%d", *(int*)it.ptr); break;   // v0.35.2: numbers, never the shown names (ComboHud saved "Misaligned" -> read back as 0)
    case EK_TOGGLE: snprintf(out, n, "%d", *(int*)it.ptr ? 1 : 0); break;
    case EK_STYLE:  { int v = *(int*)it.ptr; snprintf(out, n, "%s", (v >= 0 && v <= 4) ? kStyleNames[v] : "Auto"); } break;
    case EK_BIND: case EK_BIND_HOSTAGE: {
        int a = ActionOf(*(Target*)it.ptr);
        snprintf(out, n, "%s", a >= 0 ? kActions[a].token : "");
    } break;
    default: EnhValueText(it, out, n);
    }
}
static void EnhChange(const EnhItem& it, int dir, int mult = 1)   // dir -1 / +1, mult = held-long speed-up
{
    switch (it.kind) {
    case EK_FLOAT: {
        float* v = (float*)it.ptr;
        float k = powf(10.0f, (float)it.dec);
        float nv = roundf((*v + it.step * dir * mult) * k) / k;
        if (nv < it.lo) nv = it.lo; if (nv > it.hi) nv = it.hi;
        if (nv != *v) { *v = nv; g_enhDirty = true; }
    } break;
    case EK_INT: {
        if (it.ptr == &g_goreBullet) {   // v0.46.3: menu order Normal, Normal + splat, Bigger, Heavy (saved values unchanged)
            static const int order[4] = { 0, 3, 1, 2 };
            int pos = 0; for (int i = 0; i < 4; ++i) if (order[i] == g_goreBullet) pos = i;
            int np = pos + dir; if (np < 0) np = 0; if (np > 3) np = 3;
            if (order[np] != g_goreBullet) { g_goreBullet = order[np]; g_enhDirty = true; }
            break;
        }
        int* v = (int*)it.ptr; int nv = *v + dir;
        if (nv < (int)it.lo) nv = (int)it.lo; if (nv > (int)it.hi) nv = (int)it.hi;
        if (nv != *v) { *v = nv; g_enhDirty = true; }
    } break;
    case EK_BOOL:   *(bool*)it.ptr = !*(bool*)it.ptr; g_enhDirty = true; break;
    case EK_TOGGLE: *(int*)it.ptr = *(int*)it.ptr ? 0 : 1; g_enhDirty = true; break;
    case EK_STYLE:  { int* v = (int*)it.ptr; *v = (*v + dir + 5) % 5; g_enhDirty = true; } break;
    case EK_BIND: case EK_BIND_HOSTAGE: {
        Target* t = (Target*)it.ptr;
        int a = ActionOf(*t);
        a = a < 0 ? 0 : (a + dir + kActionCount) % kActionCount;
        *t = kActions[a].t;
        g_enhDirty = true;
    } break;
    case EK_ACTION:
        if (it.ptr) {                                   // v0.29.1: action items call their function
            ((void (*)())it.ptr)();
            char s[48];                                 // v0.50.1: each action its own message
            if (it.ptr == (void*)SavePosition) snprintf(s, sizeof(s), "Position saved");
            else if (it.ptr == (void*)LoadPosition) snprintf(s, sizeof(s), g_tpHave ? "Moved to the saved position" : "No position saved yet");
            else if (it.ptr == (void*)LogPlayerPosition) snprintf(s, sizeof(s), "Position #%d written to the log", g_posLogCount);
            else s[0] = 0;
            if (s[0]) EnhStatus(s);
        }
        else if (!g_cfg.rumble) EnhStatus("Rumble is off");
        else { Rumble(0.6f, 0.8f, 350, "menu test"); EnhStatus("Rumble test"); }
        break;
    }
    if (it.ptr == &g_cfg.rumble || it.ptr == &g_cfg.rumbleStrength) { /* nothing to restart: read live */ }
}

// ---- the ini: replace only the value of a key, keep alignment and comments
struct IniText {
    std::vector<std::string> lines;
    std::string eol = "\r\n";
    bool endsWithEol = true;
    bool Load(const char* path) {
        FILE* f = fopen(path, "rb");
        if (!f) return false;
        std::string all; char buf[4096]; size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, n);
        fclose(f);
        eol = all.find("\r\n") != std::string::npos ? "\r\n" : "\n";
        endsWithEol = !all.empty() && all.back() == '\n';
        size_t p = 0;
        while (p < all.size()) {
            size_t e = all.find('\n', p);
            std::string l = all.substr(p, e == std::string::npos ? std::string::npos : e - p);
            if (!l.empty() && l.back() == '\r') l.pop_back();
            lines.push_back(l);
            if (e == std::string::npos) break;
            p = e + 1;
        }
        return true;
    }
    static std::string Trim(const std::string& s) {
        size_t a = s.find_first_not_of(" \t"); if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t"); return s.substr(a, b - a + 1);
    }
    void Set(const char* sec, const char* key, const char* val, const char* comment = nullptr) {
        int start = -1, end = (int)lines.size();
        for (int i = 0; i < (int)lines.size(); ++i) {
            std::string t = Trim(lines[i]);
            if (t.empty() || t[0] != '[') continue;
            if (start >= 0) { end = i; break; }
            size_t e = t.find(']');
            std::string name = t.substr(1, e == std::string::npos ? std::string::npos : e - 1);
            if (!_stricmp(Trim(name).c_str(), sec)) start = i;
        }
        if (start < 0) {
            if (!lines.empty() && !Trim(lines.back()).empty()) lines.push_back("");
            lines.push_back(std::string("[") + sec + "]");
            lines.push_back(std::string(key) + "=" + val);
            return;
        }
        for (int i = start + 1; i < end; ++i) {
            std::string& L = lines[i];
            size_t p = L.find_first_not_of(" \t");
            if (p == std::string::npos || L[p] == ';' || L[p] == '#') continue;
            size_t eq = L.find('=', p);
            if (eq == std::string::npos) continue;
            if (_stricmp(Trim(L.substr(p, eq - p)).c_str(), key)) continue;
            std::string rest = L.substr(eq + 1);
            size_t c = rest.find_first_of(";#");
            std::string tail = c == std::string::npos ? "" : rest.substr(c);
            if (comment) tail = std::string("; ") + comment;                      // v0.22: binding rows get a new comment
            if (tail.empty()) { L = L.substr(0, eq + 1) + val; return; }
            std::string field = val;
            int col = c == std::string::npos ? 23 - (int)(eq + 1) : (int)c;   // new comments line up at column 24
            if (col < 1) col = 1;
            if ((int)field.size() < col) field.append(col - field.size(), ' '); else field += ' ';
            L = L.substr(0, eq + 1) + field + tail;
            return;
        }
        int at = end;
        while (at - 1 > start && Trim(lines[at - 1]).empty()) --at;
        std::string nl = std::string(key) + "=" + val;
        if (comment) { if (nl.size() < 23) nl.append(23 - nl.size(), ' '); else nl += ' '; nl += std::string("; ") + comment; }
        lines.insert(lines.begin() + at, nl);
    }
    bool Save(const char* path) {
        std::string tmp = std::string(path) + ".tmp";
        FILE* f = fopen(tmp.c_str(), "wb");
        if (!f) return false;
        for (size_t i = 0; i < lines.size(); ++i) {
            fwrite(lines[i].data(), 1, lines[i].size(), f);
            if (i + 1 < lines.size() || endsWithEol) fwrite(eol.data(), 1, eol.size(), f);
        }
        bool ok = fclose(f) == 0;
        return ok && MoveFileExA(tmp.c_str(), path, MOVEFILE_REPLACE_EXISTING);
    }
};

static bool EnhSave()
{
    char ini[MAX_PATH]; snprintf(ini, sizeof(ini), "%s\\ExtendedCut.ini", g_dir);
    IniText t;
    if (!t.Load(ini)) { Log("enhancements: could not read %s", ini); return false; }
    if (!g_enhBackupDone) {
        char bak[MAX_PATH]; snprintf(bak, sizeof(bak), "%s\\ExtendedCut.ini.bak", g_dir);
        if (CopyFileA(ini, bak, FALSE)) { g_enhBackupDone = true; Log("enhancements: old ini kept as ExtendedCut.ini.bak"); }
    }
    for (int i = 0; i < kEnhCount; ++i) {
        const EnhItem& it = kEnh[i];
        if (!it.section) continue;
        char v[32]; EnhIniValue(it, v, sizeof(v));
        if (it.kind == EK_BIND || it.kind == EK_BIND_HOSTAGE) {
            // v0.22: a binding row is written only when it differs from the file (keeps your own comments)
            char cur[64]; GetPrivateProfileStringA(it.section, it.key, "", cur, sizeof(cur), ini);
            int a = ActionOf(*(Target*)it.ptr);
            if (a < 0 || SameTarget(ParseTarget(cur), *(Target*)it.ptr)) continue;
            const char* note = (a == 0 && it.kind == EK_BIND_HOSTAGE) ? "same as normal (set in the menu)" : kActions[a].label;
            char c2[80]; snprintf(c2, sizeof(c2), "%s%s", note, a == 0 && it.kind == EK_BIND_HOSTAGE ? "" : " (set in the menu)");
            t.Set(it.section, it.key, v, c2);
            Log("enhancements: %s %s = %s", it.section, it.key, v);
            continue;
        }
        t.Set(it.section, it.key, v);
    }
    bool ok = t.Save(ini);
    Log("enhancements: settings %s ExtendedCut.ini", ok ? "saved to" : "could NOT be saved to");
    ResolutionSave();   // v0.42
    return ok;
}
// v0.42: the resolution choice goes into the widescreen fix's ini (ResX / ResY; empty = desktop), read at the next
// start. The game itself reads "Resolution Width / Height" from the registry, but the widescreen fix overrides that.
static bool ResolutionIni(char* out)
{
    char exe[MAX_PATH]; GetModuleFileNameA(NULL, exe, MAX_PATH); if (char* q = strrchr(exe, '\\')) *q = 0;
    const char* rel[] = { "scripts\\Widescreen Fix\\ThePunisher.WidescreenFix.ini", "scripts\\ThePunisher.WidescreenFix.ini",
                          "ThePunisher.WidescreenFix.ini", "scripts\\ThePunisher.FusionFix.ini", "scripts\\Widescreen Fix\\ThePunisher.FusionFix.ini" };
    for (const char* r : rel) {
        snprintf(out, MAX_PATH, "%s\\%s", exe, r);
        if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return true;
    }
    return false;
}
static void ResolutionSave()
{
    if (g_resIdx == g_resSaved || g_resIdx <= 0 || g_resIdx >= kResCount) { g_resSaved = g_resIdx; return; }
    char path[MAX_PATH];
    if (!ResolutionIni(path)) { EnhStatus("Widescreen Fix ini not found - resolution not changed"); Log("resolution: no widescreen fix ini found"); return; }
    char w[16] = "", h[16] = "";
    if (kRes[g_resIdx].w > 0) { snprintf(w, sizeof(w), "%d", kRes[g_resIdx].w); snprintf(h, sizeof(h), "%d", kRes[g_resIdx].h); }
    bool ok = WritePrivateProfileStringA("MAIN", "ResX", w, path) && WritePrivateProfileStringA("MAIN", "ResY", h, path);
    Log("resolution: %s -> %s (ResX=%s ResY=%s) %s", kRes[g_resIdx].name, path, w, h, ok ? "saved" : "FAILED");
    EnhStatus(ok ? "Resolution saved - restart the game to use it" : "Could not write the Widescreen Fix ini");
    if (ok) g_resSaved = g_resIdx;
}
// v0.43.1: at every start (in DllMain, before the game makes its window): if the Widescreen Fix ini asks for a size the
// current screen cannot show (bigger than the desktop, or not in the display mode list) - e.g. 1600x900 saved while
// docked, then started in handheld 1280x800 - the game would hang. Then ResX / ResY are cleared (= desktop / Auto) and
// the menu choice goes back to "Auto (screen size)". Values 0 / empty = Auto already.
static void ResolutionStartupCheck(const char* ourIni)
{
    char path[MAX_PATH];
    if (!ResolutionIni(path)) return;
    int w = (int)GetPrivateProfileIntA("MAIN", "ResX", 0, path), h = (int)GetPrivateProfileIntA("MAIN", "ResY", 0, path);
    if (w <= 0 || h <= 0) { Log("resolution: widescreen fix uses the desktop size (ResX=%d ResY=%d)", w, h); return; }
    DEVMODEA cur = {}; cur.dmSize = sizeof(cur);
    if (!EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &cur)) { Log("resolution: cannot read the screen size - %dx%d kept", w, h); return; }
    bool listed = false;
    DEVMODEA m = {}; m.dmSize = sizeof(m);
    for (DWORD i = 0; EnumDisplaySettingsA(NULL, i, &m); ++i)
        if ((int)m.dmPelsWidth == w && (int)m.dmPelsHeight == h) { listed = true; break; }
    bool fits = w <= (int)cur.dmPelsWidth && h <= (int)cur.dmPelsHeight;
    if (listed && fits) { Log("resolution: %dx%d ok on this screen (%lux%lu)", w, h, cur.dmPelsWidth, cur.dmPelsHeight); return; }
    bool ok = WritePrivateProfileStringA("MAIN", "ResX", "", path) && WritePrivateProfileStringA("MAIN", "ResY", "", path);
    Log("resolution: %dx%d is not available on this screen (%lux%lu, %s) -> widescreen fix set back to desktop size %s",
        w, h, cur.dmPelsWidth, cur.dmPelsHeight, fits ? "not in the mode list" : "bigger than the screen", ok ? "(saved)" : "(WRITE FAILED)");
    if (ok && ourIni) {
        g_resIdx = g_resSaved = 1;   // "Auto (screen size)"
        WritePrivateProfileStringA("Display", "Resolution", "1", ourIni);
    }
}

// v0.20.2: X = back to your defaults. The defaults are ExtendedCut_defaults.ini next to the plugin: made
// once, as a copy of ExtendedCut.ini, the first time this version starts (before the menu ever saves).
// Saving from the menu never touches it, so X always returns to the same values. To make other values
// your defaults, edit that file or delete it (the next start copies the current ExtendedCut.ini again).
static void DefaultsPath(char* out) { snprintf(out, MAX_PATH, "%s\\ExtendedCut_defaults.ini", g_dir); }
static void EnsureDefaultsFile()
{
    char ini[MAX_PATH], def[MAX_PATH];
    snprintf(ini, sizeof(ini), "%s\\ExtendedCut.ini", g_dir); DefaultsPath(def);
    if (GetFileAttributesA(def) != INVALID_FILE_ATTRIBUTES) { Log("enhancements: defaults from ExtendedCut_defaults.ini"); return; }
    if (CopyFileA(ini, def, TRUE)) Log("enhancements: ExtendedCut_defaults.ini made from your current ExtendedCut.ini");
    else Log("enhancements: could not make ExtendedCut_defaults.ini");
}
static bool EnhItemFromIni(const EnhItem& it, const char* path)
{
    if (!it.section) return false;
    char buf[64];
    GetPrivateProfileStringA(it.section, it.key, "", buf, sizeof(buf), path);
    if (it.kind == EK_BIND || it.kind == EK_BIND_HOSTAGE) { *(Target*)it.ptr = ParseTarget(buf); return true; }
    for (char* c = buf; *c; ++c) if (*c == ';' || *c == '#' || *c == ' ' || *c == '\t') { *c = 0; break; }
    if (!buf[0]) return false;
    switch (it.kind) {
    case EK_FLOAT: { float v = (float)atof(buf); if (v < it.lo) v = it.lo; if (v > it.hi) v = it.hi; *(float*)it.ptr = v; } break;
    case EK_INT:   {
        int v = atoi(buf);
        if (it.ptr == &g_betaComboHud) v = !_stricmp(buf, "Misaligned") ? 1 : !_stricmp(buf, "Aligned") ? 2 : v;   // v0.35.2
        if (v < (int)it.lo) v = (int)it.lo; if (v > (int)it.hi) v = (int)it.hi; *(int*)it.ptr = v;
    } break;
    case EK_BOOL:   *(bool*)it.ptr = atof(buf) != 0; break;
    case EK_TOGGLE: *(int*)it.ptr = atoi(buf) ? 1 : 0; break;
    case EK_STYLE: {
        int v = 4;
        for (int k = 0; k < 5; ++k) if (!_stricmp(buf, kStyleNames[k])) v = k;
        *(int*)it.ptr = v;
    } break;
    default: return false;
    }
    return true;
}
static void EnhResetDefaults()
{
    char def[MAX_PATH]; DefaultsPath(def);
    if (GetFileAttributesA(def) == INVALID_FILE_ATTRIBUTES) { EnhStatus("No ExtendedCut_defaults.ini found"); return; }
    int n = 0;
    for (int i = 0; i < kEnhCount; ++i) if (EnhItemFromIni(kEnh[i], def)) ++n;
    g_enhDirty = true;
    EnhStatus("All settings back to your defaults");
    Log("enhancements: %d settings reset from ExtendedCut_defaults.ini", n);
}

// ---- open / close / input (called from SendKeys every frame)
static bool PausedInLevel()
{
    return *(volatile BYTE*)kPaused != 0 && (*(volatile BYTE*)kLevelFlags & 1) != 0;
}
static bool PlainPause()
{
    if (!PausedInLevel() || g_menuActive || GetTickCount() - g_lastPopup < 250) return false;
    if (g_pauseEveryFrame) return GetTickCount() - g_lastPauseDraw < 250;   // the pause screen is being drawn
    return true;
}
static void EnhIgnoreHeld() { for (int i = 0; i < P_COUNT; ++i) if (g_raw[i]) g_padIgnore[i] = true; }
static void EnhOpen()
{
    g_enhOpen = true;
    EnhIgnoreHeld();
    memset(g_down, 0, sizeof(g_down)); g_rx = g_ry = g_rxLin = g_ryLin = 0;
    if (!g_enhStateLoaded) EnhStateLoad();          // v0.56: back where you were
    if (g_enhPage >= EnhPagesVisible()) { g_enhPage = 0; g_enhSel = 0; }
    { static int vis[kEnhCount]; int n = EnhVisible(g_enhPage, vis); if (g_enhSel >= n) g_enhSel = 0; }
    g_enhStatus[0] = 0;
    RECT rc = {}; HWND w = GetActiveWindow(); if (w) GetClientRect(w, &rc);
    DWORD sx = 0, sy = 0; bool sc = PickScale(&sx, &sy);
    int font = EnhFont(), h = -1;
    __try { h = ((int (__cdecl*)(int))0x5185B0)(font); } __except (1) { h = -1; }
    Log("enhancements menu opened (window %ldx%ld, font %d height %d, text scale %s %08X %08X; "
        "pause-screen hook %d site(s) %lu calls%s, hint hook %d site(s) %lu calls, %lu game texts)",
        rc.right, rc.bottom, font, h, sc ? "" : "NOT SEEN", sx, sy, g_hookedPause, g_pauseCalls,
        g_pauseEveryFrame ? " every frame" : "", g_hookedHint, g_hintCalls, g_textCalls);
}
// v0.31.5: the Options paper is the gui element "op_menu_bg" (object 0x74B4B8, built by 0x4779B0). Its rectangle
// from gui.tbl lives at +0x2C x, +0x30 y, +0x34 w, +0x38 h (screen pixels in the chosen resolution block). While
// the Extended Cut menu is open from Options the paper is stretched over the whole menu, then put back.
static void OptionsPaperBig(bool big)
{
    static int saved[4]; static bool have = false;
    volatile int* r = (volatile int*)(0x74B4B8 + 0x2C);
    __try {
        if (big) {
            if (!have) { for (int i = 0; i < 4; ++i) saved[i] = r[i]; have = true; }
            RECT rc = {}; HWND w = GetActiveWindow(); if (!w) w = GetForegroundWindow();
            int W = 1280, H = 800;
            if (w && GetClientRect(w, &rc) && rc.right >= 320 && rc.bottom >= 240) { W = rc.right; H = rc.bottom; }
            static bool logged = false;
            if (!logged) { logged = true; Log("options: paper rect was %d %d %d %d (window %dx%d)", saved[0], saved[1], saved[2], saved[3], W, H); }
            r[2] = W - 2 * saved[0];                  // same margins as the game's panel: left = right
            r[3] = H * 90 / 100 - saved[1];           // down to 90 % of the screen
        } else if (have) {
            for (int i = 0; i < 4; ++i) r[i] = saved[i];
            have = false;
        }
    } __except (1) { Log("options: paper rect not reachable"); }
}
static void EnhClose(const char* why)
{
    if (g_enhDirty) { if (EnhSave()) g_enhDirty = false; }
    EnhStateSave();                                 // v0.56: tab, row, open sections
    g_enhOpen = false;
    if (g_showLayout) HideLayout();
    if (g_enhFromOptions) *(volatile int*)(0x74B938 + 0x60) = 3;   // v0.31.4: Extended Menu selected again
    g_enhFromOptions = false;
    EnhIgnoreHeld();
    Log("enhancements menu closed (%s)", why);
}
// v0.32.4: the game's menu sounds (handles in globals, played like its own menus: 0x461300 (sound, 0, -1, 1.0f)):
// 0x73D69C up, 0x73D6A0 down (list code 0x478EC1 / 0x478F38), 0x73D6AC / 0x73D6B0 value change (Game Settings),
// 0x73D6C0 select, 0x73D6BC back (Options screen 0x4870A2 / 0x48711D)
static void MenuSound(DWORD global)
{
    __try {
        int h = *(volatile int*)global;
        if (h != -1) ((void (__cdecl*)(int, int, int, float))0x461300)(h, 0, -1, 1.0f);
    } __except (1) {}
}
static void EnhUpdate()
{
    if (g_devMenuPreview != -99) {                        // v0.72 dev: show the Achievements tab while paused, no pad
        if (PausedInLevel()) { int v = g_devMenuPreview; g_enhFromOptions = v >= 100; if (v >= 100) v -= 200; g_enhOpen = true; g_enhPage = 5; g_achDetail = v == -3 ? -2 : v; g_enhSel = 2; }
        return;
    }
    static bool prev[P_COUNT];
    bool edge[P_COUNT];
    for (int i = 0; i < P_COUNT; ++i) { edge[i] = g_raw[i] && !prev[i]; prev[i] = g_raw[i]; }
    PausePicBank(g_betaPausePic && PausedInLevel());   // v0.50.9
    if (!g_enhOpen) {
        static bool wasPlain = false;
        bool plain = g_pad && PlainPause();
        if (plain != wasPlain) { wasPlain = plain; if (plain) Log("pause screen: enhancements menu available (%s)", kPadNames[g_enhOpenBtn]); }
        if (plain && g_enhOpenBtn >= 0 && edge[g_enhOpenBtn]) EnhOpen();
        return;
    }
    if (!g_pad)           { EnhClose("controller gone"); return; }
    if (!PausedInLevel() && !g_enhFromOptions) { EnhClose("game unpaused"); return; }
    static int vis[kEnhCount];
    int n = EnhVisible(g_enhPage, vis);
    if (g_showLayout) {                                  // v0.33: the layout picture takes the pad until closed
        if (edge[P_EAST] || edge[P_SOUTH] || edge[P_START] || edge[g_enhOpenBtn]) { HideLayout(); MenuSound(0x73D6BC); }
        return;
    }
    if (g_achDetail != -2) {                             // v0.72: achievements level page
        if (g_enhPage != 5 || edge[P_LB] || edge[P_RB] || edge[g_enhOpenBtn] || edge[P_START]) g_achDetail = -2;
        else {
            if (edge[P_EAST] || edge[P_SOUTH]) { g_achDetail = -2; MenuSound(0x73D6BC); return; }
            int ids[40]; int an = AchOfLevel(g_achDetail, ids, 40); int& k = g_achPick[AchRowOf(g_achDetail)];
            if (an > 0 && g_dirPulse[0]) { k = (k + an - 1) % an; MenuSound(0x73D69C); }
            if (an > 0 && g_dirPulse[1]) { k = (k + 1) % an; MenuSound(0x73D6A0); }
            if (g_dirPulse[2] || g_dirPulse[3]) {        // previous / next level (Whole game after Ryker's)
                int L = g_achDetail + (g_dirPulse[3] ? 1 : -1);
                if (L > 15) L = -1; if (L < -1) L = 15;
                g_achDetail = L; g_achPick[AchRowOf(L)] = 0;
                for (int s = 0; s < n; ++s) if (AchRowLevel(kEnh[vis[s]].label) == L) g_enhSel = s;
                MenuSound(g_dirPulse[3] ? 0x73D6B0 : 0x73D6AC);
            }
            return;
        }
    }
    if (edge[P_EAST] || edge[g_enhOpenBtn] || edge[P_START]) { MenuSound(0x73D6BC); EnhClose("button"); return; }
    int pc = EnhPagesVisible();
    if (g_enhPage >= pc) { g_enhPage = 0; g_enhSel = 0; n = EnhVisible(g_enhPage, vis); }
    if (edge[P_LB] || edge[P_RB]) {
        g_enhPage = (g_enhPage + (edge[P_RB] ? 1 : pc - 1)) % pc;
        MenuSound(edge[P_RB] ? 0x73D6A0 : 0x73D69C);
        g_enhSel = 0; g_enhStatus[0] = 0;
        n = EnhVisible(g_enhPage, vis);
    }
    if (n <= 0) return;
    if (g_enhSel >= n) g_enhSel = n - 1;
    // v0.56: section headings are selectable (A / right = open, left = close); LT / RT jump between sections
    if (g_dirPulse[0]) { g_enhSel = (g_enhSel + n - 1) % n; MenuSound(0x73D69C); }
    if (g_dirPulse[1]) { g_enhSel = (g_enhSel + 1) % n; MenuSound(0x73D6A0); }
    if (edge[P_LT] || edge[P_RT]) {
        int d = edge[P_RT] ? 1 : -1, k = g_enhSel;
        for (int step = 0; step < n; ++step) { k = (k + d + n) % n; if (kEnh[vis[k]].kind == EK_HEADER) break; }
        g_enhSel = k; MenuSound(d > 0 ? 0x73D6A0 : 0x73D69C);
    }
    if (kEnh[vis[g_enhSel]].kind == EK_HEADER) {
        int h = vis[g_enhSel]; bool want = g_secOpen[h];
        if (edge[P_SOUTH]) want = !want;
        if (g_dirPulse[3]) want = true;
        if (g_dirPulse[2]) want = false;
        if (want != g_secOpen[h]) { g_secOpen[h] = want; MenuSound(0x73D6C0); }
        if (edge[P_WEST]) { EnhResetDefaults(); MenuSound(0x73D6C0); }
        return;
    }
    const EnhItem& it = kEnh[vis[g_enhSel]];
    // v0.20.2: holding left/right for more than 1.5 s changes 5 steps at a time
    static DWORD holdStart = 0;
    if (g_dirDown[2] || g_dirDown[3]) { if (!holdStart) holdStart = GetTickCount(); } else holdStart = 0;
    int mult = (holdStart && GetTickCount() - holdStart > 1500) ? 5 : 1;
    if (it.kind == EK_FLOAT && it.step < 0.05f && holdStart) {        // v0.29: fine 0.01 steps speed up more
        DWORD held = GetTickCount() - holdStart;
        mult = held > 3000 ? 25 : held > 1500 ? 10 : held > 600 ? 3 : 1;
    }
    if (it.kind == EK_ACTION && AchRowLevel(it.label) != -2 && edge[P_SOUTH]) {   // v0.72: open the level page
        g_achDetail = AchRowLevel(it.label); g_achPick[AchRowOf(g_achDetail)] = 0; MenuSound(0x73D6C0); return;
    }
    if (g_dirPulse[2] && it.kind != EK_ACTION) { EnhChange(it, -1, mult); MenuSound(0x73D6AC); }
    if (g_dirPulse[3] && it.kind != EK_ACTION) { EnhChange(it, +1, mult); MenuSound(0x73D6B0); }
    if (edge[P_SOUTH]) { EnhChange(it, +1); MenuSound(0x73D6C0); }
    if (edge[P_WEST]) { EnhResetDefaults(); MenuSound(0x73D6C0); }
}

// ---- drawing
static int  g_drawFont = -1;
static DWORD g_drawSx = 0, g_drawSy = 0;
static void EnhText(int x, int y, const char* s, DWORD rgb)
{
    *(volatile DWORD*)0x91327C = (rgb & 0x00FFFFFF) | 0xFF000000;    // r, g, b, a (byte order)
    ((DrawText_t)0x5186B0)(x, y, s, g_drawFont, g_drawSx, g_drawSy);
    // v0.31.6: pad icons are coloured pictures in the font, tinted by the text colour - on the paper (dark text)
    // they would turn black. Draw each icon again in white (= its own colours) on top, at the x where the game's
    // text measure 0x5185E0 (w, h, text, length, font) says it starts.
    if ((rgb & 0x00FFFFFF) == 0xFFFFFF || !g_enhFromOptions || !s) return;
    for (int i = 0; s[i]; ++i) {
        if (!IsIconChar((BYTE)s[i])) continue;
        int w = 0, h = 0;
        if (i > 0) ((void (__cdecl*)(int*, int*, const char*, int, int))0x5185E0)(&w, &h, s, i, g_drawFont);
        char one[2] = { s[i], 0 };
        *(volatile DWORD*)0x91327C = 0xFFFFFFFF;
        ((DrawText_t)0x5186B0)(x + w, y, one, g_drawFont, g_drawSx, g_drawSy);
    }
    *(volatile DWORD*)0x91327C = (rgb & 0x00FFFFFF) | 0xFF000000;
}
static const char* EnhBtn(int i)      // a pad button as an icon (or its name)
{
    int style = PromptStyleNow();
    if (style != ST_KEYBOARD) {
        const BYTE* icons = style == ST_PLAYSTATION ? kIconPS : style == ST_DECK ? kIconDeck : kIconXbox;
        bool dpad = i >= P_DUP && i <= P_DRIGHT;
        if (icons[i] && (!dpad || g_drawFont >= 0)) return g_iconStr[icons[i]];   // hud font has no D-pad icons
    }
    return kPadNames[i];
}
// ---- v0.32: the menu opened from the game's Options, drawn like the game's own option screens:
// rows 20 px apart from x 60, the game's black brush bar behind the selected row (white text), values between the
// game's arrows (misc_arrow.tga), "A / B" hints top right. Game draw calls (as its list code 0x48C3E0 uses them):
//   colour 0x510B60 (r, g, b, a); sprite 0x510C80 (tex, x, y, scale x, scale y); stretched sprite 0x510C20
//   (tex, x, y, w, h, u0, v0, u1, v1, mirror, 0, scale x, scale y, 0); texture 0x50C750 (name, -1);
//   texture size 0x50BEB0 (tex, &w, &h); text size 0x5185E0 (&w, &h, text, count, font).
//   Selection bar: left part tex [0x620B40] at text x - [0x620B44], rest tex [0x620B48] at text x, tinted black.
static int PaperTextW(const char* s)
{
    int w = 0, h = 0;
    ((void (__cdecl*)(int*, int*, const char*, int, int))0x5185E0)(&w, &h, s, -1, g_drawFont);
    return w;
}
// v0.61: help line wrapped at spaces to maxW (tester: "Gunfire head gibs" help ran off the screen). Up to 3 lines;
// returns how many lines it used.
static int EnhHelpWrapped(int x, int y, int maxW, int lh, const char* s, DWORD rgb)
{
    if (!s || !*s) return 1;
    char buf[200];
    int lines = 0;
    while (*s && lines < 3) {
        int n = (int)strlen(s), cut = n;
        if (lines < 2) {
            while (cut > 0) {
                int w = 0, h = 0;
                ((void (__cdecl*)(int*, int*, const char*, int, int))0x5185E0)(&w, &h, s, cut, g_drawFont);
                if (w <= maxW) break;
                int sp = cut - 1;
                while (sp > 0 && s[sp] != ' ') --sp;
                if (sp <= 0) break;   // one long word: draw it whole
                cut = sp;
            }
        }
        if (cut <= 0) cut = n;
        if (cut > (int)sizeof(buf) - 1) cut = sizeof(buf) - 1;
        memcpy(buf, s, cut); buf[cut] = 0;
        EnhText(x, y + lines * lh, buf, rgb);
        ++lines;
        s += cut;
        while (*s == ' ') ++s;
    }
    return lines ? lines : 1;
}
static void PaperColor(int r, int g, int b, int a) { ((void (__cdecl*)(int, int, int, int))0x510B60)(r, g, b, a); }
static void PaperSprite(int tex, int x, int y)
{
    ((void (__cdecl*)(int, int, int, DWORD, DWORD))0x510C80)(tex, x, y, *(DWORD*)0x6766D4, *(DWORD*)0x6766D8);
}
static void PaperStretch(int tex, int x, int y, int w, int h, int u0, int v0, int u1, int v1)
{
    ((void (__cdecl*)(int, int, int, int, int, int, int, int, int, int, int, DWORD, DWORD, int))0x510C20)
        (tex, x, y, w, h, u0, v0, u1, v1, 0, 0, *(DWORD*)0x6766D4, *(DWORD*)0x6766D8, 0);
}
// v0.32.1: the bar = left end [0x620B40] (width [0x620B44]) + middle [0x620B3C] stretched to the text + right end
// [0x620B48] (v0.32 drew only the two ends)
static void PaperBar(int textX, int textY, int fh, int textW)
{
    int a = *(volatile int*)0x620B40, m = *(volatile int*)0x620B3C, e = *(volatile int*)0x620B48;
    int off = *(volatile int*)0x620B44;
    int aw = 0, ah = 0, mw = 0, mh = 0, ew = 0, eh = 0;
    if (a >= 0) ((void (__cdecl*)(int, int*, int*))0x50BEB0)(a, &aw, &ah);
    if (m >= 0) ((void (__cdecl*)(int, int*, int*))0x50BEB0)(m, &mw, &mh);
    if (e >= 0) ((void (__cdecl*)(int, int*, int*))0x50BEB0)(e, &ew, &eh);
    static bool logged = false;
    if (!logged) { logged = true; Log("options: bar parts %d (%dx%d) %d (%dx%d) %d (%dx%d), left width %d", a, aw, ah, m, mw, mh, e, ew, eh, off); }
    int h = mh > 0 ? mh : ah;
    int y = textY + (fh - h) / 2;
    PaperColor(0, 0, 0, 255);
    if (a >= 0) PaperSprite(a, textX - off, textY + (fh - ah) / 2);
    if (m >= 0 && mw > 0 && textW > 0) PaperStretch(m, textX, y, textW, mh, 0, 0, mw, mh);
    if (e >= 0) PaperSprite(e, textX + textW, textY + (fh - eh) / 2);
}
// v0.32.1: in the pause menu, the main menu's full-screen notebook (options_bg.tga) below the title bar
static void PaperNotebook(int W, int H)
{
    static int tex = -2, tw = 0, th = 0;
    if (tex == -2) {
        tex = ((int (__cdecl*)(const char*, int))0x50C750)("options_bg.tga", -1);
        if (tex >= 0) ((void (__cdecl*)(int, int*, int*))0x50BEB0)(tex, &tw, &th);
        Log("options: notebook texture %d (%dx%d)", tex, tw, th);
    }
    if (tex < 0 || tw <= 0 || th <= 0) return;
    int top = H * 66 / 800;                          // the game's title bar stays visible above it
    PaperColor(255, 255, 255, 255);
    // v0.32.9: ending exactly on the last screen row put a half-bright line on screen row 0 - stop 2 px short
    PaperStretch(tex, 0, top, W, H - top - 6, 0, th * (top + 6) / H, tw, th);
    // v0.32.10: a faint light line stayed on screen row 0 - paint the top rows in the title bar's dark grey
    // (the notebook texture tinted down to about 26 / 255, like the bar) after the notebook
    PaperColor(44, 44, 44, 255);
    PaperStretch(tex, 0, 0, W, 4, 0, th / 2, tw, th / 2 + 1);
}
static void PaperArrow(int x, int y, bool left)
{
    static int tex = -2, tw = 0, th = 0;
    if (tex == -2) {
        tex = ((int (__cdecl*)(const char*, int))0x50C750)("misc_arrow.tga", -1);
        if (tex >= 0) ((void (__cdecl*)(int, int*, int*))0x50BEB0)(tex, &tw, &th);
        Log("options: arrow texture %d (%dx%d), bar textures %d %d offset %d", tex, tw, th,
            *(volatile int*)0x620B40, *(volatile int*)0x620B48, *(volatile int*)0x620B44);
    }
    if (tex < 0 || tw <= 0) return;
    PaperColor(0, 0, 0, 255);
    ((void (__cdecl*)(int, int, int, int, int, int, int, int, int, int, int, DWORD, DWORD, int))0x510C20)
        (tex, x, y, 16, 16, 0, 0, tw, th, left ? 1 : 0, 0, *(DWORD*)0x6766D4, *(DWORD*)0x6766D8, 0);
}
// v0.32.4: like the game's lists, the selected row's text pulses between white and red (200, 20, 20)
static DWORD PaperPulse()
{
    float t = (float)(GetTickCount() % 3000) / 3000.0f;   // v0.32.8: 3 s per pulse (user choice)
    float f = 0.5f - 0.5f * cosf(t * 6.2831853f);
    int r = (int)(255 + (200 - 255) * f), g = (int)(250 + (20 - 250) * f), b = (int)(250 + (20 - 250) * f);
    return (DWORD)((b << 16) | (g << 8) | r);
}
// v0.72: achievements level page - title, "n of m unlocked", one row per achievement (status | points | title), the
// picked one's description below, button hints at the bottom.
static int EnhHelpWrapped(int x, int y, int w, int lh, const char* s, DWORD rgb);
static void AchDrawLevelPage(int x0, int y, int W, int H, int fh, int lh, int cw, DWORD kInk, DWORD kSoft, DWORD kFaint, bool paper = true, DWORD kPick = 0)
{
    char line[200];
    int L = g_achDetail, ids[40]; int an = AchOfLevel(L, ids, 40), got = 0;
    for (int k = 0; k < an; ++k) if (g_achGot[ids[k]]) ++got;
    int& pick = g_achPick[AchRowOf(L)]; if (pick < 0 || pick >= an) pick = 0;
    snprintf(line, sizeof(line), "%s  -  %d of %d unlocked", L < 0 ? "Whole game" : kAchLevelNames[L], got, an);
    EnhText(x0, y, line, kInk);
    y += lh + lh / 2;
    int xs = x0 + cw, xp = xs + PaperTextW("NOT YET") + cw * 3, xt = xp + PaperTextW("00 pts") + cw * 2;
    int rows = (int)((H * 0.62f - y) / lh); if (rows < 4) rows = 4;
    int first = 0; if (an > rows) { first = pick - rows / 2; if (first < 0) first = 0; if (first > an - rows) first = an - rows; }
    if (first > 0) EnhText(xs, y - lh / 2 - lh / 2, "...", kFaint);
    for (int k = first; k < an && k < first + rows; ++k) {
        const AchDef& a = kAch[ids[k]]; bool g = g_achGot[ids[k]];
        DWORD col = k == pick ? (paper ? PaperPulse() : kPick) : g ? kInk : kFaint;
        EnhText(xs, y, g ? "GOT IT" : "NOT YET", col);
        snprintf(line, sizeof(line), "%d pt%s", a.points, a.points == 1 ? "" : "s"); EnhText(xp, y, line, col);
        if (k == pick && paper) PaperBar(xt, y, fh, PaperTextW(a.title));
        if (k == pick && !paper) EnhText(xs - cw * 2, y, ">", kPick);
        EnhText(xt, y, a.title, col);
        y += lh;
    }
    if (first + rows < an) EnhText(xs, y, "...", kFaint);
    y += lh;
    if (an > 0) {
        const AchDef& a = kAch[ids[pick]];
        const char* d = a.desc;                          // "Crackhouse: Perform ..." -> "Perform ..." (the title says the level)
        if (L >= 0) { size_t ln = strlen(kAchLevelNames[L]); if (!strncmp(d, kAchLevelNames[L], ln) && d[ln] == ':') { d += ln + 1; while (*d == ' ') ++d; } }
        snprintf(line, sizeof(line), "%s%s", d,
                 g_achGot[ids[pick]] ? "" : AchTracked(a) ? "" : "   (the plugin cannot detect this one yet)");
        y += (EnhHelpWrapped(x0, y, W - 2 * x0, lh, line, kInk) - 1) * lh;
    }
    y += lh * 2;
    snprintf(line, sizeof(line), "%s %s  Choose      %s %s  Other level      %s  Back to the list",
             EnhBtn(P_DUP), EnhBtn(P_DDOWN), EnhBtn(P_DLEFT), EnhBtn(P_DRIGHT), EnhBtn(P_EAST));
    EnhText(x0, y, line, kSoft);
}
static void EnhDrawPaper(int W, int H, int fh)
{
    const DWORD kInk = 0x000000, kOnBar = 0xFFFFFF, kSoft = 0x282828, kFaint = 0x3C3C3C;   // v0.32.2: darker
    char line[160];
    int cw = (int)(fh * g_enhCharW + 0.5f); if (cw < 4) cw = 4;
    int lh = H * 20 / 800; if (lh < fh + 2) lh = fh + 2;
    int x0 = W * 60 / 1280, y = H * 104 / 800;
    if (PausedInLevel()) PaperNotebook(W, H);   // v0.32.1: same notebook as the main menu
    // top right, where the game shows its own "A Select / B Back"
    int xr = W - W * 122 / 1280, yr = H * 30 / 800;
    const DWORD kHint = 0xC8EBC8;   // v0.32.5: the game's pale green hint colour, one space after the icon
    snprintf(line, sizeof(line), "%s Change", EnhBtn(P_SOUTH)); EnhText(xr, yr, line, kHint);
    snprintf(line, sizeof(line), "%s Back", EnhBtn(P_EAST));    EnhText(xr, yr + H * 20 / 800, line, kHint);
    // page tabs: the current page sits on the brush bar
    int x = x0;
    int tabsW = 0;   // v0.52: 8 pages - if all tabs do not fit, show previous / current / next + "page n/8"
    const int pcV = EnhPagesVisible();
    for (int p = 0; p < pcV; ++p) tabsW += PaperTextW(kEnhPages[p]) + cw * 3;
    if (x0 + tabsW < W - x0) {
        for (int p = 0; p < pcV; ++p) {
            if (p == g_enhPage) PaperBar(x, y, fh, PaperTextW(kEnhPages[p]));
            EnhText(x, y, kEnhPages[p], p == g_enhPage ? kOnBar : kFaint);
            x += PaperTextW(kEnhPages[p]) + cw * 3;
        }
    } else {
        int pv = (g_enhPage + pcV - 1) % pcV, nx = (g_enhPage + 1) % pcV;
        char t[64];
        snprintf(t, sizeof(t), "< %s", kEnhPages[pv]); EnhText(x, y, t, kFaint); x += PaperTextW(t) + cw * 3;
        PaperBar(x, y, fh, PaperTextW(kEnhPages[g_enhPage])); EnhText(x, y, kEnhPages[g_enhPage], kOnBar);
        x += PaperTextW(kEnhPages[g_enhPage]) + cw * 3;
        snprintf(t, sizeof(t), "%s >", kEnhPages[nx]); EnhText(x, y, t, kFaint); x += PaperTextW(t) + cw * 3;
        snprintf(t, sizeof(t), "(page %d/%d)", g_enhPage + 1, pcV); EnhText(x, y, t, kFaint);
    }
    y += lh * 2;
    if (g_achDetail != -2 && g_enhPage == 5) { AchDrawLevelPage(x0, y, W, H, fh, lh, cw, kInk, kSoft, kFaint); return; }   // v0.72
    static int vis[kEnhCount];
    int n = EnhVisible(g_enhPage, vis);
    // label and value columns sized from this page
    int labelW = 0, valW = 0;
    for (int k = 0; k < n; ++k) {
        const EnhItem& it = kEnh[vis[k]];
        if (it.kind == EK_HEADER) continue;
        if (it.pad >= 0) snprintf(line, sizeof(line), "%s  %s", EnhBtn(it.pad), it.label); else snprintf(line, sizeof(line), "%s", it.label);
        int w = PaperTextW(line); if (w > labelW) labelW = w;
        char v[48]; EnhValueText(it, v, sizeof(v)); w = PaperTextW(v); if (w > valW) valW = w;
    }
    int xl = x0 + cw, xv = xl + labelW + cw * 4, colW = valW + 16 * 2 + cw * 4;
    // scrolling for long pages
    static int scroll = 0, scrollPage = -1;
    if (scrollPage != g_enhPage) { scroll = 0; scrollPage = g_enhPage; }
    int rows = (int)((H * 0.84f - y - lh * 3) / lh); if (rows < 5) rows = 5;
    if (n <= rows) scroll = 0;
    else {
        int want = g_enhSel; if (want > 0 && false) --want;
        if (want < scroll) scroll = want;
        if (g_enhSel >= scroll + rows) scroll = g_enhSel - rows + 1;
        if (scroll > n - rows) scroll = n - rows;
        if (scroll < 0) scroll = 0;
    }
    if (scroll > 0) EnhText(xl, y - lh, "...", kFaint);
    for (int k = scroll; k < n && k < scroll + rows; ++k) {
        const EnhItem& it = kEnh[vis[k]];
        bool sel = k == g_enhSel;
        if (it.kind == EK_HEADER) {                    // v0.56: folding section heading
            int hi = vis[k];
            if (g_secOpen[hi]) snprintf(line, sizeof(line), "-  %s", it.label);
            else snprintf(line, sizeof(line), "+  %s   (%d)", it.label, EnhSectionSize(hi));
            if (sel) PaperBar(x0, y, fh, PaperTextW(line));
            EnhText(x0, y, line, sel ? PaperPulse() : kSoft); y += lh; continue;
        }
        const char* need = EnhNeeds(it);
        if (it.pad >= 0) snprintf(line, sizeof(line), "%s  %s", EnhBtn(it.pad), it.label); else snprintf(line, sizeof(line), "%s", it.label);
        if (sel) PaperBar(xl, y, fh, PaperTextW(line));
        EnhText(xl, y, line, sel ? PaperPulse() : need ? kFaint : kInk);
        if (it.kind == EK_ACTION) {
            if (g_enhPage == 5 && it.label == g_achRowLabel[0]) {}       // v0.70: total row - nothing to "do"
            else if (sel && AchRowLevel(it.label) != -2) { snprintf(line, sizeof(line), "%s  Open", EnhBtn(P_SOUTH)); EnhText(xv, y, line, kInk); }
            else if (sel) { snprintf(line, sizeof(line), "%s  Do it", EnhBtn(P_SOUTH)); EnhText(xv, y, line, kInk); }
        } else {
            char v[48]; EnhValueText(it, v, sizeof(v));
            int ay = y + (fh - 16) / 2;
            PaperArrow(xv, ay, true);
            EnhText(xv + (colW - PaperTextW(v)) / 2, y, v, need ? kFaint : kInk);
            PaperArrow(xv + colW - 16, ay, false);
        }
        y += lh;
    }
    if (scroll + rows < n) EnhText(xl, y, "...", kFaint);
    y += lh;
    // help for the selected row
    if (g_enhSel < n) {
        const EnhItem& it = kEnh[vis[g_enhSel]];
        const char* help = it.help;
        char hb[120];
        if (it.kind == EK_HEADER) help = g_secOpen[vis[g_enhSel]] ? "A or left: close this section" : "A or right: open this section";
        if (it.kind == EK_ACTION && AchRowLevel(it.label) != -2) help = "Press A to see every achievement of this level";   // v0.72
        if (const char* nd = (it.kind == EK_HEADER) ? nullptr : EnhNeeds(it)) help = nd;
        if (it.kind == EK_BIND || it.kind == EK_BIND_HOSTAGE) {
            int a = ActionOf(*(Target*)it.ptr);
            if (a < 0)                                     snprintf(hb, sizeof(hb), "Set in ExtendedCut.ini - left/right picks an action instead");
            else if (a == 0 && it.kind == EK_BIND_HOSTAGE) snprintf(hb, sizeof(hb), "While holding a shield this button does its normal action");
            else if (a == 0)                               snprintf(hb, sizeof(hb), "This button does nothing");
            else snprintf(hb, sizeof(hb), "Sends %s%s", kActions[a].keyName,
                          it.pad == P_BACK && kActions[a].t.value != VK_ESCAPE ? " - keep In-game menu on a button" : "");
            help = hb;
        }
        if (help) y += (EnhHelpWrapped(x0, y, W - 2 * x0, lh, help, kInk) - 1) * lh;
    }
    y += lh * 2;
    snprintf(line, sizeof(line), "%s %s  Tab      %s %s  Section      %s  Defaults", EnhBtn(P_LB), EnhBtn(P_RB), EnhBtn(P_LT), EnhBtn(P_RT), EnhBtn(P_WEST));
    EnhText(x0, y, line, kInk);
    if (g_enhStatus[0] && GetTickCount() - g_enhStatusAt < 3000) EnhText(x0, y + lh, g_enhStatus, kInk);
}
// v0.33: draws pause_controller.tga centred at 4:3 over black; logs once whether the game has the picture loaded
static void EnhLayout(int W, int H, int fh, bool backHint = true)
{
    static int tw = 0, th = 0, texFor = -1;
    int& tex = g_layoutTex;
    int want = (!backHint && g_betaPausePic == 2) ? 2 : 1;   // v0.50.10: 2 = PS2 picture (pause screen only)
    if (texFor != want) { tex = -2; texFor = want; }
    if (tex == -2) {
        // v0.50.10: PS2 = pause_controller_ps2.tga (added to misc.vpp int-pause.ceg by build_pause_ps2.py); missing
        // (a stand-in or a small texture) -> the retail Xbox picture
        if (want == 2) {
            tex = ((int (__cdecl*)(const char*, int))0x50C750)("pause_ctrl_ps2.tga", -1);   // v0.54.2: was pause_controller_ps2.tga (24 chars = crash when the bank loads)
            tw = th = 0;
            if (tex >= 0) ((void (__cdecl*)(int, int*, int*))0x50BEB0)(tex, &tw, &th);
            Log("layout: pause_ctrl_ps2.tga texture %d (%dx%d)%s", tex, tw, th, (tex < 0 || tw < 256) ? " - not installed, Xbox picture used" : "");
            if (tex < 0 || tw < 256) tex = -2; else goto have;
        }
        tex = ((int (__cdecl*)(const char*, int))0x50C750)("pause_controller.tga", -1);
        if (tex >= 0) ((void (__cdecl*)(int, int*, int*))0x50BEB0)(tex, &tw, &th);
        Log("layout: pause_controller.tga texture %d (%dx%d)", tex, tw, th);
    have:;
    }
    if (tex < 0 || tw <= 0 || th <= 0) {
        EnhText(W / 10, H / 2, "The game has not loaded the controller picture here - see ExtendedCut.log", 0xFFFFFF);
        return;
    }
    PaperColor(0, 0, 0, 255);
    PaperStretch(tex, 0, 0, W, H, 0, 0, tw, th);          // black backdrop (texture tinted black)
    int w = H * 4 / 3; if (w > W) w = W;
    int h = w * 3 / 4;
    PaperColor(255, 255, 255, 255);
    PaperStretch(tex, (W - w) / 2, (H - h) / 2, w, h, 0, 0, tw, th);
    if (!backHint) return;
    char line[80]; snprintf(line, sizeof(line), "%s Back", EnhBtn(P_EAST));
    EnhText(W - W * 122 / 1280, H * 30 / 800, line, 0xC8EBC8);
}
static void EnhDraw()
{
    bool plain = PlainPause();
    bool pausePic = plain && g_betaPausePic && g_pausePicLoaded;   // v0.50.9
    if (!g_enhOpen && !(plain && g_enhPauseHint && g_pad) && !pausePic) return;
    if (!PickScale(&g_drawSx, &g_drawSy)) {
        static bool logged = false;
        if (!logged) { logged = true; Log("enhancements: no game text seen yet - menu not drawn"); }
        return;
    }
    g_drawFont = EnhFont();
    RECT rc = {}; HWND w = GetActiveWindow(); if (!w) w = GetForegroundWindow();
    if (!w || !GetClientRect(w, &rc) || rc.right < 320 || rc.bottom < 240) return;
    int W = rc.right, H = rc.bottom;
    int fh = ((int (__cdecl*)(int))0x5185B0)(g_drawFont);
    if (fh <= 4 || fh > 200) fh = 20;
    int lh = fh + fh / 4;
    // v0.31.4: opened from the game's Options = drawn on notebook paper like the game's own menus: black text,
    // dark red selection (colour values are 0xBBGGRR)
    const bool paper = g_enhOpen && g_enhFromOptions;
    const DWORD kWhite = paper ? 0x000000 : 0xFFFFFF, kRed = paper ? 0x1818B0 : 0x3838E0,
                kGrey = paper ? 0x383838 : 0xB0B0B0, kDim = paper ? 0x707070 : 0x808080;
    volatile DWORD* color = (volatile DWORD*)0x91327C;
    DWORD saved = *color;
    char line[160];
    if (!g_enhOpen) {
        if (pausePic) { EnhLayout(W, H, fh, false); PaperColor(255, 255, 255, 255); *color = saved; }   // v0.50.9: replaces the pause screen
        if (!(g_enhPauseHint && g_pad)) { *color = saved; return; }
        snprintf(line, sizeof(line), "%s  Extended Menu", EnhBtn(g_enhOpenBtn));
        EnhText((int)(W * 0.04f), H - lh * 2, line, kGrey);
        *color = saved;
        return;
    }
    if (g_showLayout) { EnhLayout(W, H, fh); PaperColor(255, 255, 255, 255); *color = saved; return; }   // v0.33
    if (paper) { EnhDrawPaper(W, H, fh); PaperColor(255, 255, 255, 255); *color = saved; return; }   // v0.32
    int x0 = (int)(W * g_enhX), y = (int)(H * g_enhY);
    // v0.31.5: from Options, line up with the game's own lists (Controls / Game Settings start at x 60, y ~105 px)
    if (paper) { x0 = W * 60 / 1280; y = H * 104 / 800; }
    // v0.20.1: the game's text coordinates are NOT the window's pixels (v0.20 put the values at 42 % of the
    // window width = off screen), so the layout is measured in characters of the menu font instead.
    // This font is about 0.8 x its height wide per character ([Enhancements] CharWidth).
    int cw = (int)(fh * g_enhCharW + 0.5f); if (cw < 4) cw = 4;
    int firstItem, nItems = EnhPageItems(g_enhPage, &firstItem), longest = 0;
    for (int k = 0; k < nItems; ++k) { int l = (int)strlen(kEnh[firstItem + k].label) + (kEnh[firstItem + k].pad >= 0 ? 3 : 0); if (l > longest) longest = l; }
    int xv = g_enhValCol > 0 ? x0 + (int)(W * g_enhValCol) : x0 + (longest + 5) * cw;
    EnhText(x0, y, "EXTENDED MENU", kWhite);
    y += lh + lh / 2;
    int x = x0;
    int tabsC = 0;   // v0.52: same fallback as the paper menu when the 8 tabs do not fit
    const int pcV = EnhPagesVisible();
    for (int p = 0; p < pcV; ++p) tabsC += (int)strlen(kEnhPages[p]) + 3;
    if (x0 + tabsC * cw < W - x0 / 2) {
        for (int p = 0; p < pcV; ++p) {
            snprintf(line, sizeof(line), p == g_enhPage ? "[%s]" : " %s ", kEnhPages[p]);
            EnhText(x, y, line, p == g_enhPage ? kRed : kDim);
            x += (int)(strlen(line) + 1) * cw;
        }
    } else {
        int pv = (g_enhPage + pcV - 1) % pcV, nx = (g_enhPage + 1) % pcV;
        snprintf(line, sizeof(line), "< %s ", kEnhPages[pv]); EnhText(x, y, line, kDim); x += (int)(strlen(line) + 1) * cw;
        snprintf(line, sizeof(line), "[%s]", kEnhPages[g_enhPage]); EnhText(x, y, line, kRed); x += (int)(strlen(line) + 1) * cw;
        snprintf(line, sizeof(line), " %s >  (page %d/%d)", kEnhPages[nx], g_enhPage + 1, pcV); EnhText(x, y, line, kDim);
    }
    y += lh + lh / 2;
    if (g_achDetail != -2 && g_enhPage == 5) { AchDrawLevelPage(x0, y, W, H, fh, lh, cw, kWhite, kGrey, kDim, false, kRed); *color = saved; return; }   // v0.72
    static int vis[kEnhCount];
    int n = EnhVisible(g_enhPage, vis);
    // v0.29: long pages scroll - keep the selected row (and its heading) on screen
    static int scroll = 0, scrollPage = -1;
    if (scrollPage != g_enhPage) { scroll = 0; scrollPage = g_enhPage; }
    int rows = (int)((H * 0.86f - y - lh * 4) / lh); if (rows < 5) rows = 5;
    if (n <= rows) scroll = 0;
    else {
        int want = g_enhSel; if (want > 0 && false) --want;
        if (want < scroll) scroll = want;
        if (g_enhSel >= scroll + rows) scroll = g_enhSel - rows + 1;
        if (scroll > n - rows) scroll = n - rows;
        if (scroll < 0) scroll = 0;
    }
    if (scroll > 0) { EnhText(x0, y, "   ...", kDim); }
    y += lh;
    for (int k = scroll; k < n && k < scroll + rows; ++k) {
        const EnhItem& it = kEnh[vis[k]];
        bool sel = k == g_enhSel;
        if (it.kind == EK_HEADER) {                    // v0.56: folding section heading
            int hi = vis[k];
            if (g_secOpen[hi]) snprintf(line, sizeof(line), "%s-  %s", sel ? "> " : "", it.label);
            else snprintf(line, sizeof(line), "%s+  %s   (%d)", sel ? "> " : "", it.label, EnhSectionSize(hi));
            EnhText(x0, y, line, sel ? kRed : kGrey); y += lh; continue;
        }
        const char* need = EnhNeeds(it);
        if (it.pad >= 0) snprintf(line, sizeof(line), "%s%s  %s", sel ? "> " : "  ", EnhBtn(it.pad), it.label);
        else             snprintf(line, sizeof(line), "%s%s", sel ? "> " : "  ", it.label);
        EnhText(x0, y, line, sel ? kRed : need ? kDim : kWhite);
        char v[48]; EnhValueText(it, v, sizeof(v));
        if (it.kind == EK_ACTION) snprintf(line, sizeof(line), "%s", sel ? EnhBtn(P_SOUTH) : "");
        else if (sel)             snprintf(line, sizeof(line), "<  %s  >", v);
        else                      snprintf(line, sizeof(line), "   %s", v);
        EnhText(xv, y, line, sel ? kRed : need ? kDim : kWhite);
        y += lh;
    }
    if (scroll + rows < n) EnhText(x0, y, "   ...", kDim);
    y += lh + lh / 2;
    if (g_enhSel < n) {
        const EnhItem& it = kEnh[vis[g_enhSel]];
        const char* help = it.help;
        char hb[120];
        if (it.kind == EK_HEADER) help = g_secOpen[vis[g_enhSel]] ? "A or left: close this section" : "A or right: open this section";
        if (it.kind == EK_ACTION && AchRowLevel(it.label) != -2) help = "Press A to see every achievement of this level";   // v0.72
        if (const char* nd = (it.kind == EK_HEADER) ? nullptr : EnhNeeds(it)) help = nd;
        if (it.kind == EK_BIND || it.kind == EK_BIND_HOSTAGE) {
            int a = ActionOf(*(Target*)it.ptr);
            if (a < 0)                                   snprintf(hb, sizeof(hb), "Set in ExtendedCut.ini - left/right picks an action instead");
            else if (a == 0 && it.kind == EK_BIND_HOSTAGE) snprintf(hb, sizeof(hb), "While holding a shield this button does its normal action");
            else if (a == 0)                             snprintf(hb, sizeof(hb), "This button does nothing");
            else snprintf(hb, sizeof(hb), "Sends %s%s", kActions[a].keyName,
                          it.pad == P_BACK && kActions[a].t.value != VK_ESCAPE ? " - keep In-game menu on a button" : "");
            help = hb;
        }
        if (help) y += (EnhHelpWrapped(x0, y, W - x0 - (int)(W * 0.04f), lh, help, kGrey) - 1) * lh;
    }
    y += lh * 2;
    snprintf(line, sizeof(line), "%s %s change    %s %s tab    %s %s section    %s defaults    %s save & close",
             EnhBtn(P_DLEFT), EnhBtn(P_DRIGHT), EnhBtn(P_LB), EnhBtn(P_RB), EnhBtn(P_LT), EnhBtn(P_RT), EnhBtn(P_WEST), EnhBtn(P_EAST));
    EnhText(x0, y, line, kGrey);
    y += lh;
    if (g_enhStatus[0] && GetTickCount() - g_enhStatusAt < 3000) EnhText(x0, y, g_enhStatus, kWhite);
    else if (g_enhDirty) EnhText(x0, y, "Changed - saved to ExtendedCut.ini when you close", kDim);
    *color = saved;
}

static void __cdecl OnDrawPoint(int src)          // 0 = pause screen, 1 = hints
{
    DWORD t = GetTickCount();
    if (src == 0) {
        ++g_pauseCalls;
        if (t - g_lastPauseDraw < 100) { if (++g_pauseStreak >= 5 && !g_pauseEveryFrame) { g_pauseEveryFrame = true; Log("pause screen is drawn every frame - menu drawn there"); } }
        else g_pauseStreak = 0;
        g_lastPauseDraw = t;
    } else {
        ++g_hintCalls;
        if (g_pauseEveryFrame && t - g_lastPauseDraw < 250) return;   // the pause screen hook draws
    }
    if (g_enhFromOptions || g_showLayout) return;                     // v0.31 / v0.33: drawn from the text hook
    if (g_drawnFrame == g_frameNo) return;                            // once per frame
    g_drawnFrame = g_frameNo;
    static int fails = 0;
    __try { EnhDraw(); } __except (1) { if (fails++ < 5) Log("enhancements: drawing failed (exception)"); }
}
// v0.50.10: with the controller pause screen on, the game's own pause drawing (pause_bg + the "PAUSE" graphic) is
// skipped - only while the plain pause screen shows and the Extended menu is closed (the menu keeps the game's screen)
static int __cdecl PauseDrawSkip()
{
    return (g_betaPausePic && g_pausePicLoaded && !g_enhOpen && g_layoutTex >= 0) ? 1 : 0;
}
static __declspec(naked) void Hook_PauseDraw()
{
    __asm {
        pop  dword ptr [g_retPause]    // caller's return address (not recursive)
        pushad
        call PauseDrawSkip             // v0.50.10: 1 = the controller picture replaces the game's pause screen
        mov  [esp + 0x1C], eax
        popad
        test eax, eax
        jnz  skip_game_pause
        call dword ptr [g_fnPause]     // the game's pause screen, arguments / registers untouched
    skip_game_pause:
        pushad
        push 0
        call OnDrawPoint
        add  esp, 4
        popad
        push dword ptr [g_retPause]
        ret
    }
}
static __declspec(naked) void Hook_HintDraw()
{
    __asm {
        pop  dword ptr [g_retHint]
        call dword ptr [g_fnHint]
        pushad
        push 1
        call OnDrawPoint
        add  esp, 4
        popad
        push dword ptr [g_retHint]
        ret
    }
}
// v0.31: while the menu is open from Options, the game's menu text is hidden and the menu is drawn once per frame
// at the first game text of the frame (works in the main menu too, where the pause-screen hook never runs).
// A new frame = more than 4 ms since the previous game text.
static void __cdecl OnTextGate()
{
    g_skipText = 0;
    if (!g_enhOpen || (!g_enhFromOptions && !g_showLayout)) return;   // v0.33: also for the layout picture
    g_skipText = 1;
    // v0.31.3: draw with the LAST game text of the frame (= after the menu's paper panels and backgrounds):
    // the number of texts per frame is learned from the previous frame; if a frame ends without a draw (the count
    // changed), the menu is drawn at the first text of the next frame instead.
    static LARGE_INTEGER last = {};
    static int count = 0, prevCount = 0; static bool drawn = false;
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    bool newFrame = (now.QuadPart - last.QuadPart) * 1000 > g_qpf.QuadPart * 4;
    last = now;
    bool draw = false;
    if (newFrame) { if (!drawn && count > 0) draw = true; prevCount = count; count = 0; drawn = false; }
    ++count;
    if (!drawn && prevCount > 0 && count == prevCount) draw = true;
    if (!draw || drawn) return;
    drawn = true;
    static int fails = 0;
    __try { EnhDraw(); } __except (1) { if (fails++ < 5) Log("enhancements: drawing failed (exception)"); }
}
static __declspec(naked) void Hook_TextWatch()
{
    __asm {
        pushad
        lea  eax, [esp + 32]           // -> return address, then x, y, text, font, sx, sy
        push eax
        call OnTextDrawn
        add  esp, 4
        call OnTextGate
        popad
        cmp  byte ptr [g_skipOnce], 0       // v0.53: the 2004 HUD drew this text itself
        jne  skiponce
        cmp  byte ptr [g_skipText], 0
        jne  skip
        cmp  byte ptr [g_hudScaling], 0
        jne  scaled
        jmp  dword ptr [g_fnText]      // DrawGameText, unchanged
    scaled:
        jmp  HudText                   // v0.42: HUD text, enlarged (same cdecl arguments)
    skiponce:
        mov  byte ptr [g_skipOnce], 0
    skip:
        xor  eax, eax                  // cdecl: the caller removes the arguments
        ret
    }
}

static void InstallHudScale()
{
    PatchCall(0x45DA12, (void*)Hook_HudRender, 0x465530);
    PatchCallsTo(0x401000, 0x5EB000, 0x510C80, (void*)HudPic80, "picture draw (HUD size)");
    PatchCallsTo(0x401000, 0x5EB000, 0x510C20, (void*)HudPic20, "stretched picture draw (HUD size)");
    PatchCallsTo(0x401000, 0x5EB000, 0x510BE0, (void*)HudQuad, "quad draw (HUD size)");
    PatchCallsTo(0x401000, 0x5EB000, 0x510870, (void*)HudViewport, "clip area (HUD size)");   // v0.82
    Log("hud size: %s (k %.2f at start), screen %dx%d", kHudScaleNames[g_hudScaleIdx], HudScaleNow(), *(volatile int*)0x913250, *(volatile int*)0x913254);
}

static void InstallEnhancements()
{
    char ini[MAX_PATH]; snprintf(ini, sizeof(ini), "%s\\ExtendedCut.ini", g_dir);
    char buf[64];
    GetPrivateProfileStringA("Enhancements", "OpenButton", "Y", buf, sizeof(buf), ini);
    for (char* c = buf; *c; ++c) if (*c == ';' || *c == ' ' || *c == '\t') { *c = 0; break; }
    g_enhOpenBtn = P_NORTH;
    for (int i = 0; i < P_COUNT; ++i) if (!_stricmp(buf, kPadNames[i])) g_enhOpenBtn = i;
    if (g_enhOpenBtn >= P_LS_UP) g_enhOpenBtn = P_NORTH;
    g_enhPauseHint = (int)GetPrivateProfileIntA("Enhancements", "PauseHint", 1, ini);
    auto F = [&](const char* k, float def, float lo, float hi) {
        GetPrivateProfileStringA("Enhancements", k, "", buf, sizeof(buf), ini);
        float v = buf[0] ? (float)atof(buf) : def;
        return v < lo ? lo : v > hi ? hi : v;
    };
    g_enhX = F("MenuX", 0.15f, 0, 0.9f);
    g_enhY = F("MenuY", 0.14f, 0, 0.9f);
    g_enhValCol = F("ValueColumn", 0.0f, 0.0f, 0.9f);             // 0 = automatic (after the longest name)
    g_enhCharW = F("CharWidth", 0.8f, 0.3f, 2.0f);
    InitActions();
    EnsureDefaultsFile();
    GetPrivateProfileStringA("Enhancements", "Font", "Interface", buf, sizeof(buf), ini);
    for (char* c = buf; *c; ++c) if (*c == ';' || *c == ' ' || *c == '\t') { *c = 0; break; }
    g_enhFontSel = !_stricmp(buf, "Title") ? 1 : !_stricmp(buf, "Hud") ? 2 : !_stricmp(buf, "Current") ? 3 : 0;

    g_hookedText  = PatchCallsTo(0x401000, 0x5EB000, 0x5186B0, (void*)Hook_TextWatch, "text draw (watched)");
    g_hookedPause = PatchCallsTo(0x401000, 0x5EB000, 0x461D50, (void*)Hook_PauseDraw, "pause screen draw");
    g_hookedHint  = PatchCallsTo(0x401000, 0x5EB000, 0x467D00, (void*)Hook_HintDraw, "hint draw");
    Log("enhancements menu ready: open with %s on the pause screen", kPadNames[g_enhOpenBtn]);
}

// ---------------------------------------------------------------- v0.9.2: crash reporter
// If the game crashes, write where it happened to ExtendedCut_crash.log (address, registers, stack).
static LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep)
{
    char path[MAX_PATH]; snprintf(path, sizeof(path), "%s\\ExtendedCut_crash.log", g_dir);
    FILE* f = fopen(path, "a");
    if (f && ep && ep->ExceptionRecord && ep->ContextRecord) {
        EXCEPTION_RECORD* r = ep->ExceptionRecord; CONTEXT* c = ep->ContextRecord;
        SYSTEMTIME st; GetLocalTime(&st);
        fprintf(f, "=== crash %02d:%02d:%02d  code %08lX at %08lX", st.wHour, st.wMinute, st.wSecond,
                r->ExceptionCode, (DWORD)(ULONG_PTR)r->ExceptionAddress);
        if (r->ExceptionCode == 0xC0000005 && r->NumberParameters >= 2)
            fprintf(f, "  (%s address %08lX)", r->ExceptionInformation[0] ? "write to" : "read from", (DWORD)r->ExceptionInformation[1]);
        HMODULE m = nullptr; char mod[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)r->ExceptionAddress, &m)) GetModuleFileNameA(m, mod, MAX_PATH);
        fprintf(f, "\n  module %s  (+%lX, base %08lX)\n", mod, (DWORD)(ULONG_PTR)r->ExceptionAddress - (DWORD)(ULONG_PTR)m, (DWORD)(ULONG_PTR)m);   // v0.73.1: offset
        fprintf(f, "  eax %08lX ebx %08lX ecx %08lX edx %08lX esi %08lX edi %08lX ebp %08lX esp %08lX\n",
                c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
        fprintf(f, "  stack:");
        DWORD* sp = (DWORD*)c->Esp;
        for (int i = 0; i < 48; ++i) {
            DWORD v = 0;
            __try { v = sp[i]; } __except (1) { break; }
            if (v >= 0x401000 && v < 0x5F5000) fprintf(f, " %08lX", v);   // likely return addresses in pun.exe
        }
        fprintf(f, "\n  interrogation %08lX, hostage %d, menu %d\n", *(DWORD*)0x7E2DBC, (int)g_holdingHostage, (int)g_menuActive);
    }
    if (f) fclose(f);
    Log("CRASH - see ExtendedCut_crash.log");
    return EXCEPTION_CONTINUE_SEARCH;
}

// v0.29.2: bigger "Permanently Loaded Anim Pool" (holds data\perm_mot.vpp). Retail size 0x1C61A0 (1,860,000 bytes)
// leaves almost no room: adding or lengthening Punisher animations fills it, and a full pool makes the game loop
// forever on the first loading screen. The size is a constant in 20 instructions (allocation in the static
// initializer 0x5F3230, end-of-pool math and the "pool exceeded" checks). Patched here, before pun.exe's CRT
// initializers run (pool base 0x676AF0 still 0). [Settings] AnimPoolKB (default 4096, 0 = leave retail size).
static void PatchAnimPool(const char* ini)
{
    const DWORD kRetail = 0x1C61A0;
    static const DWORD imm[20] = {
        0x4197E8, 0x419960, 0x4199CB, 0x45F2AC, 0x45F3EA, 0x45F619, 0x45F8B1, 0x45F8D5, 0x45F94C, 0x49F0CE,
        0x531F22, 0x531F99, 0x53200F, 0x53208A, 0x532100, 0x5321B1, 0x53225C, 0x5322CC, 0x5323AE, 0x5F3244 };
    int kb = (int)GetPrivateProfileIntA("Settings", "AnimPoolKB", 4096, ini);
    if (kb <= 0) { Log("anim pool: retail size (AnimPoolKB=0)"); return; }
    DWORD size = (DWORD)kb * 1024;
    if (size <= kRetail) { Log("anim pool: AnimPoolKB=%d is not bigger than retail (1816) - left as it is", kb); return; }
    if (size > 64u * 1024 * 1024) size = 64u * 1024 * 1024;
    if (*(volatile DWORD*)0x676AF0 != 0) { Log("anim pool: already created before the plugin loaded - not patched"); return; }
    for (DWORD a : imm)
        if (*(DWORD*)a != kRetail) { Log("anim pool: unexpected bytes at %08X (different pun.exe?) - not patched", a - 1); return; }
    for (DWORD a : imm) {
        DWORD old; VirtualProtect((void*)a, 4, PAGE_EXECUTE_READWRITE, &old);
        *(DWORD*)a = size;
        VirtualProtect((void*)a, 4, old, &old);
    }
    Log("anim pool: %u KB (retail 1816 KB), %d sites patched", size / 1024, 20);
}

// v0.58: Level Pool / Permanent Data Pool (notes\RESEARCH_LIMITS.md). Every level .ceg (all its textures, read whole)
// and most level objects live in ONE fixed bump pool of 0x2080000 bytes (32.5 MB) at [0x676AA8]; when a level does not
// fit, the bank loader 0x50CA6E prints "Pool exceeded" and spins (jmp $) = the loading hang with smooth blacks / big
// textures. Same recipe as the anim pool: the size is an imm32 at 84 sites (+ 12 "size-1" top resets), patched before
// the CRT initializers allocate it (0x5F3214). The Permanent Data Pool (always-loaded banks, 0x922C80) the same way.
int g_levelPoolMB = 0;
static bool PatchPoolSites(const char* what, const DWORD* sites, int n, DWORD retail, DWORD value)
{
    for (int i = 0; i < n; ++i)
        if (*(DWORD*)sites[i] != retail) { Log("%s: unexpected bytes at %08X (different pun.exe?) - not patched", what, sites[i]); return false; }
    for (int i = 0; i < n; ++i) {
        DWORD old; VirtualProtect((void*)sites[i], 4, PAGE_EXECUTE_READWRITE, &old);
        *(DWORD*)sites[i] = value;
        VirtualProtect((void*)sites[i], 4, old, &old);
    }
    return true;
}
static void PatchLevelPools(const char* ini)
{
    static const DWORD lvl[84] = {
        0x438718, 0x438872, 0x4388E6, 0x45F238, 0x45F2D3, 0x462DCB, 0x471141, 0x47A198, 0x47A5F8, 0x47A9F0, 0x47AF38, 0x47B108,
        0x47B2D8, 0x47B4DE, 0x491A3A, 0x4966D2, 0x496B3A, 0x4971DD, 0x497A57, 0x497AFD, 0x498C53, 0x499EE8, 0x49A1C4, 0x49A5C2,
        0x49A939, 0x49AD37, 0x49AEF9, 0x49B169, 0x49B1DC, 0x49B2B8, 0x49B626, 0x49B684, 0x49B6DF, 0x49BA1D, 0x49BD9E, 0x49BFFD,
        0x49C091, 0x49C0F1, 0x49C269, 0x49C2D0, 0x49C44D, 0x49C4D9, 0x49C53D, 0x49C5DB, 0x49C74D, 0x49C803, 0x49D116, 0x49DBEE,
        0x49DD10, 0x49EA44, 0x49F186, 0x4A034C, 0x4A1C74, 0x4A1E54, 0x4A201E, 0x4A2ECD, 0x4A4852, 0x4A4917, 0x4A4A1C, 0x4A4D9B,
        0x4A88CE, 0x4B0B78, 0x4B0C54, 0x4B0E71, 0x4D4B88, 0x4D4C95, 0x4D4EEF, 0x4D4F7F, 0x4D5013, 0x4D5DD1, 0x4D6319, 0x4ED823,
        0x4EED2B, 0x4EEE18, 0x4EEE88, 0x4EEFCB, 0x509750, 0x50CAAD, 0x50FE28, 0x555582, 0x5555FE, 0x55619D, 0x556228,
        0x5F3214 };
    static const DWORD top[12] = { 0x4621C8, 0x471479, 0x485325, 0x49444F, 0x49F578, 0x49F5A2, 0x49F5CC, 0x49F5F6, 0x49F633,
                                   0x49F65D, 0x49F6CD, 0x52792C };
    static const DWORD perm[15] = { 0x461498, 0x461630, 0x462218, 0x462CAF, 0x49F12D, 0x4F885C, 0x50CB06, 0x50F29B, 0x50F303,
                                    0x50FD14, 0x518E8D, 0x524650, 0x524703, 0x5248A2, 0x5F31E4 };
    int mb = (int)GetPrivateProfileIntA("Settings", "LevelPoolMB", 128, ini);   // v0.80: 128 MB by default (Deck: smooth blacks in every heavy level)
    g_levelPoolMB = mb < 0 ? 0 : (mb > 256 ? 256 : mb);
    if (mb > 0) {
        if (mb > 256) mb = 256;
        DWORD size = (DWORD)mb * 1024 * 1024;
        if (size <= 0x2080000) Log("level pool: LevelPoolMB=%d is not bigger than retail (32.5 MB) - left as it is", mb);
        else if (*(volatile DWORD*)0x676AA8 != 0) Log("level pool: already created before the plugin loaded - not patched");
        else {
            // the list may hold a duplicate of the "inside the pool" check; patch each address once
            DWORD uniq[84]; int n = 0;
            for (DWORD a : lvl) { bool dup = false; for (int i = 0; i < n; ++i) if (uniq[i] == a) dup = true; if (!dup && *(DWORD*)a == 0x2080000) uniq[n++] = a; }
            if (n < 84) Log("level pool: only %d of 84 size sites found - not patched", n);
            else if (PatchPoolSites("level pool top", top, 12, 0x207FFFF, size - 1) && PatchPoolSites("level pool", uniq, n, 0x2080000, size))
                Log("level pool: %d MB (retail 32.5 MB), %d + 12 sites patched", mb, n);
        }
    } else Log("level pool: retail size (LevelPoolMB=0)");
    int pmb = (int)GetPrivateProfileIntA("Settings", "PermanentPoolMB", 0, ini);
    if (pmb > 0) {
        if (pmb > 128) pmb = 128;
        DWORD size = (DWORD)pmb * 1024 * 1024;
        if (size <= 0x922C80) Log("permanent pool: PermanentPoolMB=%d is not bigger than retail (9.1 MB) - left as it is", pmb);
        else if (*(volatile DWORD*)0x676A60 != 0) Log("permanent pool: already created - not patched");
        else if (PatchPoolSites("permanent pool", perm, 15, 0x922C80, size)) Log("permanent pool: %d MB (retail 9.1 MB), 15 sites patched", pmb);
    }
}

// v0.59: retail bug fixes + Bone Matrix Pool (notes\RESEARCH_LIMITS.md 2.1 / 3.2). Runs before the CRT initializers.
//  1. Colliding_mesh pool cap check 0x4AD7D9 `cmp [0x6512D8],0x32` reads the ITEM pool counter (header 0x6512C0 +0x18)
//     instead of its own (header 0x628580 +0x18 = 0x628598, see alloc 0x4A9AF0 [ecx+0x73B8]). With >= 50 items it
//     wrongly refuses meshes; with < 50 items and 50 meshes the 51st alloc hits "Pool exceeded" + jmp $ = a hang.
//  2. Bone Matrix Pool (per frame, 0x90 per matrix): 800 -> [Settings] BoneMatrices (default 3200). When full a
//     character is not posed that frame (0x52C389 jg) - many bodies on screen. Sites: malloc 0x5F3304, cap 0x52C385,
//     top reset 0x52C4E1 (size-1) + its static initial value 0x676BE8.
static void PatchRetailFixes(const char* ini)
{
    if (!GetPrivateProfileIntA("Settings", "RetailBugFixes", 1, ini)) { Log("retail fixes: off (RetailBugFixes=0)"); return; }
    if (PatchImm32(0x4AD7DB, 0x006512D8, 0x00628598, "colliding mesh cap counter"))
        Log("retail fix: Colliding_mesh pool cap now checks its own counter (0x4AD7DB)");
    int n = (int)GetPrivateProfileIntA("Settings", "BoneMatrices", 3200, ini);
    if (n > 8000) n = 8000;
    if (n > 800) {
        DWORD size = (DWORD)n * 0x90;
        if (*(volatile DWORD*)0x676BE0 != 0) Log("bone matrix pool: already created - not patched");
        else if (*(DWORD*)0x5F3304 == 0x1C200 && *(DWORD*)0x52C385 == 0x1C200 && *(DWORD*)0x52C4E1 == 0x1C1FF && *(DWORD*)0x676BE8 == 0x1C1FF) {
            PatchImm32(0x5F3304, 0x1C200, size, "bone pool size");
            PatchImm32(0x52C385, 0x1C200, size, "bone pool cap");
            PatchImm32(0x52C4E1, 0x1C1FF, size - 1, "bone pool top");
            PatchImm32(0x676BE8, 0x1C1FF, size - 1, "bone pool top (initial)");
            Log("bone matrix pool: %d matrices per frame (retail 800)", n);
        } else Log("bone matrix pool: unexpected bytes - not patched");
    }
}

// v0.31: "Extended Cut" as a 4th entry of the game's Options list (Controls / Game Settings / Audio/Video).
// The list = 3 text pointers at 0x74BA84 (filled by the static initializer 0x5F01B0 from the string table) given
// to the list builder 0x48D3F0 by the initializer 0x5F0240 (push items, push 3). Another menu's data follows at
// 0x74BA90, so the list moves into the plugin: all 7 references are redirected, the count becomes 4 and the
// paper panel (0x5F0260, height 0x4B = 3 rows of 0x14 + 15) one row taller. Must run before the initializers.
// each entry points at a string-table slot that holds the text pointer (v0.31 crash: text given directly)
const char* g_optText = "Extended Menu";   // v0.32.2: the entry is "Extended Menu"; "Extended Cut" is a tab
// v0.32.3: the menu title bar ("OPTIONS") is drawn by the menu manager at 0x48B6EC with the title font through
// 0x518800 (cdecl: y, x 30, scale 1.2f, 1.2f, text, font, sx, sy) - not the hooked text function. While the menu
// is open from Options, that one call shows "Extended Menu" instead of "Options".
typedef int(__cdecl* TitleText_t)(int, int, DWORD, DWORD, const char*, int, DWORD, DWORD);
// v0.68: achievement pop-up - bottom centre, 5 s each: "ACHIEVEMENT UNLOCKED  (5)" / title / description, white text
// with a dark shadow. Drawn from the HUD (levels) and from the title-text draw (menus, e.g. the debriefing).
static DWORD g_achDrawFrame = 0;
static void AchFrame()
{
    AchTick();
    if (!g_achOn) return;
    DWORD now = GetTickCount();
    if (g_achSummary > 0 && g_achShowing < 0 && g_achQN == 0 && g_achShowAt == 0) { g_achShowing = -2; g_achShowAt = now; }
    if (g_achShowing == -1 && g_achQN > 0) {
        g_achShowing = g_achQueue[0];
        for (int i = 1; i < g_achQN; ++i) g_achQueue[i - 1] = g_achQueue[i];
        --g_achQN; g_achShowAt = now;
        MenuSound(0x73D6BC);
    }
    if (g_achShowing == -1) return;
    if (now - g_achShowAt > 5000) { if (g_achShowing == -2) g_achSummary = 0; g_achShowing = -1; g_achShowAt = 0; return; }
    if (now - g_achDrawFrame < 8) return;                 // once per frame (the title hook can run a few times)
    g_achDrawFrame = now;
    DWORD sx, sy;
    if (!PickScale(&sx, &sy)) return;
    static const DWORD glob[3] = { 0x676054, 0x67604C, 0x676050 };   // interface, title, hud
    int font = -1;
    for (int k : { 0, 2, 1 }) { int f = *(volatile int*)glob[k]; if (f >= 0 && FontSeen(f)) { font = f; break; } }
    if (font < 0) return;
    int saveFont = g_drawFont; DWORD saveSx = g_drawSx, saveSy = g_drawSy, saveCol = *(volatile DWORD*)0x91327C;
    g_drawFont = font; g_drawSx = sx; g_drawSy = sy;
    int W = *(volatile int*)0x913250, H = *(volatile int*)0x913254;
    int fh = ((int (__cdecl*)(int))0x5185B0)(font); if (fh <= 4 || fh > 200) fh = 20;
    char l1[96], l2[160], l3[200];
    if (g_achShowing == -2) {
        snprintf(l1, sizeof(l1), "ACHIEVEMENTS"); snprintf(l2, sizeof(l2), "%d unlocked from your earlier progress", g_achSummary);
        snprintf(l3, sizeof(l3), "All of them: Extended Menu, Achievements page");
    } else {
        const AchDef& a = kAch[g_achShowing];
        snprintf(l1, sizeof(l1), "ACHIEVEMENT UNLOCKED  (%d)", a.points);
        snprintf(l2, sizeof(l2), "%s", a.title); snprintf(l3, sizeof(l3), "%s", a.desc);
    }
    const char* ls[3] = { l1, l2, l3 };
    int y = H - fh * 5;
    for (int i = 0; i < 3; ++i) {
        int w = 0, h = 0;
        ((void (__cdecl*)(int*, int*, const char*, int, int))0x5185E0)(&w, &h, ls[i], -1, font);
        int x = (W - w) / 2; if (x < 4) x = 4;
        EnhText(x + 2, y + 2, ls[i], 0x000000);
        EnhText(x, y, ls[i], i == 0 ? 0x30B0F0 : 0xFFFFFF);   // first line gold (0xBBGGRR)
        y += fh + fh / 4;
    }
    g_drawFont = saveFont; g_drawSx = saveSx; g_drawSy = saveSy; *(volatile DWORD*)0x91327C = saveCol;
}
static int __cdecl Hook_TitleText(int a, int b, DWORD c, DWORD d, const char* text, int font, DWORD sx, DWORD sy)
{
    if (g_enhOpen && g_enhFromOptions && text) {
        __try { if (!_stricmp(text, "Options")) text = "Extended Menu"; } __except (1) {}
    }
    if (g_scoreHudOn) return 0;   // v0.34.1: the scaled combo of the HUD score (0x46B743) - 2004 display draws its own
    int r = ((TitleText_t)0x518800)(a, b, c, d, text, font, sx, sy);
    __try { AchFrame(); } __except (1) {}   // v0.68: achievements in menus
    return r;
}
static const void* g_optItems[4] = { nullptr, nullptr, nullptr, &g_optText };
static bool PatchOptionsList()
{
    struct Ref { DWORD at, was; int slot; };
    static const Ref refs[7] = { { 0x5F01BE, 0x74BA84, 0 }, { 0x5F01DB, 0x74BA84, 0 }, { 0x5F0245, 0x74BA84, 0 },
                                 { 0x5F01ED, 0x74BA88, 1 }, { 0x5F020A, 0x74BA88, 1 },
                                 { 0x5F021C, 0x74BA8C, 2 }, { 0x5F0238, 0x74BA8C, 2 } };
    for (auto& r : refs) if (*(DWORD*)r.at != r.was) { Log("options: unexpected bytes at %08X - entry not added", r.at); return false; }
    if (*(WORD*)0x5F0249 != 0x036A || *(WORD*)0x5F02AF != 0x4B6A) { Log("options: count / height not as expected - entry not added"); return false; }
    DWORD old;
    VirtualProtect((void*)0x5F01B0, 0x110, PAGE_EXECUTE_READWRITE, &old);
    for (auto& r : refs) *(DWORD*)r.at = (DWORD)&g_optItems[r.slot];
    *(BYTE*)0x5F024A = 4;           // 4 entries
    *(BYTE*)0x5F02B0 = 0x4B + 0x14; // panel one row taller
    VirtualProtect((void*)0x5F01B0, 0x110, old, &old);
    Log("options: \"Extended Menu\" entry added to the Options list");
    return true;
}
typedef int(__thiscall* MenuSel_t)(void* self);
static void EnhOpen();
static int __fastcall Hook_OptionsSel(void* self, void*)
{
    int r = ((MenuSel_t)0x478F50)(self);
    Log("options: entry %d picked (list count %d)", r, *(volatile int*)(0x74B528 + 0x168));
    if (r == 3) {                   // our entry: the game's switch (0 / 1 / 2) does nothing for -1
        g_enhFromOptions = true;
        *(volatile int*)(0x74B938 + 0x60) = -1;   // v0.31.4: no selection bar on the hidden Options list
        EnhOpen();
        return -1;
    }
    return r;
}

// ---------------------------------------------------------------- hang reporter (v0.50.4, bug hunt A3)
// pun.exe ends 227 errors with "jmp $" (EB FE): the game freezes on purpose and the message it wrote to 0xBC5538 is
// never shown. A watcher thread samples the game thread every 2 s; on a spin it writes the place + message to
// ExtendedCut_hang.log. For the first 3 minutes it also writes where the game thread is every 10 s (finds any other
// loading hang). Own file = no clash with the game thread's Log().
static DWORD WINAPI HangWatch(LPVOID h)
{
    char path[MAX_PATH]; snprintf(path, sizeof(path), "%s\\ExtendedCut_hang.log", g_dir);
    FILE* f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "Extended Cut hang reporter - game thread samples (EIP = where the game is)\n"); fflush(f);
    DWORD lastEip = 0, t0 = GetTickCount(); int same = 0, tick = 0; bool reported = false;
    for (;;) {
        Sleep(2000); ++tick;
        CONTEXT c = {}; c.ContextFlags = CONTEXT_CONTROL;
        if (SuspendThread((HANDLE)h) == (DWORD)-1) continue;
        BOOL ok = GetThreadContext((HANDLE)h, &c); ResumeThread((HANDLE)h);
        if (!ok) continue;
        bool spin = false; __try { spin = *(WORD*)c.Eip == 0xFEEB; } __except (1) {}
        same = (spin && c.Eip == lastEip) ? same + 1 : 0; lastEip = c.Eip;
        DWORD sec = (GetTickCount() - t0) / 1000;
        if (same == 1 && !reported) {
            char m[256] = ""; __try { strncpy(m, (char*)0xBC5538, 255); } __except (1) {}
            for (char* p = m; *p; ++p) if (*p == '\n' || *p == '\r') *p = ' ';
            fprintf(f, "%5lus HANG: the game stopped itself at %08lX. Its message: \"%s\"\n", sec, c.Eip, m); fflush(f);
            reported = true;
        }
        if (sec < 180 && tick % 5 == 0) { fprintf(f, "%5lus game thread at %08lX\n", sec, c.Eip); fflush(f); }
    }
}

// ---------------------------------------------------------------- entry
BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(mod);
        {   // v0.61: only one copy per game run (scripts\ and scripts\Extended Cut\ both loaded by a newer ASI Loader
            // would hook everything twice). The second copy stays loaded but does nothing.
            char name[64]; snprintf(name, sizeof(name), "ExtendedCut_once_%lu", GetCurrentProcessId());
            HANDLE once = CreateMutexA(nullptr, FALSE, name);
            if (once && GetLastError() == ERROR_ALREADY_EXISTS) { CloseHandle(once); return TRUE; }
        }
        GetModuleFileNameA(mod, g_dir, MAX_PATH);
        if (char* s = strrchr(g_dir, '\\')) *s = 0;
        char logPath[MAX_PATH]; snprintf(logPath, sizeof(logPath), "%s\\ExtendedCut.log", g_dir);
        g_log = fopen(logPath, "w");
        Log("The Punisher: Extended Cut v0.82%s loading (%s)", kBuildKind, g_dir);
        {   // v0.50.4: hang reporter (needs a real handle of this = the game's main thread)
            HANDLE h = nullptr;
            if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &h,
                                THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0))
                CreateThread(nullptr, 0, HangWatch, h, 0, nullptr);
        }
        {   // v0.50.2: every address in this plugin is for the US 1.0 pun.exe without copy protection (2 605 056 bytes,
            // timestamp 0x41C1F8A3, 4 sections). The disc exe (copy protected, 7 sections) and the UK / European exe
            // differ -> stay off instead of writing into the wrong places.
            BYTE* base = (BYTE*)GetModuleHandleA(NULL);
            auto* nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
            DWORD ts = nt->FileHeader.TimeDateStamp; WORD ns = nt->FileHeader.NumberOfSections;
            IMAGE_SECTION_HEADER* sh = IMAGE_FIRST_SECTION(nt);
            bool known = true;                 // .decals = section added by the old Extended Cut exe patch (Deck)
            for (WORD i = 0; i < ns; ++i) {
                char nm[9] = {}; memcpy(nm, sh[i].Name, 8);
                if (strcmp(nm, ".text") && strcmp(nm, ".rdata") && strcmp(nm, ".data") && strcmp(nm, ".rsrc") && strcmp(nm, ".decals"))
                    known = false;
            }
            if (ts != 0x41C1F8A3 || !known) {
                Log("pun.exe not supported (timestamp %08X, %u sections). Needed: US 1.0 pun.exe without copy protection, "
                    "2 605 056 bytes (the one in The Punisher Gore Patch 3.7 US). Extended Cut stays off.", ts, ns);
                CreateThread(NULL, 0, [](LPVOID) -> DWORD {
                    MessageBoxA(NULL, "This pun.exe is not supported, so Extended Cut stays off.\n\n"
                                      "Extended Cut needs the US pun.exe without copy protection (2 605 056 bytes),\n"
                                      "for example the one in The Punisher Gore Patch 3.7 (US).",
                                "The Punisher: Extended Cut", MB_OK | MB_ICONWARNING | MB_TOPMOST);
                    return 0; }, NULL, 0, NULL);
                return TRUE;
            }
        }
        {   // v0.61: a fresh install has only ExtendedCut_defaults.ini -> without ExtendedCut.ini every button read as
            // "Nothing" (tester report Nov 14). Make it from the defaults before anything reads it. (An old
            // PunisherPad.ini is left to the copy further down.)
            char ini[MAX_PATH], def[MAX_PATH], exe1[MAX_PATH], pp[2][MAX_PATH];
            snprintf(ini, MAX_PATH, "%s\\ExtendedCut.ini", g_dir);
            snprintf(def, MAX_PATH, "%s\\ExtendedCut_defaults.ini", g_dir);
            GetModuleFileNameA(NULL, exe1, MAX_PATH); if (char* q = strrchr(exe1, '\\')) *q = 0;
            snprintf(pp[0], MAX_PATH, "%s\\PunisherPad.ini", g_dir);
            snprintf(pp[1], MAX_PATH, "%s\\scripts\\Punisher Pad\\PunisherPad.ini", exe1);
            bool oldPad = GetFileAttributesA(pp[0]) != INVALID_FILE_ATTRIBUTES || GetFileAttributesA(pp[1]) != INVALID_FILE_ATTRIBUTES;
            if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES && !oldPad) {
                if (CopyFileA(def, ini, TRUE)) Log("settings: no ExtendedCut.ini yet - made from ExtendedCut_defaults.ini");
                else Log("settings: NO ExtendedCut.ini and no ExtendedCut_defaults.ini next to the plugin - buttons are not set (%s)", g_dir);
            }
        }
        {   // v0.29.2: must run first, before pun.exe creates its animation pool
            char ini0[MAX_PATH]; snprintf(ini0, sizeof(ini0), "%s\\ExtendedCut.ini", g_dir);
            PatchAnimPool(ini0);
            PatchLevelPools(ini0);   // v0.58
            PatchRetailFixes(ini0);  // v0.59
            InstallBeta(ini0);
            if (FusionFixPresent()) Log("options: Fusion Fix found - its own Options entry stays; Extended Menu opens from the pause screen (Y)");
            else if (PatchOptionsList()) {
                g_optAdded = true;
                PatchCall(0x4870AF, (void*)Hook_OptionsSel, 0x478F50);
                PatchCall(0x477D68, (void*)Hook_TableOpen, 0x526F90);   // gui.tbl
                for (DWORD site : { 0x48B6ECu, 0x464233u, 0x464446u, 0x46B743u })    // v0.32.4: all title-font calls
                    PatchCall(site, (void*)Hook_TitleText, 0x518800);
            }
        }
        // v0.27: renamed from PunisherPad. Your old settings come along once (the old files are left as they are).
        {
            const char* pairs[2][2] = { { "PunisherPad.ini", "ExtendedCut.ini" }, { "PunisherPad_defaults.ini", "ExtendedCut_defaults.ini" } };
            char exe0[MAX_PATH]; GetModuleFileNameA(NULL, exe0, MAX_PATH); if (char* q = strrchr(exe0, '\\')) *q = 0;
            for (auto& pr : pairs) {
                char n[MAX_PATH]; snprintf(n, MAX_PATH, "%s\\%s", g_dir, pr[1]);
                if (GetFileAttributesA(n) != INVALID_FILE_ATTRIBUTES) continue;
                char o[2][MAX_PATH];                       // next to this plugin, or in the old "scripts\Punisher Pad" folder
                snprintf(o[0], MAX_PATH, "%s\\%s", g_dir, pr[0]); snprintf(o[1], MAX_PATH, "%s\\scripts\\Punisher Pad\\%s", exe0, pr[0]);
                for (auto& src : o)
                    if (GetFileAttributesA(src) != INVALID_FILE_ATTRIBUTES && CopyFileA(src, n, TRUE)) { Log("settings: %s copied from your old %s", pr[1], src); break; }
            }
        }
        // the old PunisherPad.asi must not run as well (every hook would be installed twice). The ASI loader may load
        // this plugin first, so look for the old FILE: next to this plugin, in the scripts folder, in "scripts\Punisher Pad".
        {
            char exe[MAX_PATH]; GetModuleFileNameA(NULL, exe, MAX_PATH); if (char* q = strrchr(exe, '\\')) *q = 0;
            char c[4][MAX_PATH]; int nc = 0; bool old = false;
            snprintf(c[nc++], MAX_PATH, "%s\\PunisherPad.asi", g_dir);
            snprintf(c[nc++], MAX_PATH, "%s\\scripts\\PunisherPad.asi", exe);
            snprintf(c[nc++], MAX_PATH, "%s\\scripts\\Punisher Pad\\PunisherPad.asi", exe);
            snprintf(c[nc++], MAX_PATH, "%s\\PunisherPad.asi", exe);
            for (int i = 0; i < nc; ++i) if (GetFileAttributesA(c[i]) != INVALID_FILE_ATTRIBUTES) { old = true; Log("old plugin found: %s", c[i]); }
            if (old || GetModuleHandleA("PunisherPad.asi")) {
                Log("Remove PunisherPad.asi (Extended Cut replaces it). Extended Cut stays off this time.");
                CreateThread(NULL, 0, [](LPVOID) -> DWORD {          // a message box is not safe while the plugin is loading
                    MessageBoxA(NULL, "The old PunisherPad.asi is still in the game's scripts folder.\n\n"
                                      "Remove it (Extended Cut replaces it), then start the game again.\nExtended Cut stays off this time.",
                                "The Punisher: Extended Cut", MB_OK | MB_ICONWARNING | MB_TOPMOST);
                    return 0; }, NULL, 0, NULL);
                return TRUE;
            }
        }
        QueryPerformanceFrequency(&g_qpf); QueryPerformanceCounter(&g_lastUpdate);
        LoadConfig();
        bool b = PatchImport("DINPUT8.dll", "DirectInput8Create", (void*)Hook_DI8Create, (void**)&o_DI8Create);
        Log("DirectInput hook %s", b ? "ok" : "FAILED");
        SetUnhandledExceptionFilter(CrashFilter);
        InstallMenuHook();
        InstallStickHook();
        char ini[MAX_PATH]; snprintf(ini, sizeof(ini), "%s\\ExtendedCut.ini", g_dir);
        g_decalDiag = GetPrivateProfileIntA("Settings", "DecalDiagnostics", 0, ini) != 0;
        g_decalLayers = (int)GetPrivateProfileIntA("Settings", "DecalLayers", 2, ini);
        if (g_decalLayers < 1) g_decalLayers = 1; if (g_decalLayers > 4) g_decalLayers = 4;
        InstallDecalDiag();   // decal wrappers (layers + optional logging)
        InstallGore(ini);     // v0.44: Exploding heads, Blood on clothes, Bullet hit blood ([Gore])
        InstallPromptHook();  // v0.10: controller icons in the hint text
        InstallChoiceStick(); // v0.10: stick picture on the interrogation choice
        InstallConsoleStick(); // v0.28: console-style interrogation stick
        InstallPopupAndFooter(); // v0.12: A/B in pop-up boxes, A/B icons in menu footers
        InstallComboHud();       // v0.34: optional 2004 TOTAL / COMBO / BODY COUNT score display
        InstallShapeGuard();     // v0.38.2: zoo crash 0x5792F3 guard
        InstallWeaponMods();     // v0.35: switch weapon add-ons on / off, M16 launcher HUD icon
        InstallWindowedAndWhispers(ini);   // v0.51: [Display] Windowed, [Beta] FlashbackWhispers
        InstallAlwaysFlashbacks(ini);      // v0.58: [Beta] AlwaysFlashbacks
        PatchCall(0x46796A, (void*)Hook_Token, 0x4670C0);   // v0.13: tutorial texts for the pad
        PatchCall(0x4683C3, (void*)Hook_ExpandText, 0x4678E0); // v0.13.1: no quotes around icons
        PatchCall(0x467C04, (void*)Hook_ExpandText, 0x4678E0);
        PatchCall(0x48488E, (void*)Hook_ExpandText, 0x4678E0);
        PatchCall(0x4C3489, (void*)Hook_ExpandText, 0x4678E0);
        InstallRumble();      // v0.14: rumble (your shots per weapon, camera shakes)
        PatchCall(0x406C9E, (void*)Wrap_CameraUpdate, 0x406A50);   // v0.49.3: photo camera after every camera mode
        InstallHudScale();     // v0.42: HUD size (after every other picture hook, so those keep their call sites)
        InstallEnhancements(); // v0.20: in-game enhancements menu (pause screen)
        Log("decal layers %d, decal logging %s", g_decalLayers, g_decalDiag ? "on" : "off");
    }
    return TRUE;
}
