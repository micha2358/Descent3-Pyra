#include "../renderer/PyraUIResolution.h"
#include <cassert>
#include <cstdio>

int main() {
  using pyra_ui_resolution::target;
  const auto game = target(1280, 720, true, false);
  assert(game.width == 1024 && game.height == 576);
  const auto menu = target(1280, 720, true, true);
  assert(menu.width == 1280 && menu.height == 720);
  const auto gameAgain = target(1280, 720, true, false);
  assert(gameAgain.width == 1024 && gameAgain.height == 576);
  const auto native = target(1280, 720, false, false);
  assert(native.width == 1280 && native.height == 720);
  const auto mainMenu = target(640, 480, true, false);
  assert(mainMenu.width == 640 && mainMenu.height == 480);
  const auto mainMenuUI = target(640, 480, true, true);
  assert(mainMenuUI.width == 640 && mainMenuUI.height == 480);
  const auto other = target(1216, 650, true, false);
  assert(other.width == 1216 && other.height == 650);
  std::puts("PASS: game 1024x576 -> sharp UI 1280x720 -> game 1024x576; menu 640x480 untouched");
}
