// Copyright (c) 2026 Sabak Studios. All rights reserved.
// Host test (no SDL, no PS4): menu navigation, button remapping, a scripted race, timing, PPM screenshots.
//   ./host_test [out_dir]   -> exit 0 when everything passes
#include "game.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } else printf("ok:   %s\n", msg); } while (0)

static void save_ppm(const char* dir, const char* name, int w, int h, const std::vector<uint32_t>& fb) {
    if (!dir) return;
    char path[256]; snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (uint32_t p : fb) { unsigned char c[3] = {(unsigned char)(p >> 16), (unsigned char)(p >> 8), (unsigned char)p}; fwrite(c, 1, 3, f); }
    fclose(f);
}
static void shot(Game& g, const char* dir, const char* name) {
    std::vector<uint32_t> fb(g.render_w() * g.render_h());
    g.render(fb.data());
    save_ppm(dir, name, g.render_w(), g.render_h(), fb);
}
static void tap(Game& g, uint32_t bits) {         // one frame pressed, one released
    PadState p; p.down = bits; g.update(1 / 60.0f, p);
    PadState r; g.update(1 / 60.0f, r);
}

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : nullptr;
    Game g;
    shot(g, out, "menu");
    // --- menu: go to CONTROLS, remap ACCELERATE (R2) -> L1, expect swap-free change
    // garage first: choose car 3 and paint 4 (second row)
    tap(g, B_DOWN);
    CHECK(g.menu_sel == 1, "menu: D-pad down moves selection");
    tap(g, B_CROSS);
    CHECK(g.state == Game::GARAGE, "menu: GARAGE opens");
    tap(g, B_RIGHT); tap(g, B_RIGHT); tap(g, B_RIGHT);
    CHECK(g.car_sel == 3, "garage: car selection changes");
    shot(g, out, "garage");
    tap(g, B_DOWN); for (int i = 0; i < 4; i++) tap(g, B_RIGHT);
    CHECK(g.paint_sel == 4, "garage: paint selection changes");
    tap(g, B_CIRCLE);
    CHECK(g.state == Game::MENU, "garage: back to menu");
    tap(g, B_DOWN);
    CHECK(g.menu_sel == 2, "menu: down to CONTROLS");
    tap(g, B_CROSS);
    CHECK(g.state == Game::CONTROLS, "menu: CONTROLS opens");
    shot(g, out, "controls");
    tap(g, B_CROSS);
    CHECK(g.capturing, "controls: capture starts");
    shot(g, out, "controls_capture");
    tap(g, B_L1);
    CHECK(!g.capturing && g.binding(A_GAS) == B_L1, "remap: ACCELERATE is now L1");
    // swap on conflict: bind BRAKE to L1 -> GAS must take BRAKE's old button (L2)
    tap(g, B_DOWN); tap(g, B_CROSS); tap(g, B_L1);
    CHECK(g.binding(A_BRAKE) == B_L1 && g.binding(A_GAS) == B_L2, "remap: conflict swaps buttons");
    // reset to defaults row
    for (int i = 0; i < A_COUNT - 1; i++) tap(g, B_DOWN);
    tap(g, B_CROSS);
    CHECK(g.binding(A_GAS) == B_R2 && g.binding(A_BRAKE) == B_L2, "controls: reset to defaults");
    tap(g, B_DOWN); tap(g, B_CROSS);                  // BACK row
    CHECK(g.state == Game::MENU, "controls: BACK row returns to menu");
    tap(g, B_CROSS); tap(g, B_CROSS); tap(g, B_R1);   // reopen controls, remap ACCELERATE to R1
    CHECK(g.binding(A_GAS) == B_R1, "remap again: ACCELERATE = R1");
    tap(g, B_CIRCLE);
    CHECK(g.state == Game::MENU, "circle goes back");
    // save / load settings round trip
    std::string blob = g.save_blob();
    Game g2; g2.load_blob(blob);
    CHECK(g2.binding(A_GAS) == B_R1 && g2.car_sel == 3 && g2.paint_sel == 4, "settings blob round-trips (buttons, car, paint)");
    // graphics + laps items
    tap(g, B_DOWN);
    CHECK(g.menu_sel == 3, "menu navigation moves");
    g.menu_sel = 3; int q0 = g.get_quality(); tap(g, B_RIGHT);
    CHECK(g.get_quality() != q0, "graphics option changes");
    g.set_quality(2);
    g.menu_sel = 0; tap(g, B_CROSS);
    CHECK(g.state == Game::COUNTDOWN, "start race");
    // --- race with the scripted driver, using the remapped gas button (R1)
    const float dt = 1 / 60.0f;
    for (int i = 0; i < 200; i++) { PadState p; g.update(dt, p); }
    CHECK(g.state == Game::RACE, "countdown ends");
    shot(g, out, "countdown");
    int shots = 0;
    for (int f = 0; f < 60 * 240 && !g.finished(); f++) {
        float v = g.speed_kmh() / 3.6f;
        float cmax = 0;
        for (float m = 8; m <= 70; m += 6) cmax = std::max(cmax, fabsf(g.upcoming_curve(m)));
        float vt = cmax > 1e-3f ? sqrtf(8.5f / cmax) : 80.0f;
        PadState p;
        bool gas = v < vt + 1.0f, brk = v > vt + 4.0f;
        if (gas) p.down |= g.binding(A_GAS);
        if (brk) p.l2 = 1.0f;
        float auth = 1.7f / (1.0f + v / 38.0f);
        float st = -g.upcoming_curve(2.0f) * v / auth + 0.12f * g.lateral() + 1.1f * g.yaw_dbg();
        p.lx = st < -1 ? -1 : (st > 1 ? 1 : st);
        g.update(dt, p);
        if ((f == 700 || f == 1500 || f == 2300 || f == 3000) && shots < 4) {
            char n[32]; snprintf(n, sizeof n, "race%d", shots++); shot(g, out, n);
            printf("   shot f=%d kmh=%.0f prog=%.0f%% pos=%d d=%.1f tris=%zu\n", f, g.speed_kmh(), g.progress() * 100, g.place(), g.lateral(), g.last_tri_count());
        }
        if (f == 900) { bool was = g.first_person(); shot(g, out, "cockpit"); tap(g, g.binding(A_CAMERA)); CHECK(g.first_person() != was, "camera button toggles view"); shot(g, out, "chase"); tap(g, g.binding(A_CAMERA)); CHECK(g.first_person() == was, "camera toggles back"); }
        if (f == 1800) { tap(g, g.binding(A_PAUSE)); CHECK(g.state == Game::PAUSED, "pause opens"); shot(g, out, "pause"); tap(g, B_CROSS); CHECK(g.state == Game::RACE, "resume works"); }
    }
    printf("   finished=%d prog=%.0f%% time=%.1f s pos=%d\n", g.finished(), g.progress() * 100, g.time_s(), g.place());
    CHECK(g.finished(), "stage finishes with the scripted driver");
    CHECK(Game().first_person(), "first-person camera is the default");
    for (int i = 0; i < 120; i++) { PadState p; g.update(dt, p); }
    shot(g, out, "finish");
    // --- timing of one frame at each internal resolution (host CPU; the PS4 Jaguar CPU is several times slower)
    for (int thr = 1; thr <= 2; thr++) for (int q = 0; q < 3; q++) {
        Game t; t.set_quality(q); t.set_threads(thr); t.menu_sel = 0; tap(t, B_CROSS);
        for (int i = 0; i < 400; i++) { PadState p; p.down = B_R2; p.r2 = 1; t.update(dt, p); }
        std::vector<uint32_t> fb(t.render_w() * t.render_h());
        t.render(fb.data());
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 20; i++) t.render(fb.data());
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 20;
        printf("TIME %dx%d threads=%d : %.2f ms/frame (host) tris=%zu\n", t.render_w(), t.render_h(), thr, ms, t.last_tri_count());
    }
    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
