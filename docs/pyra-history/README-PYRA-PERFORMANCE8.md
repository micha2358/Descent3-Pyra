# Descent 3 – DragonBox Pyra / Performance 8

Based on the working Performance 7 GLES2/SDL3 port. The existing PowerVR
library loading, `GLuint` texture IDs, GLES2-compatible packed uploads and
Offscreen-FBO presentation have not been changed.

## Changes

- `PYRA_STREAM=orphan` is the **default**. The PowerVR driver was measured to
  spend about 5 ms/frame in vertex uploads instead of about 116 ms/frame with
  the previous ring-buffer method in non-identical 1280×720 3D scenes.
- `PYRA_BATCH=1` still uses the order-preserving smart polygon queue.
  `PYRA_BATCH=0` turns it off *without changing the VBO streaming method*.
- `PYRA_STATE_CACHE=1` (default) suppresses safely repeated GLES2
  `glEnable`/`glDisable` capability states, `glBlendFunc`, `glDepthMask`,
  `glDepthFunc`, `glViewport`, and `glScissor`. The existing Performance 7
  binding no-op suppression remains in place. `PYRA_STATE_CACHE=0` disables
  just these new state reductions.
- `PYRA_PERF=1` records the exact GL function names that actually flushed a
  **nonempty** geometry queue, with top-12 breakdown. It also counts skipped
  redundant GL calls. No GL error polling is enabled by this profiler.
- Batching still flushes before a non-redundant, order-dependent GLES2 state
  mutation. It does **not** sort or reorder transparent geometry.

## Controlled benchmarks

First compile with `./build-pyra.sh`. Then run **each mode separately**:

```sh
./benchmark-pyra.sh baseline    # orphan, NO batching
./benchmark-pyra.sh batch       # orphan, batching and new cache
./benchmark-pyra.sh stateoff    # orphan, batching, new cache OFF
./benchmark-pyra.sh ring        # ring, batching (slow reference)
```

Optionally `./benchmark-pyra.sh conservative` uses the old conservative
barriers with orphan streaming. The resulting logs are
`pyra-perf8-<mode>.log` next to the script.

Measure the *same* level, static camera position, and resolution for each run;
repeat at 640×480 and 1280×720 separately. Wait for at least 3 five-second
intervals **after** loading finishes. Prefer medians of comparable polygon
counts, not the FPS from menus or level-loading screens.

```sh
python3 compare-perf8.py --csv perf8-windows.csv pyra-perf8-*.log
```

### New log keys

- `PYRA PERF8 FLUSH_TOP/frame:` top exact GL functions that flushed a pending
  polygon queue, e.g., `glEnable=NN glDepthMask=NN glViewport=NN`.
- `PYRA PERF8 NOOP/frame:` how many redundant GL function calls were avoided,
  grouped by state.
- `PYRA PERF8:` active streaming and cache settings, draws/frame, CPU upload
  time/frame, and polygon count.
- `PYRA PERF` and `PYRA PERF7` remain for backward-compatible comparisons.

The profiler reports **CPU wall time around GL calls**, not GPU execution
time. Driver synchronization can occur inside any GL call (including swap).
Testing on actual SGX544MP2 hardware is essential; the ZIP is not a
hardware-validated performance result.

## Troubleshooting / rollback

- Rendering anomaly only with Performance 8 new no-op caching:
  `PYRA_STATE_CACHE=0 ./run-pyra.sh`.
- Rendering anomaly only with batching: `PYRA_BATCH=0 ./run-pyra.sh`.
- Compare old streaming: `PYRA_STREAM=ring ./run-pyra.sh` (expected slower).
- Existing targeted GL validation (slow!): `PYRA_GL_CALL_TRACE=1 ./run-pyra.sh`.
- No additional Mesa/GL4ES dependency introduced.
