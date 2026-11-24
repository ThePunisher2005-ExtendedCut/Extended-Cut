// ec_image.h - small self-contained image helpers for the Extended Cut plugin (v1.58, costume library in game).
// PNG reading (zlib inflate + filters + Adam7, all PNG colour types / bit depths), Lanczos resize (premultiplied alpha),
// Gaussian blur (3 box passes, like Pillow), DXT5 (BC3) encoding and DDS files. No other libraries needed.
// Public domain style: written for this project, no third-party code.
#pragma once
#include <vector>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace eci {

// ---------------------------------------------------------------- inflate (RFC 1951; puff-style canonical Huffman)
struct Huff { short count[16]; short symbol[320]; };
struct Inflater {
    const uint8_t* in; size_t inLen, pos = 0; uint32_t bitbuf = 0; int bitcnt = 0; bool bad = false;
    std::vector<uint8_t>* out;
    int bits(int need) {
        uint32_t val = bitbuf;
        while (bitcnt < need) {
            if (pos >= inLen) { bad = true; return 0; }
            val |= (uint32_t)in[pos++] << bitcnt; bitcnt += 8;
        }
        bitbuf = val >> need; bitcnt -= need;
        return (int)(val & ((1u << need) - 1));
    }
    int decode(const Huff& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= bits(1); if (bad) return -1;
            int count = h.count[len];
            if (code - count < first) return h.symbol[index + (code - first)];
            index += count; first += count; first <<= 1; code <<= 1;
        }
        bad = true; return -1;
    }
    static int build(Huff& h, const short* length, int n) {
        for (int len = 0; len < 16; ++len) h.count[len] = 0;
        for (int s = 0; s < n; ++s) h.count[length[s]]++;
        if (h.count[0] == n) return 0;
        int left = 1;
        for (int len = 1; len < 16; ++len) { left <<= 1; left -= h.count[len]; if (left < 0) return left; }
        short offs[16]; offs[1] = 0;
        for (int len = 1; len < 15; ++len) offs[len + 1] = offs[len] + h.count[len];
        for (int s = 0; s < n; ++s) if (length[s]) h.symbol[offs[length[s]]++] = (short)s;
        return left;
    }
    bool codes(const Huff& lencode, const Huff& distcode) {
        static const short lbase[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
        static const short lext[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
        static const short dbase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
        static const short dext[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
        for (;;) {
            int sym = decode(lencode); if (sym < 0 || bad) return false;
            if (sym < 256) out->push_back((uint8_t)sym);
            else if (sym == 256) return true;
            else {
                sym -= 257; if (sym >= 29) return false;
                int len = lbase[sym] + bits(lext[sym]);
                int ds = decode(distcode); if (ds < 0 || ds >= 30 || bad) return false;
                size_t dist = (size_t)dbase[ds] + bits(dext[ds]);
                if (bad || dist > out->size()) return false;
                size_t from = out->size() - dist;
                for (int i = 0; i < len; ++i) out->push_back((*out)[from + i]);
            }
        }
    }
    bool stored() {
        bitbuf = 0; bitcnt = 0;
        if (pos + 4 > inLen) return false;
        unsigned len = in[pos] | (in[pos + 1] << 8), nlen = in[pos + 2] | (in[pos + 3] << 8); pos += 4;
        if (len != (~nlen & 0xFFFF) || pos + len > inLen) return false;
        out->insert(out->end(), in + pos, in + pos + len); pos += len; return true;
    }
    bool fixed() {
        static Huff lc, dc; static bool made = false;
        if (!made) {
            short l[320]; int s = 0;
            for (; s < 144; ++s) l[s] = 8; for (; s < 256; ++s) l[s] = 9; for (; s < 280; ++s) l[s] = 7; for (; s < 288; ++s) l[s] = 8;
            build(lc, l, 288);
            for (s = 0; s < 30; ++s) l[s] = 5;
            build(dc, l, 30); made = true;
        }
        return codes(lc, dc);
    }
    bool dynamic() {
        static const short order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
        int nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
        if (bad || nlen > 286 || ndist > 30) return false;
        short lengths[320]; int index;
        for (index = 0; index < ncode; ++index) lengths[order[index]] = (short)bits(3);
        for (; index < 19; ++index) lengths[order[index]] = 0;
        Huff lc, dc;
        if (build(lc, lengths, 19) != 0) return false;
        index = 0;
        while (index < nlen + ndist) {
            int sym = decode(lc); if (sym < 0 || bad) return false;
            if (sym < 16) lengths[index++] = (short)sym;
            else {
                short len = 0; int rep;
                if (sym == 16) { if (index == 0) return false; len = lengths[index - 1]; rep = 3 + bits(2); }
                else if (sym == 17) rep = 3 + bits(3);
                else rep = 11 + bits(7);
                if (index + rep > nlen + ndist) return false;
                while (rep--) lengths[index++] = len;
            }
        }
        if (lengths[256] == 0) return false;
        int err = build(lc, lengths, nlen);
        if (err < 0 || (err > 0 && nlen - lc.count[0] != 1)) return false;
        err = build(dc, lengths + nlen, ndist);
        if (err < 0 || (err > 0 && ndist - dc.count[0] != 1)) return false;
        return codes(lc, dc);
    }
    bool run() {
        int last;
        do {
            last = bits(1); int type = bits(2); if (bad) return false;
            bool ok = type == 0 ? stored() : type == 1 ? fixed() : type == 2 ? dynamic() : false;
            if (!ok) return false;
        } while (!last);
        return true;
    }
};
inline bool Inflate(const uint8_t* zlib, size_t n, std::vector<uint8_t>& out, size_t reserve)
{
    if (n < 2 || (zlib[0] & 15) != 8 || ((zlib[0] << 8) | zlib[1]) % 31) return false;
    out.clear(); out.reserve(reserve);
    Inflater f; f.in = zlib + 2; f.inLen = n - 2; f.out = &out;
    return f.run();
}

// ---------------------------------------------------------------- PNG -> RGBA8 (8 bits per channel, straight alpha)
inline uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
inline bool PngDecode(const uint8_t* d, size_t n, std::vector<uint8_t>& rgba, int& W, int& H, const char** why = nullptr)
{
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    auto fail = [&](const char* m) { if (why) *why = m; return false; };
    if (n < 33 || memcmp(d, sig, 8)) return fail("not a PNG file");
    size_t p = 8; int bitDepth = 0, colorType = 0, interlace = 0; W = H = 0;
    std::vector<uint8_t> idat, pal, trns;
    while (p + 12 <= n) {
        uint32_t len = be32(d + p); const uint8_t* type = d + p + 4; const uint8_t* data = d + p + 8;
        if (len > n - p - 12) return fail("PNG cut short");
        if (!memcmp(type, "IHDR", 4) && len >= 13) {
            W = (int)be32(data); H = (int)be32(data + 4); bitDepth = data[8]; colorType = data[9]; interlace = data[12];
        } else if (!memcmp(type, "PLTE", 4)) pal.assign(data, data + len);
        else if (!memcmp(type, "tRNS", 4)) trns.assign(data, data + len);
        else if (!memcmp(type, "IDAT", 4)) idat.insert(idat.end(), data, data + len);
        else if (!memcmp(type, "IEND", 4)) break;
        p += 12 + len;
    }
    if (W <= 0 || H <= 0 || W > 16384 || H > 16384) return fail("bad PNG size");
    int chans = colorType == 0 ? 1 : colorType == 2 ? 3 : colorType == 3 ? 1 : colorType == 4 ? 2 : colorType == 6 ? 4 : 0;
    if (!chans || !(bitDepth == 1 || bitDepth == 2 || bitDepth == 4 || bitDepth == 8 || bitDepth == 16)) return fail("PNG format not supported");
    if (colorType == 3 && pal.size() < 3) return fail("PNG palette missing");
    int bppBits = chans * bitDepth, bpp = (bppBits + 7) / 8;
    size_t expect = 0;
    static const int ax[7] = { 0, 4, 0, 2, 0, 1, 0 }, ay[7] = { 0, 0, 4, 0, 2, 0, 1 }, dx[7] = { 8, 8, 4, 4, 2, 2, 1 }, dy[7] = { 8, 8, 8, 4, 4, 2, 2 };
    int passes = interlace ? 7 : 1;
    for (int ps = 0; ps < passes; ++ps) {
        int pw = interlace ? (W - ax[ps] + dx[ps] - 1) / dx[ps] : W, ph = interlace ? (H - ay[ps] + dy[ps] - 1) / dy[ps] : H;
        if (pw > 0 && ph > 0) expect += (size_t)ph * (1 + ((size_t)pw * bppBits + 7) / 8);
    }
    std::vector<uint8_t> raw;
    if (!Inflate(idat.data(), idat.size(), raw, expect) || raw.size() < expect) return fail("PNG data damaged");
    rgba.assign((size_t)W * H * 4, 0);
    size_t at = 0;
    for (int ps = 0; ps < passes; ++ps) {
        int pw = interlace ? (W - ax[ps] + dx[ps] - 1) / dx[ps] : W, ph = interlace ? (H - ay[ps] + dy[ps] - 1) / dy[ps] : H;
        if (pw <= 0 || ph <= 0) continue;
        size_t stride = ((size_t)pw * bppBits + 7) / 8;
        std::vector<uint8_t> prev(stride, 0), cur(stride);
        for (int y = 0; y < ph; ++y) {
            int ft = raw[at++]; memcpy(cur.data(), &raw[at], stride); at += stride;
            for (size_t i = 0; i < stride; ++i) {
                int a = i >= (size_t)bpp ? cur[i - bpp] : 0, b = prev[i], c = i >= (size_t)bpp ? prev[i - bpp] : 0, v = cur[i];
                switch (ft) {
                case 1: v += a; break; case 2: v += b; break; case 3: v += (a + b) >> 1; break;
                case 4: { int pp = a + b - c, pa = abs(pp - a), pb = abs(pp - b), pc = abs(pp - c); v += (pa <= pb && pa <= pc) ? a : pb <= pc ? b : c; } break;
                default: break;
                }
                cur[i] = (uint8_t)v;
            }
            for (int x = 0; x < pw; ++x) {
                int X = interlace ? ax[ps] + x * dx[ps] : x, Y = interlace ? ay[ps] + y * dy[ps] : y;
                uint8_t* o = &rgba[((size_t)Y * W + X) * 4];
                auto sample = [&](int ch) -> int {                   // channel ch of pixel x, scaled to 8 bits
                    if (bitDepth == 8) return cur[(size_t)x * chans + ch];
                    if (bitDepth == 16) return cur[((size_t)x * chans + ch) * 2];
                    int per = 8 / bitDepth, byte = cur[(size_t)x / per], sh = 8 - bitDepth * (x % per + 1);
                    return (byte >> sh) & ((1 << bitDepth) - 1);
                };
                auto raw16 = [&](int ch) -> int { return bitDepth == 16 ? (cur[((size_t)x * chans + ch) * 2] << 8) | cur[((size_t)x * chans + ch) * 2 + 1] : sample(ch); };
                if (colorType == 3) {
                    int ix = sample(0); size_t pi = (size_t)ix * 3;
                    if (pi + 2 < pal.size()) { o[0] = pal[pi]; o[1] = pal[pi + 1]; o[2] = pal[pi + 2]; }
                    o[3] = (size_t)ix < trns.size() ? trns[ix] : 255;
                } else if (colorType == 0) {
                    int v = sample(0); int g = bitDepth < 8 ? v * 255 / ((1 << bitDepth) - 1) : v;
                    o[0] = o[1] = o[2] = (uint8_t)g; o[3] = 255;
                    if (trns.size() >= 2 && raw16(0) == ((trns[0] << 8) | trns[1])) o[3] = 0;
                } else if (colorType == 2) {
                    o[0] = (uint8_t)sample(0); o[1] = (uint8_t)sample(1); o[2] = (uint8_t)sample(2); o[3] = 255;
                    if (trns.size() >= 6 && raw16(0) == ((trns[0] << 8) | trns[1]) && raw16(1) == ((trns[2] << 8) | trns[3]) && raw16(2) == ((trns[4] << 8) | trns[5])) o[3] = 0;
                } else if (colorType == 4) {
                    o[0] = o[1] = o[2] = (uint8_t)sample(0); o[3] = (uint8_t)sample(1);
                } else {
                    o[0] = (uint8_t)sample(0); o[1] = (uint8_t)sample(1); o[2] = (uint8_t)sample(2); o[3] = (uint8_t)sample(3);
                }
            }
            std::swap(prev, cur);
        }
    }
    return true;
}

// ---------------------------------------------------------------- RGBA image + Lanczos resize (premultiplied alpha)
struct Image { int w = 0, h = 0; std::vector<uint8_t> px; uint8_t* at(int x, int y) { return &px[((size_t)y * w + x) * 4]; } };
inline double Sinc(double x) { if (x == 0) return 1; x *= 3.14159265358979323846; return sin(x) / x; }
inline double Lanczos3(double x) { return (x > -3 && x < 3) ? Sinc(x) * Sinc(x / 3) : 0; }
// one axis: for each output index, first source index + weights (Pillow's "precompute_coeffs")
inline void Coeffs(int inSize, int outSize, std::vector<int>& first, std::vector<int>& cnt, std::vector<double>& w, int& ksize)
{
    double scale = (double)inSize / outSize, filterscale = scale < 1 ? 1 : scale, support = 3 * filterscale;
    ksize = (int)ceil(support) * 2 + 1;
    first.assign(outSize, 0); cnt.assign(outSize, 0); w.assign((size_t)outSize * ksize, 0);
    for (int xx = 0; xx < outSize; ++xx) {
        double center = (xx + 0.5) * scale, ss = 1.0 / filterscale, ww = 0;
        int xmin = (int)(center - support + 0.5); if (xmin < 0) xmin = 0;
        int xmax = (int)(center + support + 0.5); if (xmax > inSize) xmax = inSize; xmax -= xmin;
        double* k = &w[(size_t)xx * ksize];
        for (int x = 0; x < xmax; ++x) { double v = Lanczos3((x + xmin - center + 0.5) * ss); k[x] = v; ww += v; }
        if (ww != 0) for (int x = 0; x < xmax; ++x) k[x] /= ww;
        first[xx] = xmin; cnt[xx] = xmax;
    }
}
inline Image Resize(const Image& src, int W, int H)
{
    Image out; out.w = W; out.h = H; out.px.assign((size_t)W * H * 4, 0);
    if (src.w <= 0 || src.h <= 0 || W <= 0 || H <= 0) return out;
    if (W == src.w && H == src.h) { out.px = src.px; return out; }
    // premultiply
    std::vector<float> pm((size_t)src.w * src.h * 4);
    for (size_t i = 0; i < (size_t)src.w * src.h; ++i) {
        float a = src.px[i * 4 + 3] / 255.0f;
        pm[i * 4] = src.px[i * 4] * a; pm[i * 4 + 1] = src.px[i * 4 + 1] * a; pm[i * 4 + 2] = src.px[i * 4 + 2] * a; pm[i * 4 + 3] = (float)src.px[i * 4 + 3];
    }
    std::vector<int> fx, cx, fy, cy; std::vector<double> wx, wy; int kx, ky;
    Coeffs(src.w, W, fx, cx, wx, kx); Coeffs(src.h, H, fy, cy, wy, ky);
    std::vector<float> tmp((size_t)W * src.h * 4);
    for (int y = 0; y < src.h; ++y)
        for (int x = 0; x < W; ++x) {
            double s[4] = { 0, 0, 0, 0 }; const double* k = &wx[(size_t)x * kx];
            for (int i = 0; i < cx[x]; ++i) { const float* p = &pm[((size_t)y * src.w + fx[x] + i) * 4]; for (int c = 0; c < 4; ++c) s[c] += p[c] * k[i]; }
            float* t = &tmp[((size_t)y * W + x) * 4]; for (int c = 0; c < 4; ++c) t[c] = (float)s[c];
        }
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            double s[4] = { 0, 0, 0, 0 }; const double* k = &wy[(size_t)y * ky];
            for (int i = 0; i < cy[y]; ++i) { const float* p = &tmp[((size_t)(fy[y] + i) * W + x) * 4]; for (int c = 0; c < 4; ++c) s[c] += p[c] * k[i]; }
            double a = s[3] < 0 ? 0 : s[3] > 255 ? 255 : s[3];
            uint8_t* o = &out.px[((size_t)y * W + x) * 4];
            for (int c = 0; c < 3; ++c) { double v = a > 0 ? s[c] * 255.0 / a : 0; o[c] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5); }
            o[3] = (uint8_t)(a + 0.5);
        }
    return out;
}
inline Image Crop(const Image& s, int x0, int y0, int x1, int y1)   // [x0, x1) x [y0, y1), clipped
{
    x0 = (std::max)(0, x0); y0 = (std::max)(0, y0); x1 = (std::min)(s.w, x1); y1 = (std::min)(s.h, y1);
    Image o; o.w = (std::max)(0, x1 - x0); o.h = (std::max)(0, y1 - y0); o.px.assign((size_t)o.w * o.h * 4, 0);
    for (int y = 0; y < o.h; ++y) memcpy(&o.px[(size_t)y * o.w * 4], &s.px[(((size_t)(y0 + y)) * s.w + x0) * 4], (size_t)o.w * 4);
    return o;
}

// ---------------------------------------------------------------- Gaussian blur of one 8-bit channel (Pillow: 3 box passes)
inline void BoxPass(std::vector<float>& a, int w, int h, float r, bool horiz)
{
    // Pillow's extended box blur: radius r (float) -> integer radius + edge weight
    int ri = (int)r; float rem = r - ri, ww = 1.0f / (2 * r + 1);
    int n = horiz ? w : h, lines = horiz ? h : w;
    std::vector<float> line(n), res(n);
    for (int l = 0; l < lines; ++l) {
        for (int i = 0; i < n; ++i) line[i] = horiz ? a[(size_t)l * w + i] : a[(size_t)i * w + l];
        auto px = [&](int i) { return line[i < 0 ? 0 : i >= n ? n - 1 : i]; };
        for (int i = 0; i < n; ++i) {
            float s = 0; for (int k = -ri; k <= ri; ++k) s += px(i + k);
            s += (px(i - ri - 1) + px(i + ri + 1)) * rem;
            res[i] = s * ww;
        }
        for (int i = 0; i < n; ++i) { if (horiz) a[(size_t)l * w + i] = res[i]; else a[(size_t)i * w + l] = res[i]; }
    }
}
inline void BoxPassFast(std::vector<float>& a, int w, int h, float r, bool horiz)   // running sum version (same result)
{
    int ri = (int)r; float rem = r - ri, ww = 1.0f / (2 * r + 1);
    int n = horiz ? w : h, lines = horiz ? h : w;
    std::vector<float> line(n), res(n);
    for (int l = 0; l < lines; ++l) {
        for (int i = 0; i < n; ++i) line[i] = horiz ? a[(size_t)l * w + i] : a[(size_t)i * w + l];
        auto px = [&](int i) { return line[i < 0 ? 0 : i >= n ? n - 1 : i]; };
        double s = 0; for (int k = -ri; k <= ri; ++k) s += px(k);
        for (int i = 0; i < n; ++i) {
            res[i] = (float)((s + (px(i - ri - 1) + px(i + ri + 1)) * rem) * ww);
            s += px(i + ri + 1) - px(i - ri);
        }
        for (int i = 0; i < n; ++i) { if (horiz) a[(size_t)l * w + i] = res[i]; else a[(size_t)i * w + l] = res[i]; }
    }
}
inline void GaussianBlur(std::vector<float>& a, int w, int h, float radius)
{
    // Pillow: _gaussian_blur_radius(radius, passes=3): sigma2 = radius^2 / passes; L = sqrt(12*sigma2 + 1); l = floor((L-1)/2)
    // a = (2l+1)*(l*(l+1) - 3*sigma2); a /= 6*(sigma2 - (l+1)^2); box radius = l + a
    float sigma2 = radius * radius / 3.0f, L = sqrtf(12 * sigma2 + 1), l = floorf((L - 1) / 2);
    float aa = (2 * l + 1) * (l * (l + 1) - 3 * sigma2); aa /= 6 * (sigma2 - (l + 1) * (l + 1));
    float br = l + aa;
    for (int p = 0; p < 3; ++p) BoxPassFast(a, w, h, br, true);
    for (int p = 0; p < 3; ++p) BoxPassFast(a, w, h, br, false);
}

// ---------------------------------------------------------------- DXT5 (BC3) encoder
inline uint16_t To565(int r, int g, int b) { return (uint16_t)(((r * 31 + 127) / 255) << 11 | ((g * 63 + 127) / 255) << 5 | ((b * 31 + 127) / 255)); }
inline void From565(uint16_t c, int* o) { int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31; o[0] = (r << 3) | (r >> 2); o[1] = (g << 2) | (g >> 4); o[2] = (b << 3) | (b >> 2); }
inline void EncodeBlock(const uint8_t blk[64], uint8_t* out)    // 16 RGBA pixels -> 16 bytes
{
    // alpha: 8-value ramp between max and min
    int amin = 255, amax = 0;
    for (int i = 0; i < 16; ++i) { amin = (std::min)(amin, (int)blk[i * 4 + 3]); amax = (std::max)(amax, (int)blk[i * 4 + 3]); }
    out[0] = (uint8_t)amax; out[1] = (uint8_t)amin;
    uint64_t abits = 0;
    if (amax > amin) {
        int pal[8] = { amax, amin };
        for (int i = 1; i < 7; ++i) pal[i + 1] = ((7 - i) * amax + i * amin + 3) / 7;
        for (int i = 0; i < 16; ++i) {
            int a = blk[i * 4 + 3], best = 0, bd = 1 << 30;
            for (int k = 0; k < 8; ++k) { int dd = abs(a - pal[k]); if (dd < bd) { bd = dd; best = k; } }
            abits |= (uint64_t)best << (3 * i);
        }
    }
    for (int i = 0; i < 6; ++i) out[2 + i] = (uint8_t)(abits >> (8 * i));
    // colour: principal axis through the 16 colours, endpoints = the extremes along it (inset 1/16), 4-colour mode
    double mean[3] = { 0, 0, 0 };
    for (int i = 0; i < 16; ++i) for (int c = 0; c < 3; ++c) mean[c] += blk[i * 4 + c];
    for (int c = 0; c < 3; ++c) mean[c] /= 16;
    double cov[6] = { 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < 16; ++i) {
        double r = blk[i * 4] - mean[0], g = blk[i * 4 + 1] - mean[1], b = blk[i * 4 + 2] - mean[2];
        cov[0] += r * r; cov[1] += r * g; cov[2] += r * b; cov[3] += g * g; cov[4] += g * b; cov[5] += b * b;
    }
    double v[3] = { 0.9, 1.0, 0.7 };
    for (int it = 0; it < 6; ++it) {
        double x = cov[0] * v[0] + cov[1] * v[1] + cov[2] * v[2], y = cov[1] * v[0] + cov[3] * v[1] + cov[4] * v[2], z = cov[2] * v[0] + cov[4] * v[1] + cov[5] * v[2];
        double m = (std::max)(fabs(x), (std::max)(fabs(y), fabs(z)));
        if (m < 1e-9) break;
        v[0] = x / m; v[1] = y / m; v[2] = z / m;
    }
    double vl = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (vl < 1e-9) { v[0] = v[1] = v[2] = 0.57735; vl = 1; }
    for (int c = 0; c < 3; ++c) v[c] /= vl;
    double tmin = 1e30, tmax = -1e30;
    for (int i = 0; i < 16; ++i) {
        double t = (blk[i * 4] - mean[0]) * v[0] + (blk[i * 4 + 1] - mean[1]) * v[1] + (blk[i * 4 + 2] - mean[2]) * v[2];
        tmin = (std::min)(tmin, t); tmax = (std::max)(tmax, t);
    }
    double inset = (tmax - tmin) / 16.0; tmin += inset; tmax -= inset;
    int e0[3], e1[3];
    for (int c = 0; c < 3; ++c) {
        double a = mean[c] + v[c] * tmax, b = mean[c] + v[c] * tmin;
        e0[c] = (int)(std::max)(0.0, (std::min)(255.0, a + 0.5)); e1[c] = (int)(std::max)(0.0, (std::min)(255.0, b + 0.5));
    }
    uint16_t c0 = To565(e0[0], e0[1], e0[2]), c1 = To565(e1[0], e1[1], e1[2]);
    if (c0 < c1) std::swap(c0, c1);
    uint32_t cbits = 0;
    if (c0 != c1) {
        int p[4][3]; From565(c0, p[0]); From565(c1, p[1]);
        for (int c = 0; c < 3; ++c) { p[2][c] = (2 * p[0][c] + p[1][c] + 1) / 3; p[3][c] = (p[0][c] + 2 * p[1][c] + 1) / 3; }
        for (int i = 0; i < 16; ++i) {
            int best = 0, bd = 1 << 30;
            for (int k = 0; k < 4; ++k) {
                int dr = blk[i * 4] - p[k][0], dg = blk[i * 4 + 1] - p[k][1], db = blk[i * 4 + 2] - p[k][2];
                int dd = dr * dr + dg * dg + db * db; if (dd < bd) { bd = dd; best = k; }
            }
            cbits |= (uint32_t)best << (2 * i);
        }
    }
    out[8] = (uint8_t)c0; out[9] = (uint8_t)(c0 >> 8); out[10] = (uint8_t)c1; out[11] = (uint8_t)(c1 >> 8);
    for (int i = 0; i < 4; ++i) out[12 + i] = (uint8_t)(cbits >> (8 * i));
}
// the launcher's neutral_darks: dark-grey block colours snapped to true grey 5-6-5 (no green tint on black boots / hair)
inline void NeutralDarks(uint8_t* blk)
{
    for (int off = 8; off <= 10; off += 2) {
        uint16_t c = (uint16_t)(blk[off] | (blk[off + 1] << 8)); int rgb[3]; From565(c, rgb);
        int mx = (std::max)(rgb[0], (std::max)(rgb[1], rgb[2])), mn = (std::min)(rgb[0], (std::min)(rgb[1], rgb[2]));
        if (mx - mn > 10 || mx >= 110) continue;
        int v = (rgb[0] + rgb[1] + rgb[2] + 1) / 3;
        int r5 = (std::min)(31, (std::max)(0, (v * 31 + 127) / 255)), ve = (r5 << 3) | (r5 >> 2);
        int g6 = (std::min)(63, (std::max)(0, (ve * 63 + 127) / 255));
        uint16_t nc = (uint16_t)((r5 << 11) | (g6 << 5) | r5);
        blk[off] = (uint8_t)nc; blk[off + 1] = (uint8_t)(nc >> 8);
    }
}
inline std::vector<uint8_t> EncodeDXT5(const Image& im, bool neutralDarks = true)   // w, h multiples of 4
{
    std::vector<uint8_t> out((size_t)(im.w / 4) * (im.h / 4) * 16);
    uint8_t blk[64]; size_t o = 0;
    for (int by = 0; by < im.h; by += 4)
        for (int bx = 0; bx < im.w; bx += 4) {
            for (int j = 0; j < 4; ++j) memcpy(blk + j * 16, &im.px[(((size_t)(by + j)) * im.w + bx) * 4], 16);
            EncodeBlock(blk, &out[o]); if (neutralDarks) NeutralDarks(&out[o]); o += 16;
        }
    return out;
}
inline void DecodeBlockDXT5(const uint8_t* b, uint8_t* px /* 64 bytes */)
{
    int a0 = b[0], a1 = b[1], ap[8] = { a0, a1 };
    if (a0 > a1) for (int i = 1; i < 7; ++i) ap[i + 1] = ((7 - i) * a0 + i * a1) / 7;
    else { for (int i = 1; i < 5; ++i) ap[i + 1] = ((5 - i) * a0 + i * a1) / 5; ap[6] = 0; ap[7] = 255; }
    uint64_t abits = 0; for (int i = 0; i < 6; ++i) abits |= (uint64_t)b[2 + i] << (8 * i);
    uint16_t c0 = (uint16_t)(b[8] | (b[9] << 8)), c1 = (uint16_t)(b[10] | (b[11] << 8));
    int p[4][3]; From565(c0, p[0]); From565(c1, p[1]);
    for (int c = 0; c < 3; ++c) { p[2][c] = (2 * p[0][c] + p[1][c]) / 3; p[3][c] = (p[0][c] + 2 * p[1][c]) / 3; }
    uint32_t cb = b[12] | (b[13] << 8) | (b[14] << 16) | ((uint32_t)b[15] << 24);
    for (int i = 0; i < 16; ++i) {
        int ci = (cb >> (2 * i)) & 3; px[i * 4] = (uint8_t)p[ci][0]; px[i * 4 + 1] = (uint8_t)p[ci][1]; px[i * 4 + 2] = (uint8_t)p[ci][2];
        px[i * 4 + 3] = (uint8_t)ap[(abits >> (3 * i)) & 7];
    }
}
inline Image DecodeDXT5(const uint8_t* d, int w, int h)
{
    Image im; im.w = w; im.h = h; im.px.assign((size_t)w * h * 4, 0); uint8_t px[64];
    for (int by = 0; by < h; by += 4) for (int bx = 0; bx < w; bx += 4) {
        DecodeBlockDXT5(d, px); d += 16;
        for (int j = 0; j < 4 && by + j < h; ++j) for (int i = 0; i < 4 && bx + i < w; ++i) memcpy(im.at(bx + i, by + j), px + (j * 4 + i) * 4, 4);
    }
    return im;
}

// ---------------------------------------------------------------- DDS headers
inline std::vector<uint8_t> DdsHeader(int w, int h, bool dxt5)
{
    std::vector<uint8_t> hd(128, 0);
    auto put = [&](int off, uint32_t v) { memcpy(&hd[off], &v, 4); };
    memcpy(&hd[0], "DDS ", 4); put(4, 124);
    put(8, dxt5 ? (0x1007 | 0x80000) : 0x100F); put(12, (uint32_t)h); put(16, (uint32_t)w);
    put(20, dxt5 ? (uint32_t)(w * h) : (uint32_t)(w * 4)); put(28, 1); put(76, 32);
    if (dxt5) { put(80, 4); memcpy(&hd[84], "DXT5", 4); }
    else { put(80, 0x41); put(88, 32); put(92, 0x00FF0000); put(96, 0x0000FF00); put(100, 0x000000FF); put(104, 0xFF000000); }
    put(108, 0x1000);
    return hd;
}
inline int Pow2Near(int n) { n = (std::max)(4, n); int lo = 1; while ((lo << 1) <= n) lo <<= 1; return (n - lo < (lo << 1) - n) ? lo : lo << 1; }
inline int Pow2Down(int n) { int lo = 4; while ((lo << 1) <= n) lo <<= 1; return lo; }
inline int Luma(const uint8_t* p) { return (p[0] * 299 + p[1] * 587 + p[2] * 114) / 1000; }   // Pillow "L"

} // namespace eci
