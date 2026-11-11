#!/bin/sh
set -eu

# Bundled: SDL3 3.2.6 + cpp-httplib 0.58.0 + plog 1.1.11. Remaining build deps come from Debian.
[ -f /usr/include/glm/glm.hpp ] || { echo "ERROR: libglm-dev is required" >&2; exit 1; }
[ -f /usr/include/zlib.h ] || { echo "ERROR: zlib1g-dev is required" >&2; exit 1; }
[ -f /usr/include/openssl/ssl.h ] || { echo "ERROR: libssl-dev is required (HTTPS support for bundled cpp-httplib)" >&2; exit 1; }

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD="$ROOT/build-pyra"
PVR=/opt/omap5-sgx-ddk-um-linux/lib

[ -f "$ROOT/third_party/SDL/CMakeLists.txt" ] || { echo "ERROR: vendored SDL3 source missing" >&2; exit 1; }
[ -f "$ROOT/third_party/plog/include/plog/Log.h" ] || { echo "ERROR: vendored plog source missing" >&2; exit 1; }
[ -e "$PVR/libEGL.so.1.14.3699939" ] || { echo "ERROR: PowerVR libEGL.so.1.14.3699939 not found" >&2; exit 1; }
[ -e "$PVR/libGLESv2.so.1" ] || { echo "ERROR: PowerVR libGLESv2.so.1 not found" >&2; exit 1; }

export LD_LIBRARY_PATH="$PVR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export CFLAGS="${CFLAGS:-} -O2 -pipe -mcpu=cortex-a15 -mfpu=neon-vfpv4 -mfloat-abi=hard -fomit-frame-pointer"
export CXXFLAGS="${CXXFLAGS:-} -O2 -pipe -mcpu=cortex-a15 -mfpu=neon-vfpv4 -mfloat-abi=hard -fomit-frame-pointer"

cmake -S "$ROOT" -B "$BUILD" -G Ninja \
  -DUSE_VCPKG=OFF \
  -DPYRA_GLES2=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DENABLE_LOGGER=ON \
  -DSDL_SHARED=OFF \
  -DSDL_STATIC=ON \
  -DSDL_TEST_LIBRARY=OFF \
  -DSDL_TESTS=OFF \
  -DSDL_EXAMPLES=OFF \
  -DSDL_INSTALL=OFF

cmake --build "$BUILD" -j2
