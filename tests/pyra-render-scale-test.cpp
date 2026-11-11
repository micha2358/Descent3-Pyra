#include "../renderer/PyraRenderScale.h"
#include <cassert>
#include <cstdio>
int main() {
  using pyra_render_scale::rect;
  auto full = rect(0, 0, 1280, 720, 1280, 720, 1024, 576);
  assert(full.x == 0 && full.y == 0 && full.w == 1024 && full.h == 576);
  auto same = rect(10, 20, 630, 460, 640, 480, 640, 480);
  assert(same.x == 10 && same.y == 20 && same.w == 630 && same.h == 460);
  auto left = rect(0, 0, 641, 720, 1280, 720, 1024, 576);
  auto right = rect(641, 0, 639, 720, 1280, 720, 1024, 576);
  assert(left.x + left.w == right.x && right.x + right.w == 1024);
  auto quarter = rect(320, 180, 640, 360, 1280, 720, 1024, 576);
  assert(quarter.x == 256 && quarter.y == 144 && quarter.w == 512 && quarter.h == 288);
  auto negative = rect(-20, -10, 100, 100, 1280, 720, 1024, 576);
  assert(negative.w >= 0 && negative.h >= 0);
  puts("PASS: Pyra 1024x576 logical-to-physical rectangle mapping");
}
