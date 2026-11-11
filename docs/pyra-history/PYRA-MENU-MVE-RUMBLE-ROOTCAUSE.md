# DragonBox Pyra — Menu movie + gameplay rumble regression fix

## Basis

Full source based on `Descent3-Pyra-PowerVR-GLES2-SDL3-gameplay-rumble-menu-fix.zip`.
The validated intro MVE-v5 video/audio fixes, native PowerVR EGL/GLES2 renderer,
performance features, bundled SDL3 and SDL2 rumble helper are preserved.

## Menu black/pixel blocks — diagnosed root cause

The menu **does use a movie**: `Descent3/mmItem.h` defines `MOVIE_MENU`.
Without an override bitmap, `mmItem.cpp` calls `StartMovie("mainmenu", true)`;
`FrameMovie` continues to decode `mainmenu.mve`. `-nointro` does not skip it.
The `PYRA_MENU_DUMP` knob applies only to the static `mainmenu.ogf` path,
so its absence in the previous log is expected.

`libmve/mvelib.cpp:mvefile_get_next_segment_size()` incorrectly read the
MVE 16-bit segment **length** as signed (`_mve_get_short`). The actual
43,310-byte menu video segment became `-22,226`, causing `mveplay.cpp`
to reject it as malformed and leave missing/stale video blocks on screen.
`mvefile_advance_segment()` *already* used an unsigned 16-bit length.

**Fix:** read segment length as **unsigned little-endian** (`_mve_get_ushort`),
check it fits inside the chunk, reject malformed/truncated segments, and log
the first three valid large segments. This preserves the confirmed RGB555
16-bit movement-vector and color-offset corrections in `decoder16.cpp`.

Verify without rebuilding Descent 3:

```sh
./test-pyra-menu-mve.sh
```

Run full game and look for:

```text
PYRA MVE: valid large segment len=43310 (16-bit unsigned)
```

`PYRA MVE VIDEO: malformed frame ... (data=-22226 ...)` must no longer occur.

## Gameplay rumble — evidence and changes

The 2026-11-02 diagnostics show that weapon shots and shield damage **do**
reach `ddio_pyra_GameplayRumble`, which writes to the SDL2 helper. Thus the
older problem with missing gameplay hooks has already been eliminated.
However the old 75-ms/38% weapon and 140-ms/23% damage commands are much
weaker than the known physically working startup probe. The helper also
allowed a later weak 100-ms legacy effect to replace a strong one; the
parent logged `accepted=1` merely after enqueueing IPC (not physical play).

**Changes:**

- Minimum gameplay event amplitude is 85% of motor full-scale **times the
  user's existing master gain**; minimum duration 260 ms, with a 380-ms
  gameplay cooldown. User gain zero still mutes rumble.
- Optional `PYRA_RUMBLE_GAME_TEST=1`: **first actual gameplay event only**
  uses the known test waveform (100%, 500 ms, subject to master gain).
- Helper protects an active strong pulse against weaker overlapping legacy
  commands and logs `PLAY accepted by SDL2 ...` / `SKIP ...` and timed stops.
- Game log reports `queued=1` for IPC transmission rather than claiming the
  motor actually played; a successful SDL2 return is still not physical
  verification. Helper retains its max-800-ms continuous burst limit and
  90-ms enforced rest. No direct evdev access, udev rules or sudo needed.

Recommended Pyra test:

```sh
./test-pyra-menu-mve.sh
./build-pyra.sh 2>&1 | tee build-pyra-menu-rumble.log
PYRA_RUMBLE_GAIN=1 PYRA_RUMBLE_GAME_TEST=1 \
  ./run-pyra.sh -nointro 2>&1 | tee pyra-menu-rumble-test.log
```

Fire a weapon once to trigger the full-strength 500-ms *gameplay* diagnostic,
then try more shots/receive damage. **Do not** use `PYRA_RUMBLE_TEST=1` for
this check, because that would also cause a separate startup impulse.

## Test status

`mvelib.cpp` regression test: passed with 43,310-byte segment and bounds check.
Helper built on x86_64; a fake SDL2 joystick integration test confirmed
`PLAY`, suppression of weaker overlapping commands, and timed stop.
`lnxforcefeedback.cpp` syntax-checked against packaged SDL3 headers.

Not yet tested against actual `mainmenu.mve` data or the physical Pyra motor.
This is an evidence-driven fix, not a claim of hardware validation.
