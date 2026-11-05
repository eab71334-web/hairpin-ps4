// Copyright (c) 2026 Sabak Studios. All rights reserved.
// PS4 platform layer (OpenOrbis toolchain): SDL2 for the window / framebuffer, ScePad for the DualShock 4.
// NOT verified on a console yet - built by .github/workflows/ps4-native.yml. If the OpenOrbis API names differ, fix them here only.
#include "game.h"
#include <SDL2/SDL.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <cstdio>
#include <cmath>
#include <vector>

static float stick(uint8_t v) {                       // 0..255 -> -1..1 with a dead zone
    float f = (float(v) - 127.5f) / 127.5f;
    return std::fabs(f) < 0.12f ? 0.0f : f;
}

int main() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    SDL_Window* win = SDL_CreateWindow("Hairpin Rush", 0, 0, SCREEN_W, SCREEN_H, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);

    sceUserServiceInitialize(nullptr);
    int userId = 0;
    sceUserServiceGetInitialUser(&userId);
    scePadInit();
    int pad = scePadOpen(userId, 0, 0, nullptr);

    Game game;
    std::vector<uint32_t> fb(SCREEN_W * SCREEN_H);
    uint32_t last = SDL_GetTicks();
    bool prev_cross = false, prev_start = false;
    for (;;) {
        OrbisPadData d;
        PadState ps;
        if (scePadReadState(pad, &d) == 0) {
            ps.steer = stick(d.leftStick.x);
            if (d.buttons & ORBIS_PAD_BUTTON_LEFT) ps.steer = -1.0f;
            if (d.buttons & ORBIS_PAD_BUTTON_RIGHT) ps.steer = 1.0f;
            ps.gas = std::fmax(d.analogButtons.r2 / 255.0f, (d.buttons & ORBIS_PAD_BUTTON_CROSS) ? 1.0f : 0.0f);
            ps.brake = std::fmax(d.analogButtons.l2 / 255.0f, (d.buttons & ORBIS_PAD_BUTTON_SQUARE) ? 1.0f : 0.0f);
            bool cross = d.buttons & ORBIS_PAD_BUTTON_CROSS, start = d.buttons & ORBIS_PAD_BUTTON_OPTIONS;
            ps.confirm = cross && !prev_cross;
            ps.start = start && !prev_start;
            prev_cross = cross;
            prev_start = start;
        }
        uint32_t now = SDL_GetTicks();
        float dt = std::fmin(0.05f, (now - last) / 1000.0f);
        last = now;
        game.update(dt, ps);
        game.render(fb.data());
        SDL_UpdateTexture(tex, nullptr, fb.data(), SCREEN_W * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);
    }
    return 0;
}
