// Copyright (c) 2026 Sabak Studios. All rights reserved.
// Platform-independent interface: the game only sees a software framebuffer and a pad state.
#pragma once
#include <cstdint>

struct PadState {
    float steer = 0;        // -1 (left) .. 1 (right)  : left stick X / d-pad
    float gas = 0;          // 0..1                      : R2 (or Cross)
    float brake = 0;        // 0..1                      : L2 (or Square)
    bool start = false;     // Options  (pressed this frame)
    bool confirm = false;   // Cross    (pressed this frame)
    bool horn = false;      // Square / Circle
};

constexpr int SCREEN_W = 1280;
constexpr int SCREEN_H = 720;
