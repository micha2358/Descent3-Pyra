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

#define AUDIO

#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <thread>
#include <vector>

#include "byteswap.h"
#include "chrono_timer.h"
#include "decoders.h"
#include "mvelib.h"
#include "mve_audio.h"
#include "mve_palette.h"

#ifdef AUDIO
#include "movie_sound.h"
#endif

#define MVE_OPCODE_ENDOFSTREAM 0x00           // mcmd_end
#define MVE_OPCODE_ENDOFCHUNK 0x01            // mcmd_next
#define MVE_OPCODE_CREATETIMER 0x02           // mcmd_syncInit
#define MVE_OPCODE_INITAUDIOBUFFERS 0x03      // mcmd_sndConfigure
#define MVE_OPCODE_STARTSTOPAUDIO 0x04        // mcmd_sndSync
#define MVE_OPCODE_INITVIDEOBUFFERS 0x05      // mcmd_nfConfig
#define MVE_OPCODE_UNKNOWN_06 0x06            // mcmd_nfDecomp
#define MVE_OPCODE_DISPLAYVIDEO 0x07          // mcmd_sfShowFrame
#define MVE_OPCODE_AUDIOFRAMEDATA 0x08        // mcmd_sndAdd
#define MVE_OPCODE_AUDIOFRAMESILENCE 0x09     // mcmd_sndSilence
#define MVE_OPCODE_INITVIDEOMODE 0x0A         // mcmd_gfxMode
#define MVE_OPCODE_UNKNOWN_0B 0x0B            // mcmd_palMakeSynthPalette
#define MVE_OPCODE_SETPALETTE 0x0C            // mcmd_palLoadPalette
#define MVE_OPCODE_SETPALETTECOMPRESSED 0x0D  // mcmd_palLoadCompPalette
#define MVE_OPCODE_UNKNOWN_0E 0x0E            // mcmd_nfChanges
#define MVE_OPCODE_SETDECODINGMAP 0x0F        // mcmd_nfParms
#define MVE_OPCODE_UNKNOWN_10 0x10            // mcmd_nfDecompChg
#define MVE_OPCODE_VIDEODATA 0x11             // mcmd_nfPkDecomp

#define MVE_AUDIO_FLAGS_STEREO 1
#define MVE_AUDIO_FLAGS_16BIT 2
#define MVE_AUDIO_FLAGS_COMPRESSED 4

int g_spdFactorNum = 0;
static int g_spdFactorDenom = 10;
static int g_frameUpdated = 0;

static short get_short(const unsigned char *data) {
  return D3::convert_le<int16_t>(data[0] | (data[1] << 8));
}

static unsigned short get_ushort(const unsigned char *data) {
  return D3::convert_le<uint16_t>(data[0] | (data[1] << 8));
}

static int get_int(const unsigned char *data) {
  return D3::convert_le<int32_t>(data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24));
}

static unsigned int unhandled_chunks[32 * 256];

static int default_seg_handler(unsigned char major, unsigned char minor, unsigned char *data, int len, void *context) {
  unhandled_chunks[major << 8 | minor]++;
  return 1;
}

/*************************
 * general handlers
 *************************/
static int end_movie_handler(unsigned char major, unsigned char minor, unsigned char *data, int len, void *context) {
  return 0;
}

/*************************
 * timer handlers
 *************************/

/*
 * timer variables
 */
static int timer_created = 0;
static int micro_frame_delay = 0;
static int timer_started = 0;
static unsigned long int timer_expire = 0;

unsigned long int timer_getmicroseconds() {
  return D3::ChronoTimer::GetTimeUS();
}

void timer_sleepmicroseconds(unsigned long int usec) {
  // Use the game's clock and round up; never display a film frame early.
  D3::ChronoTimer::SleepMS(static_cast<int>((usec + 999) / 1000));
}

static int create_timer_handler(unsigned char major, unsigned char minor, unsigned char *data, int len, void *context) {
  long temp;

  if (timer_created)
    return 1;
  else
    timer_created = 1;

  micro_frame_delay = get_int(data) * (int)get_short(data + 4);
  if (g_spdFactorNum != 0) {
    temp = micro_frame_delay;
    temp *= g_spdFactorNum;
    temp /= g_spdFactorDenom;
    micro_frame_delay = (int)temp;
  }

  return 1;
}

static void timer_stop() {
  timer_expire = 0;
  timer_started = 0;
}

static void timer_start() {
  timer_expire = timer_getmicroseconds();
  timer_expire += micro_frame_delay;
  timer_started = 1;
}

#ifdef AUDIO
static std::unique_ptr<D3::MovieSoundDevice> snd_ds;
static int mve_audio_enabled = 1;
static unsigned mve_audio_packets = 0;
static unsigned mve_audio_short_packets = 0;
#else
static int mve_audio_enabled = 0;
#endif

static void do_timer_wait() {
  uint64_t ts;
  uint64_t tv;

  if (!timer_started)
    return;

  tv = timer_getmicroseconds();
  if (tv > timer_expire)
    goto end;

  ts = timer_expire - tv;

  timer_sleepmicroseconds(ts);

end:
  timer_expire += micro_frame_delay;
}

/*************************
 * audio handlers
 *************************/

static int create_audiobuf_handler(unsigned char major, unsigned char minor, unsigned char *data, int len,
                                   void *context) {
#ifdef AUDIO
  if (!mve_audio_enabled)
    return 1;

  int flags = get_ushort(data + 2);
  int sample_rate = get_ushort(data + 4);
  // int desired_buffer = get_int(data + 6);

  int channels = (flags & MVE_AUDIO_FLAGS_STEREO) ? 2 : 1;
  int sample_size = (flags & MVE_AUDIO_FLAGS_16BIT) ? 2 : 1;

  bool is_compressed = false;
  if (minor > 0 && (flags & MVE_AUDIO_FLAGS_COMPRESSED)) {
    is_compressed = true;
  }

  snd_ds = std::make_unique<D3::MovieSoundDevice>(sample_rate, sample_size, channels, is_compressed);
#endif

  return 1;
}

static int play_audio_handler(unsigned char major, unsigned char minor, unsigned char *data, int len, void *context) {
#ifdef AUDIO
  if (snd_ds && snd_ds->IsInitialized()) {
    snd_ds->Play();
  }
#endif
  return 1;
}

static int audio_data_handler(unsigned char major, unsigned char minor, unsigned char *data, int len, void *context) {
#ifdef AUDIO
  static const int selected_chan = 1;
  if (snd_ds && snd_ds->IsInitialized() && data && len >= 6) {
    const int channels = get_ushort(data + 2);
    const size_t outputBytes = get_ushort(data + 4);
    if ((channels & selected_chan) && outputBytes != 0) {
      const bool compressed = snd_ds->IsCompressed();
      const int sampleSize = snd_ds->GetSampleSize();
      std::vector<char> pcm(outputBytes);
      bool complete = true;
      if (major == MVE_OPCODE_AUDIOFRAMEDATA) {
        complete = mveaudio_process_bounded(pcm.data(), pcm.size(), data,
                                            static_cast<size_t>(len), sampleSize, compressed, snd_ds->GetChannels());
      } else {
        // Silence is generated in the OUTPUT PCM buffer, not in the MVE stream.
        std::memset(pcm.data(), sampleSize == 1 ? 0x80 : 0, pcm.size());
      }
      // Always enqueue the decoded portion, even if the segment is short.
      // The old Pyra check discarded whole legitimate compressed chunks.
      snd_ds->FillBuffer(pcm.data(), static_cast<int>(pcm.size()));
      if (!complete && ++mve_audio_short_packets <= 6) {
        std::fprintf(stderr, "PYRA MVE AUDIO: short %s segment: bytes=%d decoded=%zu (padded with silence)\n",
                     compressed ? "compressed" : "PCM", len, outputBytes);
      }
      if (++mve_audio_packets == 1) {
        std::fprintf(stderr, "PYRA MVE AUDIO: first packet queued: %zu bytes, %s, %d-byte samples\n",
                     outputBytes, compressed ? "DPCM" : "PCM", sampleSize);
      }
    }
  }
#endif
  return 1;
}

/*************************
 * video handlers
 *************************/

static int videobuf_created = 0;
static int video_initialized = 0;
int g_width, g_height;
void *g_vBuffers = nullptr, *g_vBackBuf1, *g_vBackBuf2;

static int g_destX, g_destY;
static int g_screenWidth, g_screenHeight;
static unsigned char *g_pCurMap = nullptr;
static int g_nMapLength = 0;
static int g_truecolor;

static int create_videobuf_handler(unsigned char major, unsigned char minor, unsigned char *data, int len,
                                   void *context) {
  short w, h;
  short count, truecolor;

  if (videobuf_created)
    return 1;
  else
    videobuf_created = 1;

  w = get_short(data);
  h = get_short(data + 2);

  if (minor > 0) {
    count = get_short(data + 4);
  } else {
    count = 1;
  }

  if (minor > 1) {
    truecolor = get_short(data + 6);
  } else {
    truecolor = 0;
  }

  g_width = w << 3;
  g_height = h << 3;

  /* TODO: * 4 causes crashes on some files */
  /* only malloc once */
  if (g_vBuffers == nullptr)
    g_vBackBuf1 = g_vBuffers = mve_alloc(g_width * g_height * 8);
  if (truecolor) {
    g_vBackBuf2 = (unsigned short *)g_vBackBuf1 + (g_width * g_height);
  } else {
    g_vBackBuf2 = (unsigned char *)g_vBackBuf1 + (g_width * g_height);
  }

  memset(g_vBackBuf1, 0, g_width * g_height * 4);

  g_truecolor = truecolor;

  return 1;
}

static int display_video_handler(unsigned char major, unsigned char minor, unsigned char *data, int len,
                                 void *context) {
  if (g_destX == -1) // center it
    g_destX = (g_screenWidth - g_width) >> 1;
  if (g_destY == -1) // center it
    g_destY = (g_screenHeight - g_height) >> 1;

  mve_showframe((unsigned char *)g_vBackBuf1, g_width, g_height, 0, 0, g_width, g_height, g_destX, g_destY,
                g_truecolor);

  g_frameUpdated = 1;

  return 1;
}

static int init_video_handler(unsigned char major, unsigned char minor, unsigned char *data, int len, void *context) {
  short width, height;

  if (video_initialized)
    return 1; /* maybe we actually need to change width/height here? */
  else
    video_initialized = 1;

  width = get_short(data);
  height = get_short(data + 2);
  g_screenWidth = width;
  g_screenHeight = height;

  return 1;
}

static int video_palette_handler(unsigned char major, unsigned char minor, unsigned char *data, int len,
                                 void *context) {
  short start, count;
  unsigned char *p;

  if (!data || len < 4 || !mve_setpalette)
    return 1;
  start = get_ushort(data);
  count = get_ushort(data + 2);
  if (start < 0 || count < 0 || start > 256 || count > 256 - start ||
      static_cast<size_t>(len - 4) < static_cast<size_t>(count) * 3)
    return 1;

  p = data + 4;
  // The palette callback accepts a pointer to the FIRST entry being
  // changed; no pointer arithmetic outside the segment is permitted.
  mve_setpalette(p, start, count);

  return 1;
}

// Opcode 0x0d encodes 256 palette entries as 32 groups.  Each group has
// an eight-bit mask followed by RGB triplets for its set bits.  Older
// Pyra builds ignored this opcode, which can leave paletted .mv8 movies
// with incorrect or missing colours.
static int video_palette_compressed_handler(unsigned char major, unsigned char minor,
                                            unsigned char *data, int len, void *context) {
  if (mve_setpalette && len >= 0) {
    mve_DecodeCompressedPalette(data, static_cast<size_t>(len),
                               [](unsigned index, const uint8_t *rgb) {
                                 mve_setpalette(rgb, index, 1);
                               });
  }
  return 1;
}

static int video_codemap_handler(unsigned char major, unsigned char minor, unsigned char *data, int len,
                                 void *context) {
  g_pCurMap = data;
  g_nMapLength = len;
  return 1;
}

static int video_data_handler(unsigned char major, unsigned char minor, unsigned char *data, int len, void *context) {
  unsigned char *temp;

  // short nFrameHot = get_short(data);
  // short nFrameCold = get_short(data + 2);
  // short nXoffset = get_short(data + 4);
  // short nYoffset = get_short(data + 6);
  // short nXsize = get_short(data + 8);
  // short nYsize = get_short(data + 10);
  const size_t minMapBytes = g_width > 0 && g_height > 0 ?
                             static_cast<size_t>(g_width / 8) * static_cast<size_t>(g_height / 8) / 2 : 0;
  if (!data || len < 14 || !g_vBackBuf1 || !g_vBackBuf2 ||
      !g_pCurMap || g_nMapLength < 0 ||
      static_cast<size_t>(g_nMapLength) < minMapBytes) {
    static unsigned invalidFrames = 0;
    if (++invalidFrames <= 6)
      std::fprintf(stderr, "PYRA MVE VIDEO: malformed frame or decoding map (data=%d map=%d required=%zu)\n",
                   len, g_nMapLength, minMapBytes);
    return 1;
  }
  unsigned short nFlags = get_ushort(data + 12);

  if (nFlags & 1) {
    temp = (unsigned char *)g_vBackBuf1;
    g_vBackBuf1 = g_vBackBuf2;
    g_vBackBuf2 = temp;
  }

  /* convert the frame */
  if (g_truecolor) {
    decodeFrame16((unsigned char *)g_vBackBuf1, g_pCurMap, g_nMapLength, data + 14, len - 14);
  } else {
    decodeFrame8((unsigned char *)g_vBackBuf1, g_pCurMap, g_nMapLength, data + 14, len - 14);
  }

  return 1;
}

static int end_chunk_handler(unsigned char major, unsigned char minor, unsigned char *data, int len, void *context) {
  g_pCurMap = nullptr;
  return 1;
}

void MVE_ioCallbacks(mve_cb_Read io_read) { mve_read = io_read; }

void MVE_memCallbacks(mve_cb_Alloc mem_alloc, mve_cb_Free mem_free) {
  mve_alloc = mem_alloc;
  mve_free = mem_free;
}

void MVE_sfCallbacks(mve_cb_ShowFrame showframe) { mve_showframe = showframe; }

void MVE_palCallbacks(mve_cb_SetPalette setpalette) { mve_setpalette = setpalette; }

MVESTREAM* MVE_rmPrepMovie(void *src, int x, int y, int track) {
#ifdef AUDIO
  mve_audio_packets = 0;
  mve_audio_short_packets = 0;
#endif
  MVESTREAM *mve;
  mve = mve_open(src);

  if (!mve)
    return nullptr;

  g_destX = x;
  g_destY = y;

  for (int i = 0; i < 32; i++)
    mve_set_handler(mve, i, default_seg_handler);

  mve_set_handler(mve, MVE_OPCODE_ENDOFSTREAM, end_movie_handler);
  mve_set_handler(mve, MVE_OPCODE_ENDOFCHUNK, end_chunk_handler);
  mve_set_handler(mve, MVE_OPCODE_CREATETIMER, create_timer_handler);
  mve_set_handler(mve, MVE_OPCODE_INITAUDIOBUFFERS, create_audiobuf_handler);
  mve_set_handler(mve, MVE_OPCODE_STARTSTOPAUDIO, play_audio_handler);
  mve_set_handler(mve, MVE_OPCODE_INITVIDEOBUFFERS, create_videobuf_handler);

  mve_set_handler(mve, MVE_OPCODE_DISPLAYVIDEO, display_video_handler);
  mve_set_handler(mve, MVE_OPCODE_AUDIOFRAMEDATA, audio_data_handler);
  mve_set_handler(mve, MVE_OPCODE_AUDIOFRAMESILENCE, audio_data_handler);
  mve_set_handler(mve, MVE_OPCODE_INITVIDEOMODE, init_video_handler);

  mve_set_handler(mve, MVE_OPCODE_SETPALETTE, video_palette_handler);
  mve_set_handler(mve, MVE_OPCODE_SETPALETTECOMPRESSED, video_palette_compressed_handler);

  mve_set_handler(mve, MVE_OPCODE_SETDECODINGMAP, video_codemap_handler);

  mve_set_handler(mve, MVE_OPCODE_VIDEODATA, video_data_handler);

  mve_play_next_chunk(mve); /* video initialization chunk */
  if (mve_audio_enabled)
    mve_play_next_chunk(mve); /* audio initialization chunk */

  return mve;
}

void MVE_getVideoSpec(MVE_videoSpec *vSpec) {
  vSpec->screenWidth = g_screenWidth;
  vSpec->screenHeight = g_screenHeight;
  vSpec->width = g_width;
  vSpec->height = g_height;
  vSpec->truecolor = g_truecolor;
}

int MVE_rmStepMovie(MVESTREAM *mve) {
  static int init_timer = 0;
  int cont = 1;

  if (!timer_started)
    timer_start();

  while (cont && !g_frameUpdated) // make a "step" be a frame, not a chunk...
    cont = mve_play_next_chunk(mve);
  g_frameUpdated = 0;

  if (!cont)
    return MVE_ERR_EOF;

  if (micro_frame_delay && !init_timer) {
    timer_start();
    init_timer = 1;
  }

  do_timer_wait();

  return 0;
}

void MVE_rmEndMovie(MVESTREAM *mve) {
#ifdef AUDIO
  std::fprintf(stderr, "PYRA MVE AUDIO: film finished, queued packets=%u, short packets=%u\n",
               mve_audio_packets, mve_audio_short_packets);
#endif
  if (g_truecolor) mve16_dumpOpcodeCountsAndReset();
  timer_stop();
  timer_created = 0;

#ifdef AUDIO
  snd_ds.reset();
#endif

  mve_free(g_vBuffers);
  g_vBuffers = nullptr;
  g_pCurMap = nullptr;
  g_nMapLength = 0;
  videobuf_created = 0;
  video_initialized = 0;

  mve_close(mve);
  mve = nullptr;
}

void MVE_rmHoldMovie() { timer_started = 0; }

void MVE_sndInit(bool enable) {
#ifdef AUDIO
  if (enable) {
    mve_audio_enabled = 1;
  } else {
    mve_audio_enabled = 0;
  }
#endif
}
