# Pyra GLES2 performance build (textureidfix4 base)

This variant preserves native PowerVR EGL/GLES2, the FBO presentation, 32-bit texture object names and all game effects.

## Changes

- Uniform-controlled fragment branches avoid unnecessary texture fetches, fog distance calculations and neutral-gamma `pow()` operations; fog/gamma remain available when enabled.
- Vertex uploads no longer allocate a `std::vector` for each small polygon: raw contiguous pointers upload directly, other small iterators use stack staging.
- Avoid redundant texture-enable uniform updates.
- Opt-in `PYRA_PERF=1` writes average FPS, drawn polygons, texture uploads, present CPU time and swap CPU time every ~5 seconds. Note that CPU timing of GL command submission does not measure GPU execution time.

## Test

```sh
./build-pyra.sh 2>&1 | tee build-pyra.log
env -u PYRA_GL_CALL_TRACE -u PYRA_GL_TRACE PYRA_PERF=1 ./run-pyra.sh 2>&1 | tee run-pyra-perf.log
```

Compare an identical scene at 640x480 and 1280x720. The latter has 3x as many pixels, increasing fragment work and FBO bandwidth. To measure base performance, do not enable `PYRA_GL_CALL_TRACE` (expensive per-GL-call checking).

Caveat: uniform branching on PowerVR SGX may still be flattened by the GLSL compiler; profile to measure real benefit.
