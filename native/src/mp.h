// Copyright (c) 2026 Sabak Studios. All rights reserved.
// Local split-screen: up to 4 Game panes (one per player / controller) composed into a 1920x1080 frame.
#pragma once
#include "game.h"
#include <cstring>
#include <memory>

struct PaneRect { int x, y, w, h; };

// 2 players: one above the other. 3 players: one on top, two below. 4 players: 2x2.
inline int mp_layout(int n, PaneRect* r) {
    const int W = OUT_W, H = OUT_H;
    if (n <= 1) { r[0] = {0, 0, W, H}; return 1; }
    if (n == 2) { r[0] = {0, 0, W, H / 2}; r[1] = {0, H / 2, W, H / 2}; return 2; }
    if (n == 3) { r[0] = {0, 0, W, H / 2}; r[1] = {0, H / 2, W / 2, H / 2}; r[2] = {W / 2, H / 2, W / 2, H / 2}; return 3; }
    r[0] = {0, 0, W / 2, H / 2}; r[1] = {W / 2, 0, W / 2, H / 2}; r[2] = {0, H / 2, W / 2, H / 2}; r[3] = {W / 2, H / 2, W / 2, H / 2};
    return 4;
}

class Multi {
public:
    Game lead;                                           // player 1: owns the menus
    std::unique_ptr<Game> extra[3];
    int count = 1;                                       // active panes (1 = normal single screen)
    PaneRect rect[4];
    int serial = -1;

    bool active() const { return count > 1; }
    Game& pane(int i) { return i == 0 ? lead : *extra[i - 1]; }

    // call every frame with one pad state per player. Returns false while single screen (use lead.* as before).
    void update(float dt, const PadState* pads) {
        lead.update(dt, pads[0]);
        if (count == 1) { if (lead.take_mp_launch()) start(lead.mp_players); return; }
        if (lead.state == Game::MENU) { count = 1; lead.set_viewport(0, 0); lead.set_others({}); return; }   // leader went back to the main menu
        if (lead.race_serial() != serial) restart();      // leader restarted the race (RACE AGAIN / RESTART)
        bool paused = lead.state == Game::PAUSED || lead.state == Game::CONTROLS;
        for (int i = 1; i < count; i++) extra[i - 1]->update(paused ? 0.0f : dt, pads[i]);
        for (int i = 0; i < count; i++) {                 // everybody sees everybody
            std::vector<Rival> o;
            for (int j = 0; j < count; j++) if (j != i) o.push_back(pane(j).self_info());
            pane(i).set_others(o);
        }
    }

    void start(int n) {
        count = n < 1 ? 1 : (n > 4 ? 4 : n);
        mp_layout(count, rect);
        for (int i = 1; i < count; i++) if (!extra[i - 1]) { extra[i - 1].reset(new Game()); extra[i - 1]->set_follower(true); extra[i - 1]->set_threads(1); }
        for (int i = 0; i < count; i++) pane(i).set_viewport(rect[i].w, rect[i].h);
        restart();
    }
    void restart() {
        serial = lead.race_serial();
        for (int i = 1; i < count; i++) {
            extra[i - 1]->renderer().gpu = lead.renderer().gpu;
            extra[i - 1]->mp_begin(lead.stage_sel, (lead.car_sel + i * 3) % CAR_MESH_COUNT, (lead.paint_sel + i * 4) % 15, &lead);
        }
    }
    // draw every pane into its own buffer, then compose into a single 1920x1080 ARGB (premultiplied) frame
    void render(std::vector<uint32_t>& out, std::vector<std::vector<uint32_t>>& tmp) {
        if (count == 1) return;
        out.assign(size_t(OUT_W) * OUT_H, 0);
        tmp.resize(4);
        for (int i = 0; i < count; i++) {
            Game& g = pane(i);
            tmp[i].assign(size_t(rect[i].w) * rect[i].h, 0);
            g.render(tmp[i].data());
            for (int y = 0; y < rect[i].h; y++) memcpy(&out[size_t(rect[i].y + y) * OUT_W + rect[i].x], &tmp[i][size_t(y) * rect[i].w], size_t(rect[i].w) * 4);
        }
        // thin separators
        for (int i = 0; i < count; i++) {
            for (int x = 0; x < rect[i].w; x++) { out[size_t(rect[i].y) * OUT_W + rect[i].x + x] = 0xFF000000u; out[size_t(rect[i].y + rect[i].h - 1) * OUT_W + rect[i].x + x] = 0xFF000000u; }
            for (int y = 0; y < rect[i].h; y++) { out[size_t(rect[i].y + y) * OUT_W + rect[i].x] = 0xFF000000u; out[size_t(rect[i].y + y) * OUT_W + rect[i].x + rect[i].w - 1] = 0xFF000000u; }
        }
    }
};
