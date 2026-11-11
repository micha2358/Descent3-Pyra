# Descent 3 / DragonBox Pyra — Rumble and missing reticle assets

Based on the user's *Rumble auf der DragonBox Pyra* implementation report.

## Why `phx_pr0a.ogf`, `phx_se1a.ogf`, etc. are requested

The original game implements ship/HUD-specific reticles in `Descent3/hud.cpp`.
The HUD configuration can set `reticleprefix` (`ret`, `phx`, `mag`, etc.).
`InitReticle()` concatenates this prefix with `_pr0a.ogf`, `_se1a.ogf`, etc.
This is **not caused by the GLES2 port**. Some genuine retail datasets do
not contain all custom-prefix images. The Pyra edition checks archive file
visibility first, then uses the standard `ret` prefix **only for missing custom
images**. No data archive is modified and genuinely missing images remain
missing. A one-time informational log reports fallback activation.

## Original gameplay effects (already wired into Descent 3)

- `DoForceForWall`: local player's wall collision, `hitspeed >= 20`, strength
  increases with impact speed (100 ms effect).
- `DoForceForRecoil`: locally fired weapon with `recoil_force >= 1100`, 100 ms.
- `DoForceForWeapon`: microwave weapon impact, 600 ms original effect.
- `DoForceForShake`: ship shake/damage, 200 ms and original 200 ms throttle.
- `physics.cpp`: an applied impact force on the local player triggers
  `FORCE_TEST_FORCE` scaled to impulse magnitude (100 ms).
- Vauss/afterburner effect definitions exist but have no direct gameplay
  invocations in the source examined. They are **not artificially triggered**.
- The original `D3ForceFeedback.cpp` calls `ForceEffectsPlay`, which now reaches
  the new `ddio/lnxforcefeedback.cpp` SDL3 backend.

## SDL3 ownership and playback

- Game input opens SDL joysticks first (`ddio/sdljoy.cpp`). The rumble backend
  **borrows the same open joystick**, never opens a second one.
- Prefer `SDL_RumbleJoystick()` with equal intensities on both channels (the
  Pyra has one physical motor). If unsupported, initialize SDL haptics after
  joystick setup and try `SDL_OpenHapticFromJoystick()`, then enumerated
  haptics preferring names containing `twl6040` or `vibrator`.
- Only one backend is active, not multiple simultaneous effect owners.
- Original effect IDs, durations and gain scaling are preserved; duration is
  safety-capped at 650 ms, recurring event playback is debounced.
- Force effects stop when paused/disabled; haptics close **before** the
  gameplay joystick is released (`joy_Close()`), then SDL can shut down.
- There is no `EV_FF` direct access, fixed `/dev/input/event*`, root, or udev.

## Tests

Build normally: `./build-pyra.sh 2>&1 | tee build-pyra.log`

Normally start: `./run-pyra.sh 2>&1 | tee run-pyra-rumble.log`

For a **single 500 ms full-strength test at startup**:

`PYRA_RUMBLE_TEST=1 ./run-pyra.sh 2>&1 | tee rumble-test.log`

Startup test is disabled by default; success of SDL initialization alone
is not proof that a physical impulse occurred. Test a real wall collision
in-game after the startup test.

- `PYRA_RUMBLE=0` disables the backend.
- `PYRA_RUMBLE_BACKEND=joystick` forces the existing joystick path.
- `PYRA_RUMBLE_BACKEND=haptic` forces haptics (initialized after joystick).
- Unset `PYRA_RUMBLE_BACKEND` for automatic selection.
- In the game's Force Feedback configuration, ensure it is **enabled** and
  gain > 0. Existing pilot settings (`EnableJoystickFF`, `ForceFeedbackGain`)
  are respected.

If the startup test says **Bad file descriptor**, examine the selected SDL
backend, the joystick/haptic opening order and any other SDL controller
opens. Do not restart pyrainput or add udev rules until those are ruled out.
