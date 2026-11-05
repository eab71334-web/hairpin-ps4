// Copyright (c) 2026 Sabak Studios. All rights reserved.
#include "game.h"
#include "font.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

static const float SEG = 200.0f;            // road segment length
static const float MAX_SPEED = 22000.0f;    // ~220 km/h
static const int HORIZON = 300;             // screen row of the horizon

static inline uint32_t rgb(int r, int g, int b) { return 0xFF000000u | (r << 16) | (g << 8) | b; }
static inline uint32_t mix(uint32_t a, uint32_t b, float t) {
    int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255, br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
    return rgb(int(ar + (br - ar) * t), int(ag + (bg - ag) * t), int(ab + (bb - ab) * t));
}

Game::Game() {
    // track: straights, sweepers and three HAIRPINS (big curve values)
    struct P { int len; float c; };
    const P plan[] = {{40, 0}, {25, 0.9f}, {20, 0}, {30, -1.1f}, {25, 0}, {22, 3.4f}, {18, 0}, {30, 0.6f}, {26, -0.8f}, {14, 0},
                      {20, -3.6f}, {28, 0}, {24, 1.2f}, {20, 0}, {26, 3.2f}, {30, 0}, {25, -1.0f}, {35, 0}};
    for (auto& p : plan) for (int i = 0; i < p.len * 6; i++) {
        float t = float(i) / (p.len * 6);
        float ease = std::min(1.0f, std::min(t, 1.0f - t) * 4.0f);           // smooth in / out of each bend
        curves.push_back(p.c * (p.c == 0 ? 0 : ease));
    }
    track_len = curves.size() * SEG;
    const uint32_t cols[] = {rgb(230, 60, 60), rgb(60, 130, 230), rgb(240, 190, 40), rgb(70, 200, 110), rgb(190, 90, 220)};
    for (int i = 0; i < 5; i++) rivals.push_back({SEG * (12 + i * 9), (i % 2 ? 0.35f : -0.35f), MAX_SPEED * (0.62f + 0.05f * i), cols[i]});
}

float Game::curve_at(float zz) const {
    if (curves.empty()) return 0;
    zz = std::fmod(zz, track_len);
    if (zz < 0) zz += track_len;
    return curves[int(zz / SEG) % curves.size()];
}

float Game::upcoming_curve(float ahead) const { return curve_at(z + ahead); }

void Game::update(float dt, const PadState& pad) {
    state_t += dt;
    if (state == MENU) {
        if (pad.confirm || pad.start) { state = COUNTDOWN; state_t = 0; }
        return;
    }
    if (state == COUNTDOWN) {
        if (state_t > 3.0f) { state = RACE; state_t = 0; }
        return;
    }
    if (state == FINISH) {
        if (pad.confirm && state_t > 1.5f) *this = Game();
        return;
    }
    race_t += dt;
    // --- player
    float accel = pad.gas * 7000.0f - pad.brake * 16000.0f - (pad.gas < 0.05f ? 2500.0f : 0.0f);
    bool off = std::fabs(px) > 1.05f;
    if (off) accel -= 9000.0f;
    speed += accel * dt * (1.0f - speed / (MAX_SPEED * 1.15f));
    speed = std::clamp(speed, 0.0f, MAX_SPEED);
    if (off && speed > 5500.0f) speed = std::max(5500.0f, speed - 14000.0f * dt);
    float sp = speed / MAX_SPEED;
    px += pad.steer * dt * 2.2f * (0.35f + sp);                 // steering
    px -= curve_at(z) * sp * sp * dt * 0.95f;                    // centrifugal push to the outside of a bend
    px = std::clamp(px, -2.2f, 2.2f);
    z += speed * dt;
    if (z - lap_z0 >= track_len) { lap_z0 += track_len; lap_++; if (lap_ > laps_total) { state = FINISH; state_t = 0; lap_ = laps_total; } }
    // --- rivals
    int ahead = 0;
    for (auto& r : rivals) {
        float c = curve_at(r.z + 400);
        r.speed = std::clamp(r.speed + (MAX_SPEED * (0.66f - std::fabs(c) * 0.06f) - r.speed) * dt * 0.8f, 5000.0f, MAX_SPEED * 0.95f);
        r.z += r.speed * dt;
        float rel = std::fmod(r.z - z + track_len * 100.0f, track_len);
        if (rel > track_len / 2) rel -= track_len;
        if (rel > 0) ahead++;
        if (std::fabs(rel) < 150.0f && std::fabs(r.x - px) < 0.32f) {        // bump
            speed *= (1.0f - 3.0f * dt);
            px += (px >= r.x ? 1.0f : -1.0f) * dt * 1.5f;
            shake = 0.25f;
        }
    }
    place_ = ahead + 1;
    shake = std::max(0.0f, shake - dt);
    flash = std::max(0.0f, flash - dt);
}

void Game::rect(uint32_t* fb, int x, int y, int w, int h, uint32_t col) const {
    int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(SCREEN_W, x + w), y1 = std::min(SCREEN_H, y + h);
    for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) fb[yy * SCREEN_W + xx] = col;
}

void Game::draw_text(uint32_t* fb, int x, int y, const char* s, int scale, uint32_t col) const {
    for (; *s; s++, x += 6 * scale) {
        const uint8_t* g = glyph(*s);
        for (int r = 0; r < 7; r++) for (int c = 0; c < 5; c++) if (g[r] & (1 << (4 - c))) rect(fb, x + c * scale, y + r * scale, scale, scale, col);
    }
}

void Game::draw_car(uint32_t* fb, int cx, int cy, int w, uint32_t body, bool lights) const {
    if (w < 3) return;
    int h = w * 5 / 8;
    rect(fb, cx - w / 2, cy - h, w, h * 7 / 10, body);                                  // body
    rect(fb, cx - w * 38 / 100, cy - h - h / 3, w * 76 / 100, h / 3, mix(body, rgb(20, 24, 32), 0.55f));   // cabin
    rect(fb, cx - w * 32 / 100, cy - h - h / 4, w * 64 / 100, h / 5, rgb(30, 50, 70));  // rear window
    rect(fb, cx - w / 2 - w / 12, cy - h / 3, w / 5, h / 3, rgb(12, 12, 14));            // tyres
    rect(fb, cx + w / 2 - w / 8, cy - h / 3, w / 5, h / 3, rgb(12, 12, 14));
    if (lights) { rect(fb, cx - w * 45 / 100, cy - h * 55 / 100, w / 6, h / 8 + 1, rgb(255, 40, 30)); rect(fb, cx + w * 45 / 100 - w / 6, cy - h * 55 / 100, w / 6, h / 8 + 1, rgb(255, 40, 30)); }
}

void Game::render(uint32_t* fb) {
    const int W = SCREEN_W, H = SCREEN_H;
    int sx = 0, sy = 0;
    if (shake > 0) { sx = int(std::sin(race_t * 90) * 6 * shake * 4); sy = int(std::cos(race_t * 77) * 4 * shake * 4); }
    // sky + mountains
    for (int y = 0; y < HORIZON; y++) {
        uint32_t c = mix(rgb(250, 140, 80), rgb(40, 70, 140), 1.0f - float(y) / HORIZON);
        for (int x = 0; x < W; x++) fb[y * W + x] = c;
    }
    float bend = curve_at(z + 1200);
    for (int x = 0; x < W; x++) {
        float u = x * 0.006f + z * 0.00002f * (bend + 0.2f);
        int hgt = int(60 + 38 * std::sin(u * 1.7f) + 22 * std::sin(u * 4.1f + 1.3f));
        for (int y = HORIZON - hgt; y < HORIZON; y++) if (y >= 0) fb[y * W + x] = mix(rgb(70, 50, 90), rgb(40, 30, 60), float(HORIZON - y) / std::max(1, hgt));
    }
    // road, row by row from the bottom (near) to the horizon (far)
    static float xoff[SCREEN_H], rw[SCREEN_H];
    float dx = 0, xo = 0;
    const float K = 90000.0f;                          // camera constant: distance d = K / (y - horizon)
    for (int y = H - 1; y > HORIZON; y--) {
        float d = K / float(y - HORIZON);
        float c = curve_at(z + d);
        dx += c * 0.0075f;
        xo += dx;
        xoff[y] = xo;
        rw[y] = 1.15f * W * 0.5f * float(y - HORIZON) / (H - HORIZON) * 0.92f;
        float w = rw[y];
        float cx = W * 0.5f - px * w + xo * float(y - HORIZON) / (H - HORIZON) * 120.0f * 0 + xo * 1.0f;
        float dist = z + d;
        bool stripe = (int(dist / 800.0f) & 1) != 0;
        float fog = std::min(1.0f, d / 42000.0f);
        uint32_t grass = mix(stripe ? rgb(58, 150, 70) : rgb(48, 134, 60), rgb(150, 110, 110), fog);
        uint32_t road = mix(stripe ? rgb(88, 90, 98) : rgb(80, 82, 90), rgb(150, 120, 120), fog);
        uint32_t rumble = mix(stripe ? rgb(235, 235, 235) : rgb(220, 40, 40), rgb(170, 130, 130), fog);
        uint32_t* row = fb + y * W;
        for (int x = 0; x < W; x++) row[x] = grass;
        int l = int(cx - w), r = int(cx + w), rb = std::max(1, int(w * 0.09f));
        for (int x = std::max(0, l - rb); x < std::min(W, r + rb); x++) row[x] = rumble;
        for (int x = std::max(0, l); x < std::min(W, r); x++) row[x] = road;
        if ((int(dist / 400.0f) & 1) == 0) { int lw = std::max(1, int(w * 0.02f)); for (int x = std::max(0, int(cx) - lw); x < std::min(W, int(cx) + lw); x++) row[x] = mix(rgb(240, 240, 240), rgb(170, 130, 130), fog); }
    }
    // rivals (far to near)
    std::vector<std::pair<float, int>> order;
    for (int i = 0; i < (int)rivals.size(); i++) {
        float rel = std::fmod(rivals[i].z - z + track_len * 100.0f, track_len);
        if (rel > track_len / 2) rel -= track_len;
        if (rel > 100 && rel < 40000) order.push_back({rel, i});
    }
    std::sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.first > b.first; });
    for (auto& o : order) {
        const Rival& r = rivals[o.second];
        int y = HORIZON + int(K / o.first);
        if (y <= HORIZON || y >= H) continue;
        float w = rw[y];
        int cx = int(W * 0.5f - px * w + xoff[y] + r.x * w);
        draw_car(fb, cx + sx, y + sy, int(w * 0.42f), r.color, true);
    }
    // player car
    int lean = int(std::clamp(px - px, 0.0f, 0.0f));
    (void)lean;
    draw_car(fb, W / 2 + sx, H - 60 + sy, 330, rgb(200, 30, 40), true);
    rect(fb, W / 2 - 150 + sx, H - 60 + sy, 300, 14, rgb(0, 0, 0));
    // --- HUD
    char buf[64];
    snprintf(buf, sizeof buf, "%d", int(speed_kmh()));
    draw_text(fb, 40, H - 110, buf, 9, rgb(255, 255, 255));
    draw_text(fb, 40, H - 40, "KMH", 4, rgb(255, 150, 60));
    snprintf(buf, sizeof buf, "LAP %d/%d", lap_, laps_total);
    draw_text(fb, 40, 30, buf, 5, rgb(255, 255, 255));
    snprintf(buf, sizeof buf, "POS %d/6", place_);
    draw_text(fb, 40, 80, buf, 5, rgb(255, 210, 60));
    snprintf(buf, sizeof buf, "%d:%02d.%d", int(race_t) / 60, int(race_t) % 60, int(race_t * 10) % 10);
    draw_text(fb, W - 260, 30, buf, 5, rgb(255, 255, 255));
    float ahead = upcoming_curve(2500);
    if (std::fabs(ahead) > 2.0f) {                                         // hairpin warning
        bool on = (int(race_t * 4) & 1) == 0;
        if (on) { draw_text(fb, W / 2 - 150, 120, ahead > 0 ? "HAIRPIN >" : "< HAIRPIN", 6, rgb(255, 60, 40)); }
    }
    if (state == MENU) {
        rect(fb, 0, 0, W, H, rgb(10, 12, 18));
        for (int y = 0; y < H; y += 4) rect(fb, 0, y, W, 2, mix(rgb(10, 12, 18), rgb(70, 30, 20), float(y) / H));
        draw_text(fb, 250, 200, "HAIRPIN RUSH", 12, rgb(255, 120, 30));
        draw_text(fb, 330, 340, "PS4 NATIVE - STAGE 1", 5, rgb(200, 200, 210));
        if (int(state_t * 2) & 1) draw_text(fb, 360, 500, "PRESS X TO START", 6, rgb(255, 255, 255));
    } else if (state == COUNTDOWN) {
        int n = 3 - int(state_t);
        snprintf(buf, sizeof buf, "%d", n);
        draw_text(fb, W / 2 - 40, 220, buf, 18, rgb(255, 230, 80));
    } else if (state == FINISH) {
        rect(fb, W / 2 - 330, 200, 660, 280, rgb(10, 12, 18));
        draw_text(fb, W / 2 - 270, 230, "FINISH!", 12, rgb(255, 210, 60));
        snprintf(buf, sizeof buf, "POS %d/6", place_);
        draw_text(fb, W / 2 - 150, 340, buf, 6, rgb(255, 255, 255));
        snprintf(buf, sizeof buf, "%d:%02d.%d", int(race_t) / 60, int(race_t) % 60, int(race_t * 10) % 10);
        draw_text(fb, W / 2 - 130, 400, buf, 6, rgb(255, 255, 255));
    }
}
