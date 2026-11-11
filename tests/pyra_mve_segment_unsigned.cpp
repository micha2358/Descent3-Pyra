#include "mvelib.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
int main() {
  const unsigned length = 43310;
  std::vector<unsigned char> bytes(length+4, 0);
  bytes[0] = length & 255; bytes[1] = (length >> 8) & 255;
  bytes[2] = 0x11;
  MVEFILE file{};
  file.cur_chunk=bytes.data(); file.cur_fill=static_cast<int>(bytes.size()); file.next_segment=0;
  const int actual=mvefile_get_next_segment_size(&file);
  assert(actual == 43310);
  assert(static_cast<int16_t>(length) == -22226);
  assert(mvefile_get_next_segment_major(&file)==0x11);
  mvefile_advance_segment(&file);
  assert(file.next_segment==43314);
  file.next_segment=0; file.cur_fill=43000;
  assert(mvefile_get_next_segment_size(&file)==-1);
  std::puts("PASS: 43310 bytes unsigned MVE segment (signed would be -22226), correct advance, truncation guard");
}
