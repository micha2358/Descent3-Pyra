# Descent 3 – Pyra gameplay-rumble and movie-audio update

Base: previous `Descent3-Pyra-PowerVR-GLES2-SDL3-movie-fix.zip`.

## Gameplay rumble

`Descent3/damage.cpp`: The confirmed local player shield-loss function now triggers
`FORCE_WALLHIT` with intensity proportional to actual shield damage; remote-player
hits, damage blocked by invulnerability, and menu interaction never trigger it.

`Descent3/WeaponFire.cpp`: A successfully created weapon fired by the local player
triggers `FORCE_TEST_FORCE` for normal weapons. The original high-recoil weapon
path is preserved. The lower layer already rate-limits events and enforces motor
safety. The working SDL2-helper/device path was **not changed**.

Look for `PYRA RUMBLE: game effect=... submitted=1` following shooting or
confirmed shield damage. If there are no gameplay messages, inspect high-level
`ForceEffectsPlay`; if submitted=1 but no motor impulse, inspect helper IPC and
`pyrainput` state. `PYRA_RUMBLE_TEST=1` is a separate startup-only diagnostic.

## MVE audio

`libmve/mveplay.cpp` no longer rejects all compressed packets based on an
unverified ratio of compressed/decoded sizes. `libmve/mve_audio.cpp` now provides
bounded PCM/DPCM processing, padding only missing data with silence instead of
reading past the packet. `libmve/movie_sound.cpp` logs SDL3 audio-device open,
resume and queue errors. **Only actual hardware playback can confirm sound**.

## Video block artifacts

The previous compressed palette handler is retained (it matches the MVE opcode
0x0d layout). This update validates frame headers and coding map sizes before
passing them to 8-/16-bit block decoders. We have *not* established whether the
remaining blocks originate in compressed MVE frames or the PowerVR texture
upload, because original `.mve/.mv8` inputs have not been supplied.

To isolate the layer, run:

```sh
PYRA_MOVIE_DUMP=1 ./run-pyra.sh 2>&1 | tee pyra-movie-dump.log
```

This writes decoded **pre-GPU** frame snapshots to
`/tmp/descent3-mve-dumps/frame-*-video-frame-*.ppm` (frames 0, 20 and 60
of each film, up to 12 dumps per process). View those with an image viewer.
If the PPM itself has blocks, fix the MVE decoder; if the PPM is clean and the
screen shows blocks, focus on GL texture upload and GPU synchronization.

To compare the performance/renderer batching path separately:

```sh
PYRA_BATCH=0 ./run-pyra.sh 2>&1 | tee pyra-movie-nobatch.log
```

Note `PYRA_BATCH=0` changes rendering in all modes and is a diagnostic, not the
recommended high-performance default.

## Tests

```sh
sh ./test-pyra-movie.sh
./build-pyra.sh
./run-pyra.sh
```

Original videos were unavailable here. No actual Pyra full-build or gameplay
validation was performed. The ZIP preserves all three upstream commits, native
PowerVR GLES2, 1024x576 3D scaling, and the SDL2 rumble helper/time guard.
