# Pyra Descent 3: Rumble stop/watchdog fix

## Confirmed on user's Pyra

Rumble worked after restarting the existing `pyrainput` service using
`sudo systemctl restart pyrainput`. The motor then ran continuously during
`PYRA_RUMBLE_TEST=1 ./run-pyra.sh` rather than stopping after the intended
500 ms.

The working backend-selection and device-opening path from Rumble-Fix 3 is
**unchanged**. This update only addresses the missing explicit stop and
excessively frequent/repeated impulses.

## Changes

- `ddio/pyra_rumble_helper.cpp`: an independent `std::chrono::steady_clock`
  deadline and `poll()` loop guarantee that a STOP command reaches SDL2
  after every accepted `R magnitude duration` command, even when there is no
  subsequent input. No reliance on SDL2 or pyrainput enforcing the requested
  duration internally.
- Rapid repeated effects are limited to a continuous motor burst of at most
  800 ms, followed by a quiet period of at least 90 ms. Normal individual
  impulses use their requested duration (clamped to 25...800 ms as before).
- Explicit SDL2 joystick and haptic stops now log any SDL-level failure.
- `ddio/lnxforcefeedback.cpp`: when `PYRA_RUMBLE_TEST=1`, Descent 3 plays
  **one** 500-ms startup impulse, then explicitly issues STOP after 530 ms.
  The helper watchdog stops it after 500 ms even if the game does not.
  Repeated reinitialization does not restart the diagnostic impulse.
- No change to native PowerVR GLES2 / EGL, presentation, SDL3 input,
  1024x576 internal scaling, or upstream commits.

## Build and test on Pyra

```sh
./build-pyra.sh 2>&1 | tee build-rumble-stopfix.log
PYRA_RUMBLE_TEST=1 ./run-pyra.sh 2>&1 | tee rumble-stopfix.log
```

On startup, the motor should vibrate **once for about 0.5 s**, then stop.
The log should contain:

```
PYRA RUMBLE: one 500-ms diagnostic impulse started
PYRA RUMBLE HELPER: explicit timed rumble stop issued ...
PYRA RUMBLE: 500-ms diagnostic explicitly stopped
```

Test normal gameplay without the explicit startup diagnostic:

```sh
./run-pyra.sh 2>&1 | tee rumble-normal.log
```

Independent helper test (also performs its own explicit stop):

```sh
./build-pyra/build/pyra-rumble-helper --test
```

If the helper reports an explicit stop but the physical motor continues,
this is an important difference: the SDL/pyrainput **stop operation is not
being honored**. In that case the running test/game should be terminated,
then the already established `pyrainput` restart may be necessary to clear
the stuck motor; that service action is not executed by the game and does
not prove which underlying driver layer ignored the stop.

## Verification on development host

- C++17 helper compiles with warnings enabled.
- Modified SDL3 adapter passes a syntax-only compile using bundled SDL3.
- Mock SDL2 runtime integration test: single 500-ms impulse explicitly
  stopped after 500 ms; 28 rapid commands result in explicit stop(s),
  and standalone `--test` terminates with an explicit stop.
- Actual motor behavior on Pyra requires hardware validation.
