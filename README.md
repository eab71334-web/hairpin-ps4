# HAIRPIN RUSH — PS4 (native)

Separate native C++ project (OpenOrbis toolchain). The main game (Godot, Android/iOS) lives in `eab71334-web/P2P-Pursuit`;
Godot cannot run on PS4, so this is a **new, smaller game core** written for the console.

## Status
- **Real 3D**: `native/src/r3d.*` is a multi-threaded software rasterizer (flat shading, z-buffer, fog). `world3d.*` builds the road, scenery and cars.
  The 10 car models (with interiors: dashboard, gauges, radio, mirrors, steering wheel) and the 3 stages (mountain / snow / autumn) are **exported from the phone game** by `tools/godot/export.gd`
  and baked into `native/src/world_data.cpp` by `tools/make_world_data.py`.
- First-person cockpit (default) with a rotating wheel and hands, chase camera (R3 toggles), garage with a 3D turntable, 15 paints, car stats that change the driving.
- AAA-style HUD / menus, CONTROLS screen (remap any button, saved in /data/hairpin_rush.cfg), 540p / 720p / 1080p internal resolution (output always 1080p).
- `host_main.cpp`: host test (menus, remap, garage, scripted driver finishes a stage, timings) - runs in CI before the PKG build.
- **Not verified on a console**: OpenOrbis build was green, but nothing has been run on a real PS4. Threads (`-DHR_NO_THREADS` fallback in the workflow), SDL2 scaling and speed are untested.
- Package identity: title **Hairpin Rush**, title id `BREW00791`, content id `IV0000-BREW00791_00-HAIRPINRUSH00000` (own homebrew ids).

## Not done yet
1. Arabic UI / phone-style menus; sound; online/LAN with the phone game (protocol work needed on both sides); tunnels and ice/rock hazards from the stage data are not rendered yet.
2. Driver avatar body (only hands/forearms are drawn), glass transparency.
