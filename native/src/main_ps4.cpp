// Copyright (c) 2026 Sabak Studios. All rights reserved.
// PS4 platform layer (OpenOrbis toolchain): SDL2 window + scaling, ScePad for the DualShock 4, settings file in /data.
// Built by .github/workflows/ps4-native.yml. Never run on a console by the author - fix API-name issues here only.
#include "game.h"
#include <SDL2/SDL.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

static const char* CFG_PATH = "/data/hairpin_rush.cfg";

static float axis_norm(uint8_t v) {                       // 0..255 -> -1..1 with a dead zone
    float f = (float(v) - 127.5f) / 127.5f;
    return fabsf(f) < 0.12f ? 0.0f : f;
}

static void load_cfg(Game& g) {
    FILE* f = fopen(CFG_PATH, "rb");
    if (!f) return;
    char buf[200] = {0};
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    if (n) g.load_blob(std::string(buf, n));
}
static void save_cfg(const Game& g) {
    FILE* f = fopen(CFG_PATH, "wb");
    if (!f) return;
    std::string s = g.save_blob();
    fwrite(s.data(), 1, s.size(), f);
    fclose(f);
}

int main() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");      // bilinear upscale of the internal 540p/720p image to 1080p
    SDL_Window* win = SDL_CreateWindow("Hairpin Rush", 0, 0, OUT_W, OUT_H, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture* tex = nullptr;
    int tw = 0, th = 0;

    sceUserServiceInitialize(nullptr);
    int userId = 0;
    sceUserServiceGetInitialUser(&userId);
    scePadInit();
    int pad = scePadOpen(userId, 0, 0, nullptr);

    Game game;
    game.set_threads(6);                                        // the PS4 has 8 Jaguar cores (6-7 usable by a game)
    load_cfg(game);
    std::vector<uint32_t> fb;
    uint32_t last = SDL_GetTicks();
    for (;;) {
        PadState ps;
        OrbisPadData d;
        if (scePadReadState(pad, &d) == 0) {
            uint32_t b = d.buttons, o = 0;
            if (b & ORBIS_PAD_BUTTON_CROSS) o |= B_CROSS;
            if (b & ORBIS_PAD_BUTTON_CIRCLE) o |= B_CIRCLE;
            if (b & ORBIS_PAD_BUTTON_SQUARE) o |= B_SQUARE;
            if (b & ORBIS_PAD_BUTTON_TRIANGLE) o |= B_TRIANGLE;
            if (b & ORBIS_PAD_BUTTON_L1) o |= B_L1;
            if (b & ORBIS_PAD_BUTTON_R1) o |= B_R1;
            if (b & ORBIS_PAD_BUTTON_L3) o |= B_L3;
            if (b & ORBIS_PAD_BUTTON_R3) o |= B_R3;
            if (b & ORBIS_PAD_BUTTON_OPTIONS) o |= B_OPTIONS;
            if (b & ORBIS_PAD_BUTTON_TOUCH_PAD) o |= B_TOUCHPAD;
            if (b & ORBIS_PAD_BUTTON_UP) o |= B_UP;
            if (b & ORBIS_PAD_BUTTON_DOWN) o |= B_DOWN;
            if (b & ORBIS_PAD_BUTTON_LEFT) o |= B_LEFT;
            if (b & ORBIS_PAD_BUTTON_RIGHT) o |= B_RIGHT;
            ps.down = o;
            ps.lx = axis_norm(d.leftStick.x);
            ps.ly = axis_norm(d.leftStick.y);
            ps.l2 = d.analogButtons.l2 / 255.0f;
            ps.r2 = d.analogButtons.r2 / 255.0f;
        }
        uint32_t now = SDL_GetTicks();
        float dt = fminf(0.05f, (now - last) / 1000.0f);
        last = now;
        game.update(dt, ps);
        if (game.take_dirty()) save_cfg(game);
        int w = game.render_w(), h = game.render_h();
        if (w != tw || h != th) {                                   // quality changed -> new internal resolution
            if (tex) SDL_DestroyTexture(tex);
            tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
            fb.assign(size_t(w) * h, 0);
            tw = w; th = h;
        }
        game.render(fb.data());
        SDL_UpdateTexture(tex, nullptr, fb.data(), w * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);
    }
    return 0;
}
