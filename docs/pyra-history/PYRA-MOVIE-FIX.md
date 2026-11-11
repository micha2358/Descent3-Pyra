# Descent 3 on DragonBox Pyra – startup MVE movie correction

Base: Descent3-Pyra-PowerVR-GLES2-SDL3-upstream-rumble-stopfix.
The native PowerVR GLES2/EGL presentation, Performance 9 1024x576 internal
3D rendering, SDL3 game input and the now-working timed rumble helper remain
unchanged.

## Startup order

The **first** startup video is `movies/dolby1.mv8`, followed by
`movies/intro.mve` (`Descent3/descent.cpp`). Both are decoded by `libmve`
and uploaded through the same GLES2 bitmap renderer. A previous log line
`Movie playback interrupted (Escape): intro.mve` denotes user interruption,
not evidence of a decoder failure.

An earlier suspicion that SM_CINEMATIC closes the renderer is **not**
applicable to this tree: `SetScreenMode` immediately converts
`SM_CINEMATIC` to `SM_MENU` before reaching its renderer-initialization
branches. `Descent3/game.cpp` is therefore deliberately **unchanged**.

## Corrected defects

- `libmve/mve_audio.cpp`: the uncompressed audio decoder formerly read
  the channel mask as the PCM byte count and copied from `&data` (the stack
  address of a pointer) instead of `data` (the actual audio samples). It
  now skips the full six-byte MVE packet header and copies the PCM payload.
  This is a concrete memory-safety and audio-corruption defect.
- `libmve/mveplay.cpp`: silence packets formerly overwrote the compressed
  input instead of filling the audio output. Now they output 0x80 for
  unsigned 8-bit PCM and 0x00 for signed 16-bit PCM. Audio packets are
  checked for a valid header and sufficient segment length before decoding.
- `libmve/mveplay.cpp` and `libmve/mve_palette.h`: implement compressed
  MVE palette opcode 0x0D (32 mask groups covering 256 colors). It was
  previously ignored entirely, which can cause paletted 8-bit video to
  display with wrong/missing colors. The decoder validates all masks and
  RGB triplets before updating any palette entry.
- `Descent3/d3movie.cpp`: the palette callback now accepts a pointer
  directly to the first updated color, avoiding invalid pointer arithmetic
  from a partial palette chunk. Movie texture UV endpoints now correspond
  to the actual decoded image bounds in the power-of-two texture.
- `Descent3/d3movie.cpp`: full-screen MVE playback now fits the decoded
  picture into the 640x480 cinematic rectangle while preserving its
  aspect ratio (the PowerVR presentation path handles scaling from the
  logical canvas to the physical window). No-flip scripted movie sequences
  retain their original composition positions.

## Comparison and diagnostics

The new startup movie log includes the movie name, source pixel format,
video resolution, first-frame geometry, decoded frame count and whether
playback completed, was aborted by Escape, or failed to decode.

```sh
./build-pyra.sh 2>&1 | tee build-pyra-movie.log
./test-pyra-movie.sh
./run-pyra.sh 2>&1 | tee pyra-movies.log
```

A/B comparison with the **old native-sized movie rectangle** (without
reverting the PCM or palette corrections):

```sh
PYRA_MOVIE_SCALE=native ./run-pyra.sh 2>&1 | tee pyra-movies-native.log
```

Use `-nointro` only as a diagnostic workaround to omit both startup movies.
If movie playback remains visibly or audibly incorrect on the real Pyra,
retain the logs and note whether `dolby1.mv8` or `intro.mve` is affected.

## Validation limits

A host C++ syntax check passed for `Descent3/d3movie.cpp`,
`libmve/mveplay.cpp` and `libmve/mve_audio.cpp`. The new standalone test
validates uncompressed MVE PCM extraction and compressed-palette decoding,
including rejection of truncated palette updates.

Actual playback of the proprietary game movie assets and a full ARMv7
PowerVR runtime test cannot be performed in this environment; no claim of
physical verification is made.
