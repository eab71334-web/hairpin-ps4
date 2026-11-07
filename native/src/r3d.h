// Copyright (c) 2026 Sabak Studios. All rights reserved.
// Small multi-threaded software 3D rasterizer (flat shaded triangles, z-buffer, fog) used by the PS4 port.
#pragma once
#include <stdlib.h>
#include <math.h>
#include <cmath>
#include <cstdint>
#include <vector>

struct V3 {
    float x = 0, y = 0, z = 0;
    V3() {}
    V3(float a, float b, float c) : x(a), y(b), z(c) {}
    V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator*(float s) const { return {x * s, y * s, z * s}; }
};
inline float dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(const V3& a, const V3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float len(const V3& a) { return sqrtf(dot(a, a)); }
inline V3 norm(const V3& a) { float l = len(a); return l > 1e-9f ? a * (1.0f / l) : V3(0, 1, 0); }

struct Tri { V3 a, b, c; uint8_t r, g, bl, flags; };   // flags: 1 paint (tinted), 2 emissive, 4 glass, 8 two-sided
struct Mesh { std::vector<Tri> tris; };

// rotation about Y (yaw), then X (pitch), then Z (roll)
struct Xf {
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    V3 t;
    static Xf make(V3 pos, float yaw, float pitch = 0, float roll = 0);
    V3 apply(const V3& p) const { return {m[0] * p.x + m[1] * p.y + m[2] * p.z + t.x, m[3] * p.x + m[4] * p.y + m[5] * p.z + t.y, m[6] * p.x + m[7] * p.y + m[8] * p.z + t.z}; }
    V3 rot(const V3& p) const { return {m[0] * p.x + m[1] * p.y + m[2] * p.z, m[3] * p.x + m[4] * p.y + m[5] * p.z, m[6] * p.x + m[7] * p.y + m[8] * p.z}; }
};

inline Xf operator*(const Xf& a, const Xf& b) {
    Xf r;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { float s = 0; for (int k = 0; k < 3; k++) s += a.m[i * 3 + k] * b.m[k * 3 + j]; r.m[i * 3 + j] = s; }
    r.t = a.apply(b.t);
    return r;
}

struct Camera { V3 pos; float yaw = 0, pitch = 0, roll = 0, fov_deg = 60; };

class Renderer {
public:
    Renderer();
    void resize(int w, int h);
    void begin(const Camera& cam, uint32_t sky_top, uint32_t sky_horizon, uint32_t fog_color, float fog_start, float fog_end);
    void tri(const V3& a, const V3& b, const V3& c, uint32_t rgb, bool two_sided = false, bool emissive = false);
    void mesh(const Mesh& m, const Xf& xf, uint32_t paint_rgb, bool skip_glass, float max_dist, float lod = 1.0f);
    void end(uint32_t* fb);                      // rasterize (threaded) into fb (w*h, 0xFFRRGGBB)
    int w() const { return W; }
    int h() const { return H; }
    int threads = 4;
    // GPU mode: instead of rasterizing, collect view-space triangles (palette-UV encoded colour) for the console GPU; end() just clears fb to transparent.
    bool gpu = false;
    void set_layer(int l) { layer_ = l ? 1 : 0; }                  // 0 = world, 1 = cockpit/hands (drawn after a depth clear with a tighter near/far)
    const std::vector<float>& gpu_verts(int layer = 0) const { return gv_[layer]; }   // 5 floats per vertex: x,y,z (view space), u,v (palette); 3 vertices per triangle
    const std::vector<float>& gpu_depth(int layer = 0) const { return gz_[layer]; }   // per triangle: view-space distance of the centroid (painter sorting when no depth buffer)
    float focal() const { return f_; }
    uint32_t sky_top() const { return skyt_; }
    uint32_t sky_horizon() const { return skyh_; }
    uint32_t fog_colour() const { return fogc_; }
    size_t tri_count() const { return nt; }
    V3 sun = norm(V3(-0.45f, 0.8f, 0.4f));
    V3 cam_pos() const { return cam_.pos; }
    float horizon_y() const { return horizon_; }
private:
    struct ST { float x0, y0, x1, y1, x2, y2, z0, z1, z2; uint32_t col; };
    void raster_band(int y0, int y1, uint32_t* fb);
    void emit(const V3& a, const V3& b, const V3& c, uint32_t col);   // view-space, already near-clipped
    int W = 0, H = 0;
    std::vector<float> zbuf;
    std::vector<ST> tris;
    size_t nt = 0;
    Camera cam_;
    float cy_, sy_, cp_, sp_, cr_, sr_, f_ = 1, cx_ = 0, cyc_ = 0, horizon_ = 0;
    uint32_t skyt_ = 0, skyh_ = 0, fogc_ = 0;
    float fog0_ = 50, fog1_ = 400;
    float near_ = 0.15f;
    V3 to_view(const V3& p) const;
    std::vector<float> gv_[2], gz_[2];
    int layer_ = 0;
};
