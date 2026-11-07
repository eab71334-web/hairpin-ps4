// Copyright (c) 2026 Sabak Studios. All rights reserved.
// Procedural audio: engine (rpm/throttle driven), tyre squeal, wind and UI / race jingles, mixed to S16 stereo 48 kHz.
// The game thread writes the atomics / pushes events; the audio thread calls mix().
#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace AUD {
enum Ev { E_MOVE = 1, E_OK, E_BACK, E_BEEP, E_GO, E_FINISH, E_CRASH, E_SHIFT, E_SWAP, E_START };
static std::atomic<float> g_rpm{1000.f}, g_thr{0.f}, g_speed{0.f}, g_skid{0.f}, g_engine{0.f};   // g_engine: 0 = engine silent (menus), 1 = on
static std::atomic<int> g_q[32]; static std::atomic<unsigned> g_wr{0}, g_rd{0};
static inline void push(int e) { unsigned w = g_wr.load(); if (w - g_rd.load() >= 32) return; g_q[w & 31].store(e); g_wr.store(w + 1); }

struct V { int type; float t, dur, f0, f1, vol, ph; bool on; };   // type 0 tone, 1 noise thump, 2 soft chime
static V vs[24];
static float rs = 1.f; static unsigned rn_ = 2463534242u;
static inline float rnd() { rn_ ^= rn_ << 13; rn_ ^= rn_ >> 17; rn_ ^= rn_ << 5; return (rn_ / 4294967296.f) * 2.f - 1.f; }
static void spawn(int type, float f0, float f1, float dur, float vol, float delay = 0) {
    for (auto& v : vs) if (!v.on) { v = {type, -delay, dur, f0, f1, vol, 0, true}; return; }
}
static void event(int e) {
    switch (e) {
        case E_MOVE:   spawn(2, 1480, 1480, 0.07f, 0.22f); break;
        case E_OK:     spawn(2, 880, 880, 0.10f, 0.28f); spawn(2, 1320, 1320, 0.16f, 0.28f, 0.07f); break;
        case E_BACK:   spawn(2, 880, 880, 0.08f, 0.24f); spawn(2, 587, 587, 0.14f, 0.24f, 0.06f); break;
        case E_SWAP:   spawn(0, 300, 1100, 0.18f, 0.16f); spawn(1, 0, 0, 0.10f, 0.10f); break;
        case E_BEEP:   spawn(0, 740, 740, 0.20f, 0.30f); break;
        case E_GO:     spawn(0, 1480, 1480, 0.55f, 0.34f); spawn(0, 1480 * 1.5f, 1480 * 1.5f, 0.55f, 0.18f); break;
        case E_START:  spawn(2, 523, 523, 0.18f, 0.26f); spawn(2, 659, 659, 0.18f, 0.26f, 0.12f); spawn(2, 784, 784, 0.18f, 0.26f, 0.24f); spawn(2, 1047, 1047, 0.45f, 0.30f, 0.36f); break;
        case E_FINISH: { static const float n[6] = {523, 659, 784, 1047, 784, 1319}; for (int i = 0; i < 6; i++) spawn(2, n[i], n[i], i == 5 ? 0.9f : 0.2f, 0.28f, i * 0.14f); break; }
        case E_CRASH:  spawn(1, 0, 0, 0.35f, 0.55f); spawn(0, 90, 40, 0.30f, 0.45f); break;
        case E_SHIFT:  spawn(1, 0, 0, 0.04f, 0.18f); break;
    }
}

static inline void mix(int16_t* out, int frames) {
    static float ph = 0, ph2 = 0, lp1 = 0, lp2 = 0, nz1 = 0, nz2 = 0, nz3 = 0, rpm = 1000, thr = 0, spd = 0, skd = 0, eng = 0, turbo = 0, tph = 0, prevThr = 0, crack = 0;
    while (g_rd.load() != g_wr.load()) { int e = g_q[g_rd.load() & 31].load(); g_rd.store(g_rd.load() + 1); event(e); }
    const float SR = 48000.f, TW = 6.2831853f;
    float tr = g_rpm.load(), tt = g_thr.load(), ts = g_speed.load(), tk = g_skid.load(), te = g_engine.load();
    for (int i = 0; i < frames; i++) {
        rpm += (tr - rpm) * 0.0008f; thr += (tt - thr) * 0.0015f; spd += (ts - spd) * 0.0006f; skd += (tk - skd) * 0.002f; eng += (te - eng) * 0.0008f;
        float f = std::max(rpm, 700.f) / 60.f * 3.0f;                  // 6-cylinder firing frequency
        ph += f / SR; if (ph >= 1) ph -= 1; ph2 += f * 0.5f / SR; if (ph2 >= 1) ph2 -= 1;
        float saw = 2 * ph - 1;
        float cut = 250.f + rpm * 0.55f + thr * 1400.f, k = 1.f - expf(-TW * cut / SR);
        lp1 += k * (saw - lp1); lp2 += k * (lp1 - lp2);
        float body = lp2 * 1.3f + sinf(TW * ph2) * 0.35f + sinf(TW * ph * 2) * 0.18f;
        float n = rnd();
        nz1 += 0.08f * (n - nz1);                                       // low-passed intake noise
        float intake = nz1 * thr * 0.9f;
        float load = 0.45f + 0.55f * thr;
        float engine = std::tanh(body * (0.8f + thr * 1.2f)) * load * 0.42f + intake * 0.22f;
        // turbo whistle that flutters on lift-off
        tph += (1800.f + rpm * 0.7f) / SR; if (tph >= 1) tph -= 1;
        float spool = std::min(1.f, std::max(0.f, (rpm - 2500.f) / 4500.f)) * thr; turbo += (spool - turbo) * 0.0004f;
        engine += sinf(TW * tph) * turbo * 0.035f;
        // exhaust crackle when lifting
        if (thr < 0.08f && rpm > 3500.f && fabsf(rnd()) > 0.9994f) crack = 1.f;
        engine += rnd() * crack * 0.25f; crack *= 0.9985f;
        engine *= eng;
        // tyres + wind
        nz2 += 0.35f * (n - nz2); float hiss = n - nz2;                  // high-pass-ish
        float tyre = hiss * skd * std::min(1.f, spd / 18.f) * 0.30f * (0.6f + 0.4f * sinf(TW * 83.f * i / SR));
        nz3 += 0.02f * (n - nz3);
        float wind = nz3 * std::min(1.f, (spd / 70.f) * (spd / 70.f)) * 0.9f * eng;
        float L = engine + tyre + wind, R = L;
        // voices
        for (auto& v : vs) {
            if (!v.on) continue;
            v.t += 1.f / SR; if (v.t < 0) continue;
            if (v.t >= v.dur) { v.on = false; continue; }
            float u = v.t / v.dur, env = (1.f - u) * (1.f - u) * std::min(1.f, v.t * 400.f), s = 0;
            if (v.type == 1) s = rnd() * env;
            else {
                float fr = v.f0 + (v.f1 - v.f0) * u; v.ph += fr / SR; if (v.ph >= 1) v.ph -= 1;
                s = sinf(TW * v.ph) * env;
                if (v.type == 2) { s = s * 0.8f + sinf(TW * v.ph * 2.f) * env * 0.25f + sinf(TW * v.ph * 3.01f) * env * 0.08f; }   // bell-like
                else s += (v.ph < 0.5f ? 0.08f : -0.08f) * env;
            }
            L += s * v.vol; R += s * v.vol;
        }
        L = std::tanh(L * 1.1f); R = std::tanh(R * 1.1f);
        out[i * 2] = (int16_t)(L * 26000.f); out[i * 2 + 1] = (int16_t)(R * 26000.f);
    }
}
}  // namespace AUD
