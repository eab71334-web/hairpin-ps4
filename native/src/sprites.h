// Copyright (c) 2026 Sabak Studios. All rights reserved.
// Car sprites rendered from the phone game's own 3D car models (see tools/make_sprites.py).
// Per pixel: 5 bytes = base R,G,B (shaded, white paint), alpha, paint mask (255 = body paint, tinted at runtime).
#pragma once
#include <cstdint>
struct SpriteCar { const char* id; int w, h; const uint8_t* data; };
constexpr int SPRITE_CAR_COUNT = 10;
extern const SpriteCar SPRITE_CARS[SPRITE_CAR_COUNT];
