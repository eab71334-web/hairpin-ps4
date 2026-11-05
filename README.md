# HAIRPIN RUSH — PS4 (native)

Separate native C++ project (OpenOrbis toolchain). The main game (Godot, Android/iOS) lives in `eab71334-web/P2P-Pursuit`;
Godot cannot run on PS4, so this is a **new, smaller game core** written for the console.

## Status (stage 1)
- `native/src/game.cpp` — pseudo-3D road racer: 3 laps, 5 rivals, hairpin bends, HUD with 5x7 bitmap font, software renderer (1280x720).
- `native/src/host_main.cpp` — host test with a scripted driver + PPM screenshots. **Verified in the cloud sandbox** (race completes, screenshots checked).
- `native/src/main_ps4.cpp` — PS4 layer (SDL2 window + ScePad): steer = left stick / D-pad, gas = R2 (or ✕), brake = L2 (or □), ✕ = confirm.
  **Never compiled against the real OpenOrbis headers and never run on a console** — expect small API-name fixes on the first CI run.
- `.github/workflows/ps4-native.yml` — host test, then downloads OpenOrbis, builds `HairpinRush.pkg`, uploads artifact + release `ps4-latest`.
- `assets/sce_sys/icon0.png` (512x512) and `pic0.png` (1920x1080) — the icon and cover, copied into the package.
- Package identity: title **Hairpin Rush**, title id `BREW00791`, content id `IV0000-BREW00791_00-HAIRPINRUSH00000` (own homebrew ids).

## Not done yet (planned)
1. Controller remap screen like the Godot game; sound.
2. **LAN with phones**: the phone game uses Godot's high-level multiplayer over ENet (UDP 24565, discovery 24566). A native client cannot talk to it as is;
   the plan is a small custom UDP protocol that BOTH sides speak (a "console guest" mode added to the Godot game).
3. Port more of the real game (cars, tracks, career).

See `docs/AI_HANDOFF.md` for the full context if you continue this with another AI session.
