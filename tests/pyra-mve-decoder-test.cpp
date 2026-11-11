// Standalone unit tests for the Pyra/MVE audio + compressed-palette fixes.
// Build with: c++ -std=c++17 -O2 -Ilibmve -Ilib tests/pyra-mve-decoder-test.cpp libmve/mve_audio.cpp -o /tmp/pyra-mve-decoder-test
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
#include "mve_audio.h"
#include "mve_palette.h"

static void test_pcm() {
  // MVE header: sequence=4, channel=1, decompressed data length=4;
  // followed by 4 literal PCM bytes (the first Dolby movie may use PCM).
  std::array<uint8_t, 10> packet{{4,0,1,0,4,0,0x00,0x80,0x10,0xff}};
  std::array<char, 4> pcm{{42,42,42,42}};
  mveaudio_process(pcm.data(), packet.data(), 1, false);
  assert(static_cast<uint8_t>(pcm[0]) == 0x00);
  assert(static_cast<uint8_t>(pcm[1]) == 0x80);
  assert(static_cast<uint8_t>(pcm[2]) == 0x10);
  assert(static_cast<uint8_t>(pcm[3]) == 0xff);
  assert(packet[6] == 0x00 && packet[7] == 0x80);  // input unchanged
}

static void test_bounded_pcm() {
  std::array<uint8_t, 10> packet{{4,0,1,0,4,0,10,20,30,40}};
  std::array<char, 6> pcm{};
  assert(!mveaudio_process_bounded(pcm.data(), pcm.size(), packet.data(), packet.size(), 1, false, 1));
  assert(static_cast<uint8_t>(pcm[0]) == 10);
  assert(static_cast<uint8_t>(pcm[3]) == 40);
  assert(static_cast<uint8_t>(pcm[4]) == 0x80);
  // Encoded input with a header and no payload cannot overrun output.
  assert(!mveaudio_process_bounded(pcm.data(), pcm.size(), packet.data(), 3, 1, false, 1));
}

static void test_bounded_dpcm() {
  // Stereo Interplay DPCM: the two 16-bit predictor seeds are the first TWO
  // interleaved PCM samples. Delta bytes begin only after those seeds.
  std::array<uint8_t, 14> packet{{0,0,1,0,12,0,0xe8,0x03,0x18,0xfc,1,0xff,2,0xfe}};
  std::array<char, 12> pcm{};
  assert(mveaudio_process_bounded(pcm.data(), pcm.size(), packet.data(), packet.size(), 2, true, 2));
  auto sample = [&](int i) {
    const auto lo = static_cast<uint8_t>(pcm[2*i]);
    const auto hi = static_cast<uint8_t>(pcm[2*i + 1]);
    return static_cast<int16_t>(lo | (hi << 8));
  };
  const int16_t expected[] = {1000, -1000, 1001, -1001, 1003, -1003};
  for (int i = 0; i < 6; ++i) assert(sample(i) == expected[i]);
  assert(!mveaudio_process_bounded(pcm.data(), pcm.size(), packet.data(), 6, 2, true, 2));
  for (char value : pcm) assert(value == 0);
  // Missing one delta returns false but keeps initialized trailing PCM.
  assert(!mveaudio_process_bounded(pcm.data(), pcm.size(), packet.data(), 13, 2, true, 2));
  assert(sample(0) == 1000 && sample(1) == -1000);
  assert(sample(5) == 0);
}

static void test_palette() {
  std::vector<uint8_t> packed;
  for (unsigned group = 0; group < 32; ++group) {
    if (group == 0) {
      packed.push_back(0x81);  // indices 0 and 7
      packed.insert(packed.end(), {10,20,30,40,50,60});
    } else if (group == 31) {
      packed.push_back(0x80);  // index 255
      packed.insert(packed.end(), {61,62,63});
    } else {
      packed.push_back(0);
    }
  }
  std::array<std::array<uint8_t,3>,256> palette{};
  int changed = 0;
  auto save = [&](unsigned index, const uint8_t *rgb) {
    ++changed;
    assert(index < palette.size());
    std::memcpy(palette[index].data(), rgb, 3);
  };
  assert(mve_DecodeCompressedPalette(packed.data(), packed.size(), save));
  assert(changed == 3);
  assert(palette[0][0] == 10 && palette[7][2] == 60 && palette[255][1] == 62);

  // The decoder must reject truncation WITHOUT making partial updates.
  changed = 0;
  assert(!mve_DecodeCompressedPalette(packed.data(), packed.size() - 1, save));
  assert(changed == 0);
}

int main() {
  test_pcm();
  test_bounded_pcm();
  test_bounded_dpcm();
  test_palette();
  std::cout << "PYRA MVE: bounded PCM/DPCM and palette tests PASS\n";
}
