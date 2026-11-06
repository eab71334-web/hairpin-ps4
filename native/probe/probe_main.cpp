// Copyright (c) 2026 Sabak Studios. GPU feasibility probe for PS4 (OpenOrbis). Draws a report on screen + /data/gpu_probe.txt.
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include "font.h"

extern "C" {
int sceKernelLoadStartModule(const char* name, size_t argc, const void* argv, unsigned flags, void* opt, int* res);
int sceKernelDlsym(int handle, const char* sym, void** addr);
}

static std::vector<std::string> L;
static void add(const char* fmt, const std::string& a = "", int v = 0, bool hasv = false) {
    char b[160];
    if (hasv) snprintf(b, sizeof b, fmt, a.c_str(), v); else snprintf(b, sizeof b, fmt, a.c_str());
    L.push_back(b);
}
static bool exists(const char* p) { FILE* f = fopen(p, "rb"); if (!f) return false; fclose(f); return true; }

static void probe_module(const char* path, const char* sym) {
    bool ex = exists(path);
    int res = 0;
    int h = sceKernelLoadStartModule(path, 0, nullptr, 0, nullptr, &res);
    char b[200];
    snprintf(b, sizeof b, "%s %s h=%d", ex ? "FILE" : "NOFILE", path + (strlen(path) > 34 ? strlen(path) - 34 : 0), h);
    L.push_back(b);
    if (h > 0 && sym) {
        void* a = nullptr;
        int r = sceKernelDlsym(h, sym, &a);
        snprintf(b, sizeof b, "  DLSYM %s r=%d %s", sym, r, a ? "OK" : "NULL");
        L.push_back(b);
    }
}

int main() {
    L.push_back("HAIRPIN RUSH GPU PROBE");
    probe_module("/system/common/lib/libScePigletv2VSH.sprx", "eglGetDisplay");
    probe_module("/system/common/lib/libSceShaccVSH.sprx", nullptr);
    probe_module("/data/self/system/common/lib/libScePigletv2VSH.sprx", "eglGetDisplay");
    probe_module("/data/self/system/common/lib/libSceShaccVSH.sprx", nullptr);
    probe_module("/system/common/lib/libSceGnmDriver.sprx", "sceGnmSubmitDone");
    probe_module("/system/common/lib/libSceVideoOut.sprx", nullptr);
    L.push_back("DONE. SEND THIS SCREEN TO CLAUDE");
    if (FILE* f = fopen("/data/gpu_probe.txt", "wb")) { for (auto& s : L) { fputs(s.c_str(), f); fputc('\n', f); } fclose(f); }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    SDL_Window* win = SDL_CreateWindow("probe", 0, 0, 1920, 1080, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 1920, 1080);
    std::vector<uint32_t> fb(1920 * 1080, 0xFF101828u);
    const int S = 4;
    for (size_t i = 0; i < L.size(); i++)
        for (size_t k = 0; k < L[i].size(); k++) {
            const uint8_t* g = glyph(L[i][k]);
            for (int y = 0; y < 7; y++) for (int x = 0; x < 5; x++) if (g[y] & (0x10 >> x))
                for (int dy = 0; dy < S; dy++) for (int dx = 0; dx < S; dx++) {
                    int px = 40 + int(k) * 6 * S + x * S + dx, py = 40 + int(i) * 11 * S + y * S + dy;
                    if (px < 1920 && py < 1080) fb[size_t(py) * 1920 + px] = 0xFFFFFFFFu;
                }
        }
    for (;;) {
        SDL_UpdateTexture(tex, nullptr, fb.data(), 1920 * 4);
        SDL_RenderClear(ren); SDL_RenderCopy(ren, tex, nullptr, nullptr); SDL_RenderPresent(ren);
    }
}
