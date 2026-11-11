# Descent 3 / Pyra — isolated SDL2 force feedback (second fix)

## Cause in the earlier version

`ddio/lnxforcefeedback.cpp` previously used `SDL_RumbleJoystick(joystick, 0, 0, 20)` as an initialization probe. This is *not a physical motor test*: a zero-strength rumble can return success while producing no vibration. The code then selected the SDL3 joystick backend and never attempted a known working alternative. If the low-level initialization failed before the device was ready, `ForceInit` also created no valid gameplay effect handles.

We cannot establish solely from source code which additional evdev/pyrainput behavior occurs on the specific handheld. A successful SDL function return is **not** confirmation that the motor vibrated. This release instead chooses the working, documented SDL2 joystick route by default and exposes direct diagnostics.

## Architecture

- Existing SDL3 `pyraInput Gamepad` handle stays in the game, for input only.
- New binary `build-pyra/build/pyra-rumble-helper` is launched automatically when D3 initializes FF.
- It loads **system libSDL2-2.0.so.0** dynamically; no `libsdl2-dev` is required. It opens the virtual joystick and owns the only FF effect in its process.
- A bidirectional UNIX socket carries short commands (`R magnitude duration`, `S`, `Q`). Socket sends do not raise SIGPIPE. The helper logs the selected device and playback errors; it shuts down before joystick cleanup.
- A real 500 ms diagnostic impulse is possible, without any new udev rules, direct `EV_FF`, or root privileges.
- Existing gameplay FF definitions and event triggers (wall hit, recoil, microwave, ship shake and applied impact impulse) are preserved.
- Existing graphical, SDL3 input and native GLES2/PowerVR rendering code is otherwise unchanged.

## Build

```sh
./build-pyra.sh 2>&1 | tee build-pyra.log
```

Both `Descent3` and `pyra-rumble-helper` are in `build-pyra/build/`.

## Test just the new helper (without the game)

```sh
./build-pyra/build/pyra-rumble-helper --test 2>&1 | tee helper-test.log
```

A full-strength 500-ms pulse should be felt. This uses SDL2 `SDL_JoystickRumble` and does not touch the game's SDL3 context.

For a *separate* SDL2 haptic test:

```sh
PYRA_RUMBLE_HELPER_BACKEND=haptic ./build-pyra/build/pyra-rumble-helper --test
```

Choose only the backend that works. `SDL2` means the SDL2 *runtime* library, not development headers.

## In-game test and logging

```sh
PYRA_RUMBLE_TEST=1 ./run-pyra.sh 2>&1 | tee rumble-fix2.log
```

You should see a line similar to `PYRA RUMBLE: isolated helper initialized: SDL2-joystick pyraInput Gamepad` and `PYRA RUMBLE HELPER: first SDL2 effect accepted by API`. Confirm the pulse *physically*. When a gameplay trigger occurs, the first eight instances are logged with effect ID, strength, duration and whether the IPC submission succeeded.

Filter the log:

```sh
grep -Ei 'PYRA RUMBLE|Force: High Level' rumble-fix2.log
```

If there are no `game effect` lines, the original D3 trigger was not reached, or the pilot has disabled force feedback / set gain to zero. The `D3 high-level ready` message reports the enabled state and gain.

## Backend selection

- Default: **isolated SDL2 helper**, otherwise an available SDL3 fallback.
- `PYRA_RUMBLE_BACKEND=helper`: require the helper. Report a failure rather than silently using another SDL backend.
- `PYRA_RUMBLE_BACKEND=joystick`: test SDL3 joystick rumble on the existing gameplay handle.
- `PYRA_RUMBLE_BACKEND=haptic`: test SDL3 haptic rumble after the joystick was opened.
- `PYRA_RUMBLE_HELPER_BACKEND=joystick`: force the SDL2 helper's direct joystick path (the default).
- `PYRA_RUMBLE_HELPER_BACKEND=haptic`: force the separate SDL2 haptic path in the helper.
- `PYRA_RUMBLE_HELPER_PATH=/absolute/path`: override the helper path if needed.
- `PYRA_RUMBLE=0`: disable force feedback completely.

The helper is dynamically linked only to libc and libdl; the bundled SDL3 continues to be statically linked into the game.

## Limits

Hardware confirmation requires a test on the actual Pyra. If the helper's standalone SDL2 test fails as well, the issue is device selection or the runtime's joystick/haptic behavior, not the game's trigger logic. The helper logs exact API failures to make that distinction clear.
