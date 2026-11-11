# Descent 3 — DragonBox Pyra native EGL/GLES2 loader correction

This archive builds on `Descent3-Pyra-PowerVR-GLES2-SDL3-httplib-plog-openssl.zip`.

## Findings

The prior executable loaded both system GLVND (`libGLESv2.so.2`, `libGLdispatch.so.0`) and native PowerVR GLES2 (`libGLESv2.so.1`), and returned `(null)` for all `glGetString` queries and `0` for `glCreateShader`.

SDL3 (3.2.6) uses the `SDL_GL_LoadLibrary()` **argument as an EGL library path** in the X11/OpenGL ES backend; its GLES library path is selected via `SDL_HINT_OPENGL_LIBRARY`. In the old renderer the GLES library path was supplied to `SDL_GL_LoadLibrary` before the OpenGL ES context profile was selected. Also, SDL3's boolean `SDL_GL_LoadLibrary()` result was incorrectly tested as if it were SDL2's negative-on-failure result.

## Changes

* `renderer/HardwareOpenGL.cpp`: set SDL hints for X11/EGL and the PowerVR GLES library; fix SDL3 `SDL_Init` boolean check; select ES2 profile **before** `SDL_GL_LoadLibrary`; pass the PowerVR **EGL** library path to SDL; verify `SDL_GL_MakeCurrent`; log GL vendor, renderer, version, GLSL version and a temporary `glCreateShader` test.
* `renderer/dyna_gl.h`: correctly check SDL3 `SDL_GL_LoadLibrary`'s boolean return; explicitly open native EGL first; resolve GLES2 calls (and optional fatal GL error checks) directly from native PowerVR GLES with `dlsym` instead of potentially retrieving GLVND dispatch symbols.
* `renderer/CMakeLists.txt`: explicit dynamic loader link dependency.
* `run-pyra.sh`: export SDL driver hints before any SDL3 initialization.

The native driver files are **not** included in this archive; it expects the Pyra installation at `/opt/omap5-sgx-ddk-um-linux/lib`. SDL3, plog and cpp-httplib remain bundled. No Mesa/GL4ES shim was added.

## Build and test

```sh
./build-pyra.sh 2>&1 | tee build-pyra.log
./run-pyra.sh 2>&1 | tee run-pyra.log
```

Look for `PYRA GL_VENDOR`, `PYRA GL_RENDERER`, `PYRA GL_VERSION`, and `PYRA glCreateShader(...)`. They must show a valid PowerVR context and a nonzero shader id.

**Status:** Source modifications and static checks performed; runtime operation on actual PowerVR SGX544MP2 hardware has not been verified here. If `glGetString` is still null, investigate native EGL current-context values and loaded library symbols.
