# Descent 3 — DragonBox Pyra PowerVR GLES2 port

Target: Debian 12 armhf, OMAP5432 Cortex-A15, PowerVR SGX544MP, native OpenGL ES 2.0.

## Graphics policy
- `PYRA_GLES2` is enabled by default.
- No `libGL.so.1` fallback is used in the Pyra path.
- The requested GL library is `/opt/omap5-sgx-ddk-um-linux/lib/libGLESv2.so.1`.
- Runtime rejects a renderer that is not Imagination/PowerVR SGX.
- GLES2 context: 2.0 ES profile.
- GLSL ES 1.00 shaders.
- VAOs, `glMapBufferRange`, `glGetStringi`, and framebuffer blitting are avoided on Pyra.
- Game rendering uses a GLES2 RGBA offscreen framebuffer; a textured quad scales it to the actual window pixel dimensions when presented.
- The screen contents survive EGL buffer swaps because the offscreen framebuffer is persistent.

## Build
`./build-pyra.sh`

The upstream tree currently uses SDL3. This port keeps SDL3 for the platform/input/audio layer while converting the renderer to native GLES2. Replacing SDL3 with SDL2 is a separate API migration and is not disguised here as a completed change.

## Run
`./run-pyra.sh`

The PowerVR userspace DDK is expected in `/opt/omap5-sgx-ddk-um-linux/lib`.
