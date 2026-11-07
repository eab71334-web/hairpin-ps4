// Copyright (c) 2026 Sabak Studios. All rights reserved.
#pragma once
#include "platform.h"
#include "r3d.h"
#include "world3d.h"
#include <cstdint>
#include <string>
#include <vector>

// HAIRPIN RUSH (PS4 native): real 3D rally stages from the phone game, rendered by a multi-threaded software rasterizer,
// with AAA-style HUD / menus, garage and a button remapping screen.
enum Action { A_GAS, A_BRAKE, A_HANDBRAKE, A_RESET, A_PAUSE, A_CAMERA, A_COUNT };
struct Rival { float s, d, speed, vmax; int model, paint; };
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
    void set_threads(int n) { rend.threads = n < 1 ? 1 : n; }
    Renderer& renderer() { return rend; }       // GPU mode: main_ps4 draws the collected 3D triangles with the console GPU
    uint32_t binding(int a) const { return bind[a]; }
    std::string save_blob() const;             // settings -> text (platform stores it in /data)
    void load_blob(const std::string& s);
    bool take_dirty() { bool d = dirty; dirty = false; return d; }
    // read-only state (tests / HUD)
    float speed_kmh() const { return speed * 3.6f; }
    float rpm() const { return rpm_; }
    int gear() const { return gear_ + 1; }
    float skid() const { return skid_; }
    float lat_g() const { return lat_g_; }
    float grip_mu() const { return 1.28f * powf(gripf, 0.8f); }
    static float steer_lock_pub(float v, float mu) { return steer_lock(v, mu); }
    int place() const { return place_; }
    bool finished() const { return state == FINISH; }
    float time_s() const { return race_t; }
    float progress() const { return world.length() > 1 ? s_ / world.length() : 0.0f; }
    float lateral() const { return d_; }
    float yaw_dbg() const { return yaw_; }
    float upcoming_curve(float ahead_m) const { return world.curvature_at(s_ + ahead_m); }
    bool first_person() const { return fpv; }
    size_t last_tri_count() const { return rend.tri_count(); }
    enum State { MENU, GARAGE, CONTROLS, COUNTDOWN, RACE, PAUSED, FINISH } state = MENU;
    float state_t = 0;
    int stage_sel = 0;
    bool capturing = false;
    int menu_sel = 0, ctl_sel = 0, garage_row = 0;
    int car_sel = 0, paint_sel = 0;           // player's car (10 phone-game models) and paint (15 paints)
private:
    void reset_race();
    void apply_car();
    void load_stage();
    void scene3d(float dt_unused);
    void draw_hands(const Xf& car, int model, float phi);
    float grip_r = 0.0f, grip_l = 0.0f;
    void hud(Cv& c);
    void overlay(Cv& c);
    void garage_screen(Cv& c);
    float axis(int a, const PadState& pad, uint32_t dn) const;
    World3D world;
    Renderer rend;
    int stage_loaded = -1;
    uint32_t* fb_ = nullptr;
    // race state (metres, m/s)
    float vy_ = 0, wz_ = 0, steer_ang_ = 0, rpm_ = 1000, shift_t_ = 0, skid_ = 0, lat_g_ = 0; int gear_ = 0;   // physics state
    static float steer_lock(float v, float mu);
    void step_car(float dt, float gas, float brake, bool hb, float steer);
    float s_ = 0, d_ = 0, yaw_ = 0, speed = 0, race_t = 0, steer_vis = 0, bob_t = 0, shake = 0, braking = 0;
    int place_ = 6;
    float demo_s = 0, spin = 0;
    std::vector<Rival> rivals;
    float topf = 1, accf = 1, gripf = 1, armf = 1, vmax = 50;
    // settings
    int quality = 1;                           // 0 = 540p, 1 = 720p, 2 = 1080p internal resolution
    bool show_fps = false;
    bool fpv = true;
    uint32_t bind[A_COUNT];
    bool dirty = false;
    uint32_t prev_raw = 0, prev_nav = 0;
    float capture_t = 0, fps = 60;
    State ctl_return = MENU;
    std::vector<float> mm_x, mm_y;
};
