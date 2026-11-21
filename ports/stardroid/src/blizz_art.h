#pragma once
// Authentic StarCraft / Brood War UI asset decoders & Glue renderer (ported from StarDroid 1.9.11):
//   PCX (8bpp + glass translucency), GRP (raw & RLE), FNT (+ ClassicFontCipher), TBL,
//   86-byte BIN Dialogs + 30-byte SMK overlays, NativeGlue, NativePortrait, NativeMapPreview.

#include "engine_api.h"
#include "font8x16.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

namespace blizz {

static constexpr int W = 640;
static constexpr int H = 480;

inline uint16_t u16_le(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
inline int16_t  s16_le(const uint8_t* p) { return int16_t(u16_le(p)); }
inline uint32_t u32_le(const uint8_t* p) { return uint32_t(u16_le(p)) | (uint32_t(u16_le(p + 2)) << 16); }

// Memory-order RGBA32: byte[0]=R, byte[1]=G, byte[2]=B, byte[3]=A
inline uint32_t pack_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
}
inline uint8_t px_r(uint32_t c) { return uint8_t(c & 0xFFu); }
inline uint8_t px_g(uint32_t c) { return uint8_t((c >> 8) & 0xFFu); }
inline uint8_t px_b(uint32_t c) { return uint8_t((c >> 16) & 0xFFu); }
inline uint8_t px_a(uint32_t c) { return uint8_t((c >> 24) & 0xFFu); }

inline std::string norm_path(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c == '/') c = '\\';
        else c = char(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

inline std::vector<uint8_t> mpq_read(const std::string& path, int slot = -1) {
    uint8_t* p = nullptr;
    size_t n = 0;
    std::string np = norm_path(path);
    if (!sd_read_mpq_asset(slot, np.c_str(), &p, &n) || !p) return {};
    std::vector<uint8_t> v(p, p + n);
    sd_free_buf(p);
    return v;
}

inline std::string clean_ctrl(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (c == '\n' || c >= 32) out.push_back(char(c));
    }
    return out;
}

// Convert UTF-8 Cyrillic string literals to single-byte Windows-1251 for localized Blizzard .fnt rendering
inline std::string utf8_to_cp1251(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        uint8_t b0 = static_cast<uint8_t>(s[i]);
        if (b0 < 0x80) {
            out.push_back(char(b0));
        } else if ((b0 == 0xD0 || b0 == 0xD1) && i + 1 < s.size()) {
            uint8_t b1 = static_cast<uint8_t>(s[++i]);
            uint32_t cp = ((uint32_t(b0 & 0x1Fu) << 6) | uint32_t(b1 & 0x3Fu));
            if (cp >= 0x0410 && cp <= 0x044F) out.push_back(char(0xC0 + (cp - 0x0410)));
            else if (cp == 0x0401) out.push_back(char(0xA8)); // Ё
            else if (cp == 0x0451) out.push_back(char(0xB8)); // ё
            else out.push_back('?');
        } else if (b0 == 0xE2 && i + 2 < s.size()) {
            i += 2;
            out.push_back('-');
        } else if (b0 == 0xC2 && i + 1 < s.size()) {
            uint8_t b1 = static_cast<uint8_t>(s[++i]);
            out.push_back(b1 == 0xB7 ? '-' : ' ');
        } else {
            // Already single-byte CP1251 (e.g. from MPQ .tbl / .bin / .chk)
            out.push_back(char(b0));
        }
    }
    return out;
}

inline bool has_cp1251_cyrillic(const std::string& s) {
    int cnt = 0;
    for (unsigned char c : s) if (c >= 0xC0) ++cnt;
    return cnt >= 2;
}

inline std::string cstr_at(const uint8_t* b, size_t len, size_t off) {
    if (!b || off == 0 || off >= len) return "";
    size_t e = off;
    while (e < len && b[e] != 0) ++e;
    return std::string(reinterpret_cast<const char*>(b + off), e - off);
}

inline std::vector<std::string> parse_tbl(const std::vector<uint8_t>& b) {
    std::vector<std::string> out;
    if (b.size() < 2) return out;
    uint16_t cnt = u16_le(b.data());
    if (size_t(2) + size_t(cnt) * 2 > b.size()) return out;
    out.resize(cnt);
    for (uint16_t i = 0; i < cnt; ++i)
        out[i] = cstr_at(b.data(), b.size(), u16_le(b.data() + 2 + i * 2));
    return out;
}

// --- Software Canvas with Clipping ---
struct ImageRGBA {
    int w = 0, h = 0;
    std::vector<uint32_t> px;
    bool valid() const { return w > 0 && h > 0 && px.size() == size_t(w) * size_t(h); }
};

struct Canvas {
    uint32_t* dst = nullptr;
    int cx0 = 0, cy0 = 0, cx1 = W, cy1 = H;

    explicit Canvas(uint32_t* buf) : dst(buf) {}

    void reset_clip() { cx0 = 0; cy0 = 0; cx1 = W; cy1 = H; }
    void set_clip(int x0, int y0, int x1, int y1) {
        cx0 = std::max(0, x0);
        cy0 = std::max(0, y0);
        cx1 = std::min(W, x1);
        cy1 = std::min(H, y1);
    }

    inline void blend_px(int x, int y, uint32_t src) {
        if (x < cx0 || x >= cx1 || y < cy0 || y >= cy1) return;
        uint8_t a = px_a(src);
        if (a == 0) return;
        uint32_t& d = dst[y * W + x];
        if (a == 255) { d = src; return; }
        uint32_t inv = 255u - a;
        d = pack_rgba(uint8_t((px_r(src) * a + px_r(d) * inv) / 255u),
                      uint8_t((px_g(src) * a + px_g(d) * inv) / 255u),
                      uint8_t((px_b(src) * a + px_b(d) * inv) / 255u), 255);
    }

    void fill_rect(int x, int y, int w, int h, uint32_t col) {
        int x0 = std::max(cx0, x), y0 = std::max(cy0, y);
        int x1 = std::min(cx1, x + w), y1 = std::min(cy1, y + h);
        if (x0 >= x1 || y0 >= y1) return;
        uint8_t a = px_a(col);
        if (a == 0) return;
        if (a == 255) {
            for (int r = y0; r < y1; ++r)
                std::fill(dst + r * W + x0, dst + r * W + x1, col);
        } else {
            for (int r = y0; r < y1; ++r)
                for (int c = x0; c < x1; ++c)
                    blend_px(c, r, col);
        }
    }

    void stroke_rect(int x, int y, int w, int h, uint32_t col) {
        fill_rect(x, y, w, 1, col);
        fill_rect(x, y + h - 1, w, 1, col);
        fill_rect(x, y + 1, 1, h - 2, col);
        fill_rect(x + w - 1, y + 1, 1, h - 2, col);
    }

    void blit(const uint32_t* src, int sw, int sh, int dx, int dy,
              float brightness = 1.0f, uint8_t alpha_mul = 255) {
        if (!src || sw <= 0 || sh <= 0 || alpha_mul == 0) return;
        int x0 = std::max(cx0, dx), y0 = std::max(cy0, dy);
        int x1 = std::min(cx1, dx + sw), y1 = std::min(cy1, dy + sh);
        if (x0 >= x1 || y0 >= y1) return;
        int br = int(brightness * 256.0f + 0.5f);
        for (int y = y0; y < y1; ++y) {
            const uint32_t* srow = src + (y - dy) * sw;
            uint32_t* drow = dst + y * W;
            int x = x0;
            if (br == 256 && alpha_mul == 255) {
                const uint32_t* sp = srow + (x - dx);
                for (; x + 4 <= x1; x += 4, sp += 4) {
                    uint32_t p0 = sp[0], p1 = sp[1], p2 = sp[2], p3 = sp[3];
                    uint32_t a_or = (p0 | p1 | p2 | p3) >> 24;
                    if (a_or == 0) continue;
                    uint32_t a_and = (p0 & p1 & p2 & p3) >> 24;
                    if (a_and == 255) {
                        drow[x + 0] = p0; drow[x + 1] = p1; drow[x + 2] = p2; drow[x + 3] = p3;
                        continue;
                    }
                    uint32_t ps[4] = {p0, p1, p2, p3};
                    for (int k = 0; k < 4; ++k) {
                        uint32_t p = ps[k];
                        uint32_t a = p >> 24;
                        if (a == 255) drow[x + k] = p;
                        else if (a > 0) {
                            uint32_t d = drow[x + k], inv = 256u - a;
                            drow[x + k] = pack_rgba(uint8_t((px_r(p) * a + px_r(d) * inv) >> 8),
                                                    uint8_t((px_g(p) * a + px_g(d) * inv) >> 8),
                                                    uint8_t((px_b(p) * a + px_b(d) * inv) >> 8), 255);
                        }
                    }
                }
            }
            for (; x < x1; ++x) {
                uint32_t p = srow[x - dx];
                uint8_t a = px_a(p);
                if (a == 0) continue;
                if (alpha_mul != 255) a = uint8_t((uint32_t(a) * alpha_mul) / 255u);
                if (a == 0) continue;
                uint32_t r = px_r(p), g = px_g(p), b = px_b(p);
                if (br != 256) {
                    r = std::min(255u, (r * uint32_t(br)) >> 8);
                    g = std::min(255u, (g * uint32_t(br)) >> 8);
                    b = std::min(255u, (b * uint32_t(br)) >> 8);
                }
                if (a == 255) {
                    drow[x] = pack_rgba(uint8_t(r), uint8_t(g), uint8_t(b), 255);
                } else {
                    uint32_t d = drow[x];
                    uint32_t inv = 255u - a;
                    drow[x] = pack_rgba(uint8_t((r * a + px_r(d) * inv) / 255u),
                                        uint8_t((g * a + px_g(d) * inv) / 255u),
                                        uint8_t((b * a + px_b(d) * inv) / 255u), 255);
                }
            }
        }
    }

    void blit_img(const ImageRGBA& img, int dx, int dy, float brightness = 1.0f, uint8_t alpha_mul = 255) {
        if (img.valid()) blit(img.px.data(), img.w, img.h, dx, dy, brightness, alpha_mul);
    }

    void blit_scaled(const uint32_t* src, int sw, int sh, int dx, int dy, int dw, int dh,
                     float brightness = 1.0f, uint8_t alpha_mul = 255) {
        if (!src || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0 || alpha_mul == 0) return;
        if (sw == dw && sh == dh) { blit(src, sw, sh, dx, dy, brightness, alpha_mul); return; }
        int x0 = std::max(cx0, dx), y0 = std::max(cy0, dy);
        int x1 = std::min(cx1, dx + dw), y1 = std::min(cy1, dy + dh);
        if (x0 >= x1 || y0 >= y1) return;
        int br = int(brightness * 256.0f + 0.5f);
        for (int y = y0; y < y1; ++y) {
            int sy = (y - dy) * sh / dh;
            const uint32_t* srow = src + sy * sw;
            for (int x = x0; x < x1; ++x) {
                int sx = (x - dx) * sw / dw;
                uint32_t p = srow[sx];
                uint8_t a = px_a(p);
                if (a == 0) continue;
                if (alpha_mul != 255) a = uint8_t((uint32_t(a) * alpha_mul) / 255u);
                uint32_t r = px_r(p), g = px_g(p), b = px_b(p);
                if (br != 256) {
                    r = std::min(255u, (r * uint32_t(br)) >> 8);
                    g = std::min(255u, (g * uint32_t(br)) >> 8);
                    b = std::min(255u, (b * uint32_t(br)) >> 8);
                }
                blend_px(x, y, pack_rgba(uint8_t(r), uint8_t(g), uint8_t(b), a));
            }
        }
    }
};

// --- PCX Decoder ---
struct PCX {
    int width = 0, height = 0, stride = 0;
    std::vector<uint8_t> pixels;
    uint32_t palette[256] = {};
    ImageRGBA image;

    bool decode(const std::vector<uint8_t>& b, bool transparent, int glass, uint8_t glass_alpha = 0x99) {
        if (b.size() < 897 || b[0] != 10 || (b[2] != 0 && b[2] != 1) || b[3] != 8 || b[65] != 1)
            return false;
        width = int(u16_le(b.data() + 8)) - int(u16_le(b.data() + 4)) + 1;
        height = int(u16_le(b.data() + 10)) - int(u16_le(b.data() + 6)) + 1;
        stride = int(u16_le(b.data() + 66));
        if (width < 1 || height < 1 || width > 2048 || height > 2048 || stride < width || stride > 4096)
            return false;
        pixels.assign(size_t(stride) * size_t(height), 0);
        size_t pal_off = b.size() - 768;
        for (int i = 0; i < 256; ++i) {
            size_t p = pal_off + size_t(i) * 3;
            palette[i] = pack_rgba(b[p], b[p + 1], b[p + 2], 255);
        }
        size_t p = 128, o = 0, limit = b.size() - 769;
        if (b[2] == 0) {
            if (pixels.size() > b.size() - 897) return false;
            std::memcpy(pixels.data(), b.data() + 128, pixels.size());
        } else {
            while (o < pixels.size()) {
                if (p >= limit) return false;
                uint8_t v = b[p++];
                size_t n = 1;
                if ((v & 0xC0) == 0xC0) {
                    n = v & 0x3F;
                    if (p >= limit || n == 0) return false;
                    v = b[p++];
                }
                if (o + n > pixels.size()) return false;
                std::memset(pixels.data() + o, v, n);
                o += n;
            }
        }
        image.w = width;
        image.h = height;
        image.px.resize(size_t(width) * size_t(height));
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int idx = pixels[size_t(y) * stride + x];
                uint32_t col = palette[idx];
                if (transparent && idx == 0) col = 0;
                else if (idx == glass) col = (col & 0x00FFFFFFu) | (uint32_t(glass_alpha) << 24);
                image.px[size_t(y) * width + x] = col;
            }
        }
        return true;
    }
};

inline bool load_pcx(const std::string& path, PCX& out, bool transparent = false, int glass = -1, uint8_t glass_alpha = 0x99) {
    auto b = mpq_read(path);
    return !b.empty() && out.decode(b, transparent, glass, glass_alpha);
}

// Remove edge-connected baked backdrop pixels from sliding Glue button panels (ported from MenuPanel.java)
inline void strip_edge_backdrop(ImageRGBA& panel, const ImageRGBA& bg, int left, int top) {
    if (!panel.valid() || !bg.valid()) return;
    int w = panel.w, h = panel.h;
    std::vector<uint8_t> vis(size_t(w) * size_t(h), 0);
    std::deque<int> q;
    for (int x = 0; x < w; ++x) { q.push_back(x); q.push_back((h - 1) * w + x); }
    for (int y = 1; y < h - 1; ++y) { q.push_back(y * w); q.push_back(y * w + w - 1); }
    while (!q.empty()) {
        int i = q.front(); q.pop_front();
        if (vis[size_t(i)]) continue;
        vis[size_t(i)] = 1;
        int x = i % w, y = i / w, bx = left + x, by = top + y;
        bool clear = px_a(panel.px[size_t(i)]) == 0;
        bool match = unsigned(bx) < unsigned(bg.w) && unsigned(by) < unsigned(bg.h) &&
                     ((panel.px[size_t(i)] & 0x00FFFFFFu) == (bg.px[size_t(by) * bg.w + bx] & 0x00FFFFFFu));
        if (!clear && !match) continue;
        panel.px[size_t(i)] = 0;
        if (x > 0) q.push_back(i - 1);
        if (x + 1 < w) q.push_back(i + 1);
        if (y > 0) q.push_back(i - w);
        if (y + 1 < h) q.push_back(i + w);
    }
}

// --- GRP Decoder ---
inline ImageRGBA decode_grp_frame(const std::vector<uint8_t>& b, int id, const uint32_t palette[256],
                                  bool raw, bool centered, bool use_frame_ofs = false) {
    ImageRGBA out;
    if (b.size() < 6 || id < 0 || id >= int(u16_le(b.data()))) return out;
    size_t at = 6 + size_t(id) * 8;
    if (at + 8 > b.size()) return out;
    int xo = b[at + 0], yo = b[at + 1], w = b[at + 2], h = b[at + 3];
    uint32_t base = u32_le(b.data() + at + 4);
    int max_w = u16_le(b.data() + 2), max_h = u16_le(b.data() + 4);
    int cw = (centered || use_frame_ofs) ? max_w : w;
    int ch = (centered || use_frame_ofs) ? max_h : h;
    if (w == 0 || h == 0 || cw > 1024 || ch > 768 || w > cw || h > ch || base >= b.size()) return out;
    out.w = cw; out.h = ch;
    out.px.assign(size_t(cw) * size_t(ch), 0);
    int ox = use_frame_ofs ? std::min(cw - w, xo) : (cw - w) / 2;
    int oy = use_frame_ofs ? std::min(ch - h, yo) : (ch - h) / 2;
    if (raw) {
        if (uint64_t(base) + uint64_t(w) * uint64_t(h) > b.size()) { out.w = out.h = 0; return out; }
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                uint8_t idx = b[base + size_t(y) * w + x];
                if (idx != 0) out.px[size_t(y + oy) * cw + (x + ox)] = palette[idx];
            }
        }
    } else {
        if (base + size_t(h) * 2 > b.size()) { out.w = out.h = 0; return out; }
        for (int y = 0; y < h; ++y) {
            size_t p = base + u16_le(b.data() + base + size_t(y) * 2);
            int x = 0;
            while (x < w) {
                if (p >= b.size()) { out.w = out.h = 0; return out; }
                uint8_t v = b[p++];
                int n = (v & 0x80) ? (v & 0x7F) : ((v & 0x40) ? (v & 0x3F) : v);
                if (n == 0 || x + n > w) { out.w = out.h = 0; return out; }
                if (v & 0x80) { x += n; continue; }
                if (v & 0x40) {
                    if (p >= b.size()) { out.w = out.h = 0; return out; }
                    uint8_t idx = b[p++];
                    for (int k = 0; k < n; ++k, ++x) out.px[size_t(y + oy) * cw + (x + ox)] = palette[idx];
                } else {
                    if (p + size_t(n) > b.size()) { out.w = out.h = 0; return out; }
                    for (int k = 0; k < n; ++k, ++x) out.px[size_t(y + oy) * cw + (x + ox)] = palette[b[p++]];
                }
            }
        }
    }
    return out;
}

// --- ClassicFontCipher (exact SHA-0 + IDEA unwrap for encrypted font/font*.fnt) ---
namespace font_cipher {
    inline bool is_plain(const std::vector<uint8_t>& d) {
        return d.size() >= 8 && std::memcmp(d.data(), "FONT", 4) == 0;
    }
    inline bool is_wrapped(const std::vector<uint8_t>& d) {
        if (d.size() < 72 || d.size() > 1024 * 1024 || (d.size() - 8) % 64 != 0) return false;
        int tail = d[d.size() - 3];
        return d[d.size() - 4] == 0 && tail <= 64 && int(d.size()) - 72 + tail > 0;
    }
    // Arithmetic right shift matches Java's signed `n >> (32 - bits)` in ClassicFontCipher!
    inline int32_t arith_rot(int32_t n, int bits) {
        uint32_t left = uint32_t(n) << bits;
        int32_t right = n >> (32 - bits);
        return int32_t(left | uint32_t(right));
    }
    inline void sha0_compress(int32_t state[5], const uint8_t* in) {
        int32_t w[80];
        for (int i = 0; i < 16; ++i) w[i] = int32_t(u32_le(in + i * 4));
        for (int i = 16; i < 80; ++i) w[i] = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
        uint32_t a = uint32_t(state[0]), b = uint32_t(state[1]), c = uint32_t(state[2]),
                 d = uint32_t(state[3]), e = uint32_t(state[4]);
        for (int i = 0; i < 80; ++i) {
            uint32_t mix, k;
            if (i < 20)      { mix = (b & c) | (~b & d); k = 0x5a827999u; }
            else if (i < 40) { mix = b ^ c ^ d;          k = 0x6ed9eba1u; }
            else if (i < 60) { mix = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdcu; }
            else             { mix = b ^ c ^ d;          k = 0xca62c1d6u; }
            uint32_t next = uint32_t(arith_rot(int32_t(a), 5)) + mix + e + k + uint32_t(w[i]);
            e = d; d = c; c = uint32_t(arith_rot(int32_t(b), 30)); b = a; a = next;
        }
        state[0] = int32_t(uint32_t(state[0]) + a);
        state[1] = int32_t(uint32_t(state[1]) + b);
        state[2] = int32_t(uint32_t(state[2]) + c);
        state[3] = int32_t(uint32_t(state[3]) + d);
        state[4] = int32_t(uint32_t(state[4]) + e);
    }
    inline int idea_mul(int a, int b) {
        uint64_t aa = (a == 0) ? 65536ull : uint64_t(a);
        uint64_t bb = (b == 0) ? 65536ull : uint64_t(b);
        return int(((aa * bb) % 65537ull) & 65535ull);
    }
    inline int idea_inv(int v) {
        uint64_t res = 1, base = (v == 0) ? 65536ull : uint64_t(v);
        for (uint32_t exp = 65535; exp != 0; exp >>= 1) {
            if (exp & 1) res = (res * base) % 65537ull;
            base = (base * base) % 65537ull;
        }
        return int(res & 65535ull);
    }
    inline void idea_decrypt_block(const uint8_t* in, uint8_t* out, const int keys[52], const int inv[52]) {
        int a = idea_mul(u16_le(in), inv[48]);
        int b = (int(u16_le(in + 4)) - keys[50]) & 65535;
        int c = (int(u16_le(in + 2)) - keys[49]) & 65535;
        int d = idea_mul(u16_le(in + 6), inv[51]);
        for (int at = 42; at >= 0; at -= 6) {
            int first = idea_mul(a ^ b, keys[at + 4]);
            int second = idea_mul(((c ^ d) + first) & 65535, keys[at + 5]);
            int sum = (first + second) & 65535;
            int nb = ((c ^ sum) - keys[at + 1]) & 65535;
            int nc = ((b ^ second) - keys[at + 2]) & 65535;
            a = idea_mul(a ^ second, inv[at]);
            d = idea_mul(d ^ sum, inv[at + 3]);
            b = nb; c = nc;
        }
        out[0] = uint8_t(a); out[1] = uint8_t(a >> 8);
        out[2] = uint8_t(b); out[3] = uint8_t(b >> 8);
        out[4] = uint8_t(c); out[5] = uint8_t(c >> 8);
        out[6] = uint8_t(d); out[7] = uint8_t(d >> 8);
    }
    inline std::vector<uint8_t> unwrap(const std::vector<uint8_t>& enc, const std::vector<uint8_t>& pw, bool req_font) {
        if (pw.empty()) return {};
        uint8_t repeated[64], seed[112];
        for (size_t i = 0; i < 64; ++i) repeated[i] = pw[i % pw.size()];
        static const int32_t kInit[5] = {
            int32_t(0x67452301u), int32_t(0xefcdab89u), int32_t(0x98badcfeu),
            int32_t(0x10325476u), int32_t(0xc3d2e1f0u)
        };
        int32_t digest[5], state[5];
        std::memcpy(digest, kInit, sizeof(kInit));
        sha0_compress(digest, repeated);
        uint32_t rnd = 0x4fa7u;
        for (int i = 0; i < 112; ++i) {
            rnd = rnd * 214013u + 2531011u;
            seed[i] = uint8_t((rnd >> 16) ^ (uint32_t(digest[(i % 20) / 4]) >> ((i % 4) * 8)));
        }
        int keys[52] = {}, inv[52] = {};
        for (int i = 0; i < 8; ++i) keys[i] = u16_le(seed + i * 2);
        for (int i = 8; i < 52; ++i) {
            int lane = i % 8;
            int left = (lane < 7) ? (i - 7) : (i - 15);
            int right = (lane < 6) ? (i - 6) : (i - 14);
            keys[i] = ((keys[left] << 9) | (keys[right] >> 7)) & 65535;
        }
        for (int i = 0; i < 52; i += 6) {
            inv[i] = idea_inv(keys[i]);
            inv[i + 3] = idea_inv(keys[i + 3]);
        }
        std::memcpy(state, kInit, sizeof(kInit));
        sha0_compress(state, seed + 48);

        std::vector<uint8_t> clear(enc.size() - 8);
        for (size_t off = 0; off < clear.size(); off += 64) {
            if (off % 512 == 0) {
                for (size_t blk = off; blk < off + 64; blk += 8)
                    idea_decrypt_block(enc.data() + blk, clear.data() + blk, keys, inv);
            } else {
                std::memcpy(clear.data() + off, enc.data() + off, 64);
            }
            for (int i = 0; i < 64; ++i) {
                int at = (63 - i) % 20;
                clear[off + i] = uint8_t(clear[off + i] - uint8_t(uint32_t(state[at / 4]) >> ((at % 4) * 8)));
            }
            if (req_font && off == 0 && std::memcmp(clear.data(), "FONT", 4) != 0) return {};
            if (off % 1024 == 0) sha0_compress(state, clear.data() + off);
        }
        if (u32_le(enc.data() + clear.size()) != uint32_t(state[0])) return {};
        size_t out_len = clear.size() - 64 + enc[enc.size() - 3];
        clear.resize(out_len);
        return clear;
    }
    inline std::vector<uint8_t> decode_font_data(const std::vector<uint8_t>& raw) {
        if (is_plain(raw)) return raw;
        if (!is_wrapped(raw)) return {};
        auto gid = mpq_read("font\\font.gid");
        if (!is_wrapped(gid) || gid.size() > 4096) return {};
        std::vector<uint8_t> fixed;
        uint32_t st = 0x150bu;
        while (fixed.size() < 19) {
            st = st * 214013u + 2531011u;
            uint8_t nxt = uint8_t(st >> 16);
            if (nxt) fixed.push_back(nxt);
        }
        auto id_bytes = unwrap(gid, fixed, false);
        size_t end = 0;
        while (end < id_bytes.size() && id_bytes[end] != 0) {
            if (id_bytes[end] < 32 || id_bytes[end] > 126 || end >= 63) return {};
            ++end;
        }
        if (end == 0 || end == id_bytes.size()) return {};
        std::vector<uint8_t> pw(id_bytes.begin(), id_bytes.begin() + end);
        return unwrap(raw, pw, true);
    }
} // namespace font_cipher

// --- Blizzard .fnt Font ---
struct BlizzFont {
    std::vector<uint8_t> data;
    PCX tint;
    uint32_t palette[256] = {};
    bool game = false;
    int low = 32, high = 255, height = 14, spacing = 1;
    uint32_t fixed_color = 0, shaded_color = 0;
    std::unordered_map<uint64_t, ImageRGBA> glyph_cache;

    bool load(const char* sz_name, const PCX& tint_pcx, const uint32_t pal[256], int sp) {
        tint = tint_pcx;
        std::memcpy(palette, pal, sizeof(palette));
        spacing = sp;
        game = (tint.stride >= 64 && tint.pixels.size() >= 192);
        glyph_cache.clear();

        std::string p1 = std::string("files\\font\\font") + sz_name + ".fnt";
        std::string p2 = std::string("font\\font") + sz_name + ".fnt";
        for (const auto& path : {p1, p2}) {
            auto raw = mpq_read(path);
            if (raw.empty()) continue;
            auto dec = font_cipher::decode_font_data(raw);
            if (dec.size() >= 8 && std::memcmp(dec.data(), "FONT", 4) == 0) {
                int lo = dec[4], hi = dec[5], h = dec[7];
                if (hi >= lo && h >= 1 && h <= 64 && 8 + size_t(hi - lo + 1) * 4 <= dec.size()) {
                    data = std::move(dec);
                    low = lo; high = hi; height = h;
                    return true;
                }
            }
        }
        // Fallback to font16 if font16x missing
        if (std::strcmp(sz_name, "16x") == 0) return load("16", tint_pcx, pal, sp);
        height = std::max(8, std::atoi(sz_name));
        return false;
    }

    BlizzFont with_shaded(uint32_t col) const {
        BlizzFont f = *this;
        f.glyph_cache.clear();
        f.shaded_color = col;
        return f;
    }

    void ramp(int c, int& row, int& col) const {
        if (!game) { row = 0; col = (c >= 2 && c <= 6) ? (c - 2) : 0; return; }
        switch (c) {
        case 3:  row = 0; col = 1; break;
        case 4:  row = 0; col = 2; break;
        case 5:  row = 0; col = 3; break;
        case 6:  row = 0; col = 4; break;
        case 7:  row = 0; col = 5; break;
        case 8:  row = 1; col = 0; break;
        case 11: row = 0; col = 6; break;
        case 14: row = 1; col = 1; break;
        case 15: row = 1; col = 2; break;
        case 16: row = 1; col = 3; break;
        case 17: row = 1; col = 4; break;
        case 20: row = 0; col = 7; break;
        case 21: row = 1; col = 5; break;
        case 22: row = 1; col = 6; break;
        case 23: row = 1; col = 7; break;
        case 24: row = 2; col = 0; break;
        case 25: row = 2; col = 1; break;
        case 27: row = 2; col = 2; break;
        case 28: row = 2; col = 3; break;
        case 29: row = 2; col = 4; break;
        case 30: row = 2; col = 5; break;
        case 31: row = 2; col = 7; break;
        default: row = 0; col = 0; break;
        }
    }

    const ImageRGBA* glyph(uint8_t ch, int color) {
        uint64_t key = (uint64_t(uint32_t(color)) << 32) | ch;
        auto it = glyph_cache.find(key);
        if (it != glyph_cache.end()) return &it->second;
        if (glyph_cache.size() > 1024) glyph_cache.clear();

        ImageRGBA img;
        int rr = 0, rc = 0;
        ramp(color, rr, rc);

        if (!data.empty() && ch >= low && ch <= high) {
            uint32_t off = u32_le(data.data() + 8 + size_t(ch - low) * 4);
            if (off > 0 && off + 4 <= data.size()) {
                size_t p = off;
                int gw = data[p++], gh = data[p++], ox = data[p++], oy = data[p++];
                if (gw > 0 && gw <= 64 && gh <= 64 && ox + gw <= 96 && oy + gh <= height) {
                    int cw = std::max(1, gw + ox + 1);
                    img.w = cw; img.h = height;
                    img.px.assign(size_t(cw) * size_t(height), 0);
                    int n = 0;
                    while (n < gw * gh && p < data.size()) {
                        uint8_t b = data[p++];
                        n += (b >> 3);
                        if (n >= gw * gh) break;
                        int shade = b & 7;
                        if (shade != 0) {
                            size_t tidx = size_t(rr) * tint.stride + size_t(rc) * 8 + size_t(shade);
                            uint8_t pidx = (tidx < tint.pixels.size()) ? tint.pixels[tidx] : 1;
                            uint32_t rgb = palette[pidx];
                            bool transp = (pidx == 0) || (px_r(rgb) >= 248 && px_g(rgb) == 0 && px_b(rgb) >= 248);
                            if (!transp) {
                                if (shaded_color != 0) {
                                    uint32_t lum = std::max({px_r(rgb), px_g(rgb), px_b(rgb)});
                                    rgb = pack_rgba(uint8_t(px_r(shaded_color) * lum / 255u),
                                                    uint8_t(px_g(shaded_color) * lum / 255u),
                                                    uint8_t(px_b(shaded_color) * lum / 255u), 255);
                                }
                                if (fixed_color != 0) rgb = fixed_color;
                                img.px[size_t(n / gw + oy) * cw + size_t(n % gw + ox)] = rgb;
                            }
                            ++n;
                        }
                    }
                    return &(glyph_cache[key] = std::move(img));
                }
            }
        }

        // Fallback bitmap glyph from font8x16 shaded by active ramp
        uint8_t fc = (ch >= 32 && ch < 128) ? ch : '?';
        size_t tidx = size_t(rr) * tint.stride + size_t(rc) * 8 + 1;
        uint32_t col_rgb = (!tint.pixels.empty() && tidx < tint.pixels.size()) ? palette[tint.pixels[tidx]] : pack_rgba(205, 225, 245);
        if (shaded_color != 0) col_rgb = shaded_color;
        if (fixed_color != 0) col_rgb = fixed_color;
        img.w = 8; img.h = height;
        img.px.assign(size_t(8) * size_t(height), 0);
        const uint8_t* g8 = font8x16 + fc * 16;
        for (int y = 0; y < height; ++y) {
            uint8_t bits = g8[y * 16 / std::max(1, height)];
            for (int x = 0; x < 8; ++x)
                if (bits & (0x80u >> x)) img.px[size_t(y) * 8 + x] = col_rgb;
        }
        return &(glyph_cache[key] = std::move(img));
    }

    int advance(uint8_t c) {
        if (!data.empty()) {
            if (c == 32) return std::max(1, int(data[6]) / 2);
            if (c == 9)  return std::max(4, int(data[6]) * 2);
        } else {
            if (c == 32) return 5;
            if (c == 9)  return 20;
        }
        if (c < 32) return 0;
        const auto* g = glyph(c, 2);
        return (!g || g->w <= 0) ? 0 : (g->w - 1 + spacing);
    }

    int measure(const std::string& s) {
        int w = 0;
        for (unsigned char c : s) {
            if (c == 11 || c == 12 || c == 20) break;
            if (c >= 32 || c == 9) w += advance(c);
        }
        return w;
    }

    std::vector<std::string> wrap_lines(const std::string& raw, int max_w) {
        std::vector<std::string> out;
        std::string s;
        s.reserve(raw.size());
        for (char c : raw) if (c != '\r') s.push_back(c);

        size_t pos = 0;
        while (pos <= s.size()) {
            size_t nl = s.find('\n', pos);
            std::string para = (nl == std::string::npos) ? s.substr(pos) : s.substr(pos, nl - pos);
            if (max_w <= 0 || para.empty()) {
                out.push_back(para);
            } else {
                std::string cur;
                int x = 0;
                size_t i = 0;
                while (i < para.size()) {
                    size_t ws = i;
                    while (ws < para.size() && para[ws] != ' ') ++ws;
                    while (ws < para.size() && para[ws] == ' ') ++ws;
                    std::string tok = para.substr(i, ws - i);
                    i = ws;
                    int tw = measure(tok);
                    if (x > 0 && x + tw > max_w) {
                        while (!cur.empty() && cur.back() == ' ') cur.pop_back();
                        out.push_back(cur);
                        cur.clear();
                        x = 0;
                    }
                    if (tw > max_w) {
                        for (unsigned char ch : tok) {
                            int aw = advance(ch);
                            if (x > 0 && x + aw > max_w) {
                                out.push_back(cur);
                                cur.clear();
                                x = 0;
                            }
                            cur.push_back(char(ch));
                            x += aw;
                        }
                    } else {
                        cur += tok;
                        x += tw;
                    }
                }
                while (!cur.empty() && cur.back() == ' ') cur.pop_back();
                out.push_back(cur);
            }
            if (nl == std::string::npos) break;
            pos = nl + 1;
        }
        return out;
    }

    int draw(Canvas& c, const std::string& s, int left, int top, int width,
             int align = 0, int color = 2, int line_h = 0, uint8_t alpha = 255) {
        auto lines = wrap_lines(s, width);
        int step = (line_h > 0) ? std::max(line_h, height + 1) : height;
        for (const auto& ln : lines) {
            int cur = color, prev = color;
            bool hidden = false, locked = false;
            int a = (ln.find('\x12') != std::string::npos) ? 2
                  : (ln.find('\x13') != std::string::npos) ? 1 : align;
            int lw = measure(ln);
            int x = left + (a == 1 ? (width - lw) / 2 : (a == 2 ? (width - lw) : 0));
            for (unsigned char ch : ln) {
                if (ch < 32) {
                    if (ch == 9) x += advance(ch);
                    else if (ch == 11 || ch == 12 || ch == 20) hidden = true;
                    else if (ch == 1 && !locked) std::swap(cur, prev);
                    else if (ch != 1 && ch != 13 && ch != 18 && ch != 19 && ch != 26 && !locked) {
                        prev = cur; cur = ch;
                        if (ch == 5) locked = true;
                    }
                    continue;
                }
                if (!hidden) {
                    if (const auto* g = glyph(ch, cur))
                        c.blit_img(*g, x, top, 1.0f, alpha);
                }
                x += advance(ch);
            }
            top += step;
        }
        return top;
    }
};

// --- 86-byte .bin Dialog & 30-byte SMK Overlay ---
struct BinWidget {
    int id = 0, type = 0, x = 0, y = 0, w = 0, h = 0;
    uint32_t flags = 0, smk = 0;
    int tx = 0, ty = 0, rx = 0, ry = 0, rx2 = 0, ry2 = 0, rw = 0, rh = 0;
    std::string text;

    bool hit_contains(int px, int py, int shift_x = 0) const {
        if ((flags & 16u) != 0 && rx2 > rx && ry2 > ry) {
            return px >= x + shift_x + rx && px <= x + shift_x + rx2 &&
                   py >= y + ry && py <= y + ry2;
        }
        return px >= x + shift_x && px < x + shift_x + w && py >= y && py < y + h;
    }
};

struct BinAnim {
    std::string name;
    int flags = 0, x = 0, y = 0;
};

struct BlizzDialog {
    std::vector<uint8_t> raw;
    std::vector<BinWidget> widgets;

    bool load_bytes(std::vector<uint8_t> b) {
        widgets.clear();
        if (b.size() < 86) return false;
        raw = std::move(b);
        uint32_t p = u32_le(raw.data() + 66);
        int guard = 0;
        while (p > 0) {
            if (p + 86 > raw.size() || ++guard > 512) return false;
            BinWidget w;
            w.x = u16_le(raw.data() + p + 4);
            w.y = u16_le(raw.data() + p + 6);
            w.w = u16_le(raw.data() + p + 12);
            w.h = u16_le(raw.data() + p + 14);
            w.text = cstr_at(raw.data(), raw.size(), u32_le(raw.data() + p + 20));
            w.flags = u32_le(raw.data() + p + 24);
            w.id = u16_le(raw.data() + p + 32);
            w.type = int(u32_le(raw.data() + p + 34));
            w.smk = u32_le(raw.data() + p + 66);
            w.tx = u16_le(raw.data() + p + 70);
            w.ty = u16_le(raw.data() + p + 72);
            w.rx = u16_le(raw.data() + p + 54);
            w.ry = u16_le(raw.data() + p + 56);
            w.rx2 = u16_le(raw.data() + p + 58);
            w.ry2 = u16_le(raw.data() + p + 60);
            w.rw = u16_le(raw.data() + p + 74);
            w.rh = u16_le(raw.data() + p + 76);
            widgets.push_back(std::move(w));
            p = u32_le(raw.data() + p);
        }
        return !widgets.empty();
    }

    bool load(const std::string& bin_name) {
        std::string path = (bin_name.find('\\') == std::string::npos && bin_name.find('/') == std::string::npos)
                           ? ("rez\\" + bin_name + ".bin") : bin_name;
        return load_bytes(mpq_read(path));
    }

    BinWidget* get(int id) {
        for (auto& w : widgets) if (w.id == id) return &w;
        return nullptr;
    }
    const BinWidget* get(int id) const {
        for (const auto& w : widgets) if (w.id == id) return &w;
        return nullptr;
    }

    std::vector<BinAnim> animations(const BinWidget& w) const {
        std::vector<BinAnim> list;
        uint32_t p = w.smk;
        int guard = 0;
        while (p > 0 && p + 30 <= raw.size() && ++guard < 32) {
            BinAnim a;
            a.name = norm_path(cstr_at(raw.data(), raw.size(), u32_le(raw.data() + p + 10)));
            a.flags = u16_le(raw.data() + p + 4);
            a.x = s16_le(raw.data() + p + 18);
            a.y = s16_le(raw.data() + p + 20);
            list.push_back(std::move(a));
            p = u32_le(raw.data() + p);
        }
        std::reverse(list.begin(), list.end());
        return list;
    }
};

// --- Animated Cursor from cursor/arrow.grp ---
struct ArrowCursor {
    std::vector<ImageRGBA> frames;
    bool loaded = false;

    void init() {
        if (loaded) return;
        loaded = true;
        auto wpe = mpq_read("tileset\\badlands.wpe");
        auto grp = mpq_read("cursor\\arrow.grp");
        if (wpe.size() < 1024 || grp.size() < 6) return;
        uint32_t pal[256];
        for (int i = 0; i < 256; ++i)
            pal[i] = pack_rgba(wpe[i * 4], wpe[i * 4 + 1], wpe[i * 4 + 2], 255);
        int cnt = u16_le(grp.data());
        for (int i = 0; i < cnt && i < 16; ++i) {
            auto f = decode_grp_frame(grp, i, pal, false, true, false);
            if (f.valid()) frames.push_back(std::move(f));
        }
    }

    void draw(Canvas& c, int mx, int my, double now_ms) {
        init();
        if (!frames.empty()) {
            const auto& f = frames[size_t(now_ms / 100.0) % frames.size()];
            c.blit_img(f, mx - f.w / 2, my - f.h / 2);
            return;
        }
        // Fallback crisp green StarCraft arrow if MPQ not mounted
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x <= y && x < 9; ++x)
                c.blend_px(mx + x, my + y, (x == 0 || x == y || y == 11) ? pack_rgba(10, 20, 15) : pack_rgba(80, 235, 120));
    }
};

// --- NativePortrait: Animated SMK Unit & Briefing Portraits ---
struct BlizzPortrait {
    std::vector<uint8_t> portdata;
    std::vector<std::string> portnames;
    std::vector<int16_t> unit_portraits;
    int movie_id = -1;
    int movie_w = 60, movie_h = 56, movie_frames = 1, movie_interval = 67;
    int unit_type = -1, graphic = -1, variant = -1, selection_id = -1;
    bool talking = false, new_sel = false;
    double elapsed = 0, last_ms = -1, static_start = -1;

    void init_tables() {
        if (!portdata.empty()) return;
        portdata = mpq_read("arr\\portdata.dat");
        portnames = parse_tbl(mpq_read("arr\\portdata.tbl"));
        // Read unit_type -> portrait index directly from arr\units.dat so Briefing portraits work before engine init!
        auto udat = mpq_read("arr\\units.dat");
        if (udat.size() >= 5244) {
            size_t off = udat.size() - 5244; // 228 * (9*u16 + 5*u8) tail starting at portrait[228]
            unit_portraits.resize(228, -1);
            for (int i = 0; i < 228; ++i) {
                uint16_t p = u16_le(udat.data() + off + size_t(i) * 2);
                unit_portraits[size_t(i)] = (p == 0xFFFFu) ? -1 : int16_t(p);
            }
        }
    }

    void close() {
        if (movie_id >= 0) { sd_movie_close(movie_id); movie_id = -1; }
        unit_type = graphic = variant = -1;
    }

    void set_selection(int id) {
        if (id == selection_id) return;
        selection_id = id;
        new_sel = (id != -1);
        static_start = -1;
    }

    void set_target(int utype, bool talk, int explicit_graphic = -2) {
        init_tables();
        int g = -1;
        if (explicit_graphic != -2) g = explicit_graphic;
        else if (utype >= 0 && utype < 228) {
            g = sd_value(11, utype);
            if (g < 0 && utype < int(unit_portraits.size())) g = unit_portraits[size_t(utype)];
        }
        if (utype == unit_type && talk == talking && g == graphic && movie_id >= 0) return;
        unit_type = utype;
        talking = talk;
        graphic = g;
        variant = -1;
        elapsed = 0;
        last_ms = -1;
        open_next();
    }

    void open_next() {
        if (movie_id >= 0) { sd_movie_close(movie_id); movie_id = -1; }
        if (graphic < 0 || portdata.size() < 12 || portnames.empty()) return;
        int count = int(portdata.size() / 12);
        if (graphic >= count) return;

        int choice = 0;
        if (variant >= 0) {
            if (talking) {
                choice = std::rand() % 2;
                if (choice >= variant) ++choice;
            } else {
                int total = 0;
                for (int i = 0; i < 4; ++i) total += portdata[size_t(8 + i) * count + graphic];
                int rem = (total > 0) ? (std::rand() % total) : 0;
                for (int i = 0; i < 4; ++i) {
                    rem -= portdata[size_t(8 + i) * count + graphic];
                    if (rem < 0) { choice = i; break; }
                }
            }
        }
        variant = choice;
        elapsed = 0;

        for (int pass = 0; pass < 2 && movie_id < 0; ++pass) {
            bool use_talk = talking && (pass == 0);
            int idx = int(u32_le(portdata.data() + (use_talk ? size_t(count) * 4 : 0) + size_t(graphic) * 4)) - 1;
            if (idx < 0 || idx >= int(portnames.size())) continue;
            for (int c = choice; c >= 0; --c) {
                std::string path = "portrait\\" + portnames[size_t(idx)] + std::to_string(c) + ".smk";
                int info[5] = {};
                movie_id = sd_movie_open_asset(-1, path.c_str(), false, info);
                if (movie_id >= 0) {
                    movie_w = info[0]; movie_h = info[1];
                    movie_frames = std::max(1, info[2]); movie_interval = std::max(15, info[3]);
                    break;
                }
            }
            if (!talking) break;
        }
    }

    void draw(Canvas& c, int x, int y, int w, int h, double now_ms, bool paused,
              bool comm_monitor, const std::vector<uint8_t>* mask = nullptr) {
        if (last_ms >= 0 && !paused) elapsed += std::max(0.0, std::min(100.0, now_ms - last_ms));
        last_ms = now_ms;
        double dur = double(movie_frames) * double(movie_interval);
        if (movie_id >= 0 && elapsed >= dur) open_next();

        if (!mask) c.fill_rect(x, y, w, h, pack_rgba(0, 0, 0, 255));

        if (movie_id >= 0) {
            int mw = 0, mh = 0;
            if (const uint8_t* fr = sd_movie_frame(movie_id, elapsed, false, &mw, &mh)) {
                for (int py = 0; py < h; ++py) {
                    int sy = py * mh / h;
                    for (int px = 0; px < w; ++px) {
                        if (mask && !(*mask)[size_t(py) * w + px]) continue;
                        int sx = px * mw / w;
                        const uint8_t* sp = fr + (size_t(sy) * mw + sx) * 4;
                        uint32_t r = sp[0], g = sp[1], b = sp[2];
                        if (comm_monitor) {
                            if (!talking) { r = r * 158 / 255; g = g * 158 / 255; b = b * 158 / 255; }
                            if (py & 1)   { r = r * 112 / 255; g = g * 112 / 255; b = b * 112 / 255; }
                        }
                        c.blend_px(x + px, y + py, pack_rgba(uint8_t(r), uint8_t(g), uint8_t(b), 255));
                    }
                }
            }
        }

        if (new_sel) { static_start = now_ms; new_sel = false; }
        if (!comm_monitor && static_start >= 0 && (now_ms - static_start) < 180.0) {
            uint32_t seed = uint32_t(selection_id) ^ (uint32_t(now_ms / 32.0) * 1103515245u);
            uint8_t alpha = uint8_t(220.0 * (1.0 - (now_ms - static_start) / 180.0));
            for (int py = 0; py < h; ++py) {
                for (int px = 0; px < w; ++px) {
                    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                    uint8_t v = ((seed & 3u) == 0) ? 255 : uint8_t((seed & 15u) * 8u);
                    c.blend_px(x + px, y + py, pack_rgba(v, v, v, alpha));
                }
            }
        }
    }
};

// --- NativeGlue: Original Blizzard Glue Screens (glumain, glucmpgn, glurdy*, glucustm, gluload, gluscore) ---
struct BlizzGlue {
    static constexpr double ENTER_MS = 670.0;
    static constexpr double EXIT_MS  = 365.0;

    struct Layer {
        int widget_id = 0;
        BinAnim spec;
        int movie_id = -1;
        int mw = 0, mh = 0, frames = 1, interval = 67;
        double hot_time = 0;
        bool was_hot = false;
    };

    std::string name, pal_name;
    bool slide = false, exiting = false, lock_played = false, selected_start = false;
    double elapsed = 0, anim_elapsed = 0;
    int hot_id = -1;
    BlizzDialog layout;
    PCX background, tint;
    BlizzFont tiny, small, dialogue, large, button;
    int glass_idx = 138;
    std::map<int, PCX> panels;
    std::map<int, bool> scaled_panels;
    std::vector<Layer> layers;
    std::vector<uint8_t> dlg_grp, tile_grp;

    // Briefing Room portrait frames & glass masks
    std::vector<uint8_t> portrait_masks[4];
    ImageRGBA portrait_frames[4], speaker_frames[4];

    void close() {
        for (auto& l : layers) {
            if (l.movie_id >= 0) sd_movie_close(l.movie_id);
        }
        layers.clear();
        panels.clear();
        scaled_panels.clear();
        for (int i = 0; i < 4; ++i) portrait_masks[i].clear();
    }
    ~BlizzGlue() { close(); }

    bool open(const std::string& bin_name, const std::string& pal, bool do_slide) {
        close();
        name = bin_name;
        pal_name = pal;
        slide = do_slide;
        exiting = false;
        lock_played = !do_slide;
        selected_start = false;
        elapsed = anim_elapsed = 0;
        hot_id = -1;

        if (!layout.load(bin_name)) return false;
        if (!load_pcx("glue\\" + pal + "\\backgnd.pcx", background, false, -1) ||
            !load_pcx("glue\\" + pal + "\\tfont.pcx", tint, false, -1)) {
            pal_name = "palnl";
            if (!load_pcx("glue\\palnl\\backgnd.pcx", background, false, -1) ||
                !load_pcx("glue\\palnl\\tfont.pcx", tint, false, -1))
                return false;
        }

        tiny.load("8", tint, background.palette, 1);
        small.load("10", tint, background.palette, 1);
        dialogue.load("14", tint, background.palette, 1);
        large.load("16", tint, background.palette, 1);
        button.load("16x", tint, background.palette, 1);

        PCX teff;
        glass_idx = 138;
        if (load_pcx("glue\\" + pal_name + "\\teffect.pcx", teff, false, -1) && teff.pixels.size() > 1)
            glass_idx = teff.pixels[1];

        dlg_grp = mpq_read("glue\\" + pal_name + "\\dlg.grp");
        if (dlg_grp.empty()) dlg_grp = mpq_read("glue\\palnl\\dlg.grp");
        tile_grp = mpq_read("glue\\" + pal_name + "\\tile.grp");
        if (tile_grp.empty()) tile_grp = mpq_read("glue\\palnl\\tile.grp");

        bool is_brief = (name.rfind("glurdy", 0) == 0);

        if (name == "gluload") {
            for (auto& w : layout.widgets) {
                int nx = -1, ny = 0, nw = 0, nh = 0;
                std::string p = norm_path(w.text);
                if (w.type == 5) {
                    if (p.find("\\pinfo.pcx") != std::string::npos) { nx = 388; ny = 55; nw = 240; nh = 301; }
                    else if (p.find("\\plist.pcx") != std::string::npos) { nx = 0; ny = 0; nw = 364; nh = 356; }
                    else if (p.find("\\pcancel.pcx") != std::string::npos) { nx = 472; ny = 356; nw = 168; nh = 91; }
                    else if (p.find("\\pok.pcx") != std::string::npos) { nx = 436; ny = 336; nw = 204; nh = 71; }
                } else if (w.type == 12) {
                    nx = 64; ny = 92; nw = 286; nh = 218;
                } else if ((w.type == 14 || w.type == 1 || w.type == 2) && (w.flags & 0x80000u)) {
                    nx = 441; ny = 383; nw = 152; nh = 22;
                } else if ((w.type == 14 || w.type == 1 || w.type == 2) && (w.flags & 64u)) {
                    nx = 477; ny = 419; nw = 132; nh = 22;
                }
                if (nx >= 0) {
                    w.x = nx; w.y = ny; w.w = nw; w.h = nh;
                    w.rx = w.ry = w.tx = w.ty = 0;
                    w.rx2 = w.rw = nw - 1; w.ry2 = w.rh = nh - 1;
                }
            }
        }

        // Restore classic animated Editor/Options planet on glumain if retail BIN moved widget 5
        if (name == "glumain") {
            if (auto* opt = layout.get(5)) {
                if (opt->x == 0 && opt->y == 345 && opt->smk == 0) {
                    opt->x = 375; opt->y = 104; opt->w = 225; opt->h = 148;
                    opt->tx = 0; opt->ty = 0; opt->rw = 224; opt->rh = 147;
                    opt->rx = 3; opt->ry = 0; opt->rx2 = 160; opt->ry2 = 140;
                    opt->flags = 0x01106218u; opt->type = 14;
                }
            }
        }

        for (const auto& w : layout.widgets) {
            if (w.type == 5 && (w.flags & 8u) != 0) {
                PCX p;
                if (load_pcx(w.text, p, true, (w.flags & 0x40000u) ? glass_idx : -1)) {
                    if (slide && slide_distance(&w) != 0)
                        strip_edge_backdrop(p.image, background.image, w.x, w.y);
                    panels[w.id] = std::move(p);
                } else if (name == "glucustm" && (w.id == 1 || w.id == 2 || w.id == 19)) {
                    if (load_pcx(w.id == 2 ? "glue\\chatroom\\pjoin.pcx" : "glue\\chatroom\\plist.pcx", p, true, glass_idx)) {
                        panels[w.id] = std::move(p);
                        scaled_panels[w.id] = true;
                    }
                }
            }

            auto anims = layout.animations(w);
            if (!is_brief) {
                std::reverse(anims.begin(), anims.end());
                std::stable_sort(anims.begin(), anims.end(), [](const BinAnim& a, const BinAnim& b) {
                    return (a.flags & 8) < (b.flags & 8);
                });
            }
            for (const auto& a : anims) {
                int info[5] = {};
                int mid = sd_movie_open_asset(-1, a.name.c_str(), false, info);
                if (mid >= 0) {
                    Layer l;
                    l.widget_id = w.id; l.spec = a; l.movie_id = mid;
                    l.mw = info[0]; l.mh = info[1];
                    l.frames = std::max(1, info[2]); l.interval = std::max(15, info[3]);
                    layers.push_back(l);
                }
            }
        }

        // Classic editor planet movies if synthesized on glumain
        if (name == "glumain") {
            bool has5 = false;
            for (const auto& l : layers) if (l.widget_id == 5) has5 = true;
            if (!has5 && layout.get(5)) {
                const struct { const char* path; int flg, x, y; } ed[2] = {
                    {"glue\\mainmenu\\editor.smk", 4, 0, 0},
                    {"glue\\mainmenu\\editoron.smk", 12, 21, 16},
                };
                for (const auto& e : ed) {
                    int info[5] = {};
                    int mid = sd_movie_open_asset(-1, e.path, false, info);
                    if (mid >= 0) {
                        Layer l;
                        l.widget_id = 5; l.spec = {e.path, e.flg, e.x, e.y};
                        l.movie_id = mid; l.mw = info[0]; l.mh = info[1];
                        l.frames = std::max(1, info[2]); l.interval = std::max(15, info[3]);
                        layers.push_back(l);
                    }
                }
            }
        }

        if (is_brief) {
            char r = name.back();
            std::string pref = std::string("glue\\ready") + r + "\\";
            for (int i = 0; i < 4; ++i) {
                const auto* vw = layout.get(15 + i);
                const auto* bw = layout.get(9 + i);
                auto pit = panels.find(9 + i);
                if (vw && bw && pit != panels.end()) {
                    const PCX& pan = pit->second;
                    portrait_masks[i].assign(size_t(vw->w) * size_t(vw->h), 0);
                    for (int y = 0; y < vw->h; ++y) {
                        for (int x = 0; x < vw->w; ++x) {
                            int px = vw->x + x - bw->x, py = vw->y + y - bw->y;
                            if (px >= 0 && py >= 0 && px < pan.width && py < pan.height &&
                                pan.pixels[size_t(py) * pan.stride + px] == glass_idx)
                                portrait_masks[i][size_t(y) * vw->w + x] = 1;
                        }
                    }
                }
                PCX f1, fh;
                if (load_pcx(pref + r + "frame" + std::to_string(i + 1) + ".pcx", f1, true, -1))
                    portrait_frames[i] = std::move(f1.image);
                if (load_pcx(pref + r + "frameh" + std::to_string(i + 1) + ".pcx", fh, true, -1))
                    speaker_frames[i] = std::move(fh.image);
            }
            if (name == "glurdyt") {
                Layer* base = nullptr;
                bool has_sel = false;
                for (auto& l : layers) {
                    if (l.spec.name.find("\\start.smk") != std::string::npos) base = &l;
                    if (l.spec.name.find("\\starton.smk") != std::string::npos) has_sel = true;
                }
                if (base && !has_sel) {
                    int info[5] = {};
                    std::string spath = pref + "starton.smk";
                    int mid = sd_movie_open_asset(-1, spath.c_str(), false, info);
                    if (mid >= 0) {
                        Layer sel;
                        sel.widget_id = base->widget_id;
                        sel.spec = {spath, 12, base->spec.x + (base->mw - info[0]) / 2, base->spec.y + base->mh - info[1]};
                        sel.movie_id = mid; sel.mw = info[0]; sel.mh = info[1];
                        sel.frames = std::max(1, info[2]); sel.interval = std::max(15, info[3]);
                        layers.push_back(sel);
                    }
                }
            }
            std::stable_sort(layers.begin(), layers.end(), [](const Layer& a, const Layer& b) {
                auto is_proj = [](const Layer& l) {
                    return l.spec.name.find("\\proj.smk") != std::string::npos ||
                           l.spec.name.find("\\proloop.smk") != std::string::npos;
                };
                return (is_proj(a) ? 0 : 1) < (is_proj(b) ? 0 : 1);
            });
        }
        return true;
    }

    bool campaign_choice(int id) const {
        return (name == "glucmpgn" || name == "gluexpcmpgn") && id >= 6 && id <= 8;
    }

    int slide_distance(const BinWidget* w) const {
        if (!slide || !w) return 0;
        const BinWidget* ref = w;
        if (name == "glucmpgn" || name == "gluexpcmpgn") {
            if (w->id >= 6 && w->id <= 8) return 0;
            if (w->id == 5 || w->id == 11) ref = layout.get(1);
            else if (w->id == 9 || w->id == 10) ref = layout.get(3);
        } else if (name == "glucustm") {
            if (w->id == 12) ref = layout.get(4);
            else if (w->id == 13) ref = layout.get(3);
            else if (w->id >= 20 && w->id <= 35) ref = layout.get(19);
            else if (w->id == 7 || w->id == 8 || w->id == 10 || w->id == 11) ref = layout.get(2);
            else if (w->type != 5) ref = layout.get(1);
        } else if (name.rfind("glurdy", 0) == 0) {
            if (w->id >= 15 && w->id <= 18) ref = layout.get(w->id - 6);
            else if (w->id == 1 || w->id == 13) return 0;
            else if (w->id == 14) ref = layout.get(7);
            else if (w->id == 20) ref = layout.get(5);
            else if (w->id == 65526) ref = layout.get(9);
            else if (w->id == 65525 || w->id == 65535) ref = layout.get(6);
        }
        if (!ref) return 0;
        return (ref->x + ref->w / 2 < 320) ? -(ref->x + ref->w) : (640 - ref->x);
    }

    int shift(const BinWidget* w) const {
        if (!slide || !w) return 0;
        double p = std::min(1.0, elapsed / (exiting ? EXIT_MS : ENTER_MS));
        double amt = exiting ? (1.0 - std::pow(1.0 - p, 3.0)) : std::pow(1.0 - p, 3.0);
        return int(std::lround(double(slide_distance(w)) * amt));
    }

    bool label_visible(int id) const {
        const auto* w = layout.get(id);
        if (!w) return false;
        if (campaign_choice(id)) return !exiting && (!slide || elapsed >= ENTER_MS);
        return (!(exiting || elapsed < ENTER_MS) || slide_distance(w) == 0);
    }

    bool ready() const { return !exiting && (!slide || elapsed >= ENTER_MS); }

    double projector_intro_ms(int wid) const {
        for (const auto& l : layers)
            if (l.widget_id == wid && l.spec.name.find("\\proj.smk") != std::string::npos)
                return double(l.frames) * double(l.interval);
        return 0.0;
    }

    void draw_base(Canvas& c, double dt_ms) {
        elapsed += dt_ms;
        anim_elapsed += dt_ms;
        if (slide && !exiting && !lock_played && elapsed >= ENTER_MS) {
            lock_played = true;
            sd_play_mpq_sound(-1, "sound\\glue\\swishlock.wav", 60, false);
        }
        c.reset_clip();
        c.blit_img(background.image, 0, 0);

        bool is_brief = (name.rfind("glurdy", 0) == 0);
        for (auto& l : layers) {
            const auto* w = layout.get(l.widget_id);
            if (!w) continue;
            if (campaign_choice(w->id) && !label_visible(w->id)) continue;

            double lay_elapsed = std::max(0.0, anim_elapsed - ((slide && campaign_choice(w->id)) ? ENTER_MS : 0.0));
            bool active = (hot_id == w->id && !exiting) || (selected_start && w->id == 13);
            if (active && !l.was_hot) l.hot_time = 0;
            else if (active) l.hot_time += dt_ms;
            l.was_hot = active;

            if ((l.spec.flags & 8) != 0 && !active) continue;
            if (active && is_brief && l.spec.name.find("\\start.smk") != std::string::npos) {
                bool has_hover = false;
                for (const auto& h : layers)
                    if (h.widget_id == w->id && h.spec.name.find("\\starton.smk") != std::string::npos) has_hover = true;
                if (has_hover) continue;
            }

            double t = (is_brief && w->id == 13) ? lay_elapsed : ((l.spec.flags & 8) ? l.hot_time : lay_elapsed);
            bool loop = (l.spec.flags & 4) != 0;
            if (l.spec.name.find("\\proloop.smk") != std::string::npos) {
                t -= projector_intro_ms(w->id);
                if (t < 0) continue;
                loop = true;
            } else if (l.spec.name.find("\\proj.smk") != std::string::npos) {
                bool has_loop = false;
                for (const auto& h : layers)
                    if (h.widget_id == w->id && h.spec.name.find("\\proloop.smk") != std::string::npos) has_loop = true;
                if (has_loop && t >= double(l.frames) * double(l.interval)) continue;
                loop = false;
            }

            bool crystal = (name == "glurdyp") && (l.spec.name.find("\\glow.smk") != std::string::npos);
            float br = (!crystal && (l.spec.flags & 2) != 0 && !active) ? 0.25f : 1.0f;
            if (name == "glurdyt" && l.spec.name.find("\\start.smk") != std::string::npos) br = 0.55f;

            float alpha = (!campaign_choice(w->id) && (l.spec.flags & 1) != 0) ? float(std::min(1.0, lay_elapsed / 600.0)) : 1.0f;
            if (crystal && !active) alpha *= 0.25f;

            int sx = shift(w);
            if (name == "glumain") {
                c.set_clip(w->x + sx, w->y, w->x + sx + w->w, w->y + w->h);
            } else if (is_brief && (w->id == 20 || w->id == 14)) {
                if (const auto* pb = layout.get(w->id == 20 ? 5 : 7)) {
                    int psx = shift(pb);
                    c.set_clip(pb->x + psx, pb->y, pb->x + psx + pb->w, pb->y + pb->h);
                }
            }
            int fw = 0, fh = 0;
            if (const uint8_t* fr = sd_movie_frame(l.movie_id, t, loop, &fw, &fh)) {
                c.blit(reinterpret_cast<const uint32_t*>(fr), fw, fh,
                       w->x + sx + l.spec.x, w->y + l.spec.y, br, uint8_t(alpha * 255.0f));
            }
            c.reset_clip();
        }

        for (const auto& w : layout.widgets) {
            auto it = panels.find(w.id);
            if (it == panels.end()) continue;
            int sx = shift(&w);
            if (scaled_panels[w.id]) {
                c.blit_scaled(it->second.image.px.data(), it->second.width, it->second.height,
                              w.x + sx, w.y, w.w, w.h);
            } else {
                c.blit_img(it->second.image, w.x + sx, w.y);
            }
        }
    }

    BlizzFont& font_for(const BinWidget& w) {
        if (w.flags & 0x4000u) return button;
        if ((w.flags & 0x800u) || w.type == 14) return large;
        if (w.flags & 0x400u) return small;
        return tiny;
    }

    void draw_label(Canvas& c, int id, const char* fallback_txt = nullptr, bool enabled = true, bool force_override = false) {
        const auto* w = layout.get(id);
        if (!w || !label_visible(id)) return;
        std::string bin_txt = w->text;
        if ((w->flags & 0x200u) && !bin_txt.empty()) bin_txt.erase(0, 1);
        std::string txt = (force_override && fallback_txt) ? std::string(fallback_txt)
                        : (!bin_txt.empty() ? bin_txt : (fallback_txt ? std::string(fallback_txt) : ""));
        if (!enabled) txt = clean_ctrl(txt);
        auto& fnt = font_for(*w);
        int bw = std::max(1, (w->rw > 0 ? w->rw : w->w) - w->tx);
        int bh = std::max(1, (w->rh > 0 ? w->rh : w->h) - w->ty);
        int align = (w->flags & 0x200000u) ? 1 : ((w->flags & 0x400000u) ? 2 : 0);
        auto lines = fnt.wrap_lines(txt, bw);
        int th = int(lines.size()) * fnt.height;
        int y = w->y + w->ty + ((w->flags & 0x4000000u) ? (bh - th) : ((w->flags & 0x2000000u) ? (bh - th) / 2 : 0));
        int col = !enabled ? 5 : (hot_id == id ? 4 : (w->type == 14 ? 3 : 2));
        fnt.draw(c, txt, w->x + w->tx + shift(w), y, bw, align, col, 0);
    }

    void draw_box_frame(Canvas& c, int x, int y, int w, int h, bool is_btn, bool hov = false, bool enabled = true) {
        if (is_btn) {
            c.fill_rect(x, y, w, h, pack_rgba(18, 8, 12, 215));
            c.stroke_rect(x, y, w, h, hov ? pack_rgba(255, 65, 75) : (enabled ? pack_rgba(192, 0, 36) : pack_rgba(90, 60, 68)));
            c.stroke_rect(x + 1, y + 1, w - 2, h - 2, pack_rgba(88, 12, 22));
            return;
        }
        c.fill_rect(x, y, w, h, pack_rgba(8, 12, 18, 232));
        c.stroke_rect(x, y, w, h, pack_rgba(192, 0, 36));
        c.stroke_rect(x + 2, y + 2, w - 4, h - 4, pack_rgba(110, 14, 28));
    }

    void draw_scrollbar(Canvas& c, int x, int y, int h, float frac) {
        frac = std::max(0.0f, std::min(1.0f, frac));
        uint32_t rail = pack_rgba(192, 0, 36), bright = pack_rgba(255, 0, 51);
        c.fill_rect(x + 1, y + 17, 1, h - 33, rail);
        c.fill_rect(x + 14, y + 17, 1, h - 33, rail);
        c.fill_rect(x + 1, y + 17, 14, 1, rail);
        c.fill_rect(x + 1, y + h - 17, 14, 1, rail);
        for (int row = 0; row < 13; ++row) {
            int sp = row / 2;
            c.fill_rect(x + 7 - sp, y + 1 + row, 2, 1, bright);
            c.fill_rect(x + 7 + sp, y + 1 + row, 2, 1, bright);
            c.fill_rect(x + 7 - sp, y + h - 2 - row, 2, 1, bright);
            c.fill_rect(x + 7 + sp, y + h - 2 - row, 2, 1, bright);
        }
        c.fill_rect(x + 1, y + 13, 14, 1, bright);
        c.fill_rect(x + 1, y + h - 14, 14, 1, bright);
        int cy = y + 23 + int(std::lround(frac * float(std::max(0, h - 46))));
        c.fill_rect(x + 5, cy - 6, 7, 1, bright);
        c.fill_rect(x + 5, cy + 6, 7, 1, bright);
        for (int r = -5; r <= 5; ++r) {
            int ins = (std::abs(r) == 5) ? 2 : (std::abs(r) == 4 ? 1 : 0);
            c.fill_rect(x + 2 + ins, cy + r, 1, 1, bright);
            c.fill_rect(x + 14 - ins, cy + r, 1, 1, bright);
        }
    }
};

// --- NativeMapPreview: Authentic 128x128 Terrain + StartLoc/Resource Preview from CHK ---
struct MapMeta {
    std::string title, desc, tileset_name;
    int tw = 0, th = 0, tileset = 0;
    int owners[12] = {}, sides[12] = {}, forces[8] = {};
    int human_slots = 0, comp_slots = 0, start_locs = 0;
    bool ums_only = false;
    ImageRGBA preview;
};

inline bool build_map_preview(const std::string& path, MapMeta& out) {
    out = MapMeta{};
    uint8_t* raw = nullptr;
    size_t sz = 0;
    if (!sd_map_scenario_chk(path.c_str(), &raw, &sz) || !raw) return false;
    std::vector<uint8_t> chk(raw, raw + sz);
    sd_free_buf(raw);

    const uint8_t* mtxm = nullptr; size_t mtxm_len = 0;
    const uint8_t* units = nullptr; size_t units_len = 0;
    const uint8_t* str_sec = nullptr; size_t str_len = 0; bool is_strx = false;
    int sprp_name = 0, sprp_desc = 0;

    for (size_t p = 0; p + 8 <= chk.size();) {
        uint32_t len = u32_le(chk.data() + p + 4);
        if (len > chk.size() - p - 8) break;
        const uint8_t* body = chk.data() + p + 8;
        if (!std::memcmp(chk.data() + p, "DIM ", 4) && len >= 4) {
            out.tw = u16_le(body); out.th = u16_le(body + 2);
        } else if (!std::memcmp(chk.data() + p, "ERA ", 4) && len >= 2) {
            out.tileset = u16_le(body) & 7;
        } else if (!std::memcmp(chk.data() + p, "OWNR", 4) && len >= 12) {
            for (int i = 0; i < 12; ++i) out.owners[i] = body[i];
        } else if (!std::memcmp(chk.data() + p, "SIDE", 4) && len >= 12) {
            for (int i = 0; i < 12; ++i) out.sides[i] = body[i];
        } else if (!std::memcmp(chk.data() + p, "FORC", 4) && len >= 8) {
            for (int i = 0; i < 8; ++i) out.forces[i] = body[i];
        } else if (!std::memcmp(chk.data() + p, "SPRP", 4) && len >= 4) {
            sprp_name = u16_le(body); sprp_desc = u16_le(body + 2);
        } else if (!std::memcmp(chk.data() + p, "MTXM", 4)) {
            mtxm = body; mtxm_len = len;
        } else if (!std::memcmp(chk.data() + p, "UNIT", 4)) {
            units = body; units_len = len;
        } else if (!std::memcmp(chk.data() + p, "STRx", 4)) {
            str_sec = body; str_len = len; is_strx = true;
        } else if (!std::memcmp(chk.data() + p, "STR ", 4) && !is_strx) {
            str_sec = body; str_len = len;
        }
        p += 8 + size_t(len);
    }
    if (out.tw <= 0 || out.th <= 0) return false;

    static const char* kEras[8] = {
        "badlands", "platform", "install", "ashworld", "jungle", "desert", "ice", "twilight"
    };
    out.tileset_name = kEras[out.tileset & 7];

    auto get_chk_str = [&](int id) -> std::string {
        if (!str_sec || id <= 0) return "";
        if (is_strx) {
            if (str_len < 4) return "";
            uint32_t cnt = u32_le(str_sec);
            if (uint32_t(id) > cnt || 4 + size_t(id) * 4 > str_len) return "";
            return cstr_at(str_sec, str_len, u32_le(str_sec + 4 + size_t(id - 1) * 4));
        } else {
            if (str_len < 2) return "";
            uint16_t cnt = u16_le(str_sec);
            if (id > cnt || 2 + size_t(id) * 2 > str_len) return "";
            return cstr_at(str_sec, str_len, u16_le(str_sec + 2 + size_t(id - 1) * 2));
        }
    };
    out.title = clean_ctrl(get_chk_str(sprp_name));
    out.desc  = clean_ctrl(get_chk_str(sprp_desc));
    if (out.title.empty()) {
        size_t sl = path.find_last_of("/\\");
        out.title = (sl == std::string::npos) ? path : path.substr(sl + 1);
    }

    for (int i = 0; i < 8; ++i) {
        if (out.owners[i] == 6) ++out.human_slots;
        else if (out.owners[i] == 5) ++out.comp_slots;
    }

    // Render 128x128 terrain thumbnail
    int max_dim = std::max(out.tw, out.th);
    int pw = std::max(1, out.tw * 128 / max_dim);
    int ph = std::max(1, out.th * 128 / max_dim);
    out.preview.w = pw; out.preview.h = ph;
    out.preview.px.assign(size_t(pw) * size_t(ph), pack_rgba(12, 18, 14));

    std::string tp = std::string("tileset\\") + out.tileset_name;
    auto cv5 = mpq_read(tp + ".cv5");
    auto vx4 = mpq_read(tp + ".vx4ex");
    bool vx4_32 = !vx4.empty();
    if (!vx4_32) vx4 = mpq_read(tp + ".vx4");
    auto vr4 = mpq_read(tp + ".vr4");
    auto wpe = mpq_read(tp + ".wpe");

    if (mtxm && !cv5.empty() && !vx4.empty() && !vr4.empty() && wpe.size() >= 1024) {
        size_t mega_stride = vx4_32 ? 64 : 32;
        for (int y = 0; y < ph; ++y) {
            int ty = y * out.th / ph;
            for (int x = 0; x < pw; ++x) {
                int tx = x * out.tw / pw;
                size_t ti = (size_t(ty) * out.tw + tx) * 2;
                if (ti + 2 > mtxm_len) continue;
                uint16_t tile = u16_le(mtxm + ti);
                size_t cv_off = size_t(tile >> 4) * 52 + 20 + size_t(tile & 15) * 2;
                if (cv_off + 2 > cv5.size()) continue;
                uint16_t mega = u16_le(cv5.data() + cv_off);
                size_t vx_off = size_t(mega) * mega_stride;
                if (vx_off + (vx4_32 ? 4 : 2) > vx4.size()) continue;
                uint32_t mini = (vx4_32 ? u32_le(vx4.data() + vx_off) : u16_le(vx4.data() + vx_off)) >> 1;
                size_t vr_off = size_t(mini) * 64 + 27;
                if (vr_off >= vr4.size()) continue;
                uint8_t pidx = vr4[vr_off];
                out.preview.px[size_t(y) * pw + x] = pack_rgba(wpe[pidx * 4], wpe[pidx * 4 + 1], wpe[pidx * 4 + 2]);
            }
        }
    }

    if (units) {
        static const uint32_t kStartCols[8] = {
            pack_rgba(244, 4, 4), pack_rgba(12, 72, 204), pack_rgba(44, 180, 148), pack_rgba(136, 64, 156),
            pack_rgba(248, 140, 20), pack_rgba(112, 48, 20), pack_rgba(204, 224, 208), pack_rgba(252, 252, 56)
        };
        for (size_t u = 0; u + 36 <= units_len; u += 36) {
            int ux = u16_le(units + u + 4), uy = u16_le(units + u + 6);
            int ut = u16_le(units + u + 8), own = units[u + 16];
            int px = ux * pw / std::max(1, out.tw * 32);
            int py = uy * ph / std::max(1, out.th * 32);
            if (ut == 214) { // Player Start Location
                ++out.start_locs;
                uint32_t col = kStartCols[own & 7];
                for (int dy = -2; dy <= 2; ++dy)
                    for (int dx = -2; dx <= 2; ++dx)
                        if (unsigned(px + dx) < unsigned(pw) && unsigned(py + dy) < unsigned(ph))
                            out.preview.px[size_t(py + dy) * pw + (px + dx)] =
                                (std::abs(dx) == 2 || std::abs(dy) == 2) ? pack_rgba(255, 255, 80) : col;
            } else if ((ut >= 176 && ut <= 178) || ut == 188) { // Minerals / Vespene Geyser
                uint32_t col = (ut == 188) ? pack_rgba(40, 225, 90) : pack_rgba(0, 212, 228);
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (unsigned(px + dx) < unsigned(pw) && unsigned(py + dy) < unsigned(ph))
                            out.preview.px[size_t(py + dy) * pw + (px + dx)] = col;
            }
        }
    }
    out.ums_only = (out.start_locs < 2 && out.human_slots >= 1);
    return true;
}

} // namespace blizz
