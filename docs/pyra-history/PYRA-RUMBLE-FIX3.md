# DragonBox Pyra / Descent 3 — Rumble Fix 3

## Defect confirmed by the user's 2026-10-29 log

The previous `pyra-rumble-helper` sent `READY SDL2-joystick pyraInput Gamepad`
*before* attempting the first nonzero effect. The first actual SDL2
`SDL_JoystickRumble` returned **"That operation is not supported"**, but the
parent treated the helper as usable. As a separate issue, the game's force
feedback gain was **0.00**, so actual gameplay effects were filtered out.

## What changed

- `ddio/pyra_rumble_helper.cpp`: only reports `READY` after an actual nonzero
  effect has been accepted. Normal initialization sends one 60-ms diagnostic
  probe at full amplitude, waits 80 ms, and stops it. `--test` produces a
  500-ms impulse instead.
- Automatic SDL2 backend order: (1) joystick, (2) haptic opened **from the
  already-opened joystick**, (3) enumerated SDL haptic devices, preferentially
  `twl6040:vibrator`. No simultaneous haptic handles. Joystick is opened
  before the SDL2 haptic subsystem is initialized. A failed `RumblePlay`
  triggers cleanup and attempts the next backend.
- When the current backend fails during gameplay, the helper attempts to
  switch to another backend and sends a `BACKEND` status message.
- `ddio/lnxforcefeedback.cpp`: SDL3 fallback also validates a real, nonzero
  effect. The parent reads `FAIL` and `BACKEND` messages instead of trusting
  that merely queueing an IPC command proves rumble actually played. If the
  helper ultimately fails it may switch to an SDL3 fallback without discarding
  the registered gameplay effects.
- `Descent3/init.cpp`: initialize a missing `ForceFeedbackGain` registry key
  to 100%, rather than reading an uninitialized local byte.
- `Descent3/D3ForceFeedback.cpp`: recover legacy pilot configurations where
  force feedback is enabled but gain was incorrectly saved as 0%; the Pyra
  runtime restores 100% by default. `PYRA_RUMBLE_GAIN=0` explicitly mutes,
  and `PYRA_RUMBLE_GAIN=1` explicitly enables full gain. Disabling the game's
  Force Feedback toggle remains supported.
- No changes to PowerVR/EGL/GLES2, rendering resolution, texture IDs, SDL3
  gameplay controls, or ALSA/PulseAudio.

## Building on the Pyra

```sh
./build-pyra.sh 2>&1 | tee build-pyra.log
```

## First hardware test (without the game)

```sh
./build-pyra/build/pyra-rumble-helper --test 2>&1 | tee helper-test.log
```

The log should show one of:

- `selected backend=SDL2-joystick ...`
- `selected backend=SDL2-haptic-from-joystick ...`
- `selected backend=SDL2-haptic-separate ...`

The helper now returns nonzero rather than reporting `READY` when **every**
SDL2 path rejects playback. Accepted by SDL does **not** prove physical motor
movement: confirm the 500-ms vibration yourself.

For a specific experiment:

```sh
PYRA_RUMBLE_HELPER_BACKEND=fromjoystick ./build-pyra/build/pyra-rumble-helper --test
PYRA_RUMBLE_HELPER_BACKEND=separate     ./build-pyra/build/pyra-rumble-helper --test
```

## Test in the game

Your previous runtime log showed `gain=0.00`. This release automatically
restores 100% when the Force Feedback toggle is enabled but the saved gain
was 0. For a clean explicit test and a 500-ms pulse:

```sh
PYRA_RUMBLE_GAIN=1 PYRA_RUMBLE_TEST=1 ./run-pyra.sh 2>&1 | tee rumble-fix3.log
```

The high-level log should now include `gain=1.00`. Then test a wall impact or
weapon recoil, and look for `PYRA RUMBLE: game effect=...` lines. If those
lines never appear, the gameplay trigger was not reached, was paused, or the
saved Force Feedback toggle is disabled.

To make gain permanent, adjust the in-game Force Feedback Gain setting and
save the configuration. Disable the haptic motor with `PYRA_RUMBLE=0`.

## Limits

- No `/dev/input/event*` access, no udev rules, no root, no SDL2 development
  package. The helper loads the system SDL2 runtime dynamically.
- A simulator validated startup and late-playback fallback; **physical Pyra
  vibration is not verifiable off the device**.
- If all SDL2 paths fail, the game can fall back to SDL3. It should no longer
  falsely claim SDL2 helper initialization succeeded.
