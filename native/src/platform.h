// Copyright (c) 2026 Sabak Studios. All rights reserved.
// Platform-independent interface: the game sees a software framebuffer and a raw pad state.
#pragma once
#include <cstdint>

// Button bits (DualShock 4 layout). The platform layer fills PadState::down with these.
enum Btn : uint32_t {
    B_CROSS = 1u << 0, B_CIRCLE = 1u << 1, B_SQUARE = 1u << 2, B_TRIANGLE = 1u << 3,
    B_L1 = 1u << 4, B_R1 = 1u << 5, B_L2 = 1u << 6, B_R2 = 1u << 7,
    B_L3 = 1u << 8, B_R3 = 1u << 9, B_OPTIONS = 1u << 10, B_TOUCHPAD = 1u << 11,
    B_UP = 1u << 12, B_DOWN = 1u << 13, B_LEFT = 1u << 14, B_RIGHT = 1u << 15,
};
constexpr uint32_t B_ALL = 0xFFFFu;

struct PadState {
    uint32_t down = 0;      // Btn bits currently held (L2/R2 bits optional: the game also reads l2/r2)
    float lx = 0, ly = 0;   // left stick, -1..1 (ly > 0 = down)
    float l2 = 0, r2 = 0;   // analog triggers 0..1
};

// Output is always 1920x1080 on the console; the game renders internally at 540p / 720p / 1080p and the platform scales up.
constexpr int OUT_W = 1920;
constexpr int OUT_H = 1080;
