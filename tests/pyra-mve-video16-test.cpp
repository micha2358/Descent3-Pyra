// Tests the real 16-bit MVE decoder on platforms with both signed and unsigned char.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include "decoders.h"

int g_width = 16, g_height = 8;
void *g_vBackBuf1 = nullptr, *g_vBackBuf2 = nullptr;

static void fail(const char *msg) { std::fprintf(stderr, "FAIL: %s\n", msg); std::exit(1); }

int main() {
  const std::size_t pixels = g_width * g_height;
  std::vector<std::uint16_t> backing(pixels * 2, 0);
  auto *cur = backing.data();
  auto *prev = backing.data() + pixels;
  g_vBackBuf1 = cur;
  g_vBackBuf2 = prev;

  // Two blocks wide: raw 8x8 on left (0xB), motion copy on right (0x5).
  // A -8 horizontal motion vector must copy the previous frame's LEFT block.
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 8; ++x)
      prev[y * g_width + x] = static_cast<std::uint16_t>(0x3000 + y * 8 + x);
  std::vector<std::uint8_t> data(2 + 8*8*2 + 2, 0);
  data[0] = static_cast<std::uint8_t>(data.size()); // second stream starts at end
  data[1] = 0;
  for (int i = 0; i < 64; ++i) {
    data[2+i*2] = 0x12;
    data[2+i*2+1] = 0x34;
  }
  data[data.size()-2] = 0xF8; // -8, which fails with unsigned plain char
  data[data.size()-1] = 0;
  std::uint8_t map = 0x5B;
  decodeFrame16(reinterpret_cast<std::uint8_t*>(cur), &map, 1, data.data(), static_cast<int>(data.size()));
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 8; ++x)
      if (cur[y*g_width+x+8] != prev[y*g_width+x]) fail("signed MVE motion -8 copied wrong block");
  std::puts("PASS: opcode 0x5 signed motion vector -8");

  // Opcode 0xF checkerboard uses two RGB555 16-bit colors, not overlapping bytes.
  std::fill(backing.begin(), backing.end(), 0);
  std::uint8_t map_f = 0xFF;
  std::uint8_t check[2+2*4] = { 10,0, 0x00,0x7C,0xE0,0x03, 0x00,0x7C,0xE0,0x03 };
  decodeFrame16(reinterpret_cast<std::uint8_t*>(cur), &map_f, 1, check, sizeof(check));
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 16; ++x) {
      const std::uint16_t expected = ((x+y)&1) ? 0x03E0 : 0x7C00;
      if (cur[y*g_width+x] != expected) fail("RGB555 opcode 0xF reads second color with wrong offset");
    }
  std::puts("PASS: opcode 0xF RGB555 checkerboard colors");
  mve16_dumpOpcodeCountsAndReset();
  return 0;
}
