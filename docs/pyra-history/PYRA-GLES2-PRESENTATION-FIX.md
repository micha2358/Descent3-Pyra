# DragonBox Pyra — GLES2 rendering/presentation correction

This update builds on `PYRA-GLES2-LOADER-FIX.md` and preserves the working native
PowerVR EGL/GLES2 loader, SDL3 3.2.6, ALSA and the integrated dependencies.

## Why

The previous Pyra path disabled the desktop framebuffer blit entirely and
rendered directly into the SDL window framebuffer. Descent 3 changes its
internal rendering dimensions between menus, videos, and the main game.
Rendering at those logical sizes directly into a physical SDL window
explains content appearing in its lower-left area instead of being centered
and scaled. Additionally, the contents of a window backbuffer are not
preserved by the GLES/EGL swap contract, which may cause intermittent
visibility in legacy UI paths.

## Implementation

- `renderer/HardwareOpenGL.cpp`: Create an ES2-compatible RGBA texture FBO
  and a 16-bit depth renderbuffer at the requested logical resolution.
  Keep gameplay, menus, and movie rendering on this persistent FBO.
- At `rend_Flip()`, present that FBO through a GLSL ES 1.00 fullscreen quad
  to the actual SDL3 window pixel size, letterboxed to preserve aspect ratio.
  No ES3 `glBlitFramebuffer`, no Mesa, no GL4ES.
- Preserve/reinstate renderer GL state across the presentation pass:
  program, VBO, active texture, texture unit 0 binding, viewport,
  scissor, clear color, blend, depth, culling, and presentation attribute.
  Use shader attribute location 4 because Descent 3 uses locations 0–3.
- Stop multiplying Pyra window dimensions by SDL's display **content scale**.
  `SDL_GetWindowSizeInPixels` determines the real drawable output size.
- Correctly release FBO, texture, renderbuffer and presentation program on
  video reinitialization/shutdown, check `GL_FRAMEBUFFER_COMPLETE` and report
  a clear initialization error when the offscreen target is unsupported.
- `renderer/dyna_gl.h`: Add the few GLES2 core entry points used by the
  presentation pass to the existing PowerVR dlsym loader.

## Build and test on Pyra

```sh
./build-pyra.sh 2>&1 | tee build-pyra.log
./run-pyra.sh 2>&1 | tee run-pyra.log
```

Look for `PYRA GLES2: offscreen render WIDTHxHEIGHT, ES2 texture presentation enabled`.
Check the intro movies, main menu, loading indicator, and then 3D gameplay.
With a 4:3 internal menu resolution on a 16:9 display, black side bars are
expected because the desktop renderer also preserves aspect ratio.

This archive has not been compiled or executed on actual
Pyra/PowerVR hardware in this environment.
