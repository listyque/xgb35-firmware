#pragma once
// Dynamic RGBA8888 UI canvas, FreeType UTF-8 glyph cache, FFmpeg thumbnail decoder, and on-screen keyboard.

#include "core.h"
#include "i18n.h"

#include <ft2build.h>
#include FT_FREETYPE_H

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace jf {

inline constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
}

// Theme colors matching streamyfin_my dark neutral + purple accent palette
namespace col {
static constexpr uint32_t Bg           = rgba(18, 18, 20);
static constexpr uint32_t TopBar       = rgba(24, 23, 28);
static constexpr uint32_t Panel        = rgba(28, 27, 32);
static constexpr uint32_t Card         = rgba(23, 23, 26);
static constexpr uint32_t CardBorder   = rgba(45, 43, 52);
static constexpr uint32_t LastPlayedBg = rgba(27, 23, 31);
static constexpr uint32_t LastPlayedBd = rgba(64, 48, 76);
static constexpr uint32_t FocusBg      = rgba(57, 44, 68);
static constexpr uint32_t FocusBorder  = rgba(192, 132, 252);
static constexpr uint32_t Accent       = rgba(168, 85, 247);
static constexpr uint32_t AccentSoft   = rgba(216, 180, 254);
static constexpr uint32_t TextMain     = rgba(245, 245, 247);
static constexpr uint32_t TextMuted    = rgba(163, 163, 170);
static constexpr uint32_t TextGreen    = rgba(52, 211, 153);
static constexpr uint32_t TextRed      = rgba(248, 113, 113);
static constexpr uint32_t Divider      = rgba(38, 38, 44);
} // namespace col

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

struct HitZone {
    int id = 0;
    Rect r;
};

struct RgbaImage {
    int w = 0, h = 0;
    std::vector<uint32_t> px;
    bool valid() const { return w > 0 && h > 0 && px.size() == size_t(w) * size_t(h); }
};

inline bool decode_image_to_rgba(const void* data, size_t len, int dst_w, int dst_h, RgbaImage& out) {
    if (!data || len < 16 || dst_w <= 0 || dst_h <= 0) return false;
    const uint8_t* b = static_cast<const uint8_t*>(data);
    AVCodecID cid = AV_CODEC_ID_MJPEG;
    if (len >= 8 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G')
        cid = AV_CODEC_ID_PNG;
    else if (len >= 12 && std::memcmp(b, "RIFF", 4) == 0 && std::memcmp(b + 8, "WEBP", 4) == 0)
        cid = AV_CODEC_ID_WEBP;

    const AVCodec* dec = avcodec_find_decoder(cid);
    if (!dec) return false;
    AVCodecContext* ctx = avcodec_alloc_context3(dec);
    if (!ctx) return false;
    if (avcodec_open2(ctx, dec, nullptr) < 0) {
        avcodec_free_context(&ctx);
        return false;
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frm = av_frame_alloc();
    bool ok = false;
    if (pkt && frm) {
        pkt->data = const_cast<uint8_t*>(b);
        pkt->size = int(len);
        if (avcodec_send_packet(ctx, pkt) == 0 && avcodec_receive_frame(ctx, frm) == 0 && frm->width > 0 && frm->height > 0) {
            SwsContext* sws = sws_getContext(
                frm->width, frm->height, static_cast<AVPixelFormat>(frm->format),
                dst_w, dst_h, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr
            );
            if (sws) {
                out.w = dst_w;
                out.h = dst_h;
                out.px.resize(size_t(dst_w) * size_t(dst_h));
                uint8_t* dst_planes[4] = {reinterpret_cast<uint8_t*>(out.px.data()), nullptr, nullptr, nullptr};
                int dst_lines[4] = {dst_w * 4, 0, 0, 0};
                sws_scale(sws, frm->data, frm->linesize, 0, frm->height, dst_planes, dst_lines);
                sws_freeContext(sws);
                ok = true;
            }
        }
    }
    if (frm) av_frame_free(&frm);
    if (pkt) av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    return ok;
}

class FontRenderer {
    FT_Library ft_ = nullptr;
    FT_Face face_reg_ = nullptr;
    FT_Face face_bold_ = nullptr;

    struct Glyph {
        int w = 0, h = 0;
        int bx = 0, by = 0;
        int adv = 0;
        std::vector<uint8_t> alpha;
    };
    std::map<std::tuple<uint32_t, int, bool>, Glyph> cache_;

public:
    ~FontRenderer() {
        if (face_bold_) FT_Done_Face(face_bold_);
        if (face_reg_) FT_Done_Face(face_reg_);
        if (ft_) FT_Done_FreeType(ft_);
    }

    bool init(const std::string& reg_path, const std::string& bold_path) {
        if (FT_Init_FreeType(&ft_) != 0) return false;
        const char* reg_candidates[] = {
            reg_path.c_str(),
            "/usr/share/fonts/dejavu/DejaVuSans.ttf",
            "/desktop/usr/share/fonts/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
        };
        for (const char* p : reg_candidates) {
            if (p && *p && FT_New_Face(ft_, p, 0, &face_reg_) == 0) break;
        }
        if (!face_reg_) return false;
        if (!bold_path.empty()) FT_New_Face(ft_, bold_path.c_str(), 0, &face_bold_);
        return true;
    }

    const Glyph& get_glyph(uint32_t cp, int px_size, bool bold) {
        auto key = std::make_tuple(cp, px_size, bold);
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;
        if (cache_.size() > 4096) cache_.clear();

        FT_Face face = (bold && face_bold_) ? face_bold_ : face_reg_;
        Glyph g;
        if (face) {
            FT_Set_Pixel_Sizes(face, 0, unsigned(px_size));
            FT_UInt gidx = FT_Get_Char_Index(face, cp);
            if (FT_Load_Glyph(face, gidx, FT_LOAD_RENDER) == 0) {
                FT_GlyphSlot slot = face->glyph;
                g.w = int(slot->bitmap.width);
                g.h = int(slot->bitmap.rows);
                g.bx = slot->bitmap_left;
                g.by = slot->bitmap_top;
                g.adv = int(slot->advance.x >> 6);
                g.alpha.resize(size_t(g.w) * size_t(g.h));
                for (int y = 0; y < g.h; ++y) {
                    std::memcpy(
                        g.alpha.data() + size_t(y) * size_t(g.w),
                        slot->bitmap.buffer + y * slot->bitmap.pitch,
                        size_t(g.w)
                    );
                }
            }
        }
        return cache_.emplace(key, std::move(g)).first->second;
    }

    int text_width(const std::string& s, int px_size, bool bold = false) {
        int w = 0;
        size_t i = 0;
        while (i < s.size()) {
            uint32_t cp = next_utf8(s, i);
            if (cp == '\n') break;
            w += get_glyph(cp, px_size, bold).adv;
        }
        return w;
    }
};

struct Canvas {
    int w = 480, h = 320;
    std::vector<uint32_t> px;
    Rect clip{0, 0, 480, 320};

    void resize(int nw, int nh) {
        w = std::max(64, nw);
        h = std::max(64, nh);
        px.assign(size_t(w) * size_t(h), col::Bg);
        clip = {0, 0, w, h};
    }

    void reset_clip() { clip = {0, 0, w, h}; }

    void set_clip(Rect r) {
        int x0 = std::max(0, r.x);
        int y0 = std::max(0, r.y);
        int x1 = std::min(w, r.x + r.w);
        int y1 = std::min(h, r.y + r.h);
        clip = {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
    }

    void clear(uint32_t c = col::Bg) {
        std::fill(px.begin(), px.end(), c);
        reset_clip();
    }

    inline void blend_pixel(int x, int y, uint32_t c) {
        if (x < clip.x || x >= clip.x + clip.w || y < clip.y || y >= clip.y + clip.h) return;
        uint32_t a = (c >> 24) & 0xFFu;
        if (a == 0) return;
        uint32_t& dst = px[size_t(y) * size_t(w) + size_t(x)];
        if (a == 255) { dst = c | 0xFF000000u; return; }
        uint32_t ia = 255u - a;
        uint32_t r = (((c & 0xFFu) * a) + ((dst & 0xFFu) * ia)) >> 8;
        uint32_t g = ((((c >> 8) & 0xFFu) * a) + (((dst >> 8) & 0xFFu) * ia)) >> 8;
        uint32_t b = ((((c >> 16) & 0xFFu) * a) + (((dst >> 16) & 0xFFu) * ia)) >> 8;
        dst = r | (g << 8) | (b << 16) | 0xFF000000u;
    }

    void fill_rect(Rect r, uint32_t c) {
        int x0 = std::max(clip.x, r.x);
        int y0 = std::max(clip.y, r.y);
        int x1 = std::min(clip.x + clip.w, r.x + r.w);
        int y1 = std::min(clip.y + clip.h, r.y + r.h);
        if (x0 >= x1 || y0 >= y1) return;
        uint32_t a = (c >> 24) & 0xFFu;
        if (a == 255) {
            uint32_t solid = c | 0xFF000000u;
            for (int y = y0; y < y1; ++y) {
                uint32_t* row = px.data() + size_t(y) * size_t(w) + size_t(x0);
                std::fill(row, row + (x1 - x0), solid);
            }
        } else {
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x)
                    blend_pixel(x, y, c);
        }
    }

    void round_rect(Rect r, int rad, uint32_t fill_col, uint32_t border_col = 0) {
        if (r.w <= 0 || r.h <= 0) return;
        rad = std::max(0, std::min({rad, r.w / 2, r.h / 2}));
        int x0 = std::max(clip.x, r.x);
        int y0 = std::max(clip.y, r.y);
        int x1 = std::min(clip.x + clip.w, r.x + r.w);
        int y1 = std::min(clip.y + clip.h, r.y + r.h);
        int r2_outer = rad * rad;
        int r2_inner = std::max(0, rad - 1) * std::max(0, rad - 1);

        for (int y = y0; y < y1; ++y) {
            int dy = 0;
            if (y < r.y + rad) dy = (r.y + rad - 1) - y;
            else if (y >= r.y + r.h - rad) dy = y - (r.y + r.h - rad);
            for (int x = x0; x < x1; ++x) {
                int dx = 0;
                if (x < r.x + rad) dx = (r.x + rad - 1) - x;
                else if (x >= r.x + r.w - rad) dx = x - (r.x + r.w - rad);
                if (dx > 0 && dy > 0) {
                    int d2 = dx * dx + dy * dy;
                    if (d2 > r2_outer) continue;
                    if (border_col && d2 >= r2_inner) { blend_pixel(x, y, border_col); continue; }
                } else if (border_col && (x == r.x || x == r.x + r.w - 1 || y == r.y || y == r.y + r.h - 1)) {
                    blend_pixel(x, y, border_col);
                    continue;
                }
                if (fill_col) blend_pixel(x, y, fill_col);
            }
        }
    }

    void blit_rgba(const RgbaImage& img, int dx, int dy) {
        if (!img.valid()) return;
        int x0 = std::max(clip.x, dx);
        int y0 = std::max(clip.y, dy);
        int x1 = std::min(clip.x + clip.w, dx + img.w);
        int y1 = std::min(clip.y + clip.h, dy + img.h);
        if (x0 >= x1 || y0 >= y1) return;
        for (int y = y0; y < y1; ++y) {
            const uint32_t* srow = img.px.data() + size_t(y - dy) * size_t(img.w) + size_t(x0 - dx);
            uint32_t* drow = px.data() + size_t(y) * size_t(w) + size_t(x0);
            std::memcpy(drow, srow, size_t(x1 - x0) * sizeof(uint32_t));
        }
    }

    void draw_text(
        FontRenderer& font,
        const std::string& s,
        int x,
        int y_top,
        int px_size,
        uint32_t color,
        bool bold = false,
        int max_w = 0
    ) {
        if (s.empty()) return;
        std::string draw_str = s;
        if (max_w > 0 && font.text_width(draw_str, px_size, bold) > max_w) {
            const std::string ell = "...";
            int ell_w = font.text_width(ell, px_size, bold);
            while (!draw_str.empty() && font.text_width(draw_str, px_size, bold) + ell_w > max_w)
                pop_utf8_char(draw_str);
            draw_str += ell;
        }

        int baseline = y_top + (px_size * 4) / 5;
        int pen_x = x;
        uint32_t base_a = (color >> 24) & 0xFFu;
        uint32_t rgb = color & 0x00FFFFFFu;

        size_t i = 0;
        while (i < draw_str.size()) {
            uint32_t cp = next_utf8(draw_str, i);
            if (cp == '\n') break;
            const auto& g = font.get_glyph(cp, px_size, bold);
            int gx0 = pen_x + g.bx;
            int gy0 = baseline - g.by;
            for (int gy = 0; gy < g.h; ++gy) {
                int py = gy0 + gy;
                if (py < clip.y || py >= clip.y + clip.h) continue;
                const uint8_t* arow = g.alpha.data() + size_t(gy) * size_t(g.w);
                for (int gx = 0; gx < g.w; ++gx) {
                    uint8_t ga = arow[gx];
                    if (!ga) continue;
                    uint32_t a = (uint32_t(ga) * base_a) >> 8;
                    blend_pixel(gx0 + gx, py, rgb | (a << 24));
                }
            }
            pen_x += g.adv;
        }
    }

    void draw_text_centered(
        FontRenderer& font,
        const std::string& s,
        Rect box,
        int px_size,
        uint32_t color,
        bool bold = false
    ) {
        int tw = font.text_width(s, px_size, bold);
        int max_w = std::max(8, box.w - 8);
        int tx = box.x + std::max(4, (box.w - std::min(tw, max_w)) / 2);
        int ty = box.y + (box.h - px_size) / 2;
        draw_text(font, s, tx, ty, px_size, color, bold, max_w);
    }

    // Simple crisp vector icons (folder, play triangle, download arrow, check)
    void draw_folder_icon(int cx, int cy, int sz, uint32_t c) {
        int w0 = sz, h0 = (sz * 3) / 4;
        int x0 = cx - w0 / 2, y0 = cy - h0 / 2;
        fill_rect({x0, y0, w0 * 2 / 5, std::max(2, h0 / 4)}, c);
        round_rect({x0, y0 + std::max(2, h0 / 5), w0, h0 - std::max(2, h0 / 5)}, 2, c);
    }

    void draw_play_icon(int cx, int cy, int sz, uint32_t c) {
        int half = sz / 2;
        for (int dy = -half; dy <= half; ++dy) {
            int span = ((half - std::abs(dy)) * sz) / std::max(1, half);
            fill_rect({cx - sz / 3, cy + dy, std::max(1, span), 1}, c);
        }
    }

    void draw_cursor(int mx, int my) {
        for (int y = 0; y < 14; ++y) {
            int span = std::min(10, y + 1);
            if (y >= 10) span = std::max(2, 14 - y);
            for (int x = 0; x <= span; ++x) {
                bool edge = (x == 0 || x == span || y == 0 || y == 13);
                blend_pixel(mx + x, my + y, edge ? rgba(16, 16, 20) : rgba(245, 220, 255));
            }
        }
    }
};

// --- On-Screen Keyboard (Gamepad + Mouse friendly, EN + RU + URL shortcuts) ---

struct VirtualKeyboard {
    bool active = false;
    bool mask_password = false;
    bool ru_layout = false;
    bool shift = false;
    std::string title;
    std::string text;
    int sel_row = 0;
    int sel_col = 0;
    std::function<void(const std::string&)> on_submit;

    static const std::vector<std::vector<std::string>>& rows(bool ru, bool sh) {
        static const std::vector<std::vector<std::string>> en_lo = {
            {"http://", "https://", "192.168.", ":8096", ".", "/", "-", "_"},
            {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
            {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"},
            {"a", "s", "d", "f", "g", "h", "j", "k", "l", "@"},
            {"SHIFT", "z", "x", "c", "v", "b", "n", "m", ":", "BKSP"},
            {"LANG", "SPACE", "CLEAR", "DONE"},
        };
        static const std::vector<std::vector<std::string>> en_hi = {
            {"http://", "https://", "192.168.", ":8096", ".", "/", "-", "_"},
            {"!", "@", "#", "$", "%", "^", "&", "*", "(", ")"},
            {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P"},
            {"A", "S", "D", "F", "G", "H", "J", "K", "L", "?"},
            {"SHIFT", "Z", "X", "C", "V", "B", "N", "M", "+", "BKSP"},
            {"LANG", "SPACE", "CLEAR", "DONE"},
        };
        static const std::vector<std::vector<std::string>> ru_lo = {
            {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-"},
            {"й", "ц", "у", "к", "е", "н", "г", "ш", "щ", "з", "х"},
            {"ф", "ы", "в", "а", "п", "р", "о", "л", "д", "ж", "э"},
            {"SHIFT", "я", "ч", "с", "м", "и", "т", "ь", "б", "ю", "BKSP"},
            {"LANG", "SPACE", "CLEAR", "DONE"},
        };
        static const std::vector<std::vector<std::string>> ru_hi = {
            {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "_"},
            {"Й", "Ц", "У", "К", "Е", "Н", "Г", "Ш", "Щ", "З", "Х"},
            {"Ф", "Ы", "В", "А", "П", "Р", "О", "Л", "Д", "Ж", "Э"},
            {"SHIFT", "Я", "Ч", "С", "М", "И", "Т", "Ь", "Б", "Ю", "BKSP"},
            {"LANG", "SPACE", "CLEAR", "DONE"},
        };
        return ru ? (sh ? ru_hi : ru_lo) : (sh ? en_hi : en_lo);
    }

    void open(
        const std::string& prompt,
        const std::string& initial,
        bool is_password,
        bool start_ru,
        std::function<void(const std::string&)> cb
    ) {
        active = true;
        title = prompt;
        text = initial;
        mask_password = is_password;
        ru_layout = start_ru && !is_password;
        shift = false;
        sel_row = 2;
        sel_col = 0;
        on_submit = std::move(cb);
    }

    void clamp_sel() {
        const auto& r = rows(ru_layout, shift);
        sel_row = std::max(0, std::min(int(r.size()) - 1, sel_row));
        sel_col = std::max(0, std::min(int(r[size_t(sel_row)].size()) - 1, sel_col));
    }

    void move(int dr, int dc) {
        const auto& r = rows(ru_layout, shift);
        if (dr != 0) {
            int old_len = int(r[size_t(sel_row)].size());
            sel_row = (sel_row + dr + int(r.size())) % int(r.size());
            int new_len = int(r[size_t(sel_row)].size());
            sel_col = (sel_col * new_len) / std::max(1, old_len);
        }
        if (dc != 0) {
            int len = int(r[size_t(sel_row)].size());
            sel_col = (sel_col + dc + len) % len;
        }
        clamp_sel();
    }

    void trigger_key(const std::string& key) {
        if (key == "SHIFT") { shift = !shift; }
        else if (key == "LANG") { ru_layout = !ru_layout; clamp_sel(); }
        else if (key == "SPACE") { text.push_back(' '); }
        else if (key == "BKSP") { pop_utf8_char(text); }
        else if (key == "CLEAR") { text.clear(); }
        else if (key == "DONE") {
            active = false;
            if (on_submit) on_submit(text);
        } else {
            text += key;
        }
    }

    void activate_selected() {
        clamp_sel();
        const auto& r = rows(ru_layout, shift);
        trigger_key(r[size_t(sel_row)][size_t(sel_col)]);
    }

    void render(Canvas& c, FontRenderer& font, const Strings& s, std::vector<HitZone>& hits) {
        if (!active) return;
        c.fill_rect({0, 0, c.w, c.h}, rgba(8, 8, 12, 225));
        bool compact = (c.w < 640);
        int pad = compact ? 8 : 20;
        int fsz = compact ? 13 : 16;
        int box_w = std::min(c.w - pad * 2, 760);
        int box_h = std::min(c.h - pad * 2, compact ? 304 : 400);
        int bx = (c.w - box_w) / 2;
        int by = (c.h - box_h) / 2;

        c.round_rect({bx, by, box_w, box_h}, 10, col::TopBar, col::CardBorder);
        c.draw_text(font, title, bx + 12, by + 8, fsz - 1, col::TextMuted, false, box_w - 24);

        Rect inp{bx + 10, by + (compact ? 28 : 34), box_w - 20, compact ? 30 : 38};
        c.round_rect(inp, 8, col::Card, col::FocusBorder);
        std::string shown = mask_password ? std::string(text.size(), '*') : text;
        shown.push_back('_');
        c.draw_text(font, shown, inp.x + 10, inp.y + (inp.h - fsz) / 2, fsz, col::TextMain, false, inp.w - 20);

        const auto& r = rows(ru_layout, shift);
        int grid_top = inp.y + inp.h + (compact ? 6 : 10);
        int grid_h = (by + box_h - 8) - grid_top;
        int row_h = std::max(24, grid_h / int(r.size()));

        clamp_sel();
        for (size_t ri = 0; ri < r.size(); ++ri) {
            const auto& krow = r[ri];
            int ky = grid_top + int(ri) * row_h;
            int avail_w = box_w - 20;
            int kw = avail_w / int(krow.size());
            for (size_t ci = 0; ci < krow.size(); ++ci) {
                int kx = bx + 10 + int(ci) * kw;
                int cur_w = (ci + 1 == krow.size()) ? (bx + box_w - 10 - kx) : (kw - 3);
                Rect kr{kx, ky, cur_w, row_h - 3};
                bool sel = (int(ri) == sel_row && int(ci) == sel_col);
                std::string k = krow[ci];
                std::string label = (k == "DONE") ? s.osk_done
                                  : (k == "SPACE") ? s.osk_space
                                  : (k == "BKSP") ? s.osk_bksp
                                  : (k == "CLEAR") ? s.osk_clear
                                  : (k == "LANG") ? s.osk_lang
                                  : (k == "SHIFT") ? (shift ? "CAPS" : "Shift")
                                  : k;
                uint32_t bg = (k == "DONE") ? (sel ? col::Accent : rgba(107, 33, 168))
                                            : (sel ? col::FocusBg : col::Panel);
                c.round_rect(kr, 6, bg, sel ? col::FocusBorder : col::CardBorder);
                c.draw_text_centered(font, label, kr, compact ? 12 : 14, col::TextMain, k == "DONE");
                hits.push_back({10000 + int(ri) * 32 + int(ci), kr});
            }
        }
    }
};

} // namespace jf
