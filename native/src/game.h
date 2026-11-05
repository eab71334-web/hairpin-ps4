// Copyright (c) 2026 Sabak Studios. All rights reserved.
#pragma once
#include "platform.h"
#include <cstdint>
#include <vector>

// HAIRPIN RUSH (PS4 native, stage 1): a software-rendered pseudo-3D road racer with hairpin bends and 5 rivals.
struct Rival { float z; float x; float speed; uint32_t color; };

class Game {
public:
    Game();
    void update(float dt, const PadState& pad);
    void render(uint32_t* fb);               // SCREEN_W x SCREEN_H, 0xAARRGGBB
    // read-only state (for tests / HUD)
    float speed_kmh() const { return speed / 100.0f; }
    int lap() const { return lap_; }
    int place() const { return place_; }
    bool finished() const { return state == FINISH; }
    float time_s() const { return race_t; }
    float player_x() const { return px; }
    float upcoming_curve(float ahead) const;
    enum State { MENU, COUNTDOWN, RACE, FINISH } state = MENU;
    float state_t = 0;
    int laps_total = 3;
private:
    float curve_at(float z) const;
    void draw_text(uint32_t* fb, int x, int y, const char* s, int scale, uint32_t col) const;
    void draw_car(uint32_t* fb, int cx, int cy, int w, uint32_t body, bool rear_lights) const;
    void rect(uint32_t* fb, int x, int y, int w, int h, uint32_t col) const;
    std::vector<float> curves;               // one entry per road segment
    float track_len = 0;
    float z = 0;                             // player distance along the track
    float px = 0;                            // lateral position -1..1 (road edges)
    float speed = 0;                         // units/s (100 units = 1 km/h)
    float race_t = 0;
    int lap_ = 1, place_ = 6;
    float lap_z0 = 0;
    std::vector<Rival> rivals;
    float flash = 0;
    float shake = 0;
};
