# Descent 3 – Pyra Gameplay Rumble & Main Menu Bitmap Fix

Based on the MVE-v5-Release (intro picture and sound hardware-confirmed).

## Gameplay haptics

* The SDL2 helper is still the first/preferred backend; the working pyrainput/SDL2 code is not changed.
* Initialize ForceInit only **after** InitControls, preventing initialization before the final controller is available.
* A local successful weapon spawn and local shield damage now call the very same low-level SDL2-helper/SDL3 output code used by the menu motor test. This avoids the legacy D3ForceFeedback effect-ID map in those two paths.
* Respect the in-game Enable Force Feedback preference, the PYRA_RUMBLE=0 override, and the configured master gain. Limit pulses to 45–240ms with a 95ms minimum interval. The helper maintains its own absolute motor deadlines.
* Add `PYRA RUMBLE GAME: ... accepted=...` logs for the first 24 events and warnings for disabled game FF or backend errors.

## Static menu art

* On this build the main menu **normally loads `mainmenu.ogf`**, not an MVE movie. The intro v5 decoder should not be changed to address this unrelated bitmap path.
* By default, submit only the menu-background 128x128 bitmap tiles with immediate-mode GLES2 draws rather than queuing them across texture switches. All other game geometry retains the previous batching and performance paths.
* Disable this mitigation for an A/B comparison with `PYRA_MENU_IMMEDIATE=0`.
* `PYRA_MENU_DUMP=1` writes the raw 16-bit CPU-side **un-tiled** bitmap (converted to PPM) to `/tmp/descent3-mainmenu-cpu.ppm` before any GLES2 upload. This isolates bad OGF data/CPU decode from texture-state bugs. If still faulty compare this PPM to the actual menu screenshot.
* `PYRA_BATCH=0` remains available as an independent whole-renderer A/B baseline.

## Build & verify on Pyra

```sh
./build-pyra.sh 2>&1 | tee build-pyra-menu-rumble.log
PYRA_MENU_DUMP=1 ./run-pyra.sh 2>&1 | tee pyra-menu-rumble.log
```

In a level, fire an ordinary primary weapon and let the ship actually lose some shields. The runtime log should contain `PYRA RUMBLE GAME: weapon-fired ... accepted=1` and/or `local-shield-damage ... accepted=1`. `accepted=1` confirms successful submission to the helper socket or SDL, **not physical motor movement**. If no log appears, send the complete log and verify the Force Feedback option is enabled. If the CPU menu PPM is clean but the screen shows black tiles, the fault is in GLES2 texture/rendering rather than OGF decode.

For comparison:

```sh
PYRA_MENU_IMMEDIATE=0 PYRA_MENU_DUMP=1 ./run-pyra.sh 2>&1 | tee pyra-menu-original-path.log
```

No changes to the successful MVE-v5 decoder, SDL3/PowerVR libraries, helper timer or upstream patches. Full ARMv7 hardware validation is still needed.
