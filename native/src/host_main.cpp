// Copyright (c) 2026 Sabak Studios. All rights reserved.
// Host test (no SDL, no PS4): runs the game with a scripted driver and writes PPM screenshots.
//   ./host_test [out_dir]   -> exit code 0 when the race finishes
#include "game.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

static void save_ppm(const char* path, const std::vector<uint32_t>& fb) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    for (uint32_t p : fb) { unsigned char c[3] = {(unsigned char)(p >> 16), (unsigned char)(p >> 8), (unsigned char)p}; fwrite(c, 1, 3, f); }
    fclose(f);
}

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : nullptr;
    Game g;
    std::vector<uint32_t> fb(SCREEN_W * SCREEN_H);
    PadState pad;
    g.render(fb.data());
    if (out) { char p[256]; snprintf(p, sizeof p, "%s/menu.ppm", out); save_ppm(p, fb); }
    pad.confirm = true; g.update(1 / 60.0f, pad); pad.confirm = false;
    const float dt = 1 / 60.0f;
    int shots = 0;
    for (int f = 0; f < 60 * 60 * 6 && !g.finished(); f++) {
        // simple driver: look ahead, brake for hairpins, steer against the bend's push and towards the lane centre
        float c1 = g.upcoming_curve(600), c2 = g.upcoming_curve(2500);
        pad.gas = (std::fabs(c2) > 2.0f && g.speed_kmh() > 95) ? 0.0f : 1.0f;
        pad.brake = (std::fabs(c2) > 2.0f && g.speed_kmh() > 105) ? 1.0f : 0.0f;
        if (std::fabs(c1) < 2.0f && g.speed_kmh() > 140 && std::fabs(c2) < 2.0f) pad.gas = 0.7f;
        pad.steer = std::clamp(-g.player_x() * 1.2f + c1 * 0.5f * (g.speed_kmh() / 220.0f), -1.0f, 1.0f);
        g.update(dt, pad);
        if (out && (f == 300 || f == 1100 || f == 1900 || f == 2700) && shots < 4) {
            g.render(fb.data());
            char p[256]; snprintf(p, sizeof p, "%s/race%d.ppm", out, shots++); save_ppm(p, fb);
            printf("shot f=%d kmh=%.0f lap=%d pos=%d x=%.2f\n", f, g.speed_kmh(), g.lap(), g.place(), g.player_x());
        }
    }
    g.render(fb.data());
    if (out) { char p[256]; snprintf(p, sizeof p, "%s/end.ppm", out); save_ppm(p, fb); }
    printf("finished=%d lap=%d pos=%d time=%.1f\n", g.finished(), g.lap(), g.place(), g.time_s());
    return g.finished() ? 0 : 1;
}
