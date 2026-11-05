// Copyright (c) 2026 Sabak Studios. All rights reserved.
#include "game.h"
#include "font.h"
#include "sprites.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const float SEG = 200.0f;            // road segment length
static const float MAX_SPEED = 22000.0f;    // ~220 km/h
static const float PI_F = 3.14159265f;

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline uint32_t argb(int a, int r, int g, int b) { return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b); }
static inline uint32_t rgb(int r, int g, int b) { return argb(255, r, g, b); }
static inline uint32_t mixc(uint32_t a, uint32_t b, float t) {
    t = clampf(t, 0.0f, 1.0f);
    int ch[4];
    for (int i = 0; i < 4; i++) { int s = i * 8; int x = (a >> s) & 255, y = (b >> s) & 255; ch[i] = int(x + (y - x) * t); }
    return argb(ch[3], ch[2], ch[1], ch[0]);
}
static inline uint32_t blendpx(uint32_t d, uint32_t s, int a) {
    if (a <= 0) return d;
    if (a >= 255) return 0xFF000000u | (s & 0xFFFFFFu);
    int a256 = a + (a >> 7), ia = 256 - a256;
    int r = ((((s >> 16) & 255) * a256) + (((d >> 16) & 255) * ia)) >> 8;
    int g = ((((s >> 8) & 255) * a256) + (((d >> 8) & 255) * ia)) >> 8;
    int b = (((s & 255) * a256) + ((d & 255) * ia)) >> 8;
    return 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}
static inline uint32_t hash32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
static inline float hf(uint32_t x) { return float(hash32(x) & 0xFFFF) / 65535.0f; }

// ------------------------------------------------------------------ canvas (software 2D drawing, anti-aliased)
struct Cv {
    uint32_t* fb; int W, H; float S;
    void rect(int x, int y, int w, int h, uint32_t col) {
        int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(W, x + w), y1 = std::min(H, y + h);
        if (x1 <= x0 || y1 <= y0) return;
        int a = col >> 24;
        if (a >= 255) { uint32_t v = col | 0xFF000000u; for (int yy = y0; yy < y1; yy++) std::fill(fb + yy * W + x0, fb + yy * W + x1, v); }
        else if (a > 0) for (int yy = y0; yy < y1; yy++) { uint32_t* p = fb + yy * W; for (int xx = x0; xx < x1; xx++) p[xx] = blendpx(p[xx], col, a); }
    }
    void vgrad(int x, int y, int w, int h, uint32_t c0, uint32_t c1) {
        for (int i = 0; i < h; i++) rect(x, y + i, w, 1, mixc(c0, c1, h > 1 ? float(i) / (h - 1) : 0.0f));
    }
    // rounded rectangle, anti-aliased, vertical gradient c0 -> c1 (alpha honoured)
    void rrect(float x, float y, float w, float h, float r, uint32_t c0, uint32_t c1) {
        if (w <= 0 || h <= 0) return;
        r = std::min(r, std::min(w, h) * 0.5f);
        int xa = std::max(0, int(x) - 1), ya = std::max(0, int(y) - 1), xb = std::min(W, int(x + w) + 2), yb = std::min(H, int(y + h) + 2);
        float cx = x + w * 0.5f, cy = y + h * 0.5f, hx = w * 0.5f - r, hy = h * 0.5f - r;
        for (int yy = ya; yy < yb; yy++) {
            float t = clampf((yy + 0.5f - y) / h, 0.0f, 1.0f);
            uint32_t col = mixc(c0, c1, t);
            int ca = col >> 24;
            float dy = std::max(fabsf(yy + 0.5f - cy) - hy, 0.0f);
            uint32_t* p = fb + yy * W;
            int ia = std::max(xa, int(ceilf(cx - hx - 0.5f))), ib = std::min(xb, int(floorf(cx + hx - 0.5f)) + 1);   // pixels with dx == 0
            if (dy > 0) ia = ib = std::min(std::max(xa, int(cx)), xb);                                               // corner rows: all AA
            for (int xx = xa; xx < xb; xx++) {
                if (xx == ia && ib > ia) {                                                                           // fast interior span
                    float cov = (dy > 0) ? 0 : 1.0f;
                    if (cov > 0) {
                        if (ca >= 255) std::fill(p + ia, p + ib, col | 0xFF000000u);
                        else for (int k = ia; k < ib; k++) p[k] = blendpx(p[k], col, ca);
                    }
                    xx = ib - 1; continue;
                }
                float dx = std::max(fabsf(xx + 0.5f - cx) - hx, 0.0f);
                float d = (dx > 0 || dy > 0) ? sqrtf(dx * dx + dy * dy) - r : -r;
                float cov = clampf(0.5f - d, 0.0f, 1.0f);
                if (cov > 0) p[xx] = blendpx(p[xx], col, int(cov * ca));
            }
        }
    }
    void rrect(float x, float y, float w, float h, float r, uint32_t c) { rrect(x, y, w, h, r, c, c); }
    void circle(float cx, float cy, float r, uint32_t col) { rrect(cx - r, cy - r, r * 2, r * 2, r, col, col); }
    void ring(float cx, float cy, float r0, float r1, float a0, float a1, uint32_t col) {   // angles in radians, a0 < a1
        int xa = std::max(0, int(cx - r1) - 1), xb = std::min(W, int(cx + r1) + 2), ya = std::max(0, int(cy - r1) - 1), yb = std::min(H, int(cy + r1) + 2);
        int ca = col >> 24;
        for (int yy = ya; yy < yb; yy++) for (int xx = xa; xx < xb; xx++) {
            float dx = xx + 0.5f - cx, dy = yy + 0.5f - cy, d2 = dx * dx + dy * dy;
            if (d2 > (r1 + 1) * (r1 + 1) || d2 < (r0 - 1) * (r0 - 1)) continue;
            float rr = sqrtf(d2);
            float cov = clampf(std::min(rr - r0, r1 - rr) + 0.5f, 0.0f, 1.0f);
            if (cov <= 0) continue;
            float ang = atan2f(dy, dx);
            while (ang < a0) ang += 2 * PI_F;
            if (ang > a1) continue;
            uint32_t* p = fb + yy * W + xx;
            *p = blendpx(*p, col, int(cov * ca));
        }
    }
    void line(float x0, float y0, float x1, float y1, float th, uint32_t col) {
        float len = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
        int n = std::max(1, int(len / std::max(1.0f, th * 0.4f)));
        for (int i = 0; i <= n; i++) { float t = float(i) / n; circle(lerpf(x0, x1, t), lerpf(y0, y1, t), th * 0.5f, col); }
    }
    void tri(float cx, float y0, float y1, float half, uint32_t cl, uint32_t cr) {          // isoceles, apex at top
        for (int yy = int(y0); yy < int(y1); yy++) {
            float t = (yy - y0 + 1) / std::max(1.0f, y1 - y0);
            int hw = int(half * t);
            rect(int(cx) - hw, yy, hw, 1, cl);
            rect(int(cx), yy, hw, 1, cr);
        }
    }
    // smooth text: the 5x7 bitmap font is bilinearly sampled and thresholded -> anti-aliased glyphs at any size
    float text_w(const char* s, float sc) const { int n = int(strlen(s)); return n ? (n * 6 - 1) * sc : 0; }
    void text(float x, float y, float sc, uint32_t col, const char* s, int align = 0) {
        if (align == 1) x -= text_w(s, sc) * 0.5f; else if (align == 2) x -= text_w(s, sc);
        int ca = col >> 24;
        for (; *s; s++, x += 6 * sc) {
            const uint8_t* g = glyph(*s);
            int xa = std::max(0, int(x) - 1), xb = std::min(W, int(x + 5 * sc) + 2), ya = std::max(0, int(y) - 1), yb = std::min(H, int(y + 7 * sc) + 2);
            for (int yy = ya; yy < yb; yy++) for (int xx = xa; xx < xb; xx++) {
                float u = (xx + 0.5f - x) / sc - 0.5f, v = (yy + 0.5f - y) / sc - 0.5f;
                int iu = int(floorf(u)), iv = int(floorf(v));
                float fu = u - iu, fv = v - iv, val = 0;
                for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) {
                    int cu = iu + i, cv = iv + j;
                    float b = (cu >= 0 && cu < 5 && cv >= 0 && cv < 7 && (g[cv] & (1 << (4 - cu)))) ? 1.0f : 0.0f;
                    val += b * (i ? fu : 1 - fu) * (j ? fv : 1 - fv);
                }
                float cov = clampf((val - 0.40f) * sc + 0.5f, 0.0f, 1.0f);
                if (cov > 0) { uint32_t* p = fb + yy * W + xx; *p = blendpx(*p, col, int(cov * ca)); }
            }
        }
    }
    void text_sh(float x, float y, float sc, uint32_t col, const char* s, int align = 0) {
        text(x + sc * 0.6f, y + sc * 0.6f, sc, argb(150, 0, 0, 0), s, align);
        text(x, y, sc, col, s, align);
    }
};

// ------------------------------------------------------------------ button icons / names
static const char* btn_name(uint32_t b) {
    switch (b) {
        case B_L1: return "L1"; case B_R1: return "R1"; case B_L2: return "L2"; case B_R2: return "R2";
        case B_L3: return "L3"; case B_R3: return "R3"; case B_OPTIONS: return "OPTIONS"; case B_TOUCHPAD: return "TOUCHPAD";
        case B_UP: return "D-UP"; case B_DOWN: return "D-DOWN"; case B_LEFT: return "D-LEFT"; case B_RIGHT: return "D-RIGHT";
        case B_CROSS: return "CROSS"; case B_CIRCLE: return "CIRCLE"; case B_SQUARE: return "SQUARE"; case B_TRIANGLE: return "TRIANGLE";
    }
    return "?";
}
static float btn_icon(Cv& c, float cx, float cy, float sz, uint32_t b) {
    float S = sz;
    if (b == B_CROSS || b == B_CIRCLE || b == B_SQUARE || b == B_TRIANGLE) {
        c.circle(cx, cy, S * 0.5f, rgb(24, 28, 40));
        c.ring(cx, cy, S * 0.46f, S * 0.5f, 0, 2 * PI_F, argb(255, 90, 100, 130));
        float k = S * 0.2f, t = std::max(2.0f, S * 0.07f);
        if (b == B_CROSS) { uint32_t col = rgb(120, 175, 255); c.line(cx - k, cy - k, cx + k, cy + k, t, col); c.line(cx + k, cy - k, cx - k, cy + k, t, col); }
        else if (b == B_CIRCLE) c.ring(cx, cy, k * 1.15f - t * 0.5f, k * 1.15f + t * 0.5f, 0, 2 * PI_F, rgb(255, 100, 110));
        else if (b == B_SQUARE) { uint32_t col = rgb(245, 140, 205); c.line(cx - k, cy - k, cx + k, cy - k, t, col); c.line(cx + k, cy - k, cx + k, cy + k, t, col); c.line(cx + k, cy + k, cx - k, cy + k, t, col); c.line(cx - k, cy + k, cx - k, cy - k, t, col); }
        else { uint32_t col = rgb(100, 225, 175); c.line(cx, cy - k * 1.1f, cx + k * 1.1f, cy + k * 0.8f, t, col); c.line(cx + k * 1.1f, cy + k * 0.8f, cx - k * 1.1f, cy + k * 0.8f, t, col); c.line(cx - k * 1.1f, cy + k * 0.8f, cx, cy - k * 1.1f, t, col); }
    } else {
        const char* n = btn_name(b);
        float sc = S * 0.075f, w = c.text_w(n, sc) + S * 0.5f;
        c.rrect(cx - w * 0.5f, cy - S * 0.38f, w, S * 0.76f, S * 0.3f, rgb(40, 46, 66), rgb(26, 30, 44));
        c.text(cx, cy - 3.5f * sc, sc, rgb(235, 240, 255), n, 1);
        return w;
    }
    return S;
}


// ------------------------------------------------------------------ cars + paints (same data as the phone game's catalog)
struct CarInfo { const char* name; int rarity; float power, top, grip, armor; };
static const CarInfo CAR_INFO[SPRITE_CAR_COUNT] = {
    {"RALLY HATCH RX", 0, 1.00f, 165, 1.00f, 1.00f}, {"DUNE BUGGY", 1, 0.95f, 150, 1.25f, 0.90f}, {"MUSCLE GT", 1, 1.20f, 180, 0.88f, 1.00f},
    {"COSWORTH RS", 1, 1.15f, 178, 1.00f, 1.00f},    {"TRAIL 4X4", 2, 1.05f, 160, 1.10f, 1.35f}, {"CHARGER R/T", 2, 1.30f, 186, 0.95f, 1.15f},
    {"APEX COUPE", 2, 1.25f, 195, 1.12f, 0.95f},     {"M4 COMPETITION", 2, 1.35f, 205, 1.15f, 1.00f}, {"GT3 RACER", 3, 1.40f, 210, 1.30f, 1.00f},
    {"PHANTOM HYPER", 3, 1.50f, 225, 1.20f, 1.05f}};
struct PaintInfo { const char* name; uint8_t r, g, b; };
static const int PAINT_COUNT = 15;
static const PaintInfo PAINTS[PAINT_COUNT] = {
    {"RACING RED", 199, 20, 20}, {"OCEAN BLUE", 26, 77, 217}, {"SNOW WHITE", 237, 240, 245}, {"NIGHT BLACK", 15, 15, 20}, {"SUNNY YELLOW", 250, 199, 26},
    {"FOREST GREEN", 26, 128, 51}, {"LAVA ORANGE", 255, 102, 13}, {"VIOLET PEARL", 128, 38, 204}, {"ESTORIL BLUE", 26, 64, 191}, {"PAPAYA ORANGE", 255, 115, 5},
    {"GRANITE GREY", 92, 97, 107}, {"STEALTH MATTE", 56, 61, 69}, {"CANDY PINK", 255, 51, 140}, {"LIQUID GOLD", 255, 191, 38}, {"MIRROR CHROME", 217, 224, 242}};
static const uint32_t RARITY_COL[4] = {0xFFB3BFD1u, 0xFF4099FFu, 0xFFB359F2u, 0xFFFFB31Au};
static const char* RARITY_NAME[4] = {"COMMON", "RARE", "EPIC", "LEGENDARY"};

// draws a car sprite (bottom-centre anchored), body area tinted with the paint, optional fog blend
static void draw_sprite(Cv& c, int model, float cx, float by, float w, int paint, float fog, uint32_t fogc, float glow) {
    const SpriteCar& sp = SPRITE_CARS[model];
    if (w < 2) return;
    float dh = w * sp.h / sp.w, x0 = cx - w * 0.5f, y0 = by - dh;
    int xa = std::max(0, int(x0)), xb = std::min(c.W, int(x0 + w) + 1), ya = std::max(0, int(y0)), yb = std::min(c.H, int(by) + 1);
    if (xb <= xa || yb <= ya) return;
    const PaintInfo& pi = PAINTS[paint];
    float pr = pi.r / 255.0f * 1.06f, pg = pi.g / 255.0f * 1.06f, pb = pi.b / 255.0f * 1.06f;
    int fr = (fogc >> 16) & 255, fg = (fogc >> 8) & 255, fb2 = fogc & 255;
    float sx = sp.w / w, sy = sp.h / dh;
    for (int y = ya; y < yb; y++) {
        float v = (y + 0.5f - y0) * sy - 0.5f;
        int iv = int(floorf(v)); float fv = v - iv;
        int v0 = std::max(0, std::min(sp.h - 1, iv)), v1 = std::max(0, std::min(sp.h - 1, iv + 1));
        uint32_t* row = c.fb + y * c.W;
        for (int x = xa; x < xb; x++) {
            float u = (x + 0.5f - x0) * sx - 0.5f;
            int iu = int(floorf(u)); float fu = u - iu;
            int u0 = std::max(0, std::min(sp.w - 1, iu)), u1 = std::max(0, std::min(sp.w - 1, iu + 1));
            const uint8_t* p00 = sp.data + (v0 * sp.w + u0) * 5; const uint8_t* p10 = sp.data + (v0 * sp.w + u1) * 5;
            const uint8_t* p01 = sp.data + (v1 * sp.w + u0) * 5; const uint8_t* p11 = sp.data + (v1 * sp.w + u1) * 5;
            float w00 = (1 - fu) * (1 - fv), w10 = fu * (1 - fv), w01 = (1 - fu) * fv, w11 = fu * fv;
            float a = (p00[3] * w00 + p10[3] * w10 + p01[3] * w01 + p11[3] * w11);
            if (a < 3) continue;
            float inv = 1.0f / 255.0f, aw0 = p00[3] * w00, aw1 = p10[3] * w10, aw2 = p01[3] * w01, aw3 = p11[3] * w11, asum = aw0 + aw1 + aw2 + aw3;
            float m = (p00[4] * aw0 + p10[4] * aw1 + p01[4] * aw2 + p11[4] * aw3) / asum * inv;
            float r = (p00[0] * aw0 + p10[0] * aw1 + p01[0] * aw2 + p11[0] * aw3) / asum * inv;
            float g = (p00[1] * aw0 + p10[1] * aw1 + p01[1] * aw2 + p11[1] * aw3) / asum * inv;
            float b = (p00[2] * aw0 + p10[2] * aw1 + p01[2] * aw2 + p11[2] * aw3) / asum * inv;
            r *= 1.0f + m * (pr - 1.0f); g *= 1.0f + m * (pg - 1.0f); b *= 1.0f + m * (pb - 1.0f);
            if (glow > 0 && r > 0.8f && g < 0.45f && b < 0.4f && m < 0.3f) { r = 1.0f; g = 0.15f + 0.1f * glow; b = 0.1f; }   // brighter tail lights
            int R = int(clampf(r, 0, 1) * 255), G = int(clampf(g, 0, 1) * 255), B = int(clampf(b, 0, 1) * 255);
            if (fog > 0) { R = int(R + (fr - R) * fog); G = int(G + (fg - G) * fog); B = int(B + (fb2 - B) * fog); }
            row[x] = blendpx(row[x], argb(255, R, G, B), int(a));
        }
    }
}

// ------------------------------------------------------------------ game
static const uint32_t DEFAULT_BIND[A_COUNT] = {B_R2, B_L2, B_SQUARE, B_TRIANGLE, B_OPTIONS, B_R3};
static const char* ACTION_NAME[A_COUNT] = {"ACCELERATE", "BRAKE", "HANDBRAKE", "RESET CAR", "PAUSE", "CAMERA VIEW"};

Game::Game() {
    struct P { int len; float c; };
    const P plan[] = {{40, 0}, {25, 0.9f}, {20, 0}, {30, -1.1f}, {25, 0}, {22, 3.4f}, {18, 0}, {30, 0.6f}, {26, -0.8f}, {14, 0},
                      {20, -3.6f}, {28, 0}, {24, 1.2f}, {20, 0}, {26, 3.2f}, {30, 0}, {25, -1.0f}, {35, 0}};
    for (auto& p : plan) for (int i = 0; i < p.len * 6; i++) {
        float t = float(i) / (p.len * 6);
        float ease = std::min(1.0f, std::min(t, 1.0f - t) * 4.0f);
        curves.push_back(p.c * (p.c == 0 ? 0 : ease));
    }
    track_len = curves.size() * SEG;
    for (int i = 0; i < A_COUNT; i++) bind[i] = DEFAULT_BIND[i];
    // minimap: integrate heading, force the loop closed
    int N = int(curves.size());
    std::vector<float> hd(N + 1, 0.0f);
    for (int i = 0; i < N; i++) hd[i + 1] = hd[i] + curves[i] * 0.012f;
    float tot = hd[N];
    float k = fabsf(tot) > 0.1f ? (2 * PI_F * (tot > 0 ? 1.0f : -1.0f)) / tot : 1.0f;
    std::vector<float> xs(N + 1, 0.0f), ys(N + 1, 0.0f);
    for (int i = 0; i < N; i++) { float h = hd[i] * k; xs[i + 1] = xs[i] + cosf(h); ys[i + 1] = ys[i] + sinf(h); }
    float ex = xs[N], ey = ys[N];
    float mnx = 1e9f, mxx = -1e9f, mny = 1e9f, mxy = -1e9f;
    for (int i = 0; i <= N; i++) { xs[i] -= ex * i / N; ys[i] -= ey * i / N; mnx = std::min(mnx, xs[i]); mxx = std::max(mxx, xs[i]); mny = std::min(mny, ys[i]); mxy = std::max(mxy, ys[i]); }
    float sp = std::max(mxx - mnx, mxy - mny);
    if (sp < 1e-3f) sp = 1;
    for (int i = 0; i < N; i += 4) { mm_x.push_back((xs[i] - mnx) / sp + (1 - (mxx - mnx) / sp) * 0.5f); mm_y.push_back((ys[i] - mny) / sp + (1 - (mxy - mny) / sp) * 0.5f); }
    reset_race();
}

void Game::apply_car() {
    const CarInfo& ci = CAR_INFO[car_sel];
    topf = ci.top / 205.0f; accf = ci.power; gripf = ci.grip; armf = ci.armor;
}

void Game::reset_race() {
    apply_car();
    z = 0; px = 0; speed = 0; race_t = 0; lap_ = 1; place_ = 6; lap_z0 = 0; shake = 0; braking = 0;
    rivals.clear();
    const int models[5] = {3, 6, 2, 7, 0}, paints[5] = {1, 4, 5, 6, 12};
    for (int i = 0; i < 5; i++) rivals.push_back({SEG * (12 + i * 9), (i % 2 ? 0.35f : -0.35f), MAX_SPEED * (0.62f + 0.05f * i), models[i], paints[i]});
}

int Game::render_w() const { static const int w[3] = {960, 1280, 1920}; return w[quality]; }
int Game::render_h() const { static const int h[3] = {540, 720, 1080}; return h[quality]; }

float Game::curve_at(float zz) const {
    if (curves.empty()) return 0;
    zz = fmodf(zz, track_len);
    if (zz < 0) zz += track_len;
    return curves[int(zz / SEG) % curves.size()];
}
float Game::upcoming_curve(float ahead) const { return curve_at(z + ahead); }

float Game::axis(int a, const PadState& pad, uint32_t dn) const {
    uint32_t b = bind[a];
    if (b == B_R2) return std::max(pad.r2, (dn & B_R2) ? 1.0f : 0.0f);
    if (b == B_L2) return std::max(pad.l2, (dn & B_L2) ? 1.0f : 0.0f);
    return (dn & b) ? 1.0f : 0.0f;
}

std::string Game::save_blob() const {
    char buf[240];
    snprintf(buf, sizeof buf, "v3 %u %u %u %u %u %u q%d f%d l%d c%d k%d p%d", bind[0], bind[1], bind[2], bind[3], bind[4], bind[5], quality, show_fps ? 1 : 0, laps_total, fpv ? 1 : 0, car_sel, paint_sel);
    return buf;
}
void Game::load_blob(const std::string& s) {
    unsigned b[A_COUNT]; int q, f, l, cm, ks, ps;
    if (sscanf(s.c_str(), "v3 %u %u %u %u %u %u q%d f%d l%d c%d k%d p%d", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &q, &f, &l, &cm, &ks, &ps) != 12) return;
    fpv = cm != 0;
    car_sel = ks < 0 ? 0 : (ks >= SPRITE_CAR_COUNT ? SPRITE_CAR_COUNT - 1 : ks);
    paint_sel = ps < 0 ? 0 : (ps >= PAINT_COUNT ? PAINT_COUNT - 1 : ps);
    apply_car();
    for (int i = 0; i < A_COUNT; i++) if (b[i] && (b[i] & (b[i] - 1)) == 0 && b[i] <= B_RIGHT) bind[i] = b[i];
    quality = q < 0 ? 0 : (q > 2 ? 2 : q); show_fps = f != 0; laps_total = l < 1 ? 1 : (l > 9 ? 9 : l);
}

void Game::update(float dt, const PadState& pad) {
    fps = lerpf(fps, 1.0f / std::max(dt, 0.0005f), 0.05f);
    uint32_t dn = pad.down;
    if (pad.l2 > 0.5f) dn |= B_L2;
    if (pad.r2 > 0.5f) dn |= B_R2;
    uint32_t nv = dn;
    if (pad.lx < -0.6f) nv |= B_LEFT;
    if (pad.lx > 0.6f) nv |= B_RIGHT;
    if (pad.ly < -0.6f) nv |= B_UP;
    if (pad.ly > 0.6f) nv |= B_DOWN;
    uint32_t ed_raw = dn & ~prev_raw, ed = nv & ~prev_nav;
    prev_raw = dn; prev_nav = nv;
    state_t += dt;
    demo_z += 4500.0f * dt;
    auto wrap = [](int v, int n) { return (v % n + n) % n; };

    if (state == MENU) {
        const int N = 7;
        if (ed & B_UP) menu_sel = wrap(menu_sel - 1, N);
        if (ed & B_DOWN) menu_sel = wrap(menu_sel + 1, N);
        int dir = ((ed & B_RIGHT) ? 1 : 0) - ((ed & B_LEFT) ? 1 : 0);
        bool ok = (ed & B_CROSS) != 0;
        if (menu_sel == 0 && ok) { reset_race(); state = COUNTDOWN; state_t = 0; }
        else if (menu_sel == 1 && ok) { state = GARAGE; garage_row = 0; state_t = 0; }
        else if (menu_sel == 2 && ok) { ctl_return = MENU; state = CONTROLS; ctl_sel = 0; capturing = false; }
        else if (menu_sel == 3 && (dir || ok)) { quality = wrap(quality + (dir ? dir : 1), 3); dirty = true; }
        else if (menu_sel == 4 && (dir || ok)) { show_fps = !show_fps; dirty = true; }
        else if (menu_sel == 5 && (dir || ok)) { laps_total = std::max(1, std::min(9, laps_total + (dir ? dir : 1))); if (laps_total == 9 && !dir) laps_total = 1; dirty = true; }
        else if (menu_sel == 6 && (dir || ok)) { fpv = !fpv; dirty = true; }
        return;
    }
    if (state == GARAGE) {
        if (ed & (B_UP | B_DOWN)) garage_row = 1 - garage_row;
        int dir = ((ed & B_RIGHT) ? 1 : 0) - ((ed & B_LEFT) ? 1 : 0);
        if (dir) {
            if (garage_row == 0) car_sel = wrap(car_sel + dir, SPRITE_CAR_COUNT); else paint_sel = wrap(paint_sel + dir, PAINT_COUNT);
            apply_car(); dirty = true;
        }
        if (ed & (B_CIRCLE | B_CROSS)) { state = MENU; state_t = 0; }
        return;
    }
    if (state == CONTROLS) {
        if (capturing) {
            capture_t += dt;
            if (capture_t > 6.0f) capturing = false;
            else if (ed_raw & B_ALL) {
                uint32_t nb = 1;
                while (!(ed_raw & nb)) nb <<= 1;
                int a = ctl_sel;
                uint32_t old = bind[a];
                for (int i = 0; i < A_COUNT; i++) if (i != a && bind[i] == nb) bind[i] = old;     // swap on conflict
                bind[a] = nb;
                capturing = false; dirty = true;
            }
            return;
        }
        const int N = A_COUNT + 2;
        if (ed & B_UP) ctl_sel = wrap(ctl_sel - 1, N);
        if (ed & B_DOWN) ctl_sel = wrap(ctl_sel + 1, N);
        if (ed & B_CIRCLE) { state = ctl_return; state_t = 0; return; }
        if (ed & B_CROSS) {
            if (ctl_sel < A_COUNT) { capturing = true; capture_t = 0; }
            else if (ctl_sel == A_COUNT) { for (int i = 0; i < A_COUNT; i++) bind[i] = DEFAULT_BIND[i]; dirty = true; }
            else { state = ctl_return; state_t = 0; }
        }
        return;
    }
    if (state == COUNTDOWN) { if (state_t > 3.0f) { state = RACE; state_t = 0; } return; }
    if (state == PAUSED) {
        if (ed & B_UP) menu_sel = wrap(menu_sel - 1, 4);
        if (ed & B_DOWN) menu_sel = wrap(menu_sel + 1, 4);
        if (ed_raw & bind[A_PAUSE] && !(ed & B_CROSS)) { state = RACE; return; }
        if (ed & B_CROSS) {
            if (menu_sel == 0) state = RACE;
            else if (menu_sel == 1) { reset_race(); state = COUNTDOWN; state_t = 0; }
            else if (menu_sel == 2) { ctl_return = PAUSED; state = CONTROLS; ctl_sel = 0; capturing = false; }
            else { state = MENU; menu_sel = 0; state_t = 0; }
        }
        return;
    }
    if (state == FINISH) {
        if (state_t > 1.5f && (ed & B_CROSS)) { reset_race(); state = COUNTDOWN; state_t = 0; }
        else if (state_t > 1.5f && (ed & B_CIRCLE)) { state = MENU; menu_sel = 0; state_t = 0; }
        return;
    }
    // ---------------- RACE
    if (ed_raw & bind[A_PAUSE]) { state = PAUSED; menu_sel = 0; return; }
    if (ed_raw & bind[A_RESET]) { px = 0; speed *= 0.5f; }
    if (ed_raw & bind[A_CAMERA]) { fpv = !fpv; dirty = true; }
    race_t += dt;
    float gas = axis(A_GAS, pad, dn), brake = axis(A_BRAKE, pad, dn);
    bool hb = (dn & bind[A_HANDBRAKE]) != 0;
    float steer = fabsf(pad.lx) > 0.1f ? pad.lx : 0.0f;
    if (dn & B_LEFT) steer = -1.0f;
    if (dn & B_RIGHT) steer = 1.0f;
    braking = brake > 0.1f || hb ? 1.0f : 0.0f;
    steer_vis += (steer - steer_vis) * std::min(1.0f, dt * 9.0f);
    float accel = gas * 7000.0f * accf - brake * 16000.0f - (gas < 0.05f ? 2500.0f : 0.0f) - (hb ? 9000.0f : 0.0f);
    bool off = fabsf(px) > 1.05f;
    if (off) accel -= 9000.0f;
    speed += accel * dt * (1.0f - speed / (MAX_SPEED * 1.15f));
    speed = clampf(speed, 0.0f, MAX_SPEED * topf);
    if (off && speed > 5500.0f) speed = std::max(5500.0f, speed - 14000.0f * dt);
    float sp = speed / MAX_SPEED;
    px += steer * dt * 2.2f * (0.35f + sp) * (hb ? 1.5f : 1.0f) * powf(gripf, 0.7f);
    px -= curve_at(z) * sp * sp * dt * 0.95f / gripf;
    px = clampf(px, -2.2f, 2.2f);
    z += speed * dt;
    if (z - lap_z0 >= track_len) { lap_z0 += track_len; lap_++; if (lap_ > laps_total) { state = FINISH; state_t = 0; lap_ = laps_total; } }
    int ahead = 0;
    for (auto& r : rivals) {
        float cv = curve_at(r.z + 400);
        r.speed = clampf(r.speed + (MAX_SPEED * (0.66f - fabsf(cv) * 0.06f) - r.speed) * dt * 0.8f, 5000.0f, MAX_SPEED * 0.95f);
        r.z += r.speed * dt;
        float rel = fmodf(r.z - z + track_len * 100.0f, track_len);
        if (rel > track_len / 2) rel -= track_len;
        if (rel > 0) ahead++;
        if (fabsf(rel) < 150.0f && fabsf(r.x - px) < 0.32f) {
            speed *= (1.0f - 3.0f * dt * armf);
            px += (px >= r.x ? 1.0f : -1.0f) * dt * 1.5f;
            shake = 0.25f;
        }
    }
    place_ = ahead + 1;
    shake = std::max(0.0f, shake - dt);
}

// ------------------------------------------------------------------ rendering
void Game::scene(Cv& c, float cz, float cpx, bool race) {
    const int W = c.W, H = c.H;
    const float S = c.S;
    const int HZ = int(H * 0.42f);
    const int HE = H + H / 3;
    if ((int)xoff.size() < HE) { xoff.assign(HE, 0.0f); rwid.assign(HE, 0.0f); }
    float bend = curve_at(cz + 1500);
    // sky
    const uint32_t top = rgb(18, 34, 92), mid = rgb(196, 88, 104), low = rgb(255, 196, 120);
    for (int y = 0; y < HZ; y++) {
        float t = float(y) / HZ;
        c.rect(0, y, W, 1, t < 0.6f ? mixc(top, mid, t / 0.6f) : mixc(mid, low, (t - 0.6f) / 0.4f));
    }
    float sunx = W * 0.58f - bend * 26.0f * S, suny = HZ - 0.13f * H;
    for (int i = 0; i < 6; i++) c.circle(sunx, suny, (0.20f - i * 0.028f) * H * 0.5f * 1.9f, argb(26, 255, 220, 150));
    c.circle(sunx, suny, 0.055f * H, rgb(255, 238, 190));
    // mountains (two parallax layers)
    std::vector<int> hg(W);
    for (int layer = 0; layer < 2; layer++) {
        float amp = (layer ? 62.0f : 96.0f) * S;
        uint32_t ctop = layer ? rgb(86, 56, 92) : rgb(120, 76, 104), cbot = layer ? rgb(52, 36, 66) : rgb(84, 56, 84);
        int mx = 0;
        for (int x = 0; x < W; x++) {
            float u = x / S * (layer ? 0.0075f : 0.0045f) + cz * 0.00002f * (bend + 0.2f) * (1 + layer) + layer * 7.0f;
            hg[x] = int(amp * (0.45f + 0.30f * sinf(u * 1.7f) + 0.25f * sinf(u * 4.3f + 1.3f)));
            mx = std::max(mx, hg[x]);
        }
        for (int y = HZ - mx; y < HZ; y++) {
            int hh = HZ - y;
            uint32_t* p = c.fb + y * W;
            uint32_t col = mixc(cbot, ctop, float(hh) / std::max(1, mx));
            for (int x = 0; x < W; x++) if (hg[x] >= hh) p[x] = col;
        }
    }
    uint32_t fogc = rgb(182, 128, 124);
    c.vgrad(0, HZ - int(34 * S), W, int(34 * S), argb(0, 182, 128, 124), argb(210, 182, 128, 124));
    // road
    const float K = 90000.0f * S;
    float dx = 0, xo = 0;
    for (int y = HE - 1; y > HZ; y--) {
        float t = float(y - HZ) / (H - HZ);
        rwid[y] = 1.15f * W * 0.5f * t * 0.92f;
        if (y >= H) { xoff[y] = 0; continue; }
    }
    for (int y = H - 1; y > HZ; y--) {
        float d = K / float(y - HZ);
        dx += curve_at(cz + d) * 0.0075f / S;
        xo += dx;
        xoff[y] = xo;
        float w = rwid[y], cxr = W * 0.5f - cpx * w + xo;
        float dist = cz + d;
        bool stripe = (int(dist / 800.0f) & 1) != 0;
        float fog = std::min(1.0f, d / 42000.0f);
        uint32_t grass = mixc(stripe ? rgb(58, 150, 70) : rgb(46, 132, 60), fogc, fog);
        uint32_t road = mixc(stripe ? rgb(92, 94, 104) : rgb(82, 84, 94), fogc, fog);
        uint32_t rumb = mixc(stripe ? rgb(238, 238, 238) : rgb(222, 40, 44), fogc, fog);
        uint32_t edge = mixc(rgb(236, 236, 240), fogc, fog);
        int l = int(cxr - w), r = int(cxr + w), rb = std::max(1, int(w * 0.09f));
        uint32_t* row = c.fb + y * W;
        int a = std::max(0, std::min(W, l - rb)), b = std::max(0, std::min(W, r + rb));
        std::fill(row, row + a, grass);
        std::fill(row + b, row + W, grass);
        for (int x = a; x < std::min(W, std::max(a, l)); x++) row[x] = rumb;
        for (int x = std::max(a, std::min(W, r)); x < b; x++) row[x] = rumb;
        int rl = std::max(0, l), rr = std::min(W, r);
        if (rr > rl) std::fill(row + rl, row + rr, road);
        int ew = std::max(1, int(w * 0.022f));                                   // solid edge lines
        for (int x = std::max(0, l + ew); x < std::min(W, l + ew * 3); x++) row[x] = edge;
        for (int x = std::max(0, r - ew * 3); x < std::min(W, r - ew); x++) row[x] = edge;
        if ((int(dist / 400.0f) & 1) == 0) {                                      // dashed lane lines
            int lw = std::max(1, int(w * 0.015f));
            for (int k = -1; k <= 1; k += 2) { int lx = int(cxr + k * w * 0.34f); for (int x = std::max(0, lx - lw); x < std::min(W, lx + lw); x++) row[x] = mixc(rgb(240, 240, 240), fogc, fog); }
        }
    }
    // roadside objects + rivals, far to near
    struct Item { float d; int kind; int idx; float side; };
    std::vector<Item> items;
    const float step = 650.0f, dmin = K / float(H - HZ) * 0.9f, dmax = 40000.0f;
    long n0 = long((cz + dmin) / step) + 1, n1 = long((cz + dmax) / step);
    for (long n = n0; n <= n1; n++) {
        float dd = n * step - cz;
        if (dd < dmin) continue;
        float cv = curve_at(n * step);
        for (int sd = -1; sd <= 1; sd += 2) {
            uint32_t h = hash32(uint32_t(n * 2 + (sd > 0)));
            if (fabsf(cv) > 2.0f && (n & 1) && sd == (cv > 0 ? -1 : 1)) { items.push_back({dd, 2, int(n), float(sd)}); continue; }   // chevron boards on hairpin outsides
            if ((h & 7) < 5) items.push_back({dd, (h & 8) ? 0 : 1, int(h >> 4 & 0xFFFF), float(sd)});
        }
    }
    if (race) for (int i = 0; i < (int)rivals.size(); i++) {
        float rel = fmodf(rivals[i].z - cz + track_len * 100.0f, track_len);
        if (rel > track_len / 2) rel -= track_len;
        if (rel > 100 && rel < 40000) items.push_back({rel, 3, i, 0});
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.d > b.d; });
    for (const Item& it : items) {
        float yf = HZ + K / it.d;
        int y = int(yf);
        if (y <= HZ + 1 || y >= HE) continue;
        float w = rwid[y], cxr = W * 0.5f - cpx * w + (y < H ? xoff[y] : 0.0f);
        float fog = std::min(1.0f, it.d / 42000.0f);
        if (it.kind == 3) {
            const Rival& r = rivals[it.idx];
            draw_sprite(c, r.model, cxr + r.x * w, float(y), w * 0.46f, r.paint, fog * 0.75f, fogc, 0.0f);
            continue;
        }
        float jit = hf(uint32_t(it.idx) * 7u + 3u);
        float ox = cxr + it.side * w * (1.28f + jit * 0.9f + (it.kind == 2 ? 0.0f : 0.0f));
        float th = w * (it.kind == 0 ? 0.62f : 0.30f) * (0.8f + 0.5f * jit);
        if (it.kind == 0) {                                                          // pine tree
            uint32_t dk = mixc(rgb(20, 92, 52), fogc, fog), lt = mixc(rgb(36, 132, 70), fogc, fog);
            c.rect(int(ox - th * 0.03f), int(y - th * 0.22f), std::max(1, int(th * 0.06f)), int(th * 0.22f) + 1, mixc(rgb(80, 52, 34), fogc, fog));
            c.tri(ox, y - th, y - th * 0.45f, th * 0.20f, dk, lt);
            c.tri(ox, y - th * 0.78f, y - th * 0.22f, th * 0.28f, dk, lt);
        } else if (it.kind == 1) {                                                   // bush / rock
            c.rrect(ox - th * 0.45f, y - th * 0.55f, th * 0.9f, th * 0.55f, th * 0.25f, mixc(rgb(60, 140, 66), fogc, fog), mixc(rgb(34, 100, 50), fogc, fog));
        } else {                                                                     // hairpin chevron board
            float bh = w * 0.34f, bw = w * 0.30f;
            c.rect(int(ox - bw * 0.04f), int(y - bh), std::max(1, int(bw * 0.08f)), int(bh), mixc(rgb(90, 90, 100), fogc, fog));
            c.rrect(ox - bw * 0.5f, y - bh * 1.7f, bw, bh * 0.8f, bw * 0.06f, mixc(rgb(236, 52, 48), fogc, fog), mixc(rgb(190, 30, 36), fogc, fog));
            const char* ch = (it.side > 0) == (curve_at(cz + it.d) > 0) ? ">" : "<";
            if (bw > 14 * S) c.text(ox, y - bh * 1.62f, bw * 0.095f, rgb(255, 255, 255), ch, 1);
        }
    }
    if (!race) return;
    if (fpv) return;
    // player car
    float sx = 0, sy = 0;
    if (shake > 0) { sx = sinf(state_t * 90) * 8 * shake * 4 * S / 4; sy = cosf(state_t * 77) * 5 * shake * S; }
    c.rrect(W * 0.5f + sx - 230 * S, H - 62 * S + sy, 460 * S, 34 * S, 17 * S, argb(120, 0, 0, 0));
    draw_sprite(c, car_sel, W * 0.5f + sx, H - 40 * S + sy, 400 * S, paint_sel, 0.0f, 0, braking);
}

void Game::cockpit(Cv& c) {
    const int W = c.W, H = c.H;
    const float S = c.S;
    float bob = sinf(race_t * 17.0f) * (speed / MAX_SPEED) * 2.0f * S + (shake > 0 ? sinf(state_t * 90) * shake * 10.0f * S : 0.0f);
    uint32_t body = rgb(206, 28, 44);
    // roof + A pillars
    int py = int(H * 0.66f);
    for (int y = 0; y < py; y++) {
        float t = float(y) / py;
        int inner = int(W * (0.20f - 0.155f * t));
        uint32_t col = mixc(rgb(8, 9, 13), rgb(22, 25, 34), t);
        c.rect(0, y, inner, 1, col); c.rect(W - inner, y, inner, 1, col);
        c.rect(inner - int(3 * S), y, int(3 * S), 1, rgb(46, 50, 64)); c.rect(W - inner, y, int(3 * S), 1, rgb(46, 50, 64));
    }
    c.vgrad(0, 0, W, int(H * 0.085f), rgb(8, 9, 13), argb(0, 8, 9, 13));
    // mirrors
    c.rrect(W * 0.045f, H * 0.40f, W * 0.105f, H * 0.075f, 14 * S, mixc(body, rgb(255, 255, 255), 0.2f), mixc(body, rgb(0, 0, 0), 0.4f));
    c.rrect(W * 0.052f, H * 0.408f, W * 0.090f, H * 0.060f, 10 * S, rgb(32, 40, 56), rgb(16, 20, 30));
    c.rrect(W * 0.85f, H * 0.40f, W * 0.105f, H * 0.075f, 14 * S, mixc(body, rgb(255, 255, 255), 0.2f), mixc(body, rgb(0, 0, 0), 0.4f));
    c.rrect(W * 0.858f, H * 0.408f, W * 0.090f, H * 0.060f, 10 * S, rgb(32, 40, 56), rgb(16, 20, 30));
    c.rrect(W * 0.5f - W * 0.09f, H * 0.012f, W * 0.18f, H * 0.065f, 12 * S, rgb(20, 22, 30), rgb(10, 11, 16));
    c.rrect(W * 0.5f - W * 0.082f, H * 0.020f, W * 0.164f, H * 0.049f, 9 * S, rgb(36, 46, 66), rgb(18, 24, 36));
    // hood (red, sloping to the horizon) then dashboard
    int hy0 = int(H * 0.715f + bob), hy1 = int(H * 0.84f + bob);
    for (int y = hy0; y < hy1; y++) {
        float t = float(y - hy0) / std::max(1, hy1 - hy0);
        int half = int(W * (0.17f + 0.29f * t));
        uint32_t col = mixc(mixc(body, rgb(255, 255, 255), 0.30f), mixc(body, rgb(0, 0, 0), 0.25f), t);
        c.rect(W / 2 - half, y, half * 2, 1, col);
        c.rect(W / 2 - int(W * 0.012f * (0.4f + t)), y, int(W * 0.024f * (0.4f + t)), 1, argb(60, 255, 255, 255));
    }
    c.vgrad(0, int(H * 0.835f + bob), W, int(H * 0.17f), rgb(30, 33, 44), rgb(7, 8, 11));
    c.rect(0, int(H * 0.835f + bob), W, int(2 * S), rgb(70, 76, 96));
    // dash cluster + steering wheel
    float cx = W * 0.5f, cy = H * 0.99f + bob, R = H * 0.225f;
    float rot = steer_vis * 1.25f;                                           // radians
    c.ring(cx, cy, R - 20 * S, R, 0, 2 * PI_F, rgb(24, 26, 34));
    c.ring(cx, cy, R - 4 * S, R, 0, 2 * PI_F, rgb(58, 62, 78));
    c.ring(cx, cy, R - 20 * S, R - 16 * S, 0, 2 * PI_F, rgb(10, 11, 15));
    float m0 = -PI_F * 0.5f + rot;                                             // red top marker
    c.ring(cx, cy, R - 20 * S, R, m0 - 0.10f + 2 * PI_F, m0 + 0.10f + 2 * PI_F, rgb(230, 40, 50));
    for (int k = 0; k < 3; k++) {                                              // spokes
        float a = rot + k * PI_F * 0.5f + PI_F;
        c.line(cx, cy, cx + cosf(a) * (R - 12 * S), cy + sinf(a) * (R - 12 * S), 22 * S, rgb(20, 22, 30));
    }
    c.circle(cx, cy, 44 * S, rgb(30, 33, 44));
    c.circle(cx, cy, 30 * S, rgb(210, 40, 50));
    for (int sd = -1; sd <= 1; sd += 2) {                                      // gloved hands at 10 and 2
        float a = -PI_F * 0.5f + rot + sd * 0.98f;
        float hx = cx + cosf(a) * (R - 10 * S), hy = cy + sinf(a) * (R - 10 * S);
        c.rrect(hx - 26 * S, hy - 20 * S, 52 * S, 40 * S, 16 * S, rgb(52, 56, 70), rgb(20, 22, 30));
        for (int f = 0; f < 4; f++) c.rrect(hx - 22 * S + f * 11.5f * S, hy - 24 * S, 9 * S, 16 * S, 4 * S, rgb(44, 48, 62));
        c.rrect(hx - 10 * S, hy - 28 * S, 22 * S, 8 * S, 4 * S, argb(80, 255, 255, 255));
    }
}

void Game::hud(Cv& c) {
    const int W = c.W, H = c.H;
    const float S = c.S;
    // readability gradients
    c.vgrad(0, 0, W, int(130 * S), argb(150, 4, 6, 12), argb(0, 4, 6, 12));
    c.vgrad(0, H - int(170 * S), W, int(170 * S), argb(0, 4, 6, 12), argb(170, 4, 6, 12));
    char b[64];
    // position
    c.rrect(26 * S, 22 * S, 190 * S, 84 * S, 10 * S, argb(170, 12, 16, 28), argb(170, 20, 24, 40));
    c.rrect(26 * S, 22 * S, 7 * S, 84 * S, 3 * S, rgb(255, 120, 30));
    snprintf(b, sizeof b, "%d", place_);
    c.text_sh(54 * S, 32 * S, 9.5f * S, rgb(255, 214, 70), b);
    c.text_sh(112 * S, 62 * S, 4.0f * S, rgb(220, 224, 236), "/ 6");
    c.text(112 * S, 38 * S, 2.6f * S, rgb(150, 160, 185), "POSITION");
    // lap
    snprintf(b, sizeof b, "LAP %d / %d", lap_, laps_total);
    c.rrect(W * 0.5f - 105 * S, 22 * S, 210 * S, 46 * S, 23 * S, argb(170, 12, 16, 28), argb(170, 20, 24, 40));
    c.text_sh(W * 0.5f, 33 * S, 3.6f * S, rgb(255, 255, 255), b, 1);
    // time
    snprintf(b, sizeof b, "%d:%02d.%d", int(race_t) / 60, int(race_t) % 60, int(race_t * 10) % 10);
    c.rrect(W - 236 * S, 22 * S, 210 * S, 84 * S, 10 * S, argb(170, 12, 16, 28), argb(170, 20, 24, 40));
    c.text(W - 222 * S, 32 * S, 2.6f * S, rgb(150, 160, 185), "TIME");
    c.text_sh(W - 222 * S, 56 * S, 5.4f * S, rgb(255, 255, 255), b);
    // hairpin warning
    float ahead = upcoming_curve(2600);
    if (fabsf(ahead) > 2.0f && (int(race_t * 4) & 1) == 0) {
        float cx = W * 0.5f, cy = 150 * S;
        c.rrect(cx - 150 * S, cy - 34 * S, 300 * S, 68 * S, 12 * S, argb(215, 200, 24, 30), argb(215, 140, 10, 20));
        c.text_sh(cx, cy - 14 * S, 4.6f * S, rgb(255, 255, 255), ahead > 0 ? "HAIRPIN >>" : "<< HAIRPIN", 1);
    }
    // speedometer
    float gx = W - 190 * S, gy = H - 170 * S, gr = 124 * S;
    float a0 = 0.75f * PI_F, a1 = 2.25f * PI_F, kmh = speed_kmh(), f = clampf(kmh / 240.0f, 0.0f, 1.0f);
    c.circle(gx, gy, gr + 14 * S, argb(170, 10, 12, 22));
    c.ring(gx, gy, gr - 16 * S, gr, a0, a1, argb(255, 36, 42, 62));
    if (f > 0.005f) {
        uint32_t col = f < 0.6f ? mixc(rgb(90, 190, 255), rgb(255, 170, 50), f / 0.6f) : mixc(rgb(255, 170, 50), rgb(255, 50, 50), (f - 0.6f) / 0.4f);
        c.ring(gx, gy, gr - 16 * S, gr, a0, a0 + (a1 - a0) * f, col);
    }
    for (int i = 0; i <= 12; i++) {
        float a = a0 + (a1 - a0) * i / 12.0f, ca = cosf(a), sa = sinf(a);
        c.line(gx + ca * (gr - 26 * S), gy + sa * (gr - 26 * S), gx + ca * (gr - 20 * S), gy + sa * (gr - 20 * S), (i % 2 ? 1.5f : 2.5f) * S, rgb(190, 198, 220));
    }
    snprintf(b, sizeof b, "%d", int(kmh));
    c.text_sh(gx, gy - 30 * S, 10.0f * S, rgb(255, 255, 255), b, 1);
    c.text(gx, gy + 40 * S, 3.0f * S, rgb(255, 150, 60), "KM/H", 1);
    int gear = std::max(1, std::min(6, 1 + int(kmh / 38.0f)));
    snprintf(b, sizeof b, "%d", gear);
    c.rrect(gx - 20 * S, gy + 66 * S, 40 * S, 34 * S, 8 * S, rgb(34, 40, 60));
    c.text(gx, gy + 71 * S, 3.6f * S, rgb(255, 255, 255), b, 1);
    // minimap
    float mx = 36 * S, my = H - 214 * S, ms = 170 * S;
    c.rrect(mx - 12 * S, my - 12 * S, ms + 24 * S, ms + 24 * S, 14 * S, argb(165, 10, 14, 26));
    int np = (int)mm_x.size();
    for (int i = 0; i < np; i++) { int j = (i + 1) % np; c.line(mx + mm_x[i] * ms, my + mm_y[i] * ms, mx + mm_x[j] * ms, my + mm_y[j] * ms, 4.5f * S, rgb(70, 78, 104)); }
    auto at = [&](float zz, float& x, float& y) { float u = fmodf(zz, track_len) / track_len; if (u < 0) u += 1; int i = int(u * np) % np; x = mx + mm_x[i] * ms; y = my + mm_y[i] * ms; };
    for (auto& r : rivals) { float x, y; at(r.z, x, y); c.circle(x, y, 4.5f * S, rgb(PAINTS[r.paint].r, PAINTS[r.paint].g, PAINTS[r.paint].b)); }
    { float x, y; at(z, x, y); c.circle(x, y, 8 * S, argb(130, 255, 255, 255)); c.circle(x, y, 5.5f * S, rgb(255, 60, 70)); }
    // speed streaks
    if (kmh > 120) {
        int cnt = 14;
        for (int i = 0; i < cnt; i++) {
            uint32_t h = hash32(uint32_t(i) + uint32_t(race_t * 22.0f) * 131u);
            float ang = (hf(h) * 2 - 1) * 1.1f + (hf(h + 1) < 0.5f ? 0.0f : PI_F);
            float r0 = (0.34f + 0.12f * hf(h + 2)) * W, r1 = r0 + (0.07f + 0.09f * hf(h + 3)) * W;
            float cx = W * 0.5f, cy = H * 0.5f;
            c.line(cx + cosf(ang) * r0, cy + sinf(ang) * r0 * 0.6f, cx + cosf(ang) * r1, cy + sinf(ang) * r1 * 0.6f, 1.6f * S, argb(int(clampf((kmh - 120) / 100.0f, 0, 1) * 70), 255, 255, 255));
        }
    }
}

static void prompt(Cv& c, float x, float y, uint32_t b, const char* label, float S) {
    Cv probe{nullptr, 0, 0, S};
    float w = (b >= B_UP || b == B_L1) ? probe.text_w(btn_name(b), 30 * S * 0.075f) + 15 * S : 30 * S;
    btn_icon(c, x + w * 0.5f, y + 14 * S, 30 * S, b);
    c.text_sh(x + w + 8 * S, y + 4 * S, 3.2f * S, rgb(220, 226, 240), label);
}

void Game::garage_screen(Cv& c) {
    const int W = c.W, H = c.H;
    const float S = c.S;
    const CarInfo& ci = CAR_INFO[car_sel];
    c.text_sh(90 * S, 52 * S, 8.0f * S, rgb(255, 255, 255), "GARAGE");
    c.rrect(84 * S, 52 * S + 64 * S, 120 * S, 5 * S, 2 * S, rgb(255, 120, 30));
    // showroom
    float cx = W * 0.62f, floor_y = H * 0.56f;
    c.rrect(cx - 360 * S, floor_y - 30 * S, 720 * S, 90 * S, 45 * S, argb(150, 12, 14, 24), argb(150, 30, 34, 52));
    c.rrect(cx - 330 * S, floor_y - 18 * S, 660 * S, 8 * S, 4 * S, (uint32_t(RARITY_COL[ci.rarity]) & 0x00FFFFFFu) | 0xB4000000u);
    draw_sprite(c, car_sel, cx, floor_y + 6 * S, 400 * S, paint_sel, 0.0f, 0, 0.0f);
    char b[64];
    snprintf(b, sizeof b, "< %s >", ci.name);
    float ry = H * 0.665f;
    c.rrect(cx - 330 * S, ry, 660 * S, 50 * S, 12 * S, garage_row == 0 ? rgb(255, 130, 34) : argb(170, 18, 22, 38), garage_row == 0 ? rgb(214, 70, 24) : argb(170, 28, 32, 52));
    c.text_sh(cx, ry + 11 * S, 3.8f * S, rgb(255, 255, 255), b, 1);
    snprintf(b, sizeof b, "< %s >", PAINTS[paint_sel].name);
    c.rrect(cx - 330 * S, ry + 58 * S, 660 * S, 50 * S, 12 * S, garage_row == 1 ? rgb(255, 130, 34) : argb(170, 18, 22, 38), garage_row == 1 ? rgb(214, 70, 24) : argb(170, 28, 32, 52));
    c.text_sh(cx, ry + 69 * S, 3.8f * S, rgb(255, 255, 255), b, 1);
    c.circle(cx - 300 * S, ry + 83 * S, 14 * S, rgb(PAINTS[paint_sel].r, PAINTS[paint_sel].g, PAINTS[paint_sel].b));
    c.text_sh(cx, ry - 26 * S, 2.8f * S, uint32_t(RARITY_COL[ci.rarity]), RARITY_NAME[ci.rarity], 1);
    // stats (left panel)
    float sx = 90 * S, sy = 200 * S;
    c.rrect(sx - 20 * S, sy - 16 * S, 420 * S, 232 * S, 14 * S, argb(165, 12, 16, 28));
    const char* nm[4] = {"TOP SPEED", "POWER", "GRIP", "ARMOR"};
    float val[4] = {(ci.top - 140) / 90.0f, (ci.power - 0.8f) / 0.8f, (ci.grip - 0.8f) / 0.6f, (1.5f - ci.armor) / 0.7f};
    for (int i = 0; i < 4; i++) {
        float y = sy + i * 52 * S;
        c.text(sx, y, 2.8f * S, rgb(160, 170, 195), nm[i]);
        c.rrect(sx, y + 26 * S, 380 * S, 12 * S, 6 * S, rgb(30, 36, 54));
        c.rrect(sx, y + 26 * S, std::max(12.0f * S, 380 * S * clampf(val[i], 0.05f, 1.0f)), 12 * S, 6 * S, rgb(255, 170, 50), rgb(255, 110, 30));
    }
    snprintf(b, sizeof b, "%d KM/H", int(ci.top));
    c.text_sh(sx + 380 * S, sy, 2.8f * S, rgb(255, 255, 255), b, 2);
    // paint swatches
    for (int i = 0; i < PAINT_COUNT; i++) {
        float px0 = cx + (i - (PAINT_COUNT - 1) * 0.5f) * 40 * S, py0 = H * 0.865f;
        c.circle(px0, py0, 14 * S, rgb(PAINTS[i].r, PAINTS[i].g, PAINTS[i].b));
        if (i == paint_sel) c.ring(px0, py0, 17 * S, 21 * S, 0, 2 * PI_F, rgb(255, 255, 255));
    }
    prompt(c, 90 * S, H - 62 * S, B_LEFT, "CHANGE", S);
    prompt(c, 330 * S, H - 62 * S, B_UP, "ROW", S);
    prompt(c, 500 * S, H - 62 * S, B_CIRCLE, "BACK", S);
}

void Game::overlay(Cv& c) {
    const int W = c.W, H = c.H;
    const float S = c.S;
    char b[64];
    if (state == GARAGE) { c.rect(0, 0, W, H, argb(205, 6, 8, 16)); garage_screen(c); return; }
    bool menu_like = state == MENU || state == PAUSED || state == CONTROLS;
    if (menu_like) {
        c.rect(0, 0, W, H, argb(state == MENU ? 120 : 175, 6, 8, 16));
        c.vgrad(0, 0, W, H, argb(70, 255, 110, 30), argb(0, 255, 110, 30));
    }
    if (state == MENU || state == PAUSED) {
        const char* title = state == MENU ? "HAIRPIN RUSH" : "PAUSED";
        float sc = state == MENU ? 12.0f * S : 10.0f * S;
        c.rrect(70 * S, 96 * S, 8 * S, 100 * S, 3 * S, rgb(255, 120, 30));
        c.text(98 * S + 5 * S, 100 * S + 5 * S, sc, argb(140, 255, 90, 20), title);
        c.text(98 * S, 100 * S, sc, rgb(255, 255, 255), title);
        c.text(100 * S, 100 * S + 7 * sc + 12 * S, 3.2f * S, rgb(255, 170, 90), state == MENU ? "SABAK STUDIOS  -  PS4 EDITION" : "RACE IN PROGRESS");
        const char* mi[7]; char qb[40], lb[40];
        static const char* QN[3] = {"540P  (FASTEST)", "720P  (BALANCED)", "1080P  (SHARPEST)"};
        int n;
        if (state == MENU) {
            snprintf(qb, sizeof qb, "GRAPHICS:  < %s >", QN[quality]);
            snprintf(lb, sizeof lb, "LAPS:  < %d >", laps_total);
            mi[0] = "START RACE"; mi[1] = "GARAGE"; mi[2] = "CONTROLS"; mi[3] = qb; mi[4] = show_fps ? "FPS COUNTER:  ON" : "FPS COUNTER:  OFF"; mi[5] = lb; mi[6] = fpv ? "CAMERA:  FIRST PERSON" : "CAMERA:  CHASE"; n = 7;
        } else { mi[0] = "RESUME"; mi[1] = "RESTART"; mi[2] = "CONTROLS"; mi[3] = "MAIN MENU"; n = 4; }
        float y0 = 246 * S;
        for (int i = 0; i < n; i++) {
            float y = y0 + i * 52 * S, sel = (i == menu_sel) ? 1.0f : 0.0f, x = 90 * S + sel * 14 * S;
            c.rrect(x, y, 560 * S, 44 * S, 12 * S, sel ? rgb(255, 130, 34) : argb(170, 18, 22, 38), sel ? rgb(214, 70, 24) : argb(170, 28, 32, 52));
            if (sel) c.rrect(x - 8 * S, y + 6 * S, 5 * S, 32 * S, 2 * S, rgb(255, 230, 120));
            c.text_sh(x + 28 * S, y + 10 * S, 3.4f * S, sel ? rgb(255, 255, 255) : rgb(200, 208, 228), mi[i]);
        }
        prompt(c, 90 * S, H - 62 * S, B_CROSS, "SELECT", S);
        prompt(c, 330 * S, H - 62 * S, B_UP, "MOVE", S);
        prompt(c, 520 * S, H - 62 * S, B_LEFT, "CHANGE", S);
    } else if (state == CONTROLS) {
        float pw = 940 * S, px0 = (W - pw) * 0.5f;
        c.text_sh(px0, 56 * S, 8.0f * S, rgb(255, 255, 255), "CONTROLS");
        c.rrect(px0 - 6 * S, 56 * S + 64 * S, 120 * S, 5 * S, 2 * S, rgb(255, 120, 30));
        float y0 = 168 * S;
        c.rrect(px0, y0, pw, 50 * S, 10 * S, argb(120, 16, 20, 34));
        c.text(px0 + 24 * S, y0 + 15 * S, 3.4f * S, rgb(150, 160, 185), "STEER");
        c.text(px0 + pw - 24 * S, y0 + 15 * S, 3.4f * S, rgb(150, 160, 185), "LEFT STICK / D-PAD", 2);
        for (int i = 0; i < A_COUNT + 2; i++) {
            float y = y0 + (i + 1) * 58 * S, sel = (i == ctl_sel) ? 1.0f : 0.0f;
            c.rrect(px0, y, pw, 50 * S, 10 * S, sel ? rgb(255, 130, 34) : argb(170, 18, 22, 38), sel ? rgb(214, 70, 24) : argb(170, 28, 32, 52));
            if (i < A_COUNT) {
                c.text_sh(px0 + 24 * S, y + 15 * S, 3.6f * S, sel ? rgb(255, 255, 255) : rgb(205, 212, 232), ACTION_NAME[i]);
                if (capturing && sel) {
                    if (int(capture_t * 3) & 1) c.text_sh(px0 + pw - 24 * S, y + 15 * S, 3.4f * S, rgb(255, 255, 255), "PRESS A BUTTON...", 2);
                } else {
                    uint32_t bb = bind[i];
                    bool face = bb == B_CROSS || bb == B_CIRCLE || bb == B_SQUARE || bb == B_TRIANGLE;
                    if (face) c.text_sh(px0 + pw - 82 * S, y + 16 * S, 3.0f * S, rgb(235, 238, 250), btn_name(bb), 2);
                    btn_icon(c, px0 + pw - (face ? 42 : 80) * S, y + 27 * S, 40 * S, bb);
                }
            } else {
                c.text_sh(px0 + 24 * S, y + 15 * S, 3.6f * S, sel ? rgb(255, 255, 255) : rgb(205, 212, 232), i == A_COUNT ? "RESET TO DEFAULTS" : "BACK");
            }
        }
        prompt(c, px0, H - 62 * S, B_CROSS, "CHANGE", S);
        prompt(c, px0 + 230 * S, H - 62 * S, B_CIRCLE, "BACK", S);
        prompt(c, px0 + 440 * S, H - 62 * S, B_UP, "MOVE", S);
    } else if (state == COUNTDOWN) {
        float t = state_t;
        int n = 3 - int(t);
        float frac = t - floorf(t);
        for (int i = 0; i < 3; i++) {                                              // start lights
            bool on = i < (3 - n) + 1 && n >= 1;
            c.circle(W * 0.5f + (i - 1) * 90 * S, 150 * S, 30 * S, on ? rgb(255, 40, 30) : rgb(50, 20, 22));
            if (on) c.circle(W * 0.5f + (i - 1) * 90 * S, 150 * S, 44 * S, argb(60, 255, 40, 30));
        }
        snprintf(b, sizeof b, "%d", n);
        float sc = (22.0f + (1.0f - frac) * 8.0f) * S;
        c.text_sh(W * 0.5f, H * 0.5f - 3.5f * sc, sc, rgb(255, 232, 90), b, 1);
    } else if (state == FINISH) {
        float pw = 700 * S, ph = 380 * S, x0 = (W - pw) * 0.5f, y0 = (H - ph) * 0.5f - 20 * S;
        c.rect(0, 0, W, H, argb(120, 4, 6, 12));
        c.rrect(x0, y0, pw, ph, 18 * S, argb(235, 16, 20, 34), argb(235, 10, 12, 22));
        c.rrect(x0, y0, pw, 8 * S, 4 * S, rgb(255, 120, 30));
        c.text_sh(W * 0.5f, y0 + 36 * S, 9.0f * S, rgb(255, 214, 70), "FINISH!", 1);
        snprintf(b, sizeof b, "POSITION  %d / 6", place_);
        c.text_sh(W * 0.5f, y0 + 150 * S, 4.8f * S, rgb(255, 255, 255), b, 1);
        snprintf(b, sizeof b, "TIME  %d:%02d.%d", int(race_t) / 60, int(race_t) % 60, int(race_t * 10) % 10);
        c.text_sh(W * 0.5f, y0 + 210 * S, 4.8f * S, rgb(255, 255, 255), b, 1);
        if (state_t > 1.5f) { prompt(c, x0 + 80 * S, y0 + ph - 62 * S, B_CROSS, "RACE AGAIN", S); prompt(c, x0 + 380 * S, y0 + ph - 62 * S, B_CIRCLE, "MENU", S); }
    }
    if (show_fps) { snprintf(b, sizeof b, "%d FPS  %dP", int(fps + 0.5f), render_h()); c.text_sh(W * 0.5f, H - 28 * S, 2.6f * S, rgb(120, 255, 160), b, 1); }
}

void Game::render(uint32_t* fb) {
    Cv c{fb, render_w(), render_h(), render_h() / 720.0f};
    bool race_scene = state == RACE || state == COUNTDOWN || state == FINISH || state == PAUSED || (state == CONTROLS && ctl_return == PAUSED);
    if (race_scene) scene(c, z, px, true); else scene(c, demo_z, 0.0f, false);
    if (race_scene && fpv) cockpit(c);
    if (race_scene) hud(c);
    overlay(c);
}
