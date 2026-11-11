# DragonBox Pyra — Descent 3 November 2026 upstream update

**Checked:** 2026-11-09. **Upstream main:** `71ad8e3e0bdbd682f9efa4b613f1a7c6ff818b73` (2026-11-08).
**Base:** `Descent3-Pyra-PowerVR-GLES2-SDL3-Upstream-Okt-Nov-2026-Selective-Release.zip` (the prior Pyra selective release through 2026-11-02).

## Scope

GitHub reported **26 commits, including merge commits**, dated 2026-11-01 through 2026-11-08 on upstream `main`, and **8 new commits** after the previous upstream baseline `7c312a85e65c1e81003f6919535fcc688f299401` (2026-11-02). All eight later commits belong to the SDL3 event-polling change set merged as PR #727. The previous Pyra release already adapted some changes from November 1 and 2; see `UPSTREAM-2026-OCT-NOV-INTEGRATION.md`.

## Implemented: upstream PR #727 (`71ad8e3`), adapted for Pyra

- `Descent3/sdlmain.cpp`: remove SDL's global key/mouse event filter and register renderer-safe quit handler.
- `ddio/ddio.cpp`: poll/dispatch SDL3 key, mouse, wheel, joystick-ball and quit events on the main game thread in `ddio_Frame()`.
- `ddio/ddio_lnx.h`: explicit declarations for input event functions and the Pyra exit handler.
- `ddio/lnxkey.cpp`, `ddio/lnxkey_sdl.cpp`, new `ddio/lnxkey_sdl.h`: remove the obsolete null-keyboard mode and global volatile keyboard state, keep state in SDL implementation and ignore unmapped keycode 0.
- `ddio/lnxmouse.cpp`, `ddio/sdljoy.cpp`: change handlers from filter callbacks to event callbacks; remove redundant `SDL_PumpEvents()` calls.
- `ddio/CMakeLists.txt`: remove obsolete null backend, add header.
- Delete `ddio/lnxkey_null.cpp` from the Pyra source tree.

### Pyra-specific deviations from upstream

The official PR's `SDL_EVENT_QUIT` calls `SDL_Quit()` and `_exit(0)`. **That must not be copied into the Pyra build:** our `just_exit()` releases native PowerVR/EGL/GL objects before SDL destroys the context. `ddio_SetQuitHandler(just_exit)` preserves this behavior.

The user's independent `Descent3-Pyra-Super-Tasten-Mapping.patch` is included in the new release:
`SDLK_LALT` → `KEY_LALT`, `SDLK_RALT` (AltGr) → `KEY_RALT`, `SDLK_LGUI` → `KEY_LSUPER` (0xDB), `SDLK_RGUI` → `KEY_RSUPER` (0xDC). Keyboard menu labels and modifier handling are retained.

## Earlier November work already present

From the previous selective release: case-insensitive Linux data directory lookup (Nov 2) and `mem_strdup_unique`, RNG/seeding fixes and improved MVE/PostLevel timing carried from Oct 22–31. Three earlier Oct 23 merges are also present.

## November commits not ported into this *runtime* release

- PR #709: mechanically modernizing hundreds of `NULL` usages to `nullptr`, and related C++ tooling. Large platform-independent and platform-specific edit set, not prerequisite for SDL event fix.
- PR #734: `ENABLE_MOCK_CLOCK`, internal timer rewrites and deterministic CI tests. This is intentionally a separate debugging build; real-time Pyra clock remains unchanged.
- PR #718: logging through netgames and netcon module DLLs. A coordinated rebuild/testing of all plugin modules is required before modifying their interfaces.
- PR #711: `std::filesystem::path` refactoring in mission and networking function-pointer signatures. Requires verifying the game-module ABI.
- PR #740 / earlier C++ modernization: broader memory-helper adoption left for separate verification.
- CI-only macOS signing/notarization, Windows/Linux runner, vcpkg and packaging changes: not applicable to the Pyra native release.

**This is a selective, compatibility-oriented integration, not a complete merge of all November commits.**

## Protected unchanged components

- Native SGX544MP2 PowerVR GLES2/EGL loader, FBO upscale, VBO/batching optimizations and sharp 1280×720 UI.
- Corrected `libmve` RGB555, stereo-DPCM and unsigned large-segment parser.
- SDL2 helper-based rumble only on local shield damage (no weapon-fired rumble).
- Bundled SDL3 library and `build-pyra.sh` / `run-pyra.sh`.

## Tests

The following tests pass on the host: `test-pyra-nov-input.sh`, `test-pyra-movie.sh`, `test-pyra-menu-mve.sh`, `test-pyra-sharp-ui.sh`.
Linux `g++ -std=c++17 -fsyntax-only` passed on all eight affected implementation files with the vendored SDL3/plog headers and appropriate Linux/POSIX flags. The host has no native SGX544MP2 platform and no validated native ARMhf build of this release; on-device build and runtime tests remain required.

Run on Pyra:

    ./test-pyra-nov-input.sh
    ./test-pyra-menu-mve.sh
    ./test-pyra-movie.sh
    ./test-pyra-sharp-ui.sh
    ./build-pyra.sh
    ./run-pyra.sh

Pay particular attention to keys (including both Super keys), mouse/Alt+Enter, joystick, window close, intro/main-menu video, and shield-damage rumble.
