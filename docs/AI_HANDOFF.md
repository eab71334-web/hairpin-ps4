# hairpin-ps4 — AI hand-off

Owner: Libyan Arabic speaker, iPhone only (GitHub web + Actions), reply in short Arabic, say honestly what was tested. Studio: Sabak Studios.
Main game: `eab71334-web/P2P-Pursuit` (Godot 4.3) — read its `docs/AI_HANDOFF.md` first; PS4 notes: its `docs/ps4/PS4_PLAN.md`.
The owner has a jailbroken PS4 (6.72, GoldHEN) and an older workflow for another game (Warzone Native) that this CI is adapted from.

## Layout
- `native/src/platform.h` PadState + screen size; `game.h/.cpp` all game logic + rendering into a 0xAARRGGBB framebuffer; `font.h` bitmap font;
  `host_main.cpp` test driver (PPM screenshots); `main_ps4.cpp` the only PS4-specific file (SDL2 + ScePad).
- `assets/sce_sys/` icon0/pic0 for the PKG. Workflow: `.github/workflows/ps4-native.yml`.

## Test in the sandbox
`cd native/src && g++ -O2 -std=c++17 -Wall -o /tmp/host_test game.cpp host_main.cpp && /tmp/host_test /tmp/shots` (exit 0 = race finished),
then convert the PPMs with PIL and look at them. The sandbox cannot run PS4 code and cannot reach OpenOrbis releases (GitHub API/release downloads are blocked there) —
the PKG build only runs on GitHub Actions; fix errors from the CI log the owner pastes.

## Rules
- Never use a real commercial game's title id. Keep PS4-only API calls inside `main_ps4.cpp`.
- Unverified: OpenOrbis header/API names (`orbis/Pad.h`, `OrbisPadData`, `scePadOpen`...), SDL2 availability in the toolchain, the Makefile sed patches.
