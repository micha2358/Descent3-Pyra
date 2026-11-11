/* Descent 3 / DragonBox Pyra: logical-to-physical viewport rectangle mapper.
 * Header deliberately independent of SDL, GL and ARM so it is unit-testable.
 */
#pragma once
#include <algorithm>
#include <cmath>

namespace pyra_render_scale {
struct Rect { int x, y, w, h; };
inline int edge(int pos, int logical, int physical) {
  return logical > 0 ? int(std::lround(double(pos) * physical / logical)) : pos;
}
inline Rect rect(int x, int y, int w, int h,
                 int logicalW, int logicalH, int physicalW, int physicalH) {
  if (logicalW <= 0 || logicalH <= 0 || physicalW <= 0 || physicalH <= 0)
    return {x, y, w, h};
  const int px = edge(x, logicalW, physicalW);
  const int py = edge(y, logicalH, physicalH);
  const int px2 = edge(x + w, logicalW, physicalW);
  const int py2 = edge(y + h, logicalH, physicalH);
  return {px, py, std::max(0, px2 - px), std::max(0, py2 - py)};
}
} // namespace pyra_render_scale
