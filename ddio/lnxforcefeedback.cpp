/*
 * Descent 3 - DragonBox Pyra SDL3 rumble backend
 *
 * The original Linux implementation was a no-op stub.  This adapter maps
 * Descent 3's existing effect definitions and gameplay triggers onto the
 * single TWL6040 motor exported through pyrainput/SDL3.  No EV_FF paths,
 * device node assumptions, root permissions or udev rules are needed.
 *
 * A joystick owned by sdljoy.cpp is borrowed, NEVER opened or closed here.
 * The owning joystick stays alive until ddio_ff_DetachForce() is called by
 * joy_Close(), which shuts down haptics before releasing joystick handles.
 */
#include "forcefeedback.h"
#include "log.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <cerrno>
#include <climits>
#include <csignal>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// Existing SDL gameplay joystick; borrowed, owned exclusively by sdljoy.cpp.
extern SDL_Joystick *pyra_D3_GetGameplayJoystick();

bool ddForce_found = false;
bool ddForce_enabled = false;

namespace {
enum class Backend { none, helper, joystick, haptic };
struct Effect {
  tFFB_Effect data{};
  bool valid = false;
  uint64_t lastStarted = 0;
  uint32_t cooldown = 75;
};

Backend backend = Backend::none;
SDL_Joystick *gameplayJoystick = nullptr;  // non-owning
SDL_Haptic *haptic = nullptr;              // owned if nonnull
bool ownsHapticSubsystem = false;
bool paused = false;
float masterGain = 1.0f;
std::array<Effect, DDIO_FF_MAXEFFECTS> effects;
int nextEffectID = 0;
int consecutiveFailures = 0;
int helperFD = -1;
pid_t helperPID = -1;
bool helperRejected = false;
// A test is a one-shot diagnostic, not a continuously refreshed rumble.
bool startupTestPlayed = false;
uint64_t attemptedPlays = 0;


bool envIsFalse(const char *key) {
  const char *v = std::getenv(key);
  return v && (!std::strcmp(v, "0") || !std::strcmp(v, "false") || !std::strcmp(v, "off"));
}
bool envIsTrue(const char *key) {
  const char *v = std::getenv(key);
  return v && (!std::strcmp(v, "1") || !std::strcmp(v, "true") || !std::strcmp(v, "on"));
}
const char *envString(const char *key) {
  const char *v = std::getenv(key);
  return v ? v : "";
}

// An isolated SDL2 owner avoids mixing two haptic abstractions for the same
// pyraInput device inside the SDL3 game process.  No extra SDL2 headers are
// needed: pyra-rumble-helper dynamically loads the system SDL2 runtime.
bool sendHelper(const std::string &command) {
  if (helperFD < 0) return false;
  const ssize_t written = send(helperFD, command.data(), command.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
  if (written == static_cast<ssize_t>(command.size())) return true;
  if (errno != EAGAIN && errno != EWOULDBLOCK) {
    LOG_WARNING.printf("PYRA RUMBLE: helper IPC send failed: %s", std::strerror(errno));
  }
  return false;
}
void closeHelper() {
  if (helperFD >= 0) {
    sendHelper("Q\n");
    close(helperFD);
    helperFD = -1;
  }
  if (helperPID > 0) {
    int status = 0;
    // Do not block the game's SDL shutdown indefinitely.
    bool reaped = false;
    for (int i=0; i<10; ++i) {
      const pid_t result=waitpid(helperPID,&status,WNOHANG);
      if (result == helperPID || (result < 0 && errno == ECHILD)) {reaped=true; break;}
      usleep(10000);
    }
    if (!reaped) {
      kill(helperPID,SIGTERM);
      waitpid(helperPID,&status,0);
    }
    helperPID = -1;
  }
}
bool startHelper() {
  const char *requestedPath = envString("PYRA_RUMBLE_HELPER_PATH");
  std::string path;
  if (*requestedPath) path=requestedPath;
  else {
    char executable[PATH_MAX];
    const ssize_t n=readlink("/proc/self/exe",executable,sizeof(executable)-1);
    if(n<=0) return false;
    executable[n]='\0';
    path=(std::filesystem::path(executable).parent_path() / "pyra-rumble-helper").string();
  }
  if (access(path.c_str(), X_OK) != 0) {
    LOG_WARNING.printf("PYRA RUMBLE: helper not executable '%s': %s",path.c_str(),std::strerror(errno));
    return false;
  }
  int fds[2] = {-1,-1};
  if(socketpair(AF_UNIX,SOCK_STREAM,0,fds)!=0) {
    LOG_WARNING.printf("PYRA RUMBLE: socketpair failed: %s",std::strerror(errno));
    return false;
  }
  pid_t pid=fork();
  if(pid==0) {
    close(fds[0]);
    if(dup2(fds[1],STDIN_FILENO)<0) _exit(127);
    close(fds[1]);
    execl(path.c_str(),path.c_str(),static_cast<char*>(nullptr));
    _exit(127);
  }
  close(fds[1]);
  if(pid<0) {
    close(fds[0]);
    LOG_WARNING.printf("PYRA RUMBLE: fork failed: %s",std::strerror(errno));
    return false;
  }
  helperPID=pid;
  helperFD=fds[0];
  struct pollfd pfd { helperFD,POLLIN,0 };
  int ready=poll(&pfd,1,1600);
  if(ready>0 && (pfd.revents & POLLIN)) {
    char buffer[256]{};
    const ssize_t n=recv(helperFD,buffer,sizeof(buffer)-1,0);
    if(n>0) {
      buffer[n]='\0';
      if(std::strncmp(buffer,"READY ",6)==0) {
        backend=Backend::helper;
        LOG_INFO.printf("PYRA RUMBLE: isolated helper initialized: %s",buffer+6);
        return true;
      }
      LOG_WARNING.printf("PYRA RUMBLE: helper refused initialization: %s",buffer);
    }
  }
  LOG_WARNING << "PYRA RUMBLE: isolated SDL2 helper did not become ready; trying SDL3 fallback";
  closeHelper();
  return false;
}

void stopMotor() {
  if (backend == Backend::helper) sendHelper("S\n");
  else if (backend == Backend::joystick && gameplayJoystick)
    SDL_RumbleJoystick(gameplayJoystick, 0, 0, 0);
  else if (backend == Backend::haptic && haptic)
    SDL_StopHapticRumble(haptic);
}
void detach() {
  stopMotor();
  ddForce_enabled = false;
  ddForce_found = false;
  backend = Backend::none;
  closeHelper();
  if (haptic) {
    SDL_CloseHaptic(haptic);
    haptic = nullptr;
  }
  if (ownsHapticSubsystem) {
    SDL_QuitSubSystem(SDL_INIT_HAPTIC);
    ownsHapticSubsystem = false;
  }
  gameplayJoystick = nullptr;
  effects = {};
  nextEffectID = 0;
  consecutiveFailures = 0;
  attemptedPlays = 0;
  helperRejected = false;
}

bool useHaptic(SDL_Haptic *candidate, const char *name) {
  if (!candidate)
    return false;
  if (!SDL_InitHapticRumble(candidate)) {
    LOG_WARNING.printf("PYRA RUMBLE: SDL haptic '%s' has no simple rumble: %s", name, SDL_GetError());
    SDL_CloseHaptic(candidate);
    return false;
  }
  // SDL3 must also accept a real, non-zero effect before becoming READY.
  if (!SDL_PlayHapticRumble(candidate, 1.0f, 60)) {
    LOG_WARNING.printf("PYRA RUMBLE: SDL haptic '%s' rejected playback: %s", name, SDL_GetError());
    SDL_CloseHaptic(candidate);
    return false;
  }
  SDL_Delay(80);
  SDL_StopHapticRumble(candidate);
  haptic = candidate;
  backend = Backend::haptic;
  LOG_INFO.printf("PYRA RUMBLE: haptic '%s' initialized after gameplay joystick", name);
  return true;
}

bool initialize() {
  if (backend != Backend::none)
    return true;
  if (envIsFalse("PYRA_RUMBLE"))
    return false;

  gameplayJoystick = pyra_D3_GetGameplayJoystick();
  if (!gameplayJoystick) {
    LOG_WARNING << "PYRA RUMBLE: gameplay joystick not yet initialized";
    return false;
  }
  const char *name = SDL_GetJoystickName(gameplayJoystick);
  LOG_INFO.printf("PYRA RUMBLE: borrowing gameplay joystick '%s' (no second SDL_OpenJoystick)",
                  name ? name : "unknown");

  const char *selection = envString("PYRA_RUMBLE_BACKEND");
  // The Pyra's proven SDL2 backend is now preferred.  In particular, do
  // NOT classify a zero-strength SDL3_RumbleJoystick() as a working motor.
  const bool helperOnly = !std::strcmp(selection,"helper");
  if ((!*selection || helperOnly) && !helperRejected && startHelper()) {
    // Only the helper owns the FF effect; this process only borrows its
    // joystick for button/axis input, not for rumble.
  }
  if (backend == Backend::none && !helperOnly && std::strcmp(selection,"haptic") != 0) {
    // Don't create a silent zero-strength effect to 'test' the SDL3 driver.
    // A real startup impulse can be requested with PYRA_RUMBLE_TEST=1.
    if (SDL_GetBooleanProperty(SDL_GetJoystickProperties(gameplayJoystick),
                               SDL_PROP_JOYSTICK_CAP_RUMBLE_BOOLEAN, false)) {
      if (SDL_RumbleJoystick(gameplayJoystick, 0xFFFF, 0xFFFF, 60)) {
        SDL_Delay(80);
        SDL_RumbleJoystick(gameplayJoystick, 0, 0, 0);
        backend = Backend::joystick;
        LOG_INFO << "PYRA RUMBLE: SDL3 joystick accepted a nonzero effect (physical motor unverified)";
      } else {
        LOG_WARNING.printf("PYRA RUMBLE: SDL3 joystick rejected playback: %s", SDL_GetError());
      }
    }
  }
  if (backend == Backend::none && !helperOnly && std::strcmp(selection,"joystick") != 0) {
    if (!SDL_InitSubSystem(SDL_INIT_HAPTIC)) {
      LOG_WARNING.printf("PYRA RUMBLE: SDL haptic subsystem unavailable: %s", SDL_GetError());
    } else {
      ownsHapticSubsystem = true;
      if (SDL_IsJoystickHaptic(gameplayJoystick)) {
        useHaptic(SDL_OpenHapticFromJoystick(gameplayJoystick), "gameplay joystick");
      }
      if (backend == Backend::none) {
        int count = 0;
        SDL_HapticID *ids = SDL_GetHaptics(&count);
        if (ids) {
          for (int pass = 0; pass < 2 && backend == Backend::none; ++pass) {
            for (int i = 0; i < count && backend == Backend::none; ++i) {
              const char *hn = SDL_GetHapticNameForID(ids[i]);
              const bool preferred = hn && (std::strstr(hn, "twl6040") || std::strstr(hn, "vibrator"));
              if ((pass == 0) != preferred) continue;
              useHaptic(SDL_OpenHaptic(ids[i]), hn ? hn : "unnamed");
            }
          }
          SDL_free(ids);
        }
      }
      if (backend == Backend::none) {
        SDL_QuitSubSystem(SDL_INIT_HAPTIC);
        ownsHapticSubsystem = false;
      }
    }
  }

  ddForce_found = backend != Backend::none;
  ddForce_enabled = ddForce_found;
  if (!ddForce_found) {
    LOG_WARNING.printf("PYRA RUMBLE: no usable rumble backend: %s", SDL_GetError());
  } else if (envIsTrue("PYRA_RUMBLE_TEST") && !startupTestPlayed) {
    // Exactly one 500-ms startup diagnostic per process.  Some pyrainput
    // configurations ignore SDL's finite duration, so ALWAYS send STOP too.
    // The half-second delay is acceptable only in explicit test mode.
    startupTestPlayed = true;
    const bool testOK = backend == Backend::helper
                          ? sendHelper("R 65535 500\n")
                          : backend == Backend::joystick
                            ? SDL_RumbleJoystick(gameplayJoystick, 0xFFFF, 0xFFFF, 500)
                            : SDL_PlayHapticRumble(haptic, 1.0f, 500);
    if (testOK) {
      LOG_INFO << "PYRA RUMBLE: one 500-ms diagnostic impulse started";
      SDL_Delay(530);
      stopMotor();
      LOG_INFO << "PYRA RUMBLE: 500-ms diagnostic explicitly stopped";
    } else {
      LOG_WARNING.printf("PYRA RUMBLE: startup playback test failed: %s",
                         backend == Backend::helper ? "helper IPC failure" : SDL_GetError());
      stopMotor();
    }
  }
  return ddForce_found;
}

// Monitor the helper's *actual* playback result.  Writing R to its socket
// proves only that the command was queued; FAIL means the motor rejected it.
// Do not leave Force Feedback permanently marked successful after that.
void checkHelperFeedback() {
  if (backend != Backend::helper || helperFD < 0)
    return;
  bool failed = false;
  for (;;) {
    char buffer[320]{};
    const ssize_t n = recv(helperFD, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);
    if (n > 0) {
      buffer[n] = '\0';
      if (std::strstr(buffer, "FAIL ")) {
        LOG_WARNING.printf("PYRA RUMBLE: SDL2 helper reports playback failure: %s", buffer);
        failed = true;
      } else if (std::strstr(buffer, "BACKEND ")) {
        LOG_INFO.printf("PYRA RUMBLE: SDL2 helper switched to %s", buffer);
      }
      if (failed) break;
    } else if (n == 0) {
      LOG_WARNING << "PYRA RUMBLE: SDL2 helper exited unexpectedly";
      failed = true;
      break;
    } else {
      if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        LOG_WARNING.printf("PYRA RUMBLE: SDL2 helper IPC read failed: %s", std::strerror(errno));
        failed = true;
      }
      break;
    }
  }
  if (failed) {
    backend = Backend::none;
    closeHelper();
    helperRejected = true;
    ddForce_found = false;
    ddForce_enabled = false;
    // Try the *already open* SDL3 joystick / haptic devices as a fallback;
    // retain effect IDs so that high-level gameplay triggers keep working.
    if (!initialize()) {
      LOG_WARNING << "PYRA RUMBLE: no fallback backend; force feedback unavailable";
    }
  }
}

float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }

float effectMagnitude(const tFFB_Effect &data) {
  float strength = 1.0f;
  switch (data.Type) {
  case kConstant:
    strength = std::abs(static_cast<float>(data.TypeInfo.Constant.Mag)) / FF_NOMINALMAX;
    break;
  case kRamp:
    strength = std::max(std::abs(static_cast<float>(data.TypeInfo.Ramp.Start)),
                        std::abs(static_cast<float>(data.TypeInfo.Ramp.End))) / FF_NOMINALMAX;
    break;
  case kWave_Sine: case kWave_Square: case kWave_Triangle: case kWave_SawUp: case kWave_SawDown:
    strength = static_cast<float>(data.TypeInfo.Wave.Mag) / FF_NOMINALMAX;
    break;
  default: break;
  }
  return clamp01(strength * clamp01(static_cast<float>(data.Gain) / FF_NOMINALMAX) * masterGain);
}

uint32_t effectDuration(const tFFB_Effect &data) {
  if (data.Duration == 0 || data.Duration == kInfinite_Duration)
    return 120;
  return std::max(35u, std::min(650u, data.Duration));
}
} // namespace

// PYRA: gameplay uses one-shot events with the same physical backend that
// already passes the menu startup test. Do not gate these behind old
// DirectInput/IFR effect IDs: their validity is independent of the motor.
// The caller checks D3Use_force_feedback, so a disabled game preference is
// respected. The helper enforces a second, independent motor deadline.
void ddio_pyra_GameplayRumble(float intensity, unsigned durationMs, const char *eventName) {
  static uint64_t lastGameplayPulse = 0;
  static unsigned logged = 0;
  if (envIsFalse("PYRA_RUMBLE") || !std::isfinite(intensity) || intensity <= 0.0f)
    return;
  if (backend == Backend::none || !ddForce_found) {
    if (!initialize()) {
      if (logged++ < 12)
        LOG_WARNING.printf("PYRA RUMBLE GAME: %s cannot start: no working backend", eventName ? eventName : "event");
      return;
    }
  }
  checkHelperFeedback();
  if (backend == Backend::none || !ddForce_found) {
    if (!initialize()) return;
  }
  const uint64_t now = SDL_GetTicks();
  // The TWL6040 motor reacted to the full-strength startup probe, but
  // 75-ms/38%-strength gameplay pulses were physically imperceptible.
  // Allow a finite motor spin-up and the helper's mandatory 90-ms rest.
  if (lastGameplayPulse && now - lastGameplayPulse < 380) return;
  // Preserve the user's master gain, while using a strong enough *event*
  // strength for the single Pyra motor to become noticeable.
  float safeStrength = clamp01(std::max(intensity, 0.85f) * masterGain);
  if (safeStrength <= 0.001f) {
    if (logged++ < 12)
      LOG_WARNING.printf("PYRA RUMBLE GAME: %s muted by gain=%.2f", eventName ? eventName : "event", double(masterGain));
    return;
  }
  unsigned pulseMs = std::clamp(durationMs, 260u, 600u);
  const Uint16 amplitude = static_cast<Uint16>(safeStrength * 65535.0f);
  const bool accepted = backend == Backend::helper
      ? sendHelper("R " + std::to_string(amplitude) + " " + std::to_string(pulseMs) + "\n")
      : backend == Backend::joystick
          ? SDL_RumbleJoystick(gameplayJoystick, amplitude, amplitude, pulseMs)
          : (backend == Backend::haptic && haptic && SDL_PlayHapticRumble(haptic, safeStrength, pulseMs));
  if (logged++ < 24 || !accepted) {
    LOG_INFO.printf("PYRA RUMBLE GAME: %s strength=%.2f duration=%ums backend=%s queued=%d",
                    eventName ? eventName : "event", double(safeStrength), pulseMs,
                    backend == Backend::helper ? "SDL2-helper" : backend == Backend::joystick ? "SDL3-joystick" : "SDL3-haptic",
                    int(accepted));
  }
  if (accepted) {
    lastGameplayPulse = now;
    consecutiveFailures = 0;
  } else if (++consecutiveFailures >= 3) {
    LOG_WARNING << "PYRA RUMBLE GAME: repeated backend errors; trying fallback at next event";
    detach();
  }
}

void ddio_ff_AttachForce(void) {} // gameplay input is already owned by sdljoy.cpp
void ddio_ff_DetachForce(void) { detach(); }
int ddio_ff_Init(void) { return initialize() ? 1 : 0; }
int ddio_ffjoy_Init(void) { return ddio_ff_Init(); }
int ddio_ff_Acquire(tDevice) { return initialize() ? 1 : 0; }
int ddio_ff_Unacquire(tDevice) { stopMotor(); return 1; }
int ddio_ff_SetCoopLevel(tDevice, int) { return 1; }
int ddio_ffjoy_Query(int dev, int *buttons, int *axes) {
  if (buttons) *buttons = 0;
  if (axes) *axes = 0;
  return (dev == 0 && initialize()) ? 1 : 0;
}
void ddio_ff_GetInfo(bool *found, bool *enabled) {
  if (!ddForce_found)
    initialize();
  if (found) *found = ddForce_found;
  if (enabled) *enabled = ddForce_enabled;
}
void ddio_ffb_Pause(tDevice) { paused = true; stopMotor(); }
void ddio_ffb_Continue(tDevice) { paused = false; }
void ddio_ffb_Enable(tDevice) { ddForce_enabled = ddForce_found; }
void ddio_ffb_Disable(tDevice) { ddForce_enabled = false; stopMotor(); }

int ddio_ffb_effectCreate(tDevice, tFFB_Effect *effect) {
  if (!ddForce_found || !effect || nextEffectID >= DDIO_FF_MAXEFFECTS)
    return -1;
  const int id = nextEffectID++;
  effects[id].data = *effect;
  effects[id].valid = true;
  LOG_DEBUG.printf("PYRA RUMBLE: created effect=%d type=%d gain=%u duration=%u",
                   id, int(effect->Type), unsigned(effect->Gain), unsigned(effect->Duration));
  // Ship shaking is already rate-limited at the high-level; keep general
  // effects as one-shot impulses to avoid permanent motor activation.
  effects[id].cooldown = effect->Duration > 300 ? 130u : 55u;
  return id;
}
void ddio_ffb_DestroyAll(void) {
  stopMotor();
  effects = {};
  nextEffectID = 0;
}
void ddio_ffb_effectPlay(int16_t id) {
  checkHelperFeedback();
  if (!ddForce_found || !ddForce_enabled || paused || id < 0 || id >= DDIO_FF_MAXEFFECTS)
    return;
  Effect &effect = effects[id];
  if (!effect.valid)
    return;
  const uint64_t now = SDL_GetTicks();
  if (effect.lastStarted && now - effect.lastStarted < effect.cooldown)
    return;
  const float strength = effectMagnitude(effect.data);
  if (strength <= 0.001f)
    return;
  const uint32_t duration = effectDuration(effect.data);
  const Uint16 amplitude = static_cast<Uint16>(std::clamp(strength, 0.0f, 1.0f) * 65535.0f);
  const bool ok = (backend == Backend::helper)
                      ? sendHelper("R " + std::to_string(amplitude) + " " + std::to_string(duration) + "\n")
                      : (backend == Backend::joystick)
                        ? SDL_RumbleJoystick(gameplayJoystick, amplitude, amplitude, duration)
                        : (backend == Backend::haptic && haptic && SDL_PlayHapticRumble(haptic, strength, duration));
  if (++attemptedPlays <= 8) {
    LOG_INFO.printf("PYRA RUMBLE: game effect=%d gain=%.2f amplitude=%u duration=%ums backend=%s submitted=%d",
                    id, strength, unsigned(amplitude), unsigned(duration),
                    backend == Backend::helper ? "SDL2-helper" : backend == Backend::joystick ? "SDL3-joystick" : "SDL3-haptic",
                    int(ok));
  }
  if (ok) {
    effect.lastStarted = now;
    consecutiveFailures = 0;
  } else {
    if (++consecutiveFailures <= 3) {
      LOG_WARNING.printf("PYRA RUMBLE: playback failed (effect=%d backend=%s): %s", id,
                         backend == Backend::helper ? "helper" : backend == Backend::joystick ? "joystick" : "haptic",
                         backend == Backend::helper ? "IPC failure" : SDL_GetError());
    }
    if (consecutiveFailures >= 3) {
      LOG_WARNING << "PYRA RUMBLE: disabling after repeated playback failures; gameplay input remains active";
      ddForce_enabled = false;
      stopMotor();
    }
  }
}
void ddio_ffb_effectStop(int16_t id) {
  if (id >= 0 && id < DDIO_FF_MAXEFFECTS && effects[id].valid)
    stopMotor(); // One physical motor cannot stop independent effects.
}
void ddio_ffb_effectStopAll(tDevice) { stopMotor(); }
void ddio_ffb_effectUnload(int16_t id) {
  if (id >= 0 && id < DDIO_FF_MAXEFFECTS) {
    effects[id].valid = false;
    stopMotor();
  }
}
void ddio_ffb_effectModify(int16_t id, int *direction, uint32_t *duration, uint32_t *gain,
                           uint32_t *period, tEffInfo *info, tEffEnvelope *envelope) {
  if (id < 0 || id >= DDIO_FF_MAXEFFECTS || !effects[id].valid)
    return;
  tFFB_Effect &data = effects[id].data;
  if (direction) data.Direction = *direction;
  if (duration) data.Duration = *duration;
  if (gain) data.Gain = std::min(*gain, static_cast<uint32_t>(FF_NOMINALMAX));
  if (period) data.Period = *period;
  if (info) data.TypeInfo = *info;
  if (envelope) data.Envelope = *envelope;
}
void ddio_ffb_GetEffectData(int16_t id, int *direction, uint32_t *duration, uint32_t *gain,
                            uint32_t *period, tEffInfo *info, tEffEnvelope *envelope) {
  if (id < 0 || id >= DDIO_FF_MAXEFFECTS || !effects[id].valid)
    return;
  const tFFB_Effect &data = effects[id].data;
  if (direction) *direction = data.Direction;
  if (duration) *duration = data.Duration;
  if (gain) *gain = data.Gain;
  if (period) *period = data.Period;
  if (info) *info = data.TypeInfo;
  if (envelope) *envelope = data.Envelope;
}
void ddio_ffjoy_EnableAutoCenter(tDevice, bool) {} // unsupported by single vibrator
void ddio_ffjoy_SetGain(tDevice, float value) { masterGain = clamp01(value); }
bool ddio_ffjoy_IsAutoCentered(tDevice) { return false; }
bool ddio_ffjoy_SupportAutoCenter(tDevice) { return false; }

// Legacy Windows Immersion/IFR resource formats aren't needed.  The existing
// D3ForceFeedback.cpp creates built-in equivalent effects when these return -1.
FORCEPROJECT ddio_ForceLoadProject(std::filesystem::path &, tDevice) { return nullptr; }
void ddio_ForceUnloadProject(FORCEPROJECT) {}
int ddio_CreateForceFromProject(FORCEPROJECT, const char *) { return -1; }
