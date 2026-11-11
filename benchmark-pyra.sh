#!/usr/bin/env bash
# Descent 3 / DragonBox Pyra - Performance 9.
# Run matching scenes: same level, camera, settings, and logical 1280x720 mode.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
MODE=${1:-}
BATCH=1 SMART=1 STATE_CACHE=1 STREAM=orphan INTERNAL=1 VSYNC='' SYNC=none
case "$MODE" in
  batch|scaled)       : ;;                        # recommended 1024x576 -> 1280x720
  native)             INTERNAL=0 ;;              # exact native 1280x720 A/B
  baseline)           BATCH=0 ;;                 # orphan no batching, scaled
  stateoff)           STATE_CACHE=0 ;;
  ring)               STREAM=ring ;;
  conservative)       SMART=0 STATE_CACHE=0 ;;
  vsync0)             VSYNC=0 ;;                 # optional diagnostic
  vsync1)             VSYNC=1 ;;
  flush)              SYNC=flush ;;             # GPU sync diagnostic
  finish)             SYNC=finish ;;            # slow, diagnostic only
  *)
    cat >&2 <<USAGE
Usage: $0 batch|native|baseline|stateoff|ring|conservative|vsync0|vsync1|flush|finish
  batch       default, render 1024x576 -> output 1280x720; orphan + smart batch
  native      render native 1280x720; same other settings for A/B comparison
  baseline    1024x576 + orphan, no polygon batching
  stateoff    1024x576 + orphan, no new GL state caching
  ring        legacy VBO streaming; expected slow
  conservative original batch barriers
  vsync0/1    force SDL/EGL swap interval 0/1 (if the driver supports it)
  flush       add glFlush before presentation (diagnostic only)
  finish      add glFinish before presentation (diagnostic, forces GPU wait)
Always select the 1280x720 resolution IN the game for the 1024x576 feature.
USAGE
    exit 2 ;;
esac
OUT="$ROOT/pyra-perf9-$MODE.log"
echo "PERF9 mode=$MODE internal1024=$INTERNAL batch=$BATCH stream=$STREAM vsync=${VSYNC:-driver} gpuSync=$SYNC"
echo "Select logical 1280x720 and use the SAME 3D scene for batch and native."
echo "Logging to $OUT"
(
  cd "$ROOT"
  env -u PYRA_GL_CALL_TRACE -u PYRA_GL_TRACE \
      PYRA_BATCH="$BATCH" PYRA_SMART="$SMART" PYRA_STATE_CACHE="$STATE_CACHE" \
      PYRA_STREAM="$STREAM" PYRA_3D_1024="$INTERNAL" \
      PYRA_VSYNC="$VSYNC" PYRA_GPU_SYNC="$SYNC" PYRA_PERF=1 \
      ./run-pyra.sh
) 2>&1 | tee "$OUT"
