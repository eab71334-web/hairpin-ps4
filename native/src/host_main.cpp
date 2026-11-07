// Copyright (c) 2026 Sabak Studios. All rights reserved.
// Host test (no SDL, no PS4): menu navigation, button remapping, a scripted race, timing, PPM screenshots.
//   ./host_test [out_dir]   -> exit 0 when everything passes
#include "game.h"
#include "mp.h"
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
        float vt = 80.0f;                                                  // braking-distance-aware corner speed (planned decel 7 m/s^2)
        for (float m = 0; m <= 160; m += 8) { float c = fabsf(g.upcoming_curve(m)); if (c > 1e-3f) vt = std::min(vt, sqrtf(5.0f / c + 2.0f * 7.0f * m)); }
        PadState p;
        bool gas = v < vt + 1.0f, brk = v > vt + 1.5f;
        if (gas) p.down |= g.binding(A_GAS);
        if (brk) p.l2 = 1.0f;
        // lane-keeping driver for the slip-based physics: feed-forward from road curvature + feedback on lateral offset and heading
        float vv = std::max(v, 3.0f), curv0 = g.upcoming_curve(0.0f) * 0.6f + g.upcoming_curve(8.0f) * 0.4f;
        float wz_des = curv0 * vv - (1.26f * g.yaw_dbg() + 0.5f * g.lateral() / vv);       // critically damped lane-keeping (d'' = v*yaw')
        float delta = wz_des * 2.55f / vv;                                  // steering angle, left +
        float lock = Game::steer_lock_pub(vv, g.grip_mu());
        float un = std::max(-1.0f, std::min(1.0f, delta / lock));
        float st = -(un < 0 ? -1.0f : 1.0f) * powf(fabsf(un), 1.0f / 1.35f);
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
    // --- GPU mode: 3D collected as view-space triangles + palette UVs; fb becomes a transparent 2D layer. Emulate the GPU on the CPU to check the data.
    {
        Game t; t.set_quality(1); t.set_threads(1); t.menu_sel = 0; tap(t, B_CROSS);
        for (int i = 0; i < 400; i++) { PadState p; p.down = B_R2; p.r2 = 1; t.update(dt, p); }
        t.renderer().gpu = true;
        int W = t.render_w(), H = t.render_h();
        std::vector<uint32_t> fb(W * H, 0xDEADBEEFu);
        t.render(fb.data());
        const std::vector<float>& gv = t.renderer().gpu_verts();
        size_t ntri = gv.size() / 15;
        CHECK(ntri > 500, "gpu mode: 3D triangles collected for the GPU");
        bool uvok = true; for (size_t i = 0; i < gv.size(); i += 5) if (gv[i + 3] < 0 || gv[i + 3] > 1 || gv[i + 4] < 0 || gv[i + 4] > 1 || gv[i + 2] >= 0) uvok = false;
        CHECK(uvok, "gpu mode: palette UVs in range and vertices in front of the camera");
        size_t transparent = 0; for (uint32_t p : fb) if (p == 0) transparent++;
        CHECK(transparent > fb.size() / 2, "gpu mode: 2D layer is mostly transparent (3D not rasterized on the CPU)");
        // emulate: sky gradient, then triangles with z-buffer, then the premultiplied 2D layer
        Renderer& R = t.renderer(); float f = R.focal();
        std::vector<uint32_t> img(W * H); std::vector<float> zb(W * H, 0.f);
        auto mixc = [](uint32_t a, uint32_t b, float k) { int o = 0; for (int s = 0; s <= 16; s += 8) { int x = (a >> s) & 255, y = (b >> s) & 255; o |= int(x + (y - x) * k) << s; } return 0xFF000000u | o; };
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { float k = float(y) / H; img[y * W + x] = mixc(R.sky_top(), R.sky_horizon(), k < 0.5f ? k * 2 : 1.f); }
        for (size_t i = 0; i < ntri; i++) {
            const float* v = &gv[i * 15];
            float X[3], Y[3], Z[3];
            for (int k = 0; k < 3; k++) { Z[k] = -v[k * 5 + 2]; X[k] = W * 0.5f + f * v[k * 5] / Z[k]; Y[k] = H * 0.5f - f * v[k * 5 + 1] / Z[k]; }
            int idx = int(v[4] * 1024) * 1024 + int(v[3] * 1024);
            int r7 = (idx >> 13) & 127, g7 = (idx >> 6) & 127, b6 = idx & 63;
            uint32_t col = 0xFF000000u | (uint32_t(b6 * 255 / 63) << 0) | (uint32_t(g7 * 255 / 127) << 8) | (uint32_t(r7 * 255 / 127) << 16);
            int x0 = std::max(0, int(std::min(X[0], std::min(X[1], X[2])))), x1 = std::min(W - 1, int(std::max(X[0], std::max(X[1], X[2]))) + 1);
            int y0 = std::max(0, int(std::min(Y[0], std::min(Y[1], Y[2])))), y1 = std::min(H - 1, int(std::max(Y[0], std::max(Y[1], Y[2]))) + 1);
            float den = (Y[1] - Y[2]) * (X[0] - X[2]) + (X[2] - X[1]) * (Y[0] - Y[2]); if (fabsf(den) < 1e-6f) continue;
            for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
                float px = x + 0.5f, py = y + 0.5f;
                float a = ((Y[1] - Y[2]) * (px - X[2]) + (X[2] - X[1]) * (py - Y[2])) / den, b = ((Y[2] - Y[0]) * (px - X[2]) + (X[0] - X[2]) * (py - Y[2])) / den, c = 1 - a - b;
                if (a < 0 || b < 0 || c < 0) continue;
                float iz = a / Z[0] + b / Z[1] + c / Z[2];
                if (iz > zb[y * W + x]) { zb[y * W + x] = iz; img[y * W + x] = col; }
            }
        }
        for (int i = 0; i < W * H; i++) { uint32_t p = fb[i]; int a = p >> 24; if (!a) continue; uint32_t d = img[i]; int ia = 255 - a;
            int r = ((p >> 16) & 255) + (((d >> 16) & 255) * ia) / 255, g = ((p >> 8) & 255) + (((d >> 8) & 255) * ia) / 255, b = (p & 255) + ((d & 255) * ia) / 255;
            img[i] = 0xFF000000u | (std::min(255, r) << 16) | (std::min(255, g) << 8) | std::min(255, b); }
        if (out) { char path[512]; snprintf(path, sizeof path, "%s/gpu_emulated_race.ppm", out); FILE* fp = fopen(path, "wb"); if (fp) { fprintf(fp, "P6\n%d %d\n255\n", W, H); for (int i = 0; i < W * H; i++) { uint8_t c[3] = {uint8_t(img[i] >> 16), uint8_t(img[i] >> 8), uint8_t(img[i])}; fwrite(c, 1, 3, fp); } fclose(fp); } }
    }
    // --- split screen: 2, 3, 4 players
    for (int np = 2; np <= 4; np++) {
        static Multi M2; Multi& M = M2;
        M.count = 1; M.lead.set_viewport(0, 0); M.lead.state = Game::MENU; M.lead.menu_sel = 7; M.lead.mp_players = np; M.lead.set_others({});
        PadState pads[4], idle; M.update(1 / 60.0f, pads);
        M.lead.menu_sel = 0; pads[0].down = B_CROSS; M.update(1 / 60.0f, pads); pads[0] = idle; M.update(1 / 60.0f, pads);
        char m[64]; snprintf(m, sizeof m, "split screen %d players starts", np);
        CHECK(M.count == np && M.lead.state == Game::COUNTDOWN, m);
        for (int i = 0; i < 400; i++) { for (int k = 0; k < 4; k++) { pads[k].down = B_R2; pads[k].r2 = 1; } pads[1].lx = 0.0f; M.update(1 / 60.0f, pads); }
        bool moved = true; for (int i = 0; i < np; i++) moved = moved && M.pane(i).speed_kmh() > 30;
        snprintf(m, sizeof m, "split screen %d players: every car is driving", np); CHECK(moved, m);
        std::vector<uint32_t> mpo; std::vector<std::vector<uint32_t>> tmp;
        auto t0 = std::chrono::steady_clock::now(); M.render(mpo, tmp);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        printf("TIME split %d players CPU: %.1f ms\n", np, ms);
        char nm[32]; snprintf(nm, sizeof nm, "split_%d", np); save_ppm(out, nm, OUT_W, OUT_H, mpo);
        // leader back to the main menu ends the split screen
        M.lead.state = Game::MENU; M.update(1 / 60.0f, pads);
        snprintf(m, sizeof m, "split screen %d players: back to single screen", np); CHECK(M.count == 1, m);
    }
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
