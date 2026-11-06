// Copyright (c) 2026 Sabak Studios. All rights reserved.
#include "world3d.h"
#include <algorithm>
#include <cstring>

static inline uint32_t rgb(int r, int g, int b) { return 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b); }
static inline uint32_t hash32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
static inline float hf(uint32_t x) { return float(hash32(x) & 0xFFFF) / 65535.0f; }

static void decode(const uint8_t* data, int n, Mesh& m) {
    m.tris.reserve(n);
    for (int i = 0; i < n; i++) {
        const uint8_t* p = data + i * 22;
        int16_t q[9];
        for (int k = 0; k < 9; k++) q[k] = int16_t(p[k * 2] | (p[k * 2 + 1] << 8));
        Tri t;
        t.a = V3(q[0] / 1024.0f, q[1] / 1024.0f, q[2] / 1024.0f);
        t.c = V3(q[3] / 1024.0f, q[4] / 1024.0f, q[5] / 1024.0f);     // Godot winds front faces clockwise -> swap to CCW
        t.b = V3(q[6] / 1024.0f, q[7] / 1024.0f, q[8] / 1024.0f);
        t.r = p[18]; t.g = p[19]; t.bl = p[20]; t.flags = p[21];
        m.tris.push_back(t);
    }
}

const CarModel& car_model(int model) {
    static CarModel cache[CAR_MESH_COUNT];
    static bool loaded[CAR_MESH_COUNT] = {};
    if (model < 0 || model >= CAR_MESH_COUNT) model = 0;
    if (!loaded[model]) {
        const CarMeshData& d = CAR_MESHES[model];
        CarModel& cm = cache[model];
        decode(d.data, d.tris, cm.body);
        decode(d.wheel_data, d.wheel_tris, cm.wheel);
        auto area = [](const Tri& t) { V3 c = cross(t.b - t.a, t.c - t.a); return dot(c, c); };
        std::stable_sort(cm.body.tris.begin(), cm.body.tris.end(), [&](const Tri& a, const Tri& b) { return area(a) > area(b); });
        cm.pivot = V3(d.wheel_pivot[0], d.wheel_pivot[1], d.wheel_pivot[2]);
        cm.pitch = d.wheel_pivot[3];
        loaded[model] = true;
    }
    return cache[model];
}

void World3D::load(int stage) {
    st_ = &STAGES[stage < 0 ? 0 : (stage >= STAGE_COUNT ? STAGE_COUNT - 1 : stage)];
    n_ = st_->npts;
    if (!strcmp(st_->theme, "snow")) th_ = {rgb(70, 110, 180), rgb(205, 222, 240), rgb(210, 224, 238), rgb(236, 240, 246), rgb(220, 228, 240), rgb(74, 78, 88), rgb(190, 196, 206), rgb(34, 92, 66), rgb(240, 244, 250), rgb(120, 126, 140), rgb(170, 186, 210), true};
    else if (!strcmp(st_->theme, "autumn")) th_ = {rgb(255, 150, 90), rgb(255, 214, 160), rgb(236, 190, 150), rgb(120, 140, 56), rgb(150, 120, 52), rgb(78, 76, 82), rgb(150, 118, 84), rgb(214, 96, 28), rgb(232, 168, 40), rgb(130, 110, 96), rgb(140, 100, 100), false};
    else th_ = {rgb(60, 90, 170), rgb(190, 210, 236), rgb(190, 206, 226), rgb(84, 118, 74), rgb(100, 100, 92), rgb(70, 72, 80), rgb(150, 140, 120), rgb(36, 100, 58), rgb(60, 130, 74), rgb(118, 120, 126), rgb(120, 130, 150), false};
}

V3 World3D::pos_at(float s, float d) const {
    float f = s / 5.0f;
    int i = int(floorf(f));
    float t = f - i;
    int i0 = idx_clamp(i), i1 = idx_clamp(i + 1);
    const StagePt &a = st_->pts[i0], &b = st_->pts[i1];
    V3 c(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
    float h = heading_at(s);
    return c + V3(-cosf(h), 0, sinf(h)) * d;
}
float World3D::heading_at(float s) const {
    float f = s / 5.0f;
    int i = int(floorf(f));
    float t = f - i;
    float h0 = st_->pts[idx_clamp(i)].h, h1 = st_->pts[idx_clamp(i + 1)].h;
    float dh = h1 - h0;
    while (dh > 3.14159265f) dh -= 6.2831853f;
    while (dh < -3.14159265f) dh += 6.2831853f;
    return h0 + dh * t;
}
float World3D::slope_at(float s) const {
    V3 a = pos_at(s - 2.5f, 0), b = pos_at(s + 2.5f, 0);
    float dxz = sqrtf((b.x - a.x) * (b.x - a.x) + (b.z - a.z) * (b.z - a.z));
    return atan2f(b.y - a.y, std::max(0.01f, dxz));
}
float World3D::curvature_at(float s) const {
    float h0 = heading_at(s - 5.0f), h1 = heading_at(s + 5.0f);
    float dh = h1 - h0;
    while (dh > 3.14159265f) dh -= 6.2831853f;
    while (dh < -3.14159265f) dh += 6.2831853f;
    return dh / 10.0f;
}

void World3D::box(Renderer& r, V3 c, float sx, float sy, float sz, float yaw, uint32_t col) {
    float cy = cosf(yaw), sy2 = sinf(yaw);
    V3 ax(cy * sx * 0.5f, 0, -sy2 * sx * 0.5f), az(sy2 * sz * 0.5f, 0, cy * sz * 0.5f), ay(0, sy, 0);
    V3 p[8];
    for (int i = 0; i < 8; i++) p[i] = c + ax * (i & 1 ? 1.0f : -1.0f) + az * (i & 2 ? 1.0f : -1.0f) + ay * (i & 4 ? 1.0f : 0.0f);
    auto q = [&](int a, int b, int c2, int d) { r.tri(p[a], p[b], p[c2], col); r.tri(p[a], p[c2], p[d], col); };
    q(4, 5, 7, 6); q(0, 2, 3, 1); q(0, 1, 5, 4); q(2, 6, 7, 3); q(1, 3, 7, 5); q(0, 4, 6, 2);
}

void World3D::pine(Renderer& r, V3 base, float h, float rad, uint32_t c1, uint32_t c2, bool snowy) {
    const int N = 7;
    float tr = rad * 0.12f;
    uint32_t trunk = rgb(84, 56, 36);
    for (int i = 0; i < N; i++) {
        float a0 = i * 6.2831853f / N, a1 = (i + 1) * 6.2831853f / N;
        V3 b0 = base + V3(cosf(a0) * tr, 0, sinf(a0) * tr), b1 = base + V3(cosf(a1) * tr, 0, sinf(a1) * tr);
        r.tri(b0, b0 + V3(0, h * 0.3f, 0), b1, trunk, true);
    }
    for (int layer = 0; layer < 3; layer++) {
        float y0 = h * (0.18f + layer * 0.26f), y1 = h * (0.58f + layer * 0.26f), rr = rad * (1.0f - layer * 0.26f);
        V3 apex = base + V3(0, y1, 0);
        for (int i = 0; i < N; i++) {
            float a0 = i * 6.2831853f / N, a1 = (i + 1) * 6.2831853f / N;
            V3 b0 = base + V3(cosf(a0) * rr, y0, sinf(a0) * rr), b1 = base + V3(cosf(a1) * rr, y0, sinf(a1) * rr);
            r.tri(b1, b0, apex, (i & 1) ? c1 : c2, false);
            if (snowy) { V3 m0 = b0 * 0.45f + apex * 0.55f, m1 = b1 * 0.45f + apex * 0.55f; r.tri(m1, m0, apex, rgb(246, 248, 252), false); }
        }
    }
}

void World3D::draw(Renderer& r, const Camera& cam, float s_hint) {
    int ic = int(s_hint / 5.0f);
    int i0 = std::max(0, ic - 12), i1 = std::min(n_ - 2, ic + 70);
    const StagePt* P = st_->pts;
    auto L = [&](int i, float d) { float h = P[i].h; return V3(P[i].x - cosf(h) * d, P[i].y, P[i].z + sinf(h) * d); };
    auto quad_up = [&](V3 a, V3 b, V3 c, V3 d, uint32_t col) {          // a,b,c,d around the quad; orientation fixed so it faces up
        V3 n = cross(b - a, c - a);
        if (n.y < 0) { r.tri(a, c, b, col); r.tri(a, d, c, col); } else { r.tri(a, b, c, col); r.tri(a, c, d, col); }
    };
    for (int i = i0; i <= i1; i++) {
        bool alt = (i / 2) & 1;
        // far ground, shoulder, road
        uint32_t gcol = alt ? th_.ground : th_.ground2;
        for (int sd = -1; sd <= 1; sd += 2) {
            quad_up(L(i, sd * 10.5f) - V3(0, 0.08f, 0), L(i, sd * 90.0f) - V3(0, 0.12f, 0), L(i + 1, sd * 90.0f) - V3(0, 0.12f, 0), L(i + 1, sd * 10.5f) - V3(0, 0.08f, 0), gcol);
            quad_up(L(i, sd * 7.0f) - V3(0, 0.03f, 0), L(i, sd * 10.5f) - V3(0, 0.06f, 0), L(i + 1, sd * 10.5f) - V3(0, 0.06f, 0), L(i + 1, sd * 7.0f) - V3(0, 0.03f, 0), th_.shoulder);
        }
        uint32_t rc = alt ? th_.road : rgb(((th_.road >> 16) & 255) + 6, ((th_.road >> 8) & 255) + 6, (th_.road & 255) + 7);
        if (P[i].fl & 4) rc = rgb(150, 190, 215);                         // ice
        quad_up(L(i, 7.0f), L(i, -7.0f), L(i + 1, -7.0f), L(i + 1, 7.0f), rc);
        V3 up(0, 0.035f, 0);
        quad_up(L(i, 6.55f) + up, L(i, 6.15f) + up, L(i + 1, 6.15f) + up, L(i + 1, 6.55f) + up, rgb(236, 236, 240));
        quad_up(L(i, -6.15f) + up, L(i, -6.55f) + up, L(i + 1, -6.55f) + up, L(i + 1, -6.15f) + up, rgb(236, 236, 240));
        if ((i & 3) < 2) quad_up(L(i, 0.18f) + up, L(i, -0.18f) + up, L(i + 1, -0.18f) + up, L(i + 1, 0.18f) + up, rgb(244, 214, 90));
        // guard posts / reflectors
        if ((i & 1) == 0 && !(P[i].fl & 2)) for (int sd = -1; sd <= 1; sd += 2) {
            V3 c = L(i, sd * 8.2f);
            box(r, c, 0.16f, 0.9f, 0.16f, P[i].h, (i & 3) ? rgb(236, 236, 240) : rgb(220, 50, 40));
            if ((i & 1) == 0) box(r, c + V3(0, 0.55f, 0), 0.1f, 0.2f, 0.5f, P[i].h, rgb(236, 236, 240));
        }
        // scenery
        for (int k = 0; k < 2; k++) for (int sd = -1; sd <= 1; sd += 2) {
            uint32_t h = hash32(uint32_t(i * 977 + k * 131 + (sd > 0 ? 5 : 9)));
            if ((h & 3) == 0) continue;
            float off = 14.0f + hf(h) * (k ? 50.0f : 18.0f);
            V3 base = L(i, sd * off) - V3(0, 0.1f, 0);
            float sc = 0.7f + 0.9f * hf(h >> 3);
            if ((h >> 7 & 7) == 0) {
                float rs = 0.9f + 1.8f * sc;
                box(r, base, rs, rs * 0.7f, rs * 1.1f, hf(h >> 5) * 3.0f, th_.rock);
            } else pine(r, base, 9.0f * sc, 2.7f * sc, th_.tree, (h & 8) ? th_.tree2 : th_.tree, th_.snow);
        }
    }
    // far mountains (ring around the camera, drawn with capped fog)
    for (int i = 0; i < 40; i++) {
        float a0 = i * 6.2831853f / 40, a1 = (i + 1) * 6.2831853f / 40;
        float hh = 80.0f + 220.0f * hf(i * 7919u + 3) * (st_->relief > 1 ? 1.0f : 0.6f);
        float R = 1100.0f;
        V3 b0 = V3(cam.pos.x + cosf(a0) * R, cam.pos.y - 30.0f, cam.pos.z + sinf(a0) * R), b1 = V3(cam.pos.x + cosf(a1) * R, cam.pos.y - 30.0f, cam.pos.z + sinf(a1) * R);
        V3 pk = V3(cam.pos.x + cosf((a0 + a1) * 0.5f) * (R + 80), cam.pos.y + hh, cam.pos.z + sinf((a0 + a1) * 0.5f) * (R + 80));
        r.tri(b1, b0, pk, (i & 1) ? th_.mount : rgb(((th_.mount >> 16) & 255) * 9 / 10, ((th_.mount >> 8) & 255) * 9 / 10, (th_.mount & 255) * 9 / 10), true, true);
    }
}
