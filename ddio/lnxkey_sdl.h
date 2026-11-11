/* Descent 3 - SDL keyboard interface (upstream PR #727) */
#pragma once
#include <cstdint>
struct ddio_init_info;
bool ddio_sdl_InternalKeyInit(ddio_init_info *init_info);
void ddio_sdl_InternalKeyClose();
bool ddio_sdl_InternalKeyState(uint8_t key);
void ddio_sdl_InternalKeySuspend();
void ddio_sdl_InternalKeyResume();
float ddio_sdl_InternalKeyDownTime(uint8_t key);
void ddio_sdl_InternalResetKey(uint8_t key);
void ddio_sdl_InternalKeyFrame();
