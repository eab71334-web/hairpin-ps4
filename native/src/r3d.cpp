// Copyright (c) 2026 Sabak Studios. All rights reserved.
#include "r3d.h"
#include <algorithm>
#include <cstring>
#if !defined(HR_NO_THREADS)
#include <thread>
#endif

Xf Xf::make(V3 pos, float yaw, float pitch, float roll) {
    float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch), cr = cosf(roll), sr = sinf(roll);
    // R = Ry * Rx * Rz
    float ry[9] = {cy, 0, sy, 0, 1, 0, -sy, 0, cy}, rx[9] = {1, 0, 0, 0, cp, -sp, 0, sp, cp}, rz[9] = {cr, -sr, 0, sr, cr, 0, 0, 0, 1};
    float t[9], r[9];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { float s = 0; for (int k = 0; k < 3; k++) s += rx[i * 3 + k] * rz[k * 3 + j]; t[i * 3 + j] = s; }
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { float s = 0; for (int k = 0; k < 3; k++) s += ry[i * 3 + k] * t[k * 3 + j]; r[i * 3 + j] = s; }
    Xf x; memcpy(x.m, r, sizeof r); x.t = pos; return x;
}

Renderer::Renderer() {}

void Renderer::resize(int w, int h) {
    W = w; H = h;
    zbuf.assign(size_t(w) * h, 0.0f);
}

V3 Renderer::to_view(const V3& p) const {
    // world -> camera: translate, rotate by -yaw (around Y), -pitch (X), -roll (Z). Camera looks down -Z.
    V3 d = p - cam_.pos;
    float x = cy_ * d.x - sy_ * d.z, z = sy_ * d.x + cy_ * d.z, y = d.y;          // inverse yaw
    float y2 = cp_ * y + sp_ * z, z2 = -sp_ * y + cp_ * z;                         // inverse pitch
    float x3 = cr_ * x + sr_ * y2, y3 = -sr_ * x + cr_ * y2;                       // inverse roll
    return {x3, y3, z2};
}

void Renderer::begin(const Camera& cam, uint32_t sky_top, uint32_t sky_horizon, uint32_t fog_color, float fog_start, float fog_end) {
    cam_ = cam;
    cy_ = cosf(cam.yaw); sy_ = sinf(cam.yaw); cp_ = cosf(cam.pitch); sp_ = sinf(cam.pitch); cr_ = cosf(cam.roll); sr_ = sinf(cam.roll);
    f_ = (H * 0.5f) / tanf(cam.fov_deg * 0.5f * 3.14159265f / 180.0f);
    cx_ = W * 0.5f; cyc_ = H * 0.5f;
    skyt_ = sky_top; skyh_ = sky_horizon; fogc_ = fog_color; fog0_ = fog_start; fog1_ = fog_end;
    horizon_ = cyc_ - f_ * tanf(cam.pitch);                                         // screen row of the horizon (pitch up = horizon lower)
    horizon_ = cyc_ + f_ * tanf(cam.pitch);
    tris.clear(); nt = 0;
    gv_.clear(); gz_.clear();
}

static inline uint32_t mix_rgb(uint32_t a, uint32_t b, float t) {
    int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255, br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
    return 0xFF000000u | (uint32_t(ar + (br - ar) * t) << 16) | (uint32_t(ag + (bg - ag) * t) << 8) | uint32_t(ab + (bb - ab) * t);
}

void Renderer::emit(const V3& a, const V3& b, const V3& c, uint32_t col) {
    if (gpu) {                                                                      // GPU path: keep the view-space triangle, colour -> palette UV (7/7/6 bits)
        int r7 = (((col >> 16) & 255) * 127 + 127) / 255, g7 = (((col >> 8) & 255) * 127 + 127) / 255, b6 = ((col & 255) * 63 + 127) / 255;
        int idx = (r7 << 13) | (g7 << 6) | b6;
        float u = ((idx & 1023) + 0.5f) / 1024.0f, v = ((idx >> 10) + 0.5f) / 1024.0f;
        const V3* p[3] = {&a, &b, &c};
        for (int i = 0; i < 3; i++) { gv_.push_back(p[i]->x); gv_.push_back(p[i]->y); gv_.push_back(p[i]->z); gv_.push_back(u); gv_.push_back(v); }
        gz_.push_back(-(a.z + b.z + c.z) * (1.0f / 3.0f));
        nt++;
        return;
    }
    ST s;
    s.x0 = cx_ + f_ * a.x / -a.z; s.y0 = cyc_ - f_ * a.y / -a.z; s.z0 = 1.0f / -a.z;
    s.x1 = cx_ + f_ * b.x / -b.z; s.y1 = cyc_ - f_ * b.y / -b.z; s.z1 = 1.0f / -b.z;
    s.x2 = cx_ + f_ * c.x / -c.z; s.y2 = cyc_ - f_ * c.y / -c.z; s.z2 = 1.0f / -c.z;
    float minx = std::min(s.x0, std::min(s.x1, s.x2)), maxx = std::max(s.x0, std::max(s.x1, s.x2));
    float miny = std::min(s.y0, std::min(s.y1, s.y2)), maxy = std::max(s.y0, std::max(s.y1, s.y2));
    if (maxx < 0 || minx > W || maxy < 0 || miny > H) return;
    s.col = col;
    if (nt < tris.size()) tris[nt] = s; else tris.push_back(s);
    nt++;
}

void Renderer::tri(const V3& wa, const V3& wb, const V3& wc, uint32_t rgb, bool two_sided, bool emissive) {
    V3 n = norm(cross(wb - wa, wc - wa));
    V3 va = to_view(wa), vb = to_view(wb), vc = to_view(wc);
    if (va.z > -near_ && vb.z > -near_ && vc.z > -near_) return;
    // backface culling in view space (camera looks down -Z, CCW = front)
    V3 vn = cross(vb - va, vc - va);
    bool front = dot(vn, va) < 0;
    if (!front && !two_sided) return;
    float lum;
    if (emissive) lum = 1.0f;
    else {
        float nd = dot(n, sun);
        if (!front) nd = -nd;
        lum = 0.52f + 0.48f * std::max(0.0f, nd);
    }
    V3 ctr = (wa + wb + wc) * (1.0f / 3.0f);
    float dist = len(ctr - cam_.pos);
    int r = int(((rgb >> 16) & 255) * lum), g = int(((rgb >> 8) & 255) * lum), b = int((rgb & 255) * lum);
    uint32_t col = 0xFF000000u | (uint32_t(std::min(255, r)) << 16) | (uint32_t(std::min(255, g)) << 8) | uint32_t(std::min(255, b));
    if (dist > fog0_) col = mix_rgb(col, fogc_, std::min(1.0f, (dist - fog0_) / (fog1_ - fog0_)));
    // near-plane clipping
    V3 v[3] = {va, vb, vc};
    int inside = 0;
    for (int i = 0; i < 3; i++) if (v[i].z <= -near_) inside++;
    if (inside == 3) { emit(va, vb, vc, col); return; }
    V3 out[4]; int no = 0;
    for (int i = 0; i < 3; i++) {
        V3 p = v[i], q = v[(i + 1) % 3];
        bool pin = p.z <= -near_, qin = q.z <= -near_;
        if (pin) out[no++] = p;
        if (pin != qin) { float t = (-near_ - p.z) / (q.z - p.z); out[no++] = p + (q - p) * t; }
    }
    if (no >= 3) emit(out[0], out[1], out[2], col);
    if (no == 4) emit(out[0], out[2], out[3], col);
}

void Renderer::mesh(const Mesh& m, const Xf& xf, uint32_t paint_rgb, bool skip_glass, float max_dist, float lod) {
    V3 c = xf.t;
    if (len(c - cam_.pos) > max_dist) return;
    size_t cnt = m.tris.size();
    if (lod < 1.0f) cnt = std::max<size_t>(8, size_t(cnt * lod));          // meshes are stored biggest-triangle-first
    for (size_t ti = 0; ti < cnt; ti++) {
        const Tri& t = m.tris[ti];
        if (skip_glass && (t.flags & 4)) continue;
        uint32_t col = (t.flags & 1) ? paint_rgb : (0xFF000000u | (uint32_t(t.r) << 16) | (uint32_t(t.g) << 8) | t.bl);
        tri(xf.apply(t.a), xf.apply(t.b), xf.apply(t.c), col, (t.flags & 8) != 0, (t.flags & 2) != 0);
    }
}

void Renderer::raster_band(int by0, int by1, uint32_t* fb) {
    // sky gradient + clear z for the band
    for (int y = by0; y < by1; y++) {
        uint32_t c;
        float hy = horizon_;
        if (y < hy) { float t = hy > 1 ? float(y) / hy : 1.0f; c = mix_rgb(skyt_, skyh_, std::min(1.0f, t * t * 0.4f + t * 0.6f)); }
        else c = fogc_;
        std::fill(fb + size_t(y) * W, fb + size_t(y + 1) * W, c);
        std::fill(zbuf.begin() + size_t(y) * W, zbuf.begin() + size_t(y + 1) * W, 0.0f);
    }
    for (size_t i = 0; i < nt; i++) {
        const ST& s = tris[i];
        float miny = std::min(s.y0, std::min(s.y1, s.y2)), maxy = std::max(s.y0, std::max(s.y1, s.y2));
        if (maxy < by0 || miny >= by1) continue;
        // vertices sorted by y
        float X[3] = {s.x0, s.x1, s.x2}, Y[3] = {s.y0, s.y1, s.y2}, Z[3] = {s.z0, s.z1, s.z2};
        int o[3] = {0, 1, 2};
        if (Y[o[0]] > Y[o[1]]) std::swap(o[0], o[1]);
        if (Y[o[1]] > Y[o[2]]) std::swap(o[1], o[2]);
        if (Y[o[0]] > Y[o[1]]) std::swap(o[0], o[1]);
        float ax = X[o[0]], ay = Y[o[0]], az = Z[o[0]], bx = X[o[1]], by = Y[o[1]], bz = Z[o[1]], cx = X[o[2]], cy = Y[o[2]], cz = Z[o[2]];
        int ys = std::max(by0, int(ceilf(ay - 0.5f))), ye = std::min(by1 - 1, int(ceilf(cy - 0.5f)) - 1);
        float dyc = cy - ay;
        if (dyc < 1e-6f) continue;
        for (int y = ys; y <= ye; y++) {
            float py = y + 0.5f;
            float t1 = (py - ay) / dyc;
            float x1 = ax + (cx - ax) * t1, z1 = az + (cz - az) * t1;                 // long edge
            float x2, z2;
            if (py < by) { float t = (by - ay) > 1e-6f ? (py - ay) / (by - ay) : 0; x2 = ax + (bx - ax) * t; z2 = az + (bz - az) * t; }
            else { float t = (cy - by) > 1e-6f ? (py - by) / (cy - by) : 0; x2 = bx + (cx - bx) * t; z2 = bz + (cz - bz) * t; }
            if (x1 > x2) { std::swap(x1, x2); std::swap(z1, z2); }
            int xs = std::max(0, int(ceilf(x1 - 0.5f))), xe = std::min(W - 1, int(ceilf(x2 - 0.5f)) - 1);
            if (xe < xs) continue;
            float dx = x2 - x1, dz = dx > 1e-6f ? (z2 - z1) / dx : 0.0f;
            float z = z1 + dz * (xs + 0.5f - x1);
            float* zb = &zbuf[size_t(y) * W];
            uint32_t* px = fb + size_t(y) * W;
            for (int x = xs; x <= xe; x++, z += dz) if (z > zb[x]) { zb[x] = z; px[x] = s.col; }
        }
    }
}

void Renderer::end(uint32_t* fb) {
    if (gpu) { std::fill(fb, fb + size_t(W) * H, 0u); return; }          // 3D goes to the GPU; fb becomes the transparent 2D layer
    int T = std::max(1, threads);
#if defined(HR_NO_THREADS)
    T = 1;
#endif
    if (T == 1 || H < 64) { raster_band(0, H, fb); return; }
#if !defined(HR_NO_THREADS)
    std::vector<std::thread> pool;
    int band = (H + T - 1) / T;
    for (int t = 1; t < T; t++) {
        int y0 = t * band, y1 = std::min(H, y0 + band);
        if (y0 >= y1) break;
        try { pool.emplace_back([=]() { raster_band(y0, y1, fb); }); }
        catch (...) { raster_band(y0, y1, fb); }
    }
    raster_band(0, std::min(H, band), fb);
    for (auto& th : pool) th.join();
#endif
}
