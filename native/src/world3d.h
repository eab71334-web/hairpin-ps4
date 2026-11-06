// Copyright (c) 2026 Sabak Studios. All rights reserved.
// 3D world of the PS4 port: the real stage geometry (from the phone game), scenery, car meshes.
#pragma once
#include "r3d.h"
#include "world_data.h"

struct CarModel { Mesh body, wheel; V3 pivot; float pitch = 0; };
const CarModel& car_model(int model);            // decoded lazily from the exported data
inline const Mesh& car_mesh(int model) { return car_model(model).body; }

struct Theme {
    uint32_t sky_top, sky_hor, fog, ground, ground2, road, shoulder, tree, tree2, rock, mount;
    bool snow;
};

class World3D {
public:
    void load(int stage);                        // 0 mountain, 1 snow, 2 autumn
    int npts() const { return n_; }
    float length() const { return n_ * 5.0f; }
    // position / heading of the road centre line at distance s (metres), lateral offset d (+ = left)
    V3 pos_at(float s, float d) const;
    float heading_at(float s) const;
    float slope_at(float s) const;                // pitch (rad)
    float curvature_at(float s) const;            // 1/m, + = left
    const Theme& theme() const { return th_; }
    const char* name() const { return st_->name; }
    void draw(Renderer& r, const Camera& cam, float s_hint);
private:
    int idx_clamp(int i) const { return i < 0 ? 0 : (i >= n_ ? n_ - 1 : i); }
    void pine(Renderer& r, V3 base, float h, float rad, uint32_t c1, uint32_t c2, bool snowy);
    void box(Renderer& r, V3 c, float sx, float sy, float sz, float yaw, uint32_t col);
    const StageData* st_ = nullptr;
    int n_ = 0;
    Theme th_{};
};
