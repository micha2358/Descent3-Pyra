// DragonBox Pyra: logical UI stays at 1280x720, while active gameplay may
// use a cheaper 1024x576 internal render target. No SDL window resize needed.
#pragma once

namespace pyra_ui_resolution {
struct Size { int width; int height; };
inline Size target(int logicalW, int logicalH, bool fast3D, bool nativeGameUI) {
  if (logicalW == 1280 && logicalH == 720 && fast3D && !nativeGameUI)
    return {1024, 576};
  return {logicalW, logicalH};
}
} // namespace pyra_ui_resolution
