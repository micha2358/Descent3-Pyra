# Pyra Runtime Fixes (based on Performance 9)

## Corrected
- Normal game exit and SDL window-close path explicitly dispose the native
  OpenGL ES renderer before calling `SDL_Quit`. A `glFinish` on shutdown
  ensures pending SGX work completes before EGL device-context destruction.
- Normal ALSA period diagnostics from vendored SDL 3.2.6 no longer appear as
  `ERROR`. Real ALSA errors still get logged.
- Empty optional CFILE base paths are ignored without a warning.
- Offline legacy table-management mode and missing optional `Table.loc`
  are logged as informational conditions.
- User-aborted movies (`-6`, Escape) are not reported as decoder errors.
- OSIRIS no longer attempts to open an empty *default* script module name;
  level, mission, and custom script binding still takes place.

## Still dependent on external game data
- `phx_*.ogf`/`ret_*.ogf` reticle art belongs to the game installation and
  cannot be synthesized from the source archive. Use `check-pyra-assets.py`
  to inspect the installed HOG2 archives and standalone files.
- Missing named script modules (`BlueDev.so`, etc.) require corresponding
  platform-compatible game/mission scripts. We do not substitute unrelated
  modules or silence failures for nonempty module names.
- The Linux force-feedback backend in `ddio/lnxforcefeedback.cpp` is a stub.
  `Force Feedback System Not Found` is genuine until rumble support is ported;
  we intentionally do not simply suppress it.

## Performance
- The established fast `orphan` streaming path, batching and 1024x576-to-
  1280x720 GLES2 upscaling are untouched.
- On the supplied test runs with 700–1000 polygons/frame: native 1280x720
  averaged 16.4 FPS (6 measurements); internal 1024x576 averaged about
  21.0 FPS (5 measurements), approximately +28%, not scene-matched.

## How to test
```sh
./build-pyra.sh 2>&1 | tee build-pyra.log
./run-pyra.sh 2>&1 | tee run-pyra-fixed.log
./check-pyra-assets.py /path/to/your/descent3-game-data
```
Normal quit should now log `PYRA: releasing renderer and GPU resources before SDL_Quit`.
The absence of PowerVR teardown warnings must be confirmed on the device.
