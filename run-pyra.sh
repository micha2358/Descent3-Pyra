#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export LD_LIBRARY_PATH=/opt/omap5-sgx-ddk-um-linux/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export SDL_VIDEODRIVER=x11
# Select PowerVR's native EGL/GLES pair before SDL3 is initialized.
export SDL_VIDEO_FORCE_EGL=1
export SDL_OPENGL_LIBRARY=/opt/omap5-sgx-ddk-um-linux/lib/libGLESv2.so.1
export SDL_EGL_LIBRARY=/opt/omap5-sgx-ddk-um-linux/lib/libEGL.so.1
# The game and helper must remain adjacent. CMake installs both in build-pyra/build/.
if [ -x "$HERE/build-pyra/build/Descent3" ]; then
  exec "$HERE/build-pyra/build/Descent3" "$@"
elif [ -x "$HERE/build-pyra/Descent3/Descent3" ]; then
  # Compatibility with older builds: specify the new helper path explicitly.
  export PYRA_RUMBLE_HELPER_PATH="$HERE/build-pyra/build/pyra-rumble-helper"
  exec "$HERE/build-pyra/Descent3/Descent3" "$@"
else
  echo "ERROR: Descent3 not built; run ./build-pyra.sh first" >&2
  exit 1
fi
