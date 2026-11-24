// ec_costume_lib.h - v1.58: the game reads the Costume Launcher's library directly (included by ExtendedCut.cpp inside the
// costume loader, after DdsLoad). Kits (Kits\<group>\<name>.json), parts (Parts\<folder (slots)>\<style>.png or a folder of
// numbered pictures), skulls (Skulls\*.png) and shirt backgrounds (Shirt Backgrounds\*.png) become textures here, the same
// way the launcher's kit_textures / make_shirt / skull_files make them (costume_manager.py, shirt_creator.py). Every made
// texture is kept in <plugin>\CostumeCache\<hash>.dds, so it is converted only once (the hash covers the source files'
// sizes + dates and every setting). No more "Send to in-game picker" copies; the old Costumes\ folders are only used when
// no library is found.
#pragma once

// ---------------------------------------------------------------- UTF-8 paths (library names can have any letters)
static std::wstring LibW(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n); return w;
}
static std::string LibU(const std::wstring& w)
{
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr); return s;
}
static std::string LibJoin(const std::string& a, const std::string& b) { return a.empty() ? b : (a.back() == '\\' ? a + b : a + "\\" + b); }
static bool LibIsDir(const std::string& p) { DWORD a = GetFileAttributesW(LibW(p).c_str()); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
static bool LibIsFile(const std::string& p) { DWORD a = GetFileAttributesW(LibW(p).c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
static unsigned long long LibStamp(const std::string& p)      // size + modified time (0 = missing)
{
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(LibW(p).c_str(), GetFileExInfoStandard, &d)) return 0;
    return ((unsigned long long)d.nFileSizeLow * 1000003ULL) ^ (((unsigned long long)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime);
}
static bool LibRead(const std::string& p, std::vector<BYTE>& out)
{
    out.clear();
    FILE* f = _wfopen(LibW(p).c_str(), L"rb"); if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n > 0 && n < (256L << 20)) { out.resize(n); if (fread(out.data(), 1, n, f) != (size_t)n) out.clear(); }
    fclose(f); return !out.empty();
}
static bool LibWrite(const std::string& p, const void* d, size_t n)
{
    std::string tmp = p + ".tmp";
    FILE* f = _wfopen(LibW(tmp).c_str(), L"wb"); if (!f) return false;
    bool ok = fwrite(d, 1, n, f) == n; ok = fclose(f) == 0 && ok;
    if (!ok) { DeleteFileW(LibW(tmp).c_str()); return false; }
    return MoveFileExW(LibW(tmp).c_str(), LibW(p).c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
static void LibList(const std::string& dir, std::vector<std::string>* dirs, std::vector<std::string>* files)
{
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(LibW(LibJoin(dir, "*")).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::string n = LibU(fd.cFileName);
        if (n.empty() || n[0] == '.') continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (dirs) dirs->push_back(n); }
        else if (files) files->push_back(n);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    auto ci = [](const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) < 0; };
    if (dirs) std::sort(dirs->begin(), dirs->end(), ci);
    if (files) std::sort(files->begin(), files->end(), ci);
}
static bool LibEndsI(const std::string& s, const char* suf) { size_t n = strlen(suf); return s.size() >= n && !_stricmp(s.c_str() + s.size() - n, suf); }
static std::string LibStem(const std::string& f) { size_t d = f.rfind('.'); return d == std::string::npos ? f : f.substr(0, d); }
static bool LibIsImg(const std::string& f) { return LibEndsI(f, ".png") || LibEndsI(f, ".dds"); }

// ---------------------------------------------------------------- tiny JSON (kits, warp maps)
struct JVal {
    enum T { NUL, NUM, STR, BOOL, ARR, OBJ } t = NUL; double n = 0; bool b = false; std::string s;
    std::vector<JVal> a; std::vector<std::pair<std::string, JVal>> o;
    const JVal* get(const char* k) const { for (auto& e : o) if (e.first == k) return &e.second; return nullptr; }
    double num(const char* k, double def) const { const JVal* v = get(k); return v && v->t == NUM ? v->n : v && v->t == BOOL ? (v->b ? 1 : 0) : def; }
    std::string str(const char* k) const { const JVal* v = get(k); return v && v->t == STR ? v->s : std::string(); }
};
struct JParse {
    const char* p; const char* e; bool bad = false;
    void ws() { while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p; }
    bool str(std::string& out) {
        if (p >= e || *p != '"') return false; ++p;
        while (p < e && *p != '"') {
            if (*p == '\\' && p + 1 < e) {
                ++p; char c = *p++;
                if (c == 'n') out += '\n'; else if (c == 't') out += '\t'; else if (c == 'r') out += '\r'; else if (c == 'b') out += '\b'; else if (c == 'f') out += '\f';
                else if (c == 'u' && p + 4 <= e) {
                    unsigned u = (unsigned)strtoul(std::string(p, p + 4).c_str(), nullptr, 16); p += 4;
                    if (u >= 0xD800 && u < 0xDC00 && p + 6 <= e && p[0] == '\\' && p[1] == 'u') {
                        unsigned lo = (unsigned)strtoul(std::string(p + 2, p + 6).c_str(), nullptr, 16); p += 6;
                        u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    if (u < 0x80) out += (char)u;
                    else if (u < 0x800) { out += (char)(0xC0 | (u >> 6)); out += (char)(0x80 | (u & 63)); }
                    else if (u < 0x10000) { out += (char)(0xE0 | (u >> 12)); out += (char)(0x80 | ((u >> 6) & 63)); out += (char)(0x80 | (u & 63)); }
                    else { out += (char)(0xF0 | (u >> 18)); out += (char)(0x80 | ((u >> 12) & 63)); out += (char)(0x80 | ((u >> 6) & 63)); out += (char)(0x80 | (u & 63)); }
                } else out += c;
            } else out += *p++;
        }
        if (p >= e) return false; ++p; return true;
    }
    bool val(JVal& v, int depth) {
        ws(); if (p >= e || depth > 64) return false;
        if (*p == '{') {
            v.t = JVal::OBJ; ++p; ws();
            if (p < e && *p == '}') { ++p; return true; }
            for (;;) {
                ws(); std::string k; if (!str(k)) return false; ws();
                if (p >= e || *p != ':') return false; ++p;
                JVal c; if (!val(c, depth + 1)) return false; v.o.push_back({ k, std::move(c) }); ws();
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == '}') { ++p; return true; }
                return false;
            }
        }
        if (*p == '[') {
            v.t = JVal::ARR; ++p; ws();
            if (p < e && *p == ']') { ++p; return true; }
            for (;;) {
                JVal c; if (!val(c, depth + 1)) return false; v.a.push_back(std::move(c)); ws();
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == ']') { ++p; return true; }
                return false;
            }
        }
        if (*p == '"') { v.t = JVal::STR; return str(v.s); }
        if (e - p >= 4 && !strncmp(p, "true", 4)) { v.t = JVal::BOOL; v.b = true; p += 4; return true; }
        if (e - p >= 5 && !strncmp(p, "false", 5)) { v.t = JVal::BOOL; v.b = false; p += 5; return true; }
        if (e - p >= 4 && !strncmp(p, "null", 4)) { v.t = JVal::NUL; p += 4; return true; }
        if (e - p >= 3 && !strncmp(p, "NaN", 3)) { v.t = JVal::NUM; v.n = 0; p += 3; return true; }
        char* end = nullptr; std::string tmp(p, std::min<size_t>(64, e - p)); v.n = strtod(tmp.c_str(), &end);
        if (end == tmp.c_str()) return false;
        v.t = JVal::NUM; p += end - tmp.c_str(); return true;
    }
};
static bool JsonLoad(const std::string& path, JVal& out)
{
    std::vector<BYTE> b; if (!LibRead(path, b)) return false;
    JParse jp; jp.p = (const char*)b.data(); jp.e = jp.p + b.size();
    if (b.size() >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) jp.p += 3;
    return jp.val(out, 0);
}
static std::string JsonQ(const std::string& s)
{
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o + "\"";
}

// ---------------------------------------------------------------- the library
struct LibPart {                              // one Parts\ folder
    std::string folder, label;
    std::vector<std::pair<int, int>> slots;   // (model 0 both / 1 T-shirt / 2 coat, texture number)
    std::vector<std::string> styles, paths;   // style name -> file or folder
    bool tee = false, coat = false;           // which outfit it dresses
    int rank = 0; bool minor = false;         // v1.59: menu order; minor = under "More parts" (teeth, hair, strap, holsters)
};
// v1.59 menu order (user): Face, Neck, Shirt Front (+ Skull / Shirt background), Sleeves, T-shirt Back, Pants, Coat;
// "More parts": Teeth, Strap, Holsters, Hair Cap, Eyelashes, Hair, Hairline Edge
static int LibPartRank(const LibPart& p, bool* minor)
{
    int n = p.slots.empty() ? 99 : p.slots[0].second; *minor = false;
    if (p.coat && !p.tee && p.slots.size() > 1) return 6;          // Coats (c06-c11)
    switch (n) {
    case 1: return 0; case 3: return 1; case 4: return 2; case 8: return 3; case 7: return 4; case 5: return 5;
    }
    *minor = true;
    switch (n) { case 2: return 10; case 6: return 11; case 9: return 12; }
    return 20 + n;                                                   // 10 hair cap, 11 eyelashes, 12 hair, 13 hairline, ...
}
struct LibKit {
    std::string rel, display, file;           // "MAX/Steve Dillon - MAX", "MAX / Steve Dillon - MAX", ...\Kits\MAX\Steve Dillon - MAX.json
    std::vector<std::pair<std::string, std::string>> parts;   // folder -> style
    std::string skull, shirt;                 // Skulls\ file, Shirt Backgrounds\ file
    double fit[8] = { 0.13, 0.83, 0.19, 0.55, 0.90, 1.0, 1.0, 0.0 };   // x0 x1 top aspect bottom extend fabric straighten
    bool hasFit = false;
};
static const char* kFitKeys[8] = { "x0", "x1", "top", "aspect", "bottom", "extend", "fabric", "straighten" };
static std::string g_libDir;                  // ...\Costume Library ("" = none: old Costumes\ folders)
static std::vector<LibPart> g_libParts;
static std::vector<LibKit> g_libKits;
static std::vector<std::string> g_libSkulls, g_libShirts;   // file names in Skulls\ / Shirt Backgrounds\ (sorted)
static int g_libSmoothDarks = 1;              // [Costume] SmoothDarks: near-black fabric uncompressed (no DXT banding)
static bool LibOn() { return !g_libDir.empty(); }

static std::string LibGameDir()
{
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL, exe, MAX_PATH);
    std::string s = LibU(exe); size_t q = s.rfind('\\'); return q == std::string::npos ? s : s.substr(0, q);
}
static std::string LibFromLinux(std::string p)   // "/home/deck/..." -> "Z:\home\deck\..." (Proton maps / to Z:)
{
    while (!p.empty() && (p.back() == '\r' || p.back() == '\n' || p.back() == ' ')) p.pop_back();
    if (!p.empty() && p[0] == '/') { for (char& c : p) if (c == '/') c = '\\'; p = "Z:" + p; }
    return p;
}
static std::string LibLocate(const char* ini, std::string* how)
{
    char v[MAX_PATH] = ""; GetPrivateProfileStringA("Costume", "Library", "", v, sizeof(v), ini);
    for (char* c = v; *c; ++c) if (*c == ';') { *c = 0; break; }
    std::string s = LibFromLinux(v);
    if (!s.empty() && LibIsDir(LibJoin(s, "Kits"))) { *how = "ini [Costume] Library"; return s; }
    std::string game = LibGameDir();
    std::vector<BYTE> b;
    if (LibRead(LibJoin(game, "PunisherCostumes\\library_path.txt"), b)) {
        std::string t((const char*)b.data(), b.size()); size_t nl = t.find('\n'); if (nl != std::string::npos) t.resize(nl);
        t = LibFromLinux(t);
        if (!t.empty() && LibIsDir(LibJoin(t, "Kits"))) { *how = "PunisherCostumes\\library_path.txt (the launcher's)"; return t; }
    }
    size_t q = game.rfind('\\');
    if (q != std::string::npos) {
        std::string guess = LibJoin(game.substr(0, q), "The Punisher (PC) - Costume Launcher\\Costume Library");
        if (LibIsDir(LibJoin(guess, "Kits"))) { *how = "next to the game folder"; return guess; }
    }
    return std::string();
}
static std::vector<std::pair<int, int>> LibSlots(const std::string& folder)   // "(c06-c11)" / "(t09)" / "(02)" at the end
{
    std::vector<std::pair<int, int>> out;
    size_t r = folder.find_last_not_of(' '); if (r == std::string::npos || folder[r] != ')') return out;
    size_t l = folder.rfind('(', r); if (l == std::string::npos) return out;
    std::string in = folder.substr(l + 1, r - l - 1);
    size_t i = 0;
    auto tok = [&](int& model, int& num) -> bool {
        while (i < in.size() && in[i] == ' ') ++i;
        model = 0; if (i < in.size() && (in[i] == 't' || in[i] == 'T')) { model = 1; ++i; } else if (i < in.size() && (in[i] == 'c' || in[i] == 'C')) { model = 2; ++i; }
        while (i < in.size() && in[i] == ' ') ++i;
        if (i >= in.size() || !isdigit((unsigned char)in[i])) return false;
        num = 0; int d = 0; while (i < in.size() && isdigit((unsigned char)in[i]) && d < 2) { num = num * 10 + (in[i] - '0'); ++i; ++d; }
        while (i < in.size() && in[i] == ' ') ++i;
        return true;
    };
    while (i < in.size()) {
        int m, a; if (!tok(m, a)) return std::vector<std::pair<int, int>>();
        int b = a, m2 = m;
        if (i < in.size() && in[i] == '-') { ++i; int mm; if (!tok(mm, b)) return std::vector<std::pair<int, int>>(); m2 = mm; (void)m2; }
        for (int n = (std::min)(a, b); n <= (std::max)(a, b); ++n) if (n >= 1 && n <= 13) out.push_back({ m, n });
        if (i < in.size() && in[i] == ',') { ++i; continue; }
        if (i < in.size()) return std::vector<std::pair<int, int>>();
    }
    return out;
}
static std::string LibLabel(std::string f)     // "Holsters (T) 09 (t09)" -> "Holsters"
{
    size_t p = f.find(" ("); if (p != std::string::npos) f.resize(p);
    while (!f.empty() && (f.back() == ' ' || isdigit((unsigned char)f.back()))) f.pop_back();
    return f;
}
static int LibTexNumber(const std::string& stem)   // "06" / "pun_tshirt_06" / "punps2_trench06" -> 6, else 0
{
    size_t e = stem.size(); while (e && !isdigit((unsigned char)stem[e - 1])) --e;
    size_t b = e; while (b && isdigit((unsigned char)stem[b - 1]) && e - b < 2) --b;
    if (b == e) return 0;
    if (b > 0 && isdigit((unsigned char)stem[b - 1])) return 0;
    return atoi(stem.substr(b, e - b).c_str());
}
static void LibScanKits(const std::string& dir, const std::string& rel)
{
    std::vector<std::string> dirs, files; LibList(dir, &dirs, &files);
    for (auto& f : files) {
        if (!LibEndsI(f, ".json")) continue;
        JVal j; std::string path = LibJoin(dir, f);
        if (!JsonLoad(path, j) || j.t != JVal::OBJ) { Log("costume library: %s could not be read - skipped", path.c_str()); continue; }
        LibKit k; k.file = path;
        std::string nm = LibStem(f);
        k.rel = rel.empty() ? nm : rel + "/" + nm;
        k.display = rel.empty() ? nm : rel; for (size_t i = 0; i < k.display.size(); ++i) if (k.display[i] == '/') { k.display.replace(i, 1, " / "); i += 2; }
        if (!rel.empty()) k.display += " / " + nm;
        if (const JVal* p = j.get("parts")) for (auto& e : p->o) if (e.second.t == JVal::STR && !e.second.s.empty()) k.parts.push_back({ e.first, e.second.s });
        k.skull = j.str("skull"); k.shirt = j.str("shirt");
        if (const JVal* fit = j.get("fit")) if (fit->t == JVal::OBJ) {
            k.hasFit = true;
            for (int i = 0; i < 8; ++i) k.fit[i] = fit->num(kFitKeys[i], k.fit[i]);
        }
        g_libKits.push_back(std::move(k));
    }
    for (auto& d : dirs) if (d[0] != '_') LibScanKits(LibJoin(dir, d), rel.empty() ? d : rel + "/" + d);
}
static void LibScan()
{
    g_libParts.clear(); g_libKits.clear(); g_libSkulls.clear(); g_libShirts.clear();
    if (!LibOn()) return;
    std::vector<std::string> pdirs; LibList(LibJoin(g_libDir, "Parts"), &pdirs, nullptr);
    for (auto& f : pdirs) {
        if (f[0] == '_') continue;
        LibPart p; p.folder = f; p.slots = LibSlots(f); if (p.slots.empty()) continue;
        p.label = LibLabel(f);
        for (auto& s : p.slots) { if (s.first != 2) p.tee = true; if (s.first != 1) p.coat = true; }
        std::string full = LibJoin(LibJoin(g_libDir, "Parts"), f);
        std::vector<std::string> sd, sf; LibList(full, &sd, &sf);
        if (p.slots.size() == 1) for (auto& x : sf) if (x[0] != '_' && LibIsImg(x)) { p.styles.push_back(LibStem(x)); p.paths.push_back(LibJoin(full, x)); }
        for (auto& x : sd) if (x[0] != '_') { p.styles.push_back(x); p.paths.push_back(LibJoin(full, x)); }
        g_libParts.push_back(std::move(p));
    }
    // v1.59: menu order (LibPartRank); LibResolve still applies them by texture number (the old order)
    for (auto& p : g_libParts) p.rank = LibPartRank(p, &p.minor);
    std::stable_sort(g_libParts.begin(), g_libParts.end(), [](const LibPart& a, const LibPart& b) { return a.rank < b.rank; });
    LibScanKits(LibJoin(g_libDir, "Kits"), "");
    std::sort(g_libKits.begin(), g_libKits.end(), [](const LibKit& a, const LibKit& b) { return _stricmp(a.display.c_str(), b.display.c_str()) < 0; });
    std::vector<std::string> f;
    LibList(LibJoin(g_libDir, "Skulls"), nullptr, &f); for (auto& x : f) if (LibEndsI(x, ".png")) g_libSkulls.push_back(x);
    f.clear(); LibList(LibJoin(g_libDir, "Shirt Backgrounds"), nullptr, &f); for (auto& x : f) if (LibEndsI(x, ".png")) g_libShirts.push_back(x);
    Log("costume library: %d kit(s), %d part folder(s), %d skull(s), %d shirt background(s)", (int)g_libKits.size(), (int)g_libParts.size(),
        (int)g_libSkulls.size(), (int)g_libShirts.size());
}
static int LibPartIndex(const std::string& folder)
{
    for (size_t i = 0; i < g_libParts.size(); ++i) if (!_stricmp(g_libParts[i].folder.c_str(), folder.c_str())) return (int)i;
    return -1;
}
static int LibStyleIndex(const LibPart& p, const std::string& style)
{
    for (size_t i = 0; i < p.styles.size(); ++i) if (!_stricmp(p.styles[i].c_str(), style.c_str())) return (int)i;
    return -1;
}

// ---------------------------------------------------------------- Mix parts (the in-game overrides on top of the picked kit)
static const int kMixMax = 16;
int g_mixSel[kMixMax] = {};                   // per g_libParts index: 0 = the kit's, k = style k-1, styles+1 = None (part removed)
int g_mixSkull = 0, g_mixShirt = 0;           // 0 = the kit's; skull: k = g_libSkulls[k-1], last+1 = no skull; shirt: k = g_libShirts[k-1]
static char g_mixLabel[kMixMax][48];
static int MixChanged() { int n = (g_mixSkull != 0) + (g_mixShirt != 0); for (int i = 0; i < kMixMax; ++i) n += g_mixSel[i] != 0; return n; }
static void MixReset() { memset(g_mixSel, 0, sizeof(g_mixSel)); g_mixSkull = g_mixShirt = 0; }

// ---------------------------------------------------------------- texture sources of one choice
enum { SRC_FILE, SRC_SHIRT, SRC_SKULL, SRC_SKULL_ORANGE, SRC_SKULL_METERS };
struct LibSrc {
    int type = SRC_FILE, num = 0; bool coat = false, coatTone = false; std::string a, b;   // file / skull, shirt background
    double fit[8] = {}; std::string warp;     // shirt: fit, "tshirt" / "coat" ("" = no straighten)
};
static std::string LibKey(bool coat, int n) { char k[24]; snprintf(k, sizeof(k), coat ? "punps2_trench%02d" : "pun_tshirt_%02d", n); return k; }
static std::string LibPartFile(const LibPart& p, int style, int n)   // the picture of texture n in a style (file, or folder)
{
    const std::string& src = p.paths[style];
    if (LibIsFile(src)) return src;
    std::vector<std::string> f; LibList(src, nullptr, &f);
    for (auto& x : f) if (LibIsImg(x) && LibTexNumber(LibStem(x)) == n) return LibJoin(src, x);
    return std::string();
}
static const LibKit* LibKitOf(int sel) { return LibOn() && sel > 0 && sel <= (int)g_libKits.size() ? &g_libKits[sel - 1] : nullptr; }
// the skull picked now (kit's or the Mix parts override), "" = none; fit = the kit's, or for an override skull the fit of a
// kit that uses that skull (else the launcher's default fit)
static std::string LibSkullNow(const LibKit& k, double* fit)
{
    for (int i = 0; i < 8; ++i) fit[i] = k.fit[i];
    if (g_mixSkull == 0) return k.skull.empty() ? std::string() : LibJoin(LibJoin(g_libDir, "Skulls"), k.skull);
    if (g_mixSkull > (int)g_libSkulls.size()) return std::string();                     // "No skull"
    const std::string& f = g_libSkulls[g_mixSkull - 1];
    static const double def[8] = { 0.13, 0.83, 0.19, 0.55, 0.90, 1.0, 1.0, 0.0 };
    for (int i = 0; i < 8; ++i) fit[i] = def[i];
    for (auto& o : g_libKits) if (!_stricmp(o.skull.c_str(), f.c_str()) && o.hasFit) { for (int i = 0; i < 8; ++i) fit[i] = o.fit[i]; break; }
    return LibJoin(LibJoin(g_libDir, "Skulls"), f);
}
static std::string LibShirtNow(const LibKit& k)
{
    std::string f = g_mixShirt > 0 && g_mixShirt <= (int)g_libShirts.size() ? g_libShirts[g_mixShirt - 1] : k.shirt;
    if (f.empty()) f = g_libShirts.empty() ? std::string() : g_libShirts[0];
    return f.empty() ? f : LibJoin(LibJoin(g_libDir, "Shirt Backgrounds"), f);
}
static std::vector<std::pair<std::string, LibSrc>> LibResolve(int sel, bool withMix)
{
    std::vector<std::pair<std::string, LibSrc>> out;
    const LibKit* k = LibKitOf(sel); if (!k) return out;
    auto put = [&](const std::string& key, const LibSrc& s) { for (auto& e : out) if (e.first == key) { e.second = s; return; } out.push_back({ key, s }); };
    // apply order = by first texture number, then folder name (as v1.58): a later part wins a shared texture
    std::vector<size_t> order(g_libParts.size()); for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [](size_t a, size_t b) {
        int na = g_libParts[a].slots[0].second, nb = g_libParts[b].slots[0].second;
        return na != nb ? na < nb : _stricmp(g_libParts[a].folder.c_str(), g_libParts[b].folder.c_str()) < 0; });
    for (size_t pi : order) {
        const LibPart& p = g_libParts[pi]; int style = -1;
        if (withMix && pi < (size_t)kMixMax && g_mixSel[pi] > (int)p.styles.size()) continue;   // "None": the kit's part removed
        if (withMix && pi < (size_t)kMixMax && g_mixSel[pi] > 0) style = g_mixSel[pi] - 1;
        else for (auto& kp : k->parts) if (!_stricmp(kp.first.c_str(), p.folder.c_str())) { style = LibStyleIndex(p, kp.second); break; }
        if (style < 0 || style >= (int)p.styles.size()) continue;
        for (auto& sl : p.slots) {
            std::string f = LibPartFile(p, style, sl.second); if (f.empty()) continue;
            LibSrc s; s.type = SRC_FILE; s.num = sl.second; s.a = f;
            if (sl.first != 2) { s.coat = false; put(LibKey(false, sl.second), s); }
            if (sl.first != 1) { s.coat = true; put(LibKey(true, sl.second), s); }
        }
    }
    // the skull shirt (texture 04) where no part covers it
    double fit[8]; std::string skull = withMix ? LibSkullNow(*k, fit) : (k->skull.empty() ? std::string() : LibJoin(LibJoin(g_libDir, "Skulls"), k->skull));
    if (!withMix) for (int i = 0; i < 8; ++i) fit[i] = k->fit[i];
    std::string shirt = withMix ? LibShirtNow(*k) : (k->shirt.empty() ? std::string() : LibJoin(LibJoin(g_libDir, "Shirt Backgrounds"), k->shirt));
    bool overridden = withMix && (g_mixSkull || g_mixShirt);
    std::string saved = k->file.substr(0, k->file.size() - 5) + ".shirt.png";
    for (int coat = 0; coat < 2; ++coat) {
        std::string key = LibKey(coat != 0, 4); bool covered = false;
        for (auto& e : out) if (e.first == key) covered = true;
        if (covered) continue;
        LibSrc s; s.num = 4; s.coat = coat != 0; s.coatTone = s.coat;
        if (!skull.empty() && !shirt.empty() && LibIsFile(skull) && LibIsFile(shirt)) {
            bool straight = fit[7] > 0;
            if (!overridden && LibIsFile(saved) && !(s.coat && straight)) { s.type = SRC_FILE; s.a = saved; }
            else { s.type = SRC_SHIRT; s.a = skull; s.b = shirt; for (int i = 0; i < 8; ++i) s.fit[i] = fit[i]; s.warp = straight ? (s.coat ? "coat" : "tshirt") : ""; }
        } else if (skull.empty() && withMix && g_mixSkull > (int)g_libSkulls.size() && !shirt.empty()) { s.type = SRC_FILE; s.a = shirt; }   // "No skull" = the plain background
        else if (!overridden && LibIsFile(saved)) { s.type = SRC_FILE; s.a = saved; }
        else continue;
        put(key, s);
    }
    // the in-game skulls (plugin [Costume] Skulls = Follow costume)
    if (!skull.empty() && LibIsFile(skull)) {
        LibSrc s; s.a = skull; s.type = SRC_SKULL; put("skull01", s);
        std::string worn = LibJoin(LibJoin(g_libDir, "Skulls\\_worn"), LibStem(skull.substr(skull.rfind('\\') + 1)) + ".png");
        LibSrc w = s; if (LibIsFile(worn)) w.a = worn; put("skull01_worn", w);
        LibSrc o = s; o.type = SRC_SKULL_ORANGE;
        std::string wo = LibJoin(LibJoin(g_libDir, "Skulls\\_worn"), LibStem(skull.substr(skull.rfind('\\') + 1)) + ".orange.png");
        if (LibIsFile(wo)) { o.type = SRC_SKULL; o.a = wo; }
        put("skull_orng", o);
        LibSrc m = s; m.type = SRC_SKULL_METERS; put("meters_skull", m);
    }
    return out;
}
static unsigned long long LibSrcHash(const std::string& key, const LibSrc& s)
{
    unsigned long long h = 1469598103934665603ULL;
    auto mix = [&](const void* p, size_t n) { const BYTE* b = (const BYTE*)p; for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ULL; } };
    auto mixs = [&](const std::string& x) { mix(x.c_str(), x.size() + 1); };
    static const char kVer[] = "ec-lib-v1";
    mixs(kVer); mixs(key); mix(&s.type, sizeof(s.type)); mix(&s.num, sizeof(s.num)); mix(&s.coatTone, 1);
    mixs(s.a); unsigned long long st = LibStamp(s.a); mix(&st, 8);
    mixs(s.b); st = s.b.empty() ? 0 : LibStamp(s.b); mix(&st, 8);
    mix(s.fit, sizeof(s.fit)); mixs(s.warp);
    if (!s.warp.empty()) { st = LibStamp(LibJoin(g_libDir, "..\\" + s.warp + "_warp.json")); mix(&st, 8); }
    mix(&g_libSmoothDarks, sizeof(int)); mix(&g_costumeMaxSize, sizeof(int));
    return h;
}

// ---------------------------------------------------------------- making the textures (launcher's _encode / to_dxt5 / skull_files)
static bool LibDecode(const std::string& path, eci::Image& im)
{
    std::vector<BYTE> b; if (!LibRead(path, b)) { Log("costume library: %s could not be read", path.c_str()); return false; }
    const char* why = nullptr;
    if (!eci::PngDecode(b.data(), b.size(), im.px, im.w, im.h, &why)) { Log("costume library: %s - %s", path.c_str(), why ? why : "?"); return false; }
    return true;
}
static void LibToTex(const eci::Image& im, bool dxt, CostumeTex& t)
{
    t.w = im.w; t.h = im.h; t.mips = 1;
    if (dxt) { t.fmt = MAKEFOURCC('D', 'X', 'T', '5'); t.data = eci::EncodeDXT5(im); }
    else {                                                  // A8R8G8B8 = BGRA bytes
        t.fmt = 21; t.data.resize((size_t)im.w * im.h * 4);
        for (size_t i = 0; i < (size_t)im.w * im.h; ++i) { t.data[i * 4] = im.px[i * 4 + 2]; t.data[i * 4 + 1] = im.px[i * 4 + 1]; t.data[i * 4 + 2] = im.px[i * 4]; t.data[i * 4 + 3] = im.px[i * 4 + 3]; }
    }
}
static void LibEncodePart(eci::Image im, int n, CostumeTex& t)   // the launcher's _encode in picker mode
{
    bool alpha = n >= 11;
    int cap = (std::min)(g_costumeMaxSize, 2048);
    if (g_libSmoothDarks) {
        eci::Image sm = eci::Resize(im, 128, 128); int dark = 0; long long sum = 0;
        for (int i = 0; i < 128 * 128; ++i) { int l = eci::Luma(&sm.px[i * 4]); dark += l < 40; sum += l; }
        if (dark / 16384.0 >= 0.45 || sum / 16384 < 48) {    // near-black fabric: DXT would band it -> 32-bit, up to 1024 (2048 tall)
            int w = eci::Pow2Down(im.w), h = eci::Pow2Down(im.h), uc = 1024;
            while ((long long)w * h > 2LL * uc * uc || (std::max)(w, h) > 2 * uc) { w /= 2; h /= 2; }
            if (w != im.w || h != im.h) im = eci::Resize(im, w, h);
            if (!alpha) for (size_t i = 0; i < (size_t)im.w * im.h; ++i) im.px[i * 4 + 3] = 255;
            LibToTex(im, false, t); return;
        }
    }
    if (alpha) {                                             // hair etc.: keep alpha, PS2 0-128 alpha to 0-255, fill clear pixels
        int mx = 0; for (size_t i = 0; i < (size_t)im.w * im.h; ++i) mx = (std::max)(mx, (int)im.px[i * 4 + 3]);
        if (mx > 0 && mx <= 160) for (size_t i = 0; i < (size_t)im.w * im.h; ++i) im.px[i * 4 + 3] = (BYTE)(std::min)(255, im.px[i * 4 + 3] * 255 / 128);
        double s[3] = { 0, 0, 0 }; long long c = 0;
        for (size_t i = 0; i < (size_t)im.w * im.h; ++i) if (im.px[i * 4 + 3] > 128) { s[0] += im.px[i * 4]; s[1] += im.px[i * 4 + 1]; s[2] += im.px[i * 4 + 2]; ++c; }
        BYTE fill[3] = { 128, 128, 128 }; if (c) for (int k = 0; k < 3; ++k) fill[k] = (BYTE)(s[k] / c);
        for (size_t i = 0; i < (size_t)im.w * im.h; ++i) if (im.px[i * 4 + 3] <= 16) memcpy(&im.px[i * 4], fill, 3);
    }
    int w = eci::Pow2Near(im.w), h = eci::Pow2Near(im.h);
    while ((std::max)(w, h) > cap) { w = (std::max)(4, w / 2); h = (std::max)(4, h / 2); }
    if (w != im.w || h != im.h) im = eci::Resize(im, w, h);
    if (!alpha) for (size_t i = 0; i < (size_t)im.w * im.h; ++i) im.px[i * 4 + 3] = 255;
    LibToTex(im, true, t);
}
static eci::Image LibDecal(const eci::Image& src)   // the launcher's decal_rgba: alpha stretched to full, clear pixels = the skull's mean colour
{
    eci::Image im = src; int mx = 1;
    for (size_t i = 0; i < (size_t)im.w * im.h; ++i) mx = (std::max)(mx, (int)im.px[i * 4 + 3]);
    double s[3] = { 0, 0, 0 }; long long c = 0;
    for (int y = 0; y < im.h; y += 2) for (int x = 0; x < im.w; x += 2) {
        const BYTE* p = &im.px[((size_t)y * im.w + x) * 4];
        if ((std::min)(255, p[3] * 255 / mx) > 128) { s[0] += p[0]; s[1] += p[1]; s[2] += p[2]; ++c; }
    }
    BYTE fill[3] = { 255, 255, 255 }; if (c) for (int k = 0; k < 3; ++k) fill[k] = (BYTE)(s[k] / c);
    for (size_t i = 0; i < (size_t)im.w * im.h; ++i) {
        BYTE* p = &im.px[i * 4]; int a = (std::min)(255, p[3] * 255 / mx);
        if (a <= 16) memcpy(p, fill, 3);
        p[3] = (BYTE)a;
    }
    return im;
}
static eci::Image LibOrange(const eci::Image& src)   // the launcher's orange_skull: brightness -> the game's orange shades
{
    static const double xs[6] = { 0, 60, 110, 170, 230, 255 };
    static const double ys[3][6] = { { 30, 70, 110, 150, 170, 180 }, { 15, 38, 60, 85, 98, 106 }, { 2, 5, 8, 11, 13, 14 } };
    eci::Image o = src;
    for (size_t i = 0; i < (size_t)o.w * o.h; ++i) {
        BYTE* p = &o.px[i * 4]; double l = p[0] * 0.299 + p[1] * 0.587 + p[2] * 0.114;
        int k = 0; while (k < 4 && l > xs[k + 1]) ++k;
        double f = (l - xs[k]) / (xs[k + 1] - xs[k]); f = (std::max)(0.0, (std::min)(1.0, f));
        for (int c = 0; c < 3; ++c) p[c] = (BYTE)(std::max)(0.0, (std::min)(255.0, ys[c][k] + (ys[c][k + 1] - ys[c][k]) * f));
    }
    return o;
}
// ---- the skull shirt (shirt_creator.make without wear): place + teeth + straighten + fabric shade + composite
static void LibRuns(const eci::Image& sk, std::vector<int>& counts)
{
    counts.assign(sk.h, 0);
    for (int y = 0; y < sk.h; ++y) {
        int run = 0, n = 0;
        for (int x = 0; x <= sk.w; ++x) {
            bool on = x < sk.w && sk.px[((size_t)y * sk.w + x) * 4 + 3] > 128;
            if (on) ++run; else { if (run >= 3) ++n; run = 0; }
        }
        counts[y] = n;
    }
}
static int LibBarsStart(const eci::Image& sk)   // first row of the teeth bars, -1 = none
{
    std::vector<int> c; LibRuns(sk, c); int h = sk.h, last = -1;
    for (int y = h - 1; y >= 0; --y) if (c[y] > 0) { last = y; break; }
    if (last < 0) return -1;
    std::vector<int> tail; for (int y = (std::max)(0, last - (int)(h * 0.10)); y <= last; ++y) if (c[y] > 0) tail.push_back(c[y]);
    int n = 0, best = -1; std::vector<int> u = tail; std::sort(u.begin(), u.end()); u.erase(std::unique(u.begin(), u.end()), u.end());
    for (int v : u) { int cnt = (int)std::count(tail.begin(), tail.end(), v); if (cnt > best) { best = cnt; n = v; } }
    if (n < 2) return -1;
    int y = last; while (y > 0 && c[y] != n) --y;
    int bottom = y; while (y > 0 && c[y] == n) --y;
    return (bottom - y) > h * 0.06 ? y + 1 : -1;
}
static eci::Image LibVStack(const eci::Image& top, const eci::Image& bot)
{
    eci::Image o; o.w = top.w; o.h = top.h + bot.h; o.px = top.px; o.px.insert(o.px.end(), bot.px.begin(), bot.px.end()); return o;
}
static long long PyRound(double v) { double f = floor(v), r = v - f; if (r > 0.5) return (long long)f + 1; if (r < 0.5) return (long long)f; return ((long long)f % 2 == 0) ? (long long)f : (long long)f + 1; }
struct WarpMap { bool ok = false; double step = 32; int rows = 0, cols = 0; std::vector<float> du, dv; };
static const WarpMap& LibWarp(const std::string& name)
{
    static WarpMap maps[2]; static unsigned long long stamps[2] = { 0, 0 };
    int i = name == "coat" ? 1 : 0;
    std::string path = LibJoin(g_libDir, "..\\" + name + "_warp.json"); unsigned long long st = LibStamp(path);
    if (st == stamps[i] && (maps[i].ok || !st)) return maps[i];
    stamps[i] = st; WarpMap m; JVal j;
    if (st && JsonLoad(path, j)) {
        const JVal* du = j.get("du"); const JVal* dv = j.get("dv");
        if (du && dv && du->t == JVal::ARR && dv->t == JVal::ARR && !du->a.empty() && du->a.size() == dv->a.size()) {
            m.step = j.num("step", 32); m.rows = (int)du->a.size(); m.cols = (int)du->a[0].a.size(); m.ok = m.cols >= 2 && m.rows >= 2;
            for (int r = 0; r < m.rows && m.ok; ++r) {
                if ((int)du->a[r].a.size() != m.cols || (int)dv->a[r].a.size() != m.cols) { m.ok = false; break; }
                for (int c = 0; c < m.cols; ++c) { m.du.push_back((float)du->a[r].a[c].n); m.dv.push_back((float)dv->a[r].a[c].n); }
            }
        }
    }
    if (!m.ok) Log("costume library: %s_warp.json not found next to the library - skull not straightened", name.c_str());
    maps[i] = m; return maps[i];
}
static eci::Image LibStraighten(const eci::Image& sk, int& x, int& y, int W, int H, double amount, const std::string& warp)
{
    const WarpMap& wp = LibWarp(warp);
    if (amount <= 0 || !wp.ok) return sk;
    double sx = W / 512.0, sy = H / 1024.0; float mu = 0, mv = 0;
    for (float v : wp.du) mu = (std::max)(mu, fabsf(v)); for (float v : wp.dv) mv = (std::max)(mv, fabsf(v));
    int m = (int)ceil((std::max)(mu * sx, mv * sy) * amount) + 2;
    int w = sk.w + 2 * m, h = sk.h + 2 * m; std::vector<float> a((size_t)w * h * 4, 0.0f);
    for (int yy = 0; yy < sk.h; ++yy) for (int xx = 0; xx < sk.w; ++xx) for (int c = 0; c < 4; ++c) a[(((size_t)(yy + m)) * w + xx + m) * 4 + c] = sk.px[((size_t)yy * sk.w + xx) * 4 + c];
    x -= m; y -= m;
    auto G = [&](const std::vector<float>& g, int r, int c) { return g[(size_t)r * wp.cols + c]; };
    std::vector<float> out((size_t)w * h * 4, 0.0f);
    for (int jj = 0; jj < h; ++jj) for (int ii = 0; ii < w; ++ii) {
        double gu = (ii + x) / sx / wp.step, gv = (jj + y) / sy / wp.step;
        int r0 = (std::max)(0, (std::min)((int)floor(gv), wp.rows - 2)), c0 = (std::max)(0, (std::min)((int)floor(gu), wp.cols - 2));
        double fy = (std::max)(0.0, (std::min)(1.0, gv - r0)), fx = (std::max)(0.0, (std::min)(1.0, gu - c0));
        auto bil = [&](const std::vector<float>& g) { return (G(g, r0, c0) * (1 - fy) + G(g, r0 + 1, c0) * fy) * (1 - fx) + (G(g, r0, c0 + 1) * (1 - fy) + G(g, r0 + 1, c0 + 1) * fy) * fx; };
        double srcx = ii + bil(wp.du) * sx * amount, srcy = jj + bil(wp.dv) * sy * amount;
        int x0 = (int)floor(srcx), y0 = (int)floor(srcy); double ffx = srcx - x0, ffy = srcy - y0;
        auto px = [&](int yy, int xx, int c) -> double { return (xx >= 0 && xx < w && yy >= 0 && yy < h) ? a[((size_t)yy * w + xx) * 4 + c] : 0.0; };
        for (int c = 0; c < 4; ++c)
            out[((size_t)jj * w + ii) * 4 + c] = (float)((px(y0, x0, c) * (1 - ffx) + px(y0, x0 + 1, c) * ffx) * (1 - ffy) + (px(y0 + 1, x0, c) * (1 - ffx) + px(y0 + 1, x0 + 1, c) * ffx) * ffy);
    }
    int cx0 = (std::max)(0, -x), cy0 = (std::max)(0, -y), cx1 = w - (std::max)(0, x + w - W), cy1 = h - (std::max)(0, y + h - H);
    eci::Image o; o.w = (std::max)(0, cx1 - cx0); o.h = (std::max)(0, cy1 - cy0); o.px.assign((size_t)o.w * o.h * 4, 0);
    for (int yy = 0; yy < o.h; ++yy) for (int xx = 0; xx < o.w; ++xx) for (int c = 0; c < 4; ++c)
        o.px[((size_t)yy * o.w + xx) * 4 + c] = (BYTE)(std::max)(0.0f, (std::min)(255.0f, out[(((size_t)(yy + cy0)) * w + xx + cx0) * 4 + c] + 0.5f));
    x = (std::max)(0, x); y = (std::max)(0, y);
    return o;
}
static bool LibRenderShirt(const LibSrc& s, eci::Image& outImg)
{
    eci::Image skull, shirt;
    if (!LibDecode(s.a, skull) || !LibDecode(s.b, shirt)) return false;
    int W = shirt.w, H = shirt.h;
    // place(): crop to the alpha box (> 20), squeeze to the shirt box, extend the teeth down to "bottom"
    int bx0 = skull.w, by0 = skull.h, bx1 = -1, by1 = -1;
    for (int y = 0; y < skull.h; ++y) for (int x = 0; x < skull.w; ++x) if (skull.px[((size_t)y * skull.w + x) * 4 + 3] > 20) { bx0 = (std::min)(bx0, x); by0 = (std::min)(by0, y); bx1 = (std::max)(bx1, x); by1 = (std::max)(by1, y); }
    if (bx1 < 0) { Log("costume library: %s has no visible skull (alpha) - plain shirt", s.a.c_str()); outImg = shirt; return true; }
    skull = eci::Crop(skull, bx0, by0, bx1 + 1, by1 + 1);
    double x0 = s.fit[0], x1 = s.fit[1], top = s.fit[2], aspect = s.fit[3] > 0.05 ? s.fit[3] : 0.55, bottom = s.fit[4]; bool extend = s.fit[5] != 0;
    int X0 = (int)(x0 * W), X1 = (int)(x1 * W), T = (int)(top * H), w = (std::max)(1, X1 - X0);
    int h = (std::max)(1, (int)PyRound(w / aspect));
    eci::Image sk = eci::Resize(skull, w, h);
    int extra = (int)(bottom * H) - (T + h);
    if (extend && extra > 0) {
        int yb = LibBarsStart(sk);
        int start = yb < 0 ? (int)(h * 0.75) : yb + (int)(0.15 * (h - yb));
        eci::Image topI = eci::Crop(sk, 0, 0, w, start), botI = eci::Crop(sk, 0, start, w, h);
        sk = LibVStack(topI, eci::Resize(botI, w, (std::max)(1, h - start + extra)));
    }
    X0 = (std::max)(0, X0); T = (std::max)(0, (std::min)(T, H - 1));
    if (sk.w > W - X0 || sk.h > H - T) sk = eci::Crop(sk, 0, 0, (std::min)(sk.w, W - X0), (std::min)(sk.h, H - T));
    int x = X0, y = T;
    if (!s.warp.empty()) sk = LibStraighten(sk, x, y, W, H, s.fit[7], s.warp);
    // fabric_shade(): the shirt's folds and light under the print
    double strength = s.fit[6];
    if (strength > 0 && sk.w > 0 && sk.h > 0) {
        std::vector<float> L((size_t)W * H), big;
        for (size_t i = 0; i < (size_t)W * H; ++i) L[i] = (float)eci::Luma(&shirt.px[i * 4]);
        big = L; eci::GaussianBlur(big, W, H, 40.0f);
        for (float& v : big) v = floorf((std::max)(0.0f, (std::min)(255.0f, v)) + 0.5f);
        std::vector<float> reg; reg.reserve((size_t)sk.w * sk.h);
        for (int yy = 0; yy < sk.h; ++yy) for (int xx = 0; xx < sk.w; ++xx) reg.push_back(big[(size_t)(y + yy) * W + x + xx]);
        std::vector<float> srt = reg; size_t mid = srt.size() / 2;
        std::nth_element(srt.begin(), srt.begin() + mid, srt.end()); double med = srt[mid];
        if (srt.size() % 2 == 0) { double lo = *std::max_element(srt.begin(), srt.begin() + mid); med = (med + lo) / 2; }
        const double eps = 8.0;
        for (int yy = 0; yy < sk.h; ++yy) for (int xx = 0; xx < sk.w; ++xx) {
            double l = L[(size_t)(y + yy) * W + x + xx], b = big[(size_t)(y + yy) * W + x + xx];
            double folds = (l + eps) / (b + eps), light = (std::max)(0.35, (std::min)(1.6, (b + eps) / (med + eps)));
            double shade = 0.88 * pow(folds, 1.6) * pow(light, 1.1); shade = 1 + (shade - 1) * strength; shade = (std::max)(0.15, (std::min)(1.25, shade));
            BYTE* p = &sk.px[((size_t)yy * sk.w + xx) * 4];
            for (int c = 0; c < 3; ++c) p[c] = (BYTE)(std::max)(0.0, (std::min)(255.0, floor(p[c] * shade)));
        }
    }
    // composite onto the shirt (opaque result)
    outImg = shirt;
    for (int yy = 0; yy < sk.h && y + yy < H; ++yy) for (int xx = 0; xx < sk.w && x + xx < W; ++xx) {
        const BYTE* sp = &sk.px[((size_t)yy * sk.w + xx) * 4]; BYTE* dp = outImg.at(x + xx, y + yy); int a = sp[3];
        for (int c = 0; c < 3; ++c) dp[c] = (BYTE)((sp[c] * a + dp[c] * (255 - a) + 127) / 255);
        dp[3] = 255;
    }
    for (size_t i = 0; i < (size_t)W * H; ++i) outImg.px[i * 4 + 3] = 255;
    return true;
}
static void LibCoatTone(eci::Image& im)        // the coat lights its shirt front brighter: tone the light parts down (COAT_TONE 0.7)
{
    for (size_t i = 0; i < (size_t)im.w * im.h; ++i) for (int c = 0; c < 3; ++c) {
        BYTE& v = im.px[i * 4 + c]; if (v > 110) v = (BYTE)(std::min)(255.0, 110 + (v - 110) * 0.7 + 0.5);
    }
}
static bool LibBuild(const std::string& key, const LibSrc& s, CostumeTex& t)
{
    unsigned long long hh = LibSrcHash(key, s);
    char cdir[MAX_PATH], cpath[MAX_PATH]; snprintf(cdir, sizeof(cdir), "%s\\CostumeCache", g_dir); CreateDirectoryA(cdir, nullptr);
    snprintf(cpath, sizeof(cpath), "%s\\%016llx.dds", cdir, hh);
    if (GetFileAttributesA(cpath) != INVALID_FILE_ATTRIBUTES && DdsLoad(cpath, t)) return true;
    DWORD t0 = GetTickCount();
    if (s.type == SRC_FILE && LibEndsI(s.a, ".dds")) {          // a DDS part: used as it is
        std::vector<BYTE> b; return LibRead(s.a, b) && DdsParse(b, s.a.c_str(), t);
    }
    eci::Image im;
    if (s.type == SRC_SHIRT) { if (!LibRenderShirt(s, im)) return false; }
    else if (!LibDecode(s.a, im)) return false;
    if (s.type == SRC_FILE || s.type == SRC_SHIRT) {
        if (s.coatTone && s.num == 4) LibCoatTone(im);
        LibEncodePart(im, s.num, t);
    } else {
        eci::Image d = LibDecal(s.type == SRC_SKULL_ORANGE ? LibOrange(im) : im);
        if (s.type == SRC_SKULL_METERS) LibToTex(eci::Resize(eci::Resize(d, 28, 41), 32, 64), true, t);
        else LibToTex(eci::Resize(d, 512, 512), true, t);
    }
    std::vector<uint8_t> hdr = eci::DdsHeader(t.w, t.h, t.fmt != 21 && t.fmt != 22);
    std::vector<uint8_t> file = hdr; file.insert(file.end(), t.data.begin(), t.data.end());
    if (!LibWrite(cpath, file.data(), file.size())) Log("costume library: could not write the cache file %s", cpath);
    Log("costume library: %s made from %s%s%s (%dx%d %s, %lu ms)", key.c_str(), s.a.substr(g_libDir.size() + (s.a.size() > g_libDir.size() ? 1 : 0)).c_str(),
        s.type == SRC_SHIRT ? " + " : "", s.type == SRC_SHIRT ? s.b.substr(s.b.rfind('\\') + 1).c_str() : "", t.w, t.h, t.fmt == 21 ? "32-bit" : "DXT5", GetTickCount() - t0);
    return true;
}
