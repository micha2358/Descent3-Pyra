/*
 * Descent 3 / DragonBox Pyra: isolated SDL2 force-feedback owner.
 *
 * The gameplay process uses SDL3 for input. This helper alone opens the SDL2
 * joystick/haptic devices; after opening the joystick it initializes haptics.
 * No direct evdev access, hard-coded device nodes, or special permissions.
 *
 * IMPORTANT: selecting an SDL2 device is not evidence that it can rumble.
 * We require a real, nonzero playback command to succeed before replying READY.
 * A short 60-ms startup probe is deliberately performed once. This proves
 * only API acceptance, not that the physical TWL6040 motor actually moved.
 */
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace {
using U16 = uint16_t;
using U32 = uint32_t;
constexpr U32 INIT_JOYSTICK = 0x00000200u;
constexpr U32 INIT_HAPTIC  = 0x00001000u;

template<typename T> bool sym(void *lib, T &dst, const char *name) {
  dst = reinterpret_cast<T>(dlsym(lib, name));
  return dst != nullptr;
}
struct Sdl2 {
  void *library = nullptr;
  int (*Init)(U32) = nullptr;
  void (*Quit)() = nullptr;
  const char *(*GetError)() = nullptr;
  int (*NumJoysticks)() = nullptr;
  const char *(*JoystickNameForIndex)(int) = nullptr;
  void *(*JoystickOpen)(int) = nullptr;
  void (*JoystickClose)(void *) = nullptr;
  int (*JoystickRumble)(void *,U16,U16,U32) = nullptr;
  int (*InitSubSystem)(U32) = nullptr;
  int (*NumHaptics)() = nullptr;
  const char *(*HapticName)(int) = nullptr;
  void *(*HapticOpen)(int) = nullptr;
  void *(*HapticOpenFromJoystick)(void *) = nullptr;
  int (*HapticRumbleSupported)(void *) = nullptr;
  int (*HapticRumbleInit)(void *) = nullptr;
  int (*HapticRumblePlay)(void *,float,U32) = nullptr;
  int (*HapticRumbleStop)(void *) = nullptr;
  void (*HapticClose)(void *) = nullptr;
  bool load() {
    library = dlopen("libSDL2-2.0.so.0",RTLD_NOW | RTLD_LOCAL);
    if (!library) return false;
#define REQUIRED(name) if (!sym(library,name,"SDL_" #name)) return false
    REQUIRED(Init); REQUIRED(Quit); REQUIRED(GetError);
    REQUIRED(NumJoysticks); REQUIRED(JoystickNameForIndex);
    REQUIRED(JoystickOpen); REQUIRED(JoystickClose);
    REQUIRED(InitSubSystem); REQUIRED(NumHaptics); REQUIRED(HapticName);
    REQUIRED(HapticOpen); REQUIRED(HapticOpenFromJoystick);
    REQUIRED(HapticRumbleSupported); REQUIRED(HapticRumbleInit);
    REQUIRED(HapticRumblePlay); REQUIRED(HapticRumbleStop); REQUIRED(HapticClose);
#undef REQUIRED
    // Some SDL2 versions have haptics but no SDL_JoystickRumble.
    sym(library,JoystickRumble,"SDL_JoystickRumble");
    return true;
  }
};

bool contains(const char *str, const char *needle) {
  return str && std::strstr(str,needle);
}
bool isPyra(const char *name) {
  return contains(name,"pyraInput") || contains(name,"Pyra");
}
bool isPreferredHaptic(const char *name) {
  return contains(name,"twl6040") || contains(name,"vibrator");
}
void reply(const std::string &message) {
  const std::string wire = message + "\n";
  (void)send(STDIN_FILENO,wire.data(),wire.size(),MSG_NOSIGNAL);
}
void log(const char *format, const std::string &arg = "") {
  std::fprintf(stderr,"PYRA RUMBLE HELPER: ");
  std::fprintf(stderr,format,arg.c_str());
  std::fputc('\n',stderr);
  std::fflush(stderr);
}

enum class Backend { none, joystick, fromJoystick, separate };
const char *backendName(Backend b) {
  switch (b) {
    case Backend::joystick: return "SDL2-joystick";
    case Backend::fromJoystick: return "SDL2-haptic-from-joystick";
    case Backend::separate: return "SDL2-haptic-separate";
    default: return "none";
  }
}
struct Owner {
  Sdl2 sdl;
  void *joy = nullptr;
  void *haptic = nullptr;
  Backend backend = Backend::none;
  std::string device;
  bool hapticStarted = false;
  std::string mode;

  bool init() {
    if (!sdl.load()) { log("unable to load SDL2 runtime/required entry points"); return false; }
    if (sdl.Init(INIT_JOYSTICK) != 0) {
      log("SDL2 joystick init failed: %s",sdl.GetError()); return false;
    }
    int n = sdl.NumJoysticks();
    int id = -1;
    for (int i = 0; i < n; ++i) {
      if (isPyra(sdl.JoystickNameForIndex(i))) { id = i; break; }
    }
    if (id < 0 && n > 0) id = 0;
    if (id >= 0) {
      joy = sdl.JoystickOpen(id);
      if (joy) {
        const char *name = sdl.JoystickNameForIndex(id);
        device = name ? name : "unnamed joystick";
        log("opened gameplay-compatible SDL2 joystick: %s",device);
      } else log("SDL2 JoystickOpen failed: %s",sdl.GetError());
    }
    const char *requested = std::getenv("PYRA_RUMBLE_HELPER_BACKEND");
    mode = requested ? requested : "auto";
    if (mode == "haptic") mode = "auto-haptic"; // fromJoystick then separate
    if (mode != "auto" && mode != "joystick" && mode != "auto-haptic" &&
        mode != "fromjoystick" && mode != "separate") {
      log("unknown PYRA_RUMBLE_HELPER_BACKEND (auto/joystick/haptic/fromjoystick/separate): %s",mode);
      return false;
    }
    return true;
  }
  bool initHaptic() {
    if (hapticStarted) return true;
    if (sdl.InitSubSystem(INIT_HAPTIC) != 0) {
      log("SDL2 haptic subsystem unavailable: %s",sdl.GetError());
      return false;
    }
    hapticStarted = true;
    return true;
  }
  void closeHaptic() {
    if (haptic) {
      sdl.HapticRumbleStop(haptic);
      sdl.HapticClose(haptic);
      haptic = nullptr;
    }
    if (backend == Backend::fromJoystick || backend == Backend::separate)
      backend = Backend::none;
  }
  bool play(U16 strength, U32 ms) {
    if (backend == Backend::joystick && joy && sdl.JoystickRumble)
      return sdl.JoystickRumble(joy,strength,strength,ms) == 0;
    if ((backend == Backend::fromJoystick || backend == Backend::separate) && haptic)
      return sdl.HapticRumblePlay(haptic,strength / 65535.0f,ms) == 0;
    return false;
  }
  void stop() {
    // On the Pyra, SDL's requested duration is not always sufficient to
    // terminate the physical motor.  Explicitly transmit a zero-strength
    // effect or a HapticRumbleStop when a deadline expires.
    if (backend == Backend::joystick && joy && sdl.JoystickRumble) {
      if (sdl.JoystickRumble(joy,0,0,0) != 0)
        log("SDL2 joystick explicit stop failed: %s",sdl.GetError());
    }
    if ((backend == Backend::fromJoystick || backend == Backend::separate) && haptic) {
      if (sdl.HapticRumbleStop(haptic) != 0)
        log("SDL2 haptic explicit stop failed: %s",sdl.GetError());
    }
  }
  // Return READY only after an actual nonzero effect has been accepted.
  bool testCandidate(Backend kind, const std::string &name, U32 pulseMs) {
    backend = kind;
    device = name;
    if (!play(0xFFFF,pulseMs)) {
      log((std::string(backendName(kind)) + " playback rejected: " + sdl.GetError()).c_str());
      if (kind == Backend::joystick) backend = Backend::none;
      else closeHaptic();
      return false;
    }
    log((std::string(backendName(kind)) + " accepted nonzero effect on " + name + " (physical motor unverified)").c_str());
    // Give the short startup probe enough time to reach the vibrator, then stop.
    if (pulseMs < 100) { usleep((pulseMs + 20) * 1000); stop(); }
    return true;
  }
  bool tryJoystick(U32 pulseMs) {
    if (!joy || !sdl.JoystickRumble) return false;
    return testCandidate(Backend::joystick,device,pulseMs);
  }
  bool tryFromJoystick(U32 pulseMs) {
    if (!joy || !initHaptic()) return false;
    void *candidate = sdl.HapticOpenFromJoystick(joy);
    if (!candidate) {
      log("HapticOpenFromJoystick failed: %s",sdl.GetError());
      return false;
    }
    if (!sdl.HapticRumbleSupported(candidate) || sdl.HapticRumbleInit(candidate) != 0) {
      log("HapticRumbleInit from joystick failed: %s",sdl.GetError());
      sdl.HapticClose(candidate);
      return false;
    }
    haptic = candidate;
    return testCandidate(Backend::fromJoystick,device,pulseMs);
  }
  bool trySeparate(U32 pulseMs) {
    if (!initHaptic()) return false;
    const int count = sdl.NumHaptics();
    if (count <= 0) { log("SDL2 reports no separate haptic devices"); return false; }
    // Prefer the physical twl6040 vibrator, then other reported devices.
    for (int pass = 0; pass != 2; ++pass) {
      for (int i = 0; i < count; ++i) {
        const char *name = sdl.HapticName(i);
        if (isPreferredHaptic(name) != (pass == 0)) continue;
        void *candidate = sdl.HapticOpen(i);
        if (!candidate) {
          log("HapticOpen failed: %s",sdl.GetError());
          continue;
        }
        if (!sdl.HapticRumbleSupported(candidate) || sdl.HapticRumbleInit(candidate) != 0) {
          log("HapticRumbleInit on separate device failed: %s",sdl.GetError());
          sdl.HapticClose(candidate);
          continue;
        }
        haptic = candidate;
        const std::string selectedName = name ? name : "unnamed haptic";
        if (testCandidate(Backend::separate,selectedName,pulseMs)) return true;
      }
    }
    return false;
  }
  bool select(U32 pulseMs, Backend skip = Backend::none) {
    stop(); closeHaptic(); backend = Backend::none;
    const bool joyAllowed = mode == "auto" || mode == "joystick";
    const bool fromAllowed = mode == "auto" || mode == "auto-haptic" || mode == "fromjoystick";
    const bool separateAllowed = mode == "auto" || mode == "auto-haptic" || mode == "separate";
    if (joyAllowed && skip != Backend::joystick && tryJoystick(pulseMs)) return true;
    if (fromAllowed && skip != Backend::fromJoystick && tryFromJoystick(pulseMs)) return true;
    if (separateAllowed && skip != Backend::separate && trySeparate(pulseMs)) return true;
    return false;
  }
  bool sendEffect(U16 magnitude, U32 ms) {
    if (play(magnitude,ms)) return true;
    const Backend failed = backend;
    log((std::string(backendName(failed)) + " effect failed, searching alternative: " + sdl.GetError()).c_str());
    if (mode != "auto" && mode != "auto-haptic") return false;
    // Switch only after an actual failure; do not keep simultaneous FF handles.
    if (!select(60,failed)) return false;
    reply(std::string("BACKEND ") + backendName(backend) + " " + device);
    return play(magnitude,ms);
  }
  void shutdown() {
    stop(); closeHaptic();
    if (joy) { sdl.JoystickClose(joy); joy=nullptr; }
    if (sdl.Quit) sdl.Quit();
  }
};
} // namespace

int main(int argc, char **argv) {
  const bool selfTest = argc >= 2 && !std::strcmp(argv[1],"--test");
  Owner owner;
  if (!owner.init()) {
    if (!selfTest) reply("ERROR SDL2 initialization failed");
    owner.shutdown();
    return 2;
  }
  // --test performs a full 500-ms pulse; normal init validates with a brief 60-ms pulse.
  const U32 probeMs = selfTest ? 500 : 60;
  if (!owner.select(probeMs)) {
    log("no SDL2 backend accepted a real rumble effect");
    if (!selfTest) reply("ERROR no SDL2 rumble backend accepted playback");
    owner.shutdown();
    return 3;
  }
  std::fprintf(stderr,"PYRA RUMBLE HELPER: selected backend=%s device='%s'\n",
               backendName(owner.backend),owner.device.c_str());
  std::fflush(stderr);
  if (selfTest) {
    log("500ms full-strength test accepted by SDL2 (verify physical vibration)");
    usleep(650000);
    owner.shutdown();
    return 0;
  }
  reply(std::string("READY ") + backendName(owner.backend) + " " + owner.device);

  // The Pyra/pyrainput chain may keep running a motor even if SDL accepted a
  // finite effect duration.  Maintain our OWN monotonic deadline for EVERY
  // command.  This poll loop must be allowed to time out while the game is
  // otherwise idle: fgets() would block indefinitely and never stop rumble.
  using Clock = std::chrono::steady_clock;
  const auto noDeadline = Clock::time_point{};
  auto stopAt = noDeadline;
  auto burstBegan = noDeadline;
  auto silenceUntil = noDeadline;
  std::string pending;
  unsigned timedStops = 0;
  unsigned actualPlays = 0;
  unsigned suppressedPlays = 0;
  U16 activeMagnitude = 0;
  constexpr unsigned maxContinuousMs = 800;
  constexpr unsigned restMs = 90;

  auto timedStop = [&]() {
    owner.stop();
    stopAt = noDeadline;
    burstBegan = noDeadline;
    activeMagnitude = 0;
    silenceUntil = Clock::now() + std::chrono::milliseconds(restMs);
    if (++timedStops <= 4)
      log("explicit timed rumble stop issued (motor may require pyrainput restart if it ignores stop)");
  };

  bool quit = false;
  while (!quit) {
    if (stopAt != noDeadline && Clock::now() >= stopAt) {
      timedStop();
      continue;
    }
    int timeoutMs = -1;
    if (stopAt != noDeadline) {
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(stopAt - Clock::now()).count();
      timeoutMs = static_cast<int>(std::max<int64_t>(0,remaining));
    }
    struct pollfd pfd { STDIN_FILENO, POLLIN, 0 };
    const int ready = poll(&pfd,1,timeoutMs);
    if (ready == 0) {
      if (stopAt != noDeadline) timedStop();
      continue;
    }
    if (ready < 0) {
      if (errno == EINTR) continue;
      log("IPC poll failed: %s",std::strerror(errno));
      break;
    }
    if (!(pfd.revents & (POLLIN | POLLHUP))) break;
    char buffer[256];
    const ssize_t n = recv(STDIN_FILENO,buffer,sizeof(buffer),0);
    if (n <= 0) {
      if (n < 0 && errno == EINTR) continue;
      if (n < 0) log("IPC receive failed: %s",std::strerror(errno));
      break;
    }
    pending.append(buffer,static_cast<size_t>(n));
    if (pending.size() > 1024) {
      log("IPC command too long, discarding");
      pending.clear();
      owner.stop();
      stopAt = noDeadline;
      burstBegan = noDeadline;
      continue;
    }
    for (;;) {
      const size_t eol=pending.find('\n');
      if (eol==std::string::npos) break;
      const std::string command=pending.substr(0,eol);
      pending.erase(0,eol+1);
      if (command == "Q") { quit=true; break; }
      if (command == "S") {
        owner.stop();
        stopAt=noDeadline;
        burstBegan=noDeadline;
        activeMagnitude=0;
        silenceUntil=Clock::now() + std::chrono::milliseconds(restMs);
        continue;
      }
      unsigned magnitude=0,ms=0;
      if (std::sscanf(command.c_str(),"R %u %u",&magnitude,&ms)!=2) continue;
      magnitude=std::min(magnitude,65535u);
      ms=std::clamp(ms,25u,800u);
      const auto now=Clock::now();
      // Stop requests are unconditional; a queued rest must not prevent them.
      if (magnitude==0) {
        owner.stop();
        stopAt=noDeadline;
        burstBegan=noDeadline;
        activeMagnitude=0;
        continue;
      }
      // Never let legacy 3-20%-strength D3 effect commands cancel a real
      // 85%-strength gameplay pulse before the motor has spun up.
      if (stopAt != noDeadline && now < stopAt && magnitude <= activeMagnitude) {
        if (++suppressedPlays <= 12)
          std::fprintf(stderr,"PYRA RUMBLE HELPER: SKIP weaker overlapping effect amplitude=%u active=%u\n",
                       magnitude, unsigned(activeMagnitude));
        continue;
      }
      if (now < silenceUntil) {
        if (++suppressedPlays <= 12)
          std::fprintf(stderr,"PYRA RUMBLE HELPER: SKIP during required motor rest amplitude=%u\n",magnitude);
        continue;
      }
      if (burstBegan==noDeadline) burstBegan=now;
      const auto hardStop=burstBegan + std::chrono::milliseconds(maxContinuousMs);
      if (now>=hardStop) { timedStop(); continue; }
      if (!owner.sendEffect(static_cast<U16>(magnitude),ms)) {
        log("rumble command failed on every available backend");
        reply(std::string("FAIL ") + owner.sdl.GetError());
        owner.stop();
        stopAt=noDeadline;
        burstBegan=noDeadline;
        activeMagnitude=0;
      } else {
        const auto requestedStop=Clock::now()+std::chrono::milliseconds(ms);
        stopAt=std::min(requestedStop,hardStop);
        activeMagnitude=static_cast<U16>(magnitude);
        if (++actualPlays <= 24)
          std::fprintf(stderr,"PYRA RUMBLE HELPER: PLAY accepted by SDL2 amplitude=%u duration=%ums (physical motor unverified)\n",magnitude,ms);
      }
    }
  }
  owner.shutdown();
  return 0;
}
