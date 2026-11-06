// Copyright (c) 2026 Sabak Studios. All rights reserved.
// Car meshes + stages exported from the phone game (tools/godot/export.gd -> tools/make_world_data.py).
#pragma once
#include <cstdint>
struct CarMeshData { const char* id; int tris; const uint8_t* data; int wheel_tris; const uint8_t* wheel_data; float wheel_pivot[4]; };  // pivot: x,y,z, pitch (rad)   // 22 bytes per triangle: 9 x int16 (1/1024 m), r, g, b, flags
constexpr int CAR_MESH_COUNT = 10;
extern const CarMeshData CAR_MESHES[CAR_MESH_COUNT];
struct StagePt { float x, y, z, h; int fl; };        // fl: 1 tunnel, 2 open (cliff, no rails), 4 ice
struct StageObs { int index, kind, side; };          // kind 0 rock, 1 log, 2 bump
struct StageData { const char* name; const char* theme; float relief; int npts; const StagePt* pts; int nobs; const StageObs* obs; };
constexpr int STAGE_COUNT = 3;
extern const StageData STAGES[STAGE_COUNT];
