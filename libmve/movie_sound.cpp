/*
 * Descent 3
 * Copyright (C) 2024 Descent Developers
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

#include "movie_sound.h"
#include <SDL3/SDL_log.h>

namespace D3 {

MovieSoundDevice::MovieSoundDevice(int sample_rate, uint16_t sample_size, uint8_t channels, bool is_compressed) {
  SDL_AudioSpec spec{};
  spec.freq = sample_rate;
  spec.format = (sample_size == 2) ? SDL_AUDIO_S16LE : SDL_AUDIO_U8;
  spec.channels = channels;

  this->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (!this->stream)
    SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "PYRA MVE AUDIO: SDL_OpenAudioDeviceStream failed: %s", SDL_GetError());
  else
    SDL_Log("PYRA MVE AUDIO: stream opened rate=%d channels=%d sampleBytes=%d compressed=%d",
            sample_rate, channels, sample_size, int(is_compressed));
  this->m_is_compressed = is_compressed;
  this->m_sample_size = sample_size;
  this->m_channels = channels;
};

MovieSoundDevice::~MovieSoundDevice() {
  if (this->stream != nullptr) {
    SDL_CloseAudioDevice(SDL_GetAudioStreamDevice(this->stream));
  }
}

void MovieSoundDevice::FillBuffer(char *buffer, int len) const {
  if (this->stream && !SDL_PutAudioStreamData(this->stream, buffer, len)) {
    static unsigned failures = 0;
    if (++failures <= 6)
      SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "PYRA MVE AUDIO: queue failed: %s", SDL_GetError());
  }
}

void MovieSoundDevice::Play() {
  if (this->stream && !SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(this->stream)))
    SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "PYRA MVE AUDIO: resume failed: %s", SDL_GetError());
}

void MovieSoundDevice::Stop() {
  if (this->stream) SDL_PauseAudioDevice(SDL_GetAudioStreamDevice(this->stream));
}

void MovieSoundDevice::Lock() {}

void MovieSoundDevice::Unlock() {}

} // namespace D3
