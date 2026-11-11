# DragonBox Pyra Descent 3 — MVE v5 integration

## Provenance and baseline

This release starts from `Descent3-Pyra-PowerVR-GLES2-SDL3-gameplay-rumble-mve-audio-fix.zip` and integrates the changes in `corrected/` from `Descent3-Pyra-MVE-Intro-Diagnose-v5-RGB555-fix.zip`.

The user confirmed **artifact-free picture and audio** for `intro.mve` in the standalone v5 Pyra player. Full Descent 3 SDL3 playback is **not yet hardware-verified**; the MVE decoder is shared, but the complete game uses its own SDL3 audio and GLES2 image presentation paths.

## Changed production files

- `libmve/decoder16.cpp`: signed 8-bit motion vectors for opcode 0x05 (no `char` ambiguity on armhf); correct second RGB555 pixel offset for opcode 0x0F; optional RGB555 block-opcode summary after each movie.
- `libmve/mve_audio.cpp`: bounded DPCM decoding outputs the initial predictor samples as the first PCM samples and then interleaved deltas, with explicit little-endian output, proper mono/stereo handling and bounds checks. Uncompressed PCM path retained.
- `libmve/mveplay.cpp`: passes real channel count and reports decoder-opcode histogram.
- `libmve/mve_audio.h`, `libmve/decoders.h`: matching declarations.
- `libmve/movie_sound.cpp`, `libmve/movie_sound.h`: SDL3 movie sound device exposes its actual channel count to the MVE decoder. No changes to SDL3 playback backend, audio device selection or SDL lifecycle.

## Preserved unchanged

- Existing PowerVR GLES2/EGL renderer, GL batching/orphaning/presentation and platform-specific launcher.
- SDL3 game input and SDL2 force-feedback helper, time guard, gameplay weapon and damage triggers.
- All prior Pyra video fixes, game-data discovery, existing build scripts, and the three upstream commits 76049bb, 9522300 and 66a0d07.

## Verification

```sh
./test-pyra-movie.sh
./build-pyra.sh 2>&1 | tee build-mve-v5-release.log
./run-pyra.sh 2>&1 | tee run-mve-v5-release.log
```

`test-pyra-movie.sh` performs PCM/palette/DPCM regression tests and the RGB555 opcode regression test with both `-funsigned-char` and `-fsigned-char`.

The standalone v5 player remains available independently for rapid A/B investigations. The release archive does not contain the proprietary movie/game data.
