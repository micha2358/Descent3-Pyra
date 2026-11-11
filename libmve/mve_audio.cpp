/*
 * Copyright (C) 2002-2024 D2X Project
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <algorithm>
#include <cstring>

#include "byteswap.h"

static int16_t audio_exp_table[256] = {
    0,      1,      2,      3,      4,      5,      6,      7,      //  8
    8,      9,      10,     11,     12,     13,     14,     15,     //
    16,     17,     18,     19,     20,     21,     22,     23,     //
    24,     25,     26,     27,     28,     29,     30,     31,     //
    32,     33,     34,     35,     36,     37,     38,     39,     //
    40,     41,     42,     43,     47,     51,     56,     61,     //
    66,     72,     79,     86,     94,     102,    112,    122,    //
    133,    145,    158,    173,    189,    206,    225,    245,    //  64
    267,    292,    318,    348,    379,    414,    452,    493,    //
    538,    587,    640,    699,    763,    832,    908,    991,    //
    1081,   1180,   1288,   1405,   1534,   1673,   1826,   1993,   //
    2175,   2373,   2590,   2826,   3084,   3365,   3672,   4008,   //
    4373,   4772,   5208,   5683,   6202,   6767,   7385,   8059,   //
    8794,   9597,   10472,  11428,  12471,  13609,  14851,  16206,  //
    17685,  19298,  21060,  22981,  25078,  27367,  29864,  32589,  //
    -29973, -26728, -23186, -19322, -15105, -10503, -5481,  -1,     //  128
    1,      1,      5481,   10503,  15105,  19322,  23186,  26728,  //
    29973,  -32589, -29864, -27367, -25078, -22981, -21060, -19298, //
    -17685, -16206, -14851, -13609, -12471, -11428, -10472, -9597,  //
    -8794,  -8059,  -7385,  -6767,  -6202,  -5683,  -5208,  -4772,  //
    -4373,  -4008,  -3672,  -3365,  -3084,  -2826,  -2590,  -2373,  //
    -2175,  -1993,  -1826,  -1673,  -1534,  -1405,  -1288,  -1180,  //
    -1081,  -991,   -908,   -832,   -763,   -699,   -640,   -587,   //
    -538,   -493,   -452,   -414,   -379,   -348,   -318,   -292,   //  196
    -267,   -245,   -225,   -206,   -189,   -173,   -158,   -145,   //
    -133,   -122,   -112,   -102,   -94,    -86,    -79,    -72,    //
    -66,    -61,    -56,    -51,    -47,    -43,    -42,    -41,    //
    -40,    -39,    -38,    -37,    -36,    -35,    -34,    -33,    //
    -32,    -31,    -30,    -29,    -28,    -27,    -26,    -25,    //
    -24,    -23,    -22,    -21,    -20,    -19,    -18,    -17,    //
    -16,    -15,    -14,    -13,    -12,    -11,    -10,    -9,     //
    -8,     -7,     -6,     -5,     -4,     -3,     -2,     -1,     //  256
};

static int16_t getWord(unsigned char **fin) {
  int16_t value = D3::convert_le<int16_t>(((*fin)[1] << 8) | (*fin)[0]);
  *fin += 2;
  return value;
}

void mveaudio_process(char *buffer, unsigned char *data, int sample_size, bool is_compressed) {
  if (is_compressed) {
    int nCurOffsets[2];

    data += 4;
    int samples = static_cast<uint16_t>(getWord(&data)) / sample_size;
    // Fill predictors
    nCurOffsets[0] = getWord(&data);
    nCurOffsets[1] = getWord(&data);

    for (int i = 0; i < samples; i++) {
      nCurOffsets[i & 1] = std::clamp(nCurOffsets[i & 1] + audio_exp_table[data[i]], -32768, 32767);
      memcpy(buffer + i * sample_size, &nCurOffsets[i & 1], sample_size);
    }
  } else {
    // Interplay MVE audio packet: sequence(2), channel mask(2),
    // decoded byte count(2), then PCM samples.  The old implementation
    // interpreted the channel mask as the byte count and copied from
    // &data (the address of a pointer on the stack), corrupting audio.
    data += 4;
    const int size = static_cast<uint16_t>(getWord(&data));
    memcpy(buffer, data, size);
  }
}

// Output length is the decoded PCM byte count; packetBytes is the actual
// MVE segment length. Never infer compressed-input length from PCM byte count.
// Corrected Interplay DPCM: the predictor for EACH channel is also the
// first output sample for that channel. Old Pyra codec dropped the seed samples
// and padded 4 bytes of silence on every stereo packet.
// Packet header: sequence(2), streamMask(2), PCMbyteCount(2), seeds(2*channels), deltas...
// This code only replaces the bounded decoder in the isolated test, not the game.
bool mveaudio_process_bounded(char *out, size_t outBytes, const unsigned char *packet,
                              size_t packetBytes, int sampleSize, bool compressed, int channels) {
  if (!out || !packet || (sampleSize != 1 && sampleSize != 2) || (channels != 1 && channels != 2)) return false;
  std::memset(out, sampleSize == 1 ? 0x80 : 0, outBytes);
  if (packetBytes < 6 || outBytes % static_cast<size_t>(sampleSize) != 0) return false;
  const size_t wantedSamples = outBytes / static_cast<size_t>(sampleSize);
  if (!compressed) {
    const size_t available = packetBytes - 6;
    const size_t copied = std::min(outBytes, available);
    std::memcpy(out, packet + 6, copied);
    return copied == outBytes;
  }
  if (sampleSize != 2) return false; // Interplay compressed DPCM always produces signed 16-bit PCM.
  const size_t seedBytes = static_cast<size_t>(channels)*2;
  if (packetBytes < 6 + seedBytes) return false;
  int predictor[2] = {0, 0};
  auto put_sample = [&](size_t index, int value) {
    // Explicit LE output: avoids dependence on host endianness.
    out[2*index] = static_cast<char>(value & 0xff);
    out[2*index+1] = static_cast<char>((value >> 8) & 0xff);
  };
  const size_t seeds = std::min(static_cast<size_t>(channels), wantedSamples);
  for (size_t ch=0; ch<static_cast<size_t>(channels); ++ch) {
    const size_t off = 6 + 2*ch;
    predictor[ch] = static_cast<int16_t>(static_cast<uint16_t>(packet[off] | (packet[off+1] << 8)));
    if (ch < seeds) put_sample(ch, predictor[ch]);
  }
  const size_t availableDeltas = packetBytes - 6 - seedBytes;
  const size_t wantedDeltas = wantedSamples - seeds;
  const size_t count = std::min(wantedDeltas, availableDeltas);
  for (size_t i=0; i<count; ++i) {
    const size_t ch = i % static_cast<size_t>(channels);
    predictor[ch] = std::clamp(predictor[ch] + audio_exp_table[packet[6 + seedBytes + i]], -32768, 32767);
    put_sample(seeds+i, predictor[ch]);
  }
  return count == wantedDeltas;
}
