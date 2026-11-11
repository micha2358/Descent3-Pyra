# DragonBox Pyra — Descent 3 upstream integration, October–November 2026

Date checked: 2026-11-03
Upstream: https://github.com/DescentDevelopers/Descent3
Upstream `main` examined through: `7c312a85e65c1e81003f6919535fcc688f299401` (2026-11-02)
Local base: `Descent3-Pyra-PowerVR-GLES2-SDL3-Sharp-Ingame-UI-Release.zip`

## Scope and completeness

This is a **selective source integration**, not a claim that all 80 commits
in upstream's October-November range are merged. The main branch has 80
commits (including merge commits) between the first October change on
2026-10-22 and 2026-11-02. Three merge commits from 2026-10-23 (76049bb,
9522300, 66a0d07) were already integrated into the local base.

The following upstream work was manually adapted to the existing native ARMv7
PowerVR/SDL3 Pyra tree. Some groups represent chains of dependent commits.

| Upstream SHA | Adapted behavior |
|---|---|
| 964df314 | Linux data directories use lowercase install path, handle case-insensitive legacy directory name and absent system data directory. Portable Pyra install preserved. |
| dd3e6b5d | Case-insensitive directory test uses filesystem identity, not case-sensitive path strings. |
| 55488696 | `-randseed` command line option. |
| f3e0e8fc | Seed `rand()` and `ps_rand()` before I/O initialization. |
| 3949c414 | Validate unsigned 32-bit decimal `-randseed` strictly. |
| e422c419 | Stop reseeding the game RNG from the clock while generating temp names. |
| 867151d9 | Thread-local private PRNG for temporary file names; add regression test. |
| 13fe0394 | Stop single-player/gameplay time-based reseeding in gravity effects, room flicker, and scripted thieves. Multiplayer/demo re-seeding intentionally left untouched. |
| a185d23c | Let MVE playback delays use the `ChronoTimer` interface. |
| 9bae35c6 | Sleep rather than busy-spin during post-level results screens. |
| 046534cf | Fix a stray closing parenthesis in the optional non-RTL memory configuration. |
| 1e8dd7af | Add an owned `mem_strdup_unique()` helper. |

Three already-integrated upstream merges: `76049bb`, `9522300`, `66a0d07`.

## Changes intentionally not integrated into this Pyra build

* **Mechanical `NULL` -> `nullptr` campaign** and tooling: several hundred
  changed source files (including OS-specific code), plus changes to variadic
  sentinels and signatures. Blind replacement risks varargs and platform-specific
  correctness. This is a source modernization, not a required runtime feature.
* **Optional mock-clock/testing infrastructure** (`ENABLE_MOCK_CLOCK`,
  platform presets, timer implementation, test harness): this is for deterministic
  CI, not normal gameplay. The real clock and Pyra framecap remain untouched.
* **Network plugin logging and `std::filesystem::path` ABI refactor**:
  affects game DLL modules and exported function-pointer interfaces; requires
  an entire coordinated rebuild and multiplayer regression tests.
* **macOS signing/notarization, Windows CI images, vcpkg baseline, GitHub Actions**:
  they do not apply to native Pyra binary operation.
* **Other upstream refactor work** is not represented as integrated until its
  behavior and platform-specific ABI effects are verified.

This archive is therefore **not an official upstream checkout** and should
not be presented as a complete October–November upstream merge.

## Pyra invariants retained

* PowerVR SGX544MP2 hardware OpenGL ES 2.0 + EGL without Mesa/GL4ES fallback.
* Local SDL3 runtime, ALSA/PulseAudio handling, PowerVR batch/VBO optimizations.
* 1024x576 gameplay framebuffer and sharp 1280x720 in-game UI.
* Fixed RGB555 MVE block decoding, stereo-DPCM audio, large MVE segments,
  and animated main menu video.
* No rumble on weapon fire. Rumble on local shield damage, with motor timer.
* Existing pilot control defaults and remappings not silently altered.

## Verification and known limits

Host `g++ -std=c++17 -fsyntax-only` passed for the touched game-side files
(`init.cpp`, `screens.cpp`, `render.cpp`, `fireball.cpp`, `scripts/AIGame.cpp`,
`ddio/file.cpp`, `mem/mem.cpp`, `cfile/cfile.cpp`, `libmve/mveplay.cpp`)
with the repository's SDL3/plog headers, generated compile-time path header,
and Linux/POSIX platform defines. `scripts/AIGame.cpp` has pre-existing
integer overflow warnings unrelated to this merge.

`./test-pyra-movie.sh`, `./test-pyra-menu-mve.sh`,
and `./test-pyra-sharp-ui.sh` pass on the host.

A full executable or hardware runtime test **was not possible** in this
runtime because CMake could not find a configured SDL3 development package.
The actual native ARMv7/PowerVR runtime requires confirmation on the Pyra.

Build and test on the Pyra:

    ./test-pyra-movie.sh
    ./test-pyra-menu-mve.sh
    ./test-pyra-sharp-ui.sh
    ./build-pyra.sh
    ./run-pyra.sh -nointro

A unified source patch against the prior Sharp UI ZIP is distributed
separately; the archive already contains its changes.
