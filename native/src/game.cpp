// Copyright (c) 2026 Sabak Studios. All rights reserved.
#include "game.h"
#include "font.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
static inline uint32_t blendpx(uint32_t d, uint32_t s, int a) {      // d is premultiplied ARGB (opaque dst gives the usual result); result is premultiplied
    if (a <= 0) return d;
    if (a >= 255) return 0xFF000000u | (s & 0xFFFFFFu);
    int a256 = a + (a >> 7), ia = 256 - a256;
    int da = d >> 24;
    int r = ((((s >> 16) & 255) * a256) + (((d >> 16) & 255) * ia)) >> 8;
    int g = ((((s >> 8) & 255) * a256) + (((d >> 8) & 255) * ia)) >> 8;
    int b = (((s & 255) * a256) + ((d & 255) * ia)) >> 8;
    int oa = da >= 255 ? 255 : std::min(255, ((a256 * 255) >> 8) + ((da * ia) >> 8));
    return (uint32_t(oa) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
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
static const CarInfo CAR_INFO[CAR_MESH_COUNT] = {
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


static const uint32_t DEFAULT_BIND[A_COUNT] = {B_R2, B_L2, B_SQUARE, B_TRIANGLE, B_OPTIONS, B_R3};
static const char* ACTION_NAME[A_COUNT] = {"ACCELERATE", "BRAKE", "HANDBRAKE", "RESET CAR", "PAUSE", "CAMERA VIEW"};

Game::Game() {
    for (int i = 0; i < A_COUNT; i++) bind[i] = DEFAULT_BIND[i];
    rend.threads = 4;
    load_stage();
    reset_race();
}

void Game::load_stage() {
    if (stage_loaded == stage_sel) return;
    world.load(stage_sel);
    stage_loaded = stage_sel;
    mm_x.clear(); mm_y.clear();
    float mnx = 1e9f, mxx = -1e9f, mnz = 1e9f, mxz = -1e9f;
    const StageData& sd = STAGES[stage_sel];
    for (int i = 0; i < sd.npts; i++) { mnx = std::min(mnx, sd.pts[i].x); mxx = std::max(mxx, sd.pts[i].x); mnz = std::min(mnz, sd.pts[i].z); mxz = std::max(mxz, sd.pts[i].z); }
    float sp = std::max(mxx - mnx, mxz - mnz);
    if (sp < 1) sp = 1;
    float ox = (1.0f - (mxx - mnx) / sp) * 0.5f, oy = (1.0f - (mxz - mnz) / sp) * 0.5f;
    for (int i = 0; i < sd.npts; i += 3) { mm_x.push_back((sd.pts[i].x - mnx) / sp + ox); mm_y.push_back((sd.pts[i].z - mnz) / sp + oy); }
}

void Game::apply_car() {
    const CarInfo& ci = CAR_INFO[car_sel];
    topf = ci.top / 205.0f; accf = ci.power; gripf = ci.grip; armf = ci.armor;
    vmax = ci.top / 3.6f * 0.97f;
}

void Game::reset_race() {
    load_stage();
    apply_car();
    race_serial_++;
    s_ = 0; d_ = 0; yaw_ = 0; speed = 0; race_t = 0; place_ = 6; shake = 0; braking = 0; steer_vis = 0;
    vy_ = 0; wz_ = 0; steer_ang_ = 0; rpm_ = 1000; gear_ = 0; shift_t_ = 0; skid_ = 0; lat_g_ = 0;
    rivals.clear();
    const int models[5] = {3, 6, 2, 7, 0}, paints[5] = {1, 4, 5, 6, 12};
    for (int i = 0; i < 5; i++) {
        float top = CAR_INFO[models[i]].top / 3.6f;
        rivals.push_back({16.0f + i * 13.0f, (i % 2 ? 2.6f : -2.6f), 0.0f, top * (0.66f + 0.045f * i), models[i], paints[i]});
    }
}

int Game::render_w() const { static const int w[3] = {960, 1280, 1920}; return vp_w ? vp_w : w[quality]; }
int Game::render_h() const { static const int h[3] = {540, 720, 1080}; return vp_h ? vp_h : h[quality]; }
void Game::mp_begin(int stage, int car, int paint, const Game* src) {
    stage_sel = stage; car_sel = car; paint_sel = paint;
    if (src) { for (int i = 0; i < A_COUNT; i++) bind[i] = src->bind[i]; fpv = src->fpv; quality = src->quality; }
    reset_race(); state = COUNTDOWN; state_t = 0;
}

float Game::axis(int a, const PadState& pad, uint32_t dn) const {
    uint32_t b = bind[a];
    if (b == B_R2) return std::max(pad.r2, (dn & B_R2) ? 1.0f : 0.0f);
    if (b == B_L2) return std::max(pad.l2, (dn & B_L2) ? 1.0f : 0.0f);
    return (dn & b) ? 1.0f : 0.0f;
}

std::string Game::save_blob() const {
    char buf[240];
    snprintf(buf, sizeof buf, "v4 %u %u %u %u %u %u q%d f%d s%d c%d k%d p%d", bind[0], bind[1], bind[2], bind[3], bind[4], bind[5], quality, show_fps ? 1 : 0, stage_sel, fpv ? 1 : 0, car_sel, paint_sel);
    return buf;
}
void Game::load_blob(const std::string& s) {
    unsigned b[A_COUNT]; int q, f, st, cm, ks, ps;
    if (sscanf(s.c_str(), "v4 %u %u %u %u %u %u q%d f%d s%d c%d k%d p%d", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &q, &f, &st, &cm, &ks, &ps) != 12) return;
    for (int i = 0; i < A_COUNT; i++) if (b[i] && (b[i] & (b[i] - 1)) == 0 && b[i] <= B_RIGHT) bind[i] = b[i];
    quality = q < 0 ? 0 : (q > 2 ? 2 : q); show_fps = f != 0; fpv = cm != 0;
    stage_sel = st < 0 ? 0 : (st >= STAGE_COUNT ? STAGE_COUNT - 1 : st);
    car_sel = ks < 0 ? 0 : (ks >= CAR_MESH_COUNT ? CAR_MESH_COUNT - 1 : ks);
    paint_sel = ps < 0 ? 0 : (ps >= PAINT_COUNT ? PAINT_COUNT - 1 : ps);
    apply_car();
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
    demo_s += 16.0f * dt;
    spin += dt * 0.5f;
    auto wrap = [](int v, int n) { return (v % n + n) % n; };

    if (state == MENU) {
        const int N = 8;
        if (ed & B_UP) menu_sel = wrap(menu_sel - 1, N);
        if (ed & B_DOWN) menu_sel = wrap(menu_sel + 1, N);
        int dir = ((ed & B_RIGHT) ? 1 : 0) - ((ed & B_LEFT) ? 1 : 0);
        bool ok = (ed & B_CROSS) != 0;
        if (menu_sel == 0 && ok) { reset_race(); state = COUNTDOWN; state_t = 0; if (mp_players > 1) mp_launch = true; }
        else if (menu_sel == 1 && ok) { state = GARAGE; garage_row = 0; state_t = 0; }
        else if (menu_sel == 2 && ok) { ctl_return = MENU; state = CONTROLS; ctl_sel = 0; capturing = false; }
        else if (menu_sel == 3 && (dir || ok)) { quality = wrap(quality + (dir ? dir : 1), 3); dirty = true; }
        else if (menu_sel == 4 && (dir || ok)) { show_fps = !show_fps; dirty = true; }
        else if (menu_sel == 5 && (dir || ok)) { stage_sel = wrap(stage_sel + (dir ? dir : 1), STAGE_COUNT); dirty = true; }
        else if (menu_sel == 6 && (dir || ok)) { fpv = !fpv; dirty = true; }
        else if (menu_sel == 7 && (dir || ok)) { mp_players = (mp_players - 1 + (dir ? dir : 1) + 4) % 4 + 1; }
        return;
    }
    if (state == GARAGE) {
        if (ed & (B_UP | B_DOWN)) garage_row = 1 - garage_row;
        int dir = ((ed & B_RIGHT) ? 1 : 0) - ((ed & B_LEFT) ? 1 : 0);
        if (dir) {
            if (garage_row == 0) car_sel = wrap(car_sel + dir, CAR_MESH_COUNT); else paint_sel = wrap(paint_sel + dir, PAINT_COUNT);
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
        if (follower) return;
        if (state_t > 1.5f && (ed & B_CROSS)) { reset_race(); state = COUNTDOWN; state_t = 0; }
        else if (state_t > 1.5f && (ed & B_CIRCLE)) { state = MENU; menu_sel = 0; state_t = 0; }
        return;
    }
    // ---------------- RACE
    if (!follower && (ed_raw & bind[A_PAUSE])) { state = PAUSED; menu_sel = 0; return; }
    if (ed_raw & bind[A_RESET]) { d_ = 0; yaw_ = 0; vy_ = 0; wz_ = 0; speed *= 0.4f; }
    if (ed_raw & bind[A_CAMERA]) { fpv = !fpv; dirty = true; }
    race_t += dt;
    float gas = axis(A_GAS, pad, dn), brake = axis(A_BRAKE, pad, dn);
    bool hb = (dn & bind[A_HANDBRAKE]) != 0;
    float steer = fabsf(pad.lx) > 0.08f ? pad.lx : 0.0f;                   // + = right
    if (dn & B_LEFT) steer = -1.0f;
    if (dn & B_RIGHT) steer = 1.0f;
    steer_vis += (steer - steer_vis) * std::min(1.0f, dt * 9.0f);
    braking = (brake > 0.1f || hb) ? 1.0f : 0.0f;
    step_car(dt, gas, brake, hb, steer);
    bob_t += dt * speed * 0.5f;
    // rivals
    int ahead = 0;
    for (auto& r : rivals) {
        float c2 = world.curvature_at(r.s + 25.0f);
        float vc = c2 > 1e-3f || c2 < -1e-3f ? sqrtf(9.0f / fabsf(c2)) : 99.0f;
        float tv = std::min(r.vmax, vc);
        r.speed += clampf(tv - r.speed, -14.0f * dt, 9.0f * dt);
        r.s += r.speed * dt;
        float ds = r.s - s_, dd = r.d - d_;
        if (ds > 0) ahead++;
        if (fabsf(ds) < 4.6f && fabsf(dd) < 1.9f) {
            speed *= (1.0f - 3.0f * dt * armf);
            d_ += (dd > 0 ? -1.0f : 1.0f) * 3.5f * dt;
            shake = 0.3f;
        }
    }
    place_ = ahead + 1;
    shake = std::max(0.0f, shake - dt);
    if (s_ >= world.length() - 20.0f) { state = FINISH; state_t = 0; }
}



float Game::steer_lock(float v, float mu) {       // max road-wheel angle (rad): about 1.3x the grip limit, so full stick slides a little instead of spinning
    return clampf(1.3f * mu * 9.81f * 2.55f / (v * v + 15.0f), 0.05f, 0.52f);
}

// ------------------------------------------------------------------ car physics (bicycle model with tyre slip, weight transfer, gearbox)
// State is road-relative: s_ along the road, d_ lateral (left +), yaw_ = body heading relative to the road tangent (left +),
// speed = forward speed in the car frame, vy_ = sideways speed (left +), wz_ = yaw rate (left +).
void Game::step_car(float dt, float gas, float brake, bool hb, float steer) {
    const float M = 1350.0f, A = 1.25f, B = 1.30f, L = A + B, IZ = 2100.0f, G = 9.81f, HCG = 0.52f;
    float steps = std::max(1.0f, ceilf(dt / 0.008f));
    float h = dt / steps;
    bool off = fabsf(d_) > 7.6f;
    float mu = 1.28f * powf(gripf, 0.8f) * (off ? 0.58f : 1.0f);
    float P = 118000.0f * accf;                                            // engine power, W
    float vtop = vmax;
    for (int it = 0; it < int(steps); it++) {
        float vx = std::max(speed, 0.0f);
        float ve = std::max(vx, 2.5f);                                     // slip-angle speed floor (low-speed stability)
        // steering: speed sensitive lock, smoothed, with counter-steer assist while sliding
        float lock = Game::steer_lock(vx, mu);
        float tgt = -clampf(steer, -1.0f, 1.0f);
        tgt = (tgt < 0 ? -1.0f : 1.0f) * powf(fabsf(tgt), 1.35f) * lock;
        float beta = atan2f(vy_, ve);
        if (vx > 6.0f) tgt += clampf(-beta * 0.55f, -0.18f, 0.18f) * (fabsf(steer) < 0.2f ? 1.0f : 0.5f);   // drift assist
        float rate = (fabsf(tgt) < fabsf(steer_ang_) ? 7.5f : 5.0f) * (1.0f + 0.5f * std::min(1.0f, vx / 20.0f));
        steer_ang_ += clampf(tgt - steer_ang_, -rate * h * lock * 2.0f, rate * h * lock * 2.0f);
        // longitudinal forces
        float drag = P * vx * vx / (vtop * vtop * vtop) + 90.0f + (off ? 1500.0f + vx * 120.0f : 0.0f);
        float rpm_ratio = 0.0f; int gear = gear_;
        static const float GMAX[6] = {0.19f, 0.34f, 0.50f, 0.67f, 0.84f, 1.02f};
        if (shift_t_ > 0) shift_t_ -= h;
        if (gear < 5 && vx > GMAX[gear] * vtop * 0.97f) { gear++; shift_t_ = 0.14f; }
        if (gear > 0 && vx < GMAX[gear - 1] * vtop * 0.82f) { gear--; shift_t_ = 0.10f; }
        gear_ = gear;
        float gmax = GMAX[gear] * vtop;
        rpm_ratio = clampf(vx / gmax, 0.0f, 1.1f);
        rpm_ = lerpf(rpm_, 1000.0f + rpm_ratio * 6600.0f + gas * 350.0f, std::min(1.0f, h * 14.0f));
        float tq = 1.0f - 0.45f * (rpm_ratio - 0.72f) * (rpm_ratio - 0.72f) * 4.0f;                       // torque curve peaks at ~70% rpm
        float Fdrive = gas * std::min(P * clampf(tq, 0.4f, 1.0f) / std::max(vx, 7.0f) * (gear == 0 ? 1.15f : 1.0f), 1.0e9f);
        if (shift_t_ > 0) Fdrive *= 0.15f;
        if (vx >= vtop) Fdrive = 0;
        float Fbrake = brake * 1.0f * mu * M * G * 0.92f + (hb ? 0.30f * M * G : 0.0f) + (gas < 0.05f ? 0.06f * M * G : 0.0f);
        float Fx = Fdrive - Fbrake * (vx > 0.2f ? 1.0f : 0.0f) - drag * (vx > 0.05f ? 1.0f : 0.0f);
        float ax = Fx / M;
        // weight transfer
        float Fzf = M * G * B / L - M * ax * HCG / L, Fzr = M * G * A / L + M * ax * HCG / L;
        Fzf = std::max(Fzf, 1500.0f); Fzr = std::max(Fzr, 1500.0f);
        // tyre forces (saturating, friction circle on the driven rear axle)
        float af = atan2f(vy_ + A * wz_, ve) - steer_ang_, ar = atan2f(vy_ - B * wz_, ve);
        float rear_mu = mu * (hb ? 0.42f : 1.0f);
        float use_r = clampf(fabsf(Fdrive) / std::max(rear_mu * Fzr, 1.0f), 0.0f, 0.9f);
        float Fyf = -mu * Fzf * sinf(1.45f * atanf(8.5f * af));
        float Fyr = -rear_mu * Fzr * sinf(1.45f * atanf(8.0f * ar)) * sqrtf(1.0f - use_r * use_r * 0.55f);
        float slip_amt = std::max(fabsf(af), fabsf(ar));
        skid_ = lerpf(skid_, clampf((slip_amt - 0.10f) * 5.0f, 0.0f, 1.0f) * (vx > 4.0f ? 1.0f : 0.0f) + (hb && vx > 8.0f ? 0.6f : 0.0f), std::min(1.0f, h * 10.0f));
        // rigid-body update in the car frame
        float ay = (Fyf * cosf(steer_ang_) + Fyr) / M;
        float wdot = (A * Fyf * cosf(steer_ang_) - B * Fyr) / IZ - wz_ * 0.9f * (1.0f + vx / 40.0f);       // yaw damping keeps it controllable
        float vxn = vx + (ax + wz_ * vy_) * h;
        vy_ += (ay - wz_ * vx) * h;
        wz_ += wdot * h;
        speed = std::max(0.0f, vxn);
        if (speed < 1.5f) { vy_ *= (1.0f - 6.0f * h); wz_ *= (1.0f - 6.0f * h); }
        // world motion relative to the road
        float curv = world.curvature_at(s_);                                // + = road turns left
        yaw_ += (wz_ - curv * speed) * h;
        yaw_ = clampf(yaw_, -1.2f, 1.2f);
        float cy = cosf(yaw_), sy = sinf(yaw_);
        s_ += (speed * cy - vy_ * sy) * h;
        d_ += (speed * sy + vy_ * cy) * h;
        lat_g_ = ay / G;
        if (d_ > 13.0f || d_ < -13.0f) {                                     // barrier: bounce, scrub speed
            float nrm = d_ > 0 ? -1.0f : 1.0f;
            d_ = clampf(d_, -13.0f, 13.0f);
            vy_ = nrm * fabsf(vy_) * 0.3f; speed *= (1.0f - 2.5f * h); wz_ *= 0.8f; shake = 0.35f;
        }
    }
    if (off && speed > 16.0f) speed = std::max(16.0f, speed - 6.0f * dt);
}

// ------------------------------------------------------------------ 3D scene
static uint32_t paint_rgb(int p) { return 0xFF000000u | (uint32_t(PAINTS[p].r) << 16) | (uint32_t(PAINTS[p].g) << 8) | PAINTS[p].b; }

static void draw_car3d(Renderer& r, int model, const Xf& xf, uint32_t paint, bool skip_glass, float wheel_angle) {
    const CarModel& cm = car_model(model);
    float dist = len(xf.t - r.cam_pos());
    float lod = clampf(1.45f - dist / 80.0f, 0.10f, 1.0f);
    r.mesh(cm.body, xf, paint, skip_glass, 450.0f, lod);
    if (!cm.wheel.tris.empty() && dist < 40.0f) {
        Xf pv = xf * Xf::make(cm.pivot, 0.0f, cm.pitch, 0.0f) * Xf::make(V3(0, 0, 0), wheel_angle);
        r.mesh(cm.wheel, pv, 0xFF111111u, skip_glass, 450.0f, 1.0f);
    }
}


// a thick line between two points (forearm)
static void rod(Renderer& r, V3 a, V3 b, float th, uint32_t col) {
    V3 ax = norm(b - a);
    V3 up = fabsf(ax.y) < 0.9f ? V3(0, 1, 0) : V3(1, 0, 0);
    V3 u = norm(cross(ax, up)) * th, v = norm(cross(ax, u)) * th;
    V3 p[8] = {a - u - v, a + u - v, a + u + v, a - u + v, b - u - v, b + u - v, b + u + v, b - u + v};
    auto q = [&](int i, int j, int k, int l) { r.tri(p[i], p[j], p[k], col, true); r.tri(p[i], p[k], p[l], col, true); };
    q(0, 1, 2, 3); q(4, 7, 6, 5); q(0, 4, 5, 1); q(1, 5, 6, 2); q(2, 6, 7, 3); q(3, 7, 4, 0);
}

void Game::draw_hands(const Xf& car, int model, float phi) {
    const CarModel& cm = car_model(model);
    if (cm.wheel.tris.empty()) return;
    // hand over hand, like the phone game: a hand rides the rim inside a comfortable arc, then lets go and grips again further back
    float psi_r = grip_r + phi, psi_l = grip_l + phi;
    if (psi_r > 1.05f) grip_r -= 1.9f; else if (psi_r < -1.05f) grip_r += 1.9f;
    if (psi_l > 1.05f) grip_l -= 1.9f; else if (psi_l < -1.05f) grip_l += 1.9f;
    Xf pv = car * Xf::make(cm.pivot, 0.0f, cm.pitch, 0.0f);
    const float R = 0.15f;
    float ang[2] = {grip_r + phi, 3.14159265f + grip_l + phi};
    for (int k = 0; k < 2; k++) {
        V3 hp = pv.apply(V3(cosf(ang[k]) * R, 0.0f, -sinf(ang[k]) * R));
        V3 shoulder = car.apply(V3(k == 0 ? -0.22f : -0.68f, 1.12f, 0.55f));
        V3 elbow = hp + (shoulder - hp) * 0.55f - car.rot(V3(0, 0.16f, 0));
        rod(rend, hp, elbow, 0.034f, 0xFF20242Eu);                    // sleeve
        rod(rend, elbow, shoulder, 0.05f, 0xFF20242Eu);
        V3 up = pv.rot(V3(0, 1, 0));
        rod(rend, hp - up * 0.03f, hp + up * 0.045f, 0.045f, 0xFFC9926Au);   // hand
    }
}

void Game::scene3d(float) {
    const int W = render_w(), H = render_h();
    if (rend.w() != W || rend.h() != H) rend.resize(W, H);
    load_stage();
    const Theme& th = world.theme();
    bool race_scene = state == RACE || state == COUNTDOWN || state == FINISH || state == PAUSED || (state == CONTROLS && ctl_return == PAUSED);
    uint32_t pcol = paint_rgb(paint_sel);
    if (state == GARAGE) {
        Camera cam; cam.fov_deg = 34;
        float R = 12.5f;
        V3 target(0, 0.2f, 0);
        cam.pos = target + V3(sinf(spin) * R, 3.4f, cosf(spin) * R);
        cam.yaw = spin; cam.pitch = -atan2f(3.2f, R);
        cam.pos = cam.pos - V3(cosf(cam.yaw), 0, -sinf(cam.yaw)) * 3.6f;        // shift the car to the right half of the screen
        rend.begin(cam, 0xFF0B0E18u, 0xFF1C2236u, 0xFF141A2Au, 60, 300);
        // studio floor: disc + glowing ring in the rarity colour
        for (int i = 0; i < 40; i++) {
            float a0 = i * 6.2831853f / 40, a1 = (i + 1) * 6.2831853f / 40;
            rend.tri(V3(0, 0, 0), V3(sinf(a1) * 4.0f, 0, cosf(a1) * 4.0f), V3(sinf(a0) * 4.0f, 0, cosf(a0) * 4.0f), (i & 1) ? 0xFF1A2032u : 0xFF20273Bu, true);
            V3 o0(sinf(a0) * 4.0f, 0.01f, cosf(a0) * 4.0f), o1(sinf(a1) * 4.0f, 0.01f, cosf(a1) * 4.0f), i0(sinf(a0) * 3.85f, 0.01f, cosf(a0) * 3.85f), i1(sinf(a1) * 3.85f, 0.01f, cosf(a1) * 3.85f);
            uint32_t rc = RARITY_COL[CAR_INFO[car_sel].rarity];
            rend.tri(i0, o0, o1, rc, true, true); rend.tri(i0, o1, i1, rc, true, true);
        }
        draw_car3d(rend, car_sel, Xf::make(V3(0, 0, 0), 0.0f), pcol, false, 0.0f);
        rend.end(fb_);
        return;
    }
    float s_cam;
    Camera cam;
    cam.fov_deg = 64;
    rend.threads = rend.threads;
    if (race_scene) {
        s_cam = s_;
        V3 p = world.pos_at(s_, d_);
        float hh = world.heading_at(s_) + yaw_, sl = world.slope_at(s_);
        Xf pxf = Xf::make(p, hh, sl, -steer_vis * 0.03f);
        V3 fwd(-sinf(hh), 0, -cosf(hh));
        float sk = shake * 0.07f * sinf(race_t * 70.0f);
        if (fpv) {
            cam.pos = pxf.apply(V3(-0.45f, 1.52f + sk, 0.12f));
            cam.yaw = hh; cam.pitch = sl - 0.02f; cam.roll = -steer_vis * 0.02f;
            cam.fov_deg = 74 + speed * 0.12f;
        } else {
            float hr = world.heading_at(s_);
            V3 f2(-sinf(hr * 0.5f + hh * 0.5f), 0, -cosf(hr * 0.5f + hh * 0.5f));
            cam.pos = p - f2 * 7.4f + V3(0, 2.9f + sk, 0);
            cam.yaw = hr * 0.5f + hh * 0.5f; cam.pitch = sl - 0.11f;
            cam.fov_deg = 62 + speed * 0.16f;
        }
        rend.begin(cam, th.sky_top, th.sky_hor, th.fog, 70, 460);
        world.draw(rend, cam, s_cam);
        std::vector<const Rival*> all; for (const Rival& r : rivals) all.push_back(&r); for (const Rival& r : others) all.push_back(&r);
        for (const Rival* rp_ : all) {
            const Rival& r = *rp_;
            V3 rp = world.pos_at(r.s, r.d);
            Xf rx = Xf::make(rp, world.heading_at(r.s), world.slope_at(r.s));
            draw_car3d(rend, r.model, rx, paint_rgb(r.paint), false, 0.0f);
        }
        if (fpv) rend.set_layer(1);                                   // cockpit + hands: GPU draws them after a depth clear (no z-fighting)
        draw_car3d(rend, car_sel, pxf, pcol, fpv, -steer_vis * 1.6f);
        if (fpv) draw_hands(pxf, car_sel, -steer_vis * 1.6f);
        rend.end(fb_);
        return;
    }
    // menu background: the selected car cruising along the stage, seen from a low front-side angle
    s_cam = demo_s + 20.0f;
    float ds = fmodf(demo_s, std::max(100.0f, world.length() - 200.0f));
    V3 p = world.pos_at(ds + 20.0f, 0);
    float hh = world.heading_at(ds + 20.0f), sl = world.slope_at(ds + 20.0f);
    Xf pxf = Xf::make(p, hh, sl);
    V3 side = pxf.rot(V3(1, 0, 0)), fwd = pxf.rot(V3(0, 0, -1));
    cam.pos = p + side * 5.6f - fwd * 8.5f + V3(0, 1.7f, 0);
    V3 look = p + fwd * 3.0f + V3(0, 0.9f, 0);
    V3 dv = look - cam.pos;
    cam.yaw = atan2f(-dv.x, -dv.z); cam.pitch = atan2f(dv.y, sqrtf(dv.x * dv.x + dv.z * dv.z));
    cam.fov_deg = 52;
    rend.begin(cam, th.sky_top, th.sky_hor, th.fog, 70, 460);
    world.draw(rend, cam, ds + 20.0f);
    draw_car3d(rend, car_sel, pxf, pcol, false, 0.0f);
    rend.end(fb_);
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
    // stage + progress
    c.rrect(W * 0.5f - 190 * S, 22 * S, 380 * S, 62 * S, 14 * S, argb(170, 12, 16, 28), argb(170, 20, 24, 40));
    c.text_sh(W * 0.5f, 30 * S, 3.2f * S, rgb(255, 255, 255), world.name(), 1);
    c.rrect(W * 0.5f - 160 * S, 62 * S, 320 * S, 8 * S, 4 * S, rgb(34, 40, 60));
    c.rrect(W * 0.5f - 160 * S, 62 * S, std::max(8.0f * S, 320 * S * clampf(progress(), 0, 1)), 8 * S, 4 * S, rgb(255, 150, 40), rgb(255, 100, 30));
    // time
    snprintf(b, sizeof b, "%d:%02d.%d", int(race_t) / 60, int(race_t) % 60, int(race_t * 10) % 10);
    c.rrect(W - 236 * S, 22 * S, 210 * S, 84 * S, 10 * S, argb(170, 12, 16, 28), argb(170, 20, 24, 40));
    c.text(W - 222 * S, 32 * S, 2.6f * S, rgb(150, 160, 185), "TIME");
    c.text_sh(W - 222 * S, 56 * S, 5.4f * S, rgb(255, 255, 255), b);
    // hairpin warning
    float ahead = upcoming_curve(60.0f) * 0.5f + upcoming_curve(110.0f) * 0.5f;
    if (fabsf(ahead) > 0.032f && (int(race_t * 4) & 1) == 0) {
        float cx = W * 0.5f, cy = 150 * S;
        c.rrect(cx - 200 * S, cy - 34 * S, 400 * S, 68 * S, 12 * S, argb(215, 200, 24, 30), argb(215, 140, 10, 20));
        c.text_sh(cx, cy - 14 * S, 4.6f * S, rgb(255, 255, 255), ahead < 0 ? "SHARP RIGHT >>" : "<< SHARP LEFT", 1);
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
    for (int i = 0; i + 1 < np; i++) c.line(mx + mm_x[i] * ms, my + mm_y[i] * ms, mx + mm_x[i + 1] * ms, my + mm_y[i + 1] * ms, 4.5f * S, rgb(70, 78, 104));
    auto at = [&](float sm, float& x, float& y) { int i = int(sm / 15.0f); if (i < 0) i = 0; if (i >= np) i = np - 1; x = mx + mm_x[i] * ms; y = my + mm_y[i] * ms; };
    for (auto& r : rivals) { float x, y; at(r.s, x, y); c.circle(x, y, 4.5f * S, rgb(PAINTS[r.paint].r, PAINTS[r.paint].g, PAINTS[r.paint].b)); }
    { float x, y; at(s_, x, y); c.circle(x, y, 8 * S, argb(130, 255, 255, 255)); c.circle(x, y, 5.5f * S, rgb(255, 60, 70)); }
    // speed streaks
    if (kmh > 130) {
        int cnt = 14;
        for (int i = 0; i < cnt; i++) {
            uint32_t h = hash32(uint32_t(i) + uint32_t(race_t * 22.0f) * 131u);
            float ang = (hf(h) * 2 - 1) * 1.1f + (hf(h + 1) < 0.5f ? 0.0f : PI_F);
            float r0 = (0.34f + 0.12f * hf(h + 2)) * W, r1 = r0 + (0.07f + 0.09f * hf(h + 3)) * W;
            float cx = W * 0.5f, cy = H * 0.5f;
            c.line(cx + cosf(ang) * r0, cy + sinf(ang) * r0 * 0.6f, cx + cosf(ang) * r1, cy + sinf(ang) * r1 * 0.6f, 1.6f * S, argb(int(clampf((kmh - 130) / 100.0f, 0, 1) * 70), 255, 255, 255));
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
    float cx = W * 0.62f;
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
    if (state == GARAGE) { garage_screen(c); return; }
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
        const char* mi[8]; char qb[40], lb[40], mb[48];
        static const char* QN[3] = {"540P  (FASTEST)", "720P  (BALANCED)", "1080P  (SHARPEST)"};
        int n;
        if (state == MENU) {
            snprintf(qb, sizeof qb, "GRAPHICS:  < %s >", QN[quality]);
            snprintf(lb, sizeof lb, "STAGE:  < %s >", STAGES[stage_sel].name);
            mi[0] = "START RACE"; mi[1] = "GARAGE"; mi[2] = "CONTROLS"; mi[3] = qb; mi[4] = show_fps ? "FPS COUNTER:  ON" : "FPS COUNTER:  OFF"; mi[5] = lb; mi[6] = fpv ? "CAMERA:  FIRST PERSON" : "CAMERA:  CHASE";
            if (mp_players == 1) snprintf(mb, sizeof mb, "SPLIT SCREEN:  < OFF >"); else snprintf(mb, sizeof mb, "SPLIT SCREEN:  < %d PLAYERS >", mp_players);
            mi[7] = mb; n = 8;
        } else { mi[0] = "RESUME"; mi[1] = "RESTART"; mi[2] = "CONTROLS"; mi[3] = "MAIN MENU"; n = 4; }
        float y0 = 246 * S;
        for (int i = 0; i < n; i++) {
            float y = y0 + i * 50 * S, sel = (i == menu_sel) ? 1.0f : 0.0f, x = 90 * S + sel * 14 * S;
            c.rrect(x, y, 600 * S, 42 * S, 12 * S, sel ? rgb(255, 130, 34) : argb(170, 18, 22, 38), sel ? rgb(214, 70, 24) : argb(170, 28, 32, 52));
            if (sel) c.rrect(x - 8 * S, y + 6 * S, 5 * S, 30 * S, 2 * S, rgb(255, 230, 120));
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
        if (state_t > 1.5f && !follower) { prompt(c, x0 + 80 * S, y0 + ph - 62 * S, B_CROSS, "RACE AGAIN", S); prompt(c, x0 + 380 * S, y0 + ph - 62 * S, B_CIRCLE, "MENU", S); }
    }
    if (show_fps) { snprintf(b, sizeof b, "%d FPS  %dP", int(fps + 0.5f), render_h()); c.text_sh(W * 0.5f, H - 28 * S, 2.6f * S, rgb(120, 255, 160), b, 1); }
}


void Game::render(uint32_t* fb) {
    fb_ = fb;
    scene3d(0);
    Cv c{fb, render_w(), render_h(), render_h() / 720.0f};
    bool race_scene = state == RACE || state == COUNTDOWN || state == FINISH || state == PAUSED || (state == CONTROLS && ctl_return == PAUSED);
    if (race_scene) hud(c);
    overlay(c);
}
