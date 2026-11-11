/*
 * Interplay MVE 8-bit compressed palette decoder (opcode 0x0d).
 * This header deliberately has no SDL or renderer dependency so that its
 * bounds and palette-index behaviour can be unit-tested independently.
 */
#pragma once

#include <cstddef>
#include <cstdint>

template <typename SetEntry>
bool mve_DecodeCompressedPalette(const uint8_t *data, size_t size, SetEntry setEntry) {
  if (!data || size < 32) return false;

  // Validate before modifying any palette entry.  Each of the 32 groups
  // contains a bitmask and 3 bytes per set bit.
  size_t offset = 0;
  for (unsigned group = 0; group < 32; ++group) {
    if (offset >= size) return false;
    const unsigned mask = data[offset++];
    for (unsigned bit = 0; bit < 8; ++bit) {
      if (mask & (1u << bit)) {
        if (size - offset < 3) return false;
        offset += 3;
      }
    }
  }

  offset = 0;
  for (unsigned group = 0; group < 32; ++group) {
    const unsigned mask = data[offset++];
    for (unsigned bit = 0; bit < 8; ++bit) {
      if (mask & (1u << bit)) {
        setEntry(group * 8 + bit, data + offset);
        offset += 3;
      }
    }
  }
  return true;
}
