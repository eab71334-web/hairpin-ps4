// Copyright (c) 2026 Sabak Studios. All rights reserved.
#pragma once
#include "platform.h"
#include <cstdint>
#include <string>
#include <vector>

// HAIRPIN RUSH (PS4 native): software-rendered pseudo-3D road racer with hairpin bends, 5 rivals,
// AAA-style HUD / menus and a button remapping screen.
enum Action { A_GAS, A_BRAKE, A_HANDBRAKE, A_RESET, A_PAUSE, A_CAMERA, A_COUNT };
struct Rival { float z; float x; float speed; int model; int paint; };
struct Cv;

class Game {
public:
    Game();
    void update(float dt, const PadState& pad);
    void render(uint32_t* fb);                 // render_w() x render_h(), 0xAARRGGBB
    int render_w() const;
    int render_h() const;
    void set_quality(int q) { quality = q < 0 ? 0 : (q > 2 ? 2 : q); dirty = true; }
    int get_quality() const { return quality; }
    bool first_person() const { return fpv; }
    uint32_t binding(int a) const { return bind[a]; }
    std::string save_blob() const;             // settings -> text (platform stores it in /data)
    void load_blob(const std::string& s);
    bool take_dirty() { bool d = dirty; dirty = false; return d; }
    // read-only state (tests / HUD)
    float speed_kmh() const { return speed / 100.0f; }
    int lap() const { return lap_; }
    int place() const { return place_; }
    bool finished() const { return state == FINISH; }
    float time_s() const { return race_t; }
    float player_x() const { return px; }
    float upcoming_curve(float ahead) const;
    enum State { MENU, GARAGE, CONTROLS, COUNTDOWN, RACE, PAUSED, FINISH } state = MENU;
    float state_t = 0;
    int laps_total = 3;
    bool capturing = false;
    int menu_sel = 0, ctl_sel = 0, garage_row = 0;
    int car_sel = 0, paint_sel = 0;           // player's car (index into the 10 phone-game models) and paint (15 paints)
private:
    float curve_at(float z) const;
    void reset_race();
    void scene(Cv& c, float cam_z, float cam_px, bool race);
    void hud(Cv& c);
    void cockpit(Cv& c);
    void garage_screen(Cv& c);
    void overlay(Cv& c);
    float axis(int a, const PadState& pad, uint32_t dn) const;
    std::vector<float> curves;
    float track_len = 0;
    float z = 0, px = 0, speed = 0, race_t = 0;
    int lap_ = 1, place_ = 6;
    float lap_z0 = 0;
    std::vector<Rival> rivals;
    float shake = 0, braking = 0, demo_z = 0;
    // settings
    int quality = 1;                           // 0 = 540p, 1 = 720p, 2 = 1080p internal resolution
    bool show_fps = false;
    bool fpv = true;                           // first-person (cockpit) camera, default
    float steer_vis = 0;
    float topf = 1, accf = 1, gripf = 1, armf = 1;   // from the selected car's stats
    void apply_car();
    uint32_t bind[A_COUNT];
    bool dirty = false;
    // input edges
    uint32_t prev_raw = 0, prev_nav = 0;
    float capture_t = 0, fps = 60;
    State ctl_return = MENU;
    // minimap (normalised 0..1)
    std::vector<float> mm_x, mm_y;
    std::vector<float> xoff, rwid;
};
