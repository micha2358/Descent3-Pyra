/*
* Descent 3 
* Copyright (C) 2024 Parallax Software
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

#pragma once

#include <cstdlib>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#if defined(PYRA_GLES2)
#include <dlfcn.h>

// Kept alive for the lifetime of the process: SDL3 and the renderer must
// resolve GLES functions from the same PowerVR implementation.
inline void *gPyraEGLHandle = nullptr;
inline void *gPyraGLESHandle = nullptr;
#endif

#if defined(PYRA_GLES2)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#ifndef GL_TEXTURE0_ARB
#define GL_TEXTURE0_ARB GL_TEXTURE0
#endif
#ifndef GL_INVALID_INDEX
#define GL_INVALID_INDEX 0xFFFFFFFFu
#endif
#else
#define GL_GLEXT_PROTOTYPES
#include <SDL3/SDL_opengl.h>
#endif

#include "descent.h"
#include "log.h"
#include "module.h"

#if defined(WIN32)
#define GLFUNCCALL __stdcall
#elif defined(POSIX)
#include <unistd.h>
#define GLFUNCCALL
#endif

inline void CheckError() {
#if defined(FATAL_GL_ERRORS)
#if defined(PYRA_GLES2)
  static auto _dglGetError = reinterpret_cast<GLenum (GLFUNCCALL *)()>(
      gPyraGLESHandle ? dlsym(gPyraGLESHandle, "glGetError") : nullptr);
#else
  static auto _dglGetError = reinterpret_cast<GLenum (GLFUNCCALL *)()>(
      SDL_GL_GetProcAddress("glGetError"));
#endif
  if (!_dglGetError) {
    throw std::runtime_error("failed to resolve glGetError");
  }
  auto err = _dglGetError();
  switch (err) {
  case GL_NO_ERROR:
    return;
  case GL_INVALID_ENUM:
    throw std::runtime_error("invalid enum");
  case GL_INVALID_VALUE:
    throw std::runtime_error("invalid value");
  case GL_INVALID_OPERATION:
    throw std::runtime_error("invalid operation");
  case GL_INVALID_FRAMEBUFFER_OPERATION:
    throw std::runtime_error("invalid framebuffer operation");
  case GL_INVALID_INDEX:
    throw std::runtime_error("invalid index");
  default:
    throw std::runtime_error("unknown error: " + std::to_string(err));
  }
#endif
}

#if defined(PYRA_GLES2)
// Optional targeted diagnostics: records the offending GLES2 function as
// soon as the driver reports an error, instead of discovering it much later
// at rend_Flip(). Disabled unless PYRA_GL_CALL_TRACE=1 is set.
inline void PyraTraceGLError(std::string_view functionName) {
  static const bool enabled = [] {
    const char *value = std::getenv("PYRA_GL_CALL_TRACE");
    return value && value[0] && value[0] != '0';
  }();
  if (!enabled || !gPyraGLESHandle || functionName == "glGetError") return;
  using GetErrorFn = GLenum (GLFUNCCALL *)();
  static GetErrorFn getError = reinterpret_cast<GetErrorFn>(dlsym(gPyraGLESHandle, "glGetError"));
  if (!getError) return;
  GLenum err;
  static unsigned count = 0;
  while ((err = getError()) != GL_NO_ERROR) {
    if (count < 80 || count % 256 == 0) {
      LOG_ERROR.printf("PYRA GL CALL ERROR #%u: %.*s -> 0x%04X",
                       count, (int)functionName.size(), functionName.data(), (unsigned)err);
    }
    ++count;
  }
}
#endif

#if defined(PYRA_GLES2)
// PERFORMANCE 8: Keep the command stream ordered, but avoid flushing an
// outstanding polygon batch for read-only operations or proven GL no-ops.
// Cached bindings are conservative and explicitly invalidated when the
// context is (re)created or an object can be deleted.
inline bool PyraSmartEnabled() {
  static const bool enabled = [] {
    const char *env = std::getenv("PYRA_SMART");
    return !(env && env[0] == '0' && env[1] == '\0');
  }();
  return enabled;
}

struct PyraGLBindingCache {
  bool activeKnown = false;
  GLenum activeUnit = GL_TEXTURE0;
  bool bufferKnown = false;
  GLuint arrayBuffer = 0;
  bool programKnown = false;
  GLuint program = 0;
  bool framebufferKnown = false;
  GLuint framebuffer = 0;
  static constexpr unsigned kTrackedTextureUnits = 8;
  bool textureKnown[kTrackedTextureUnits] = {};
  GLuint texture2D[kTrackedTextureUnits] = {};
  // Only skip mutations whose exact prior value was established by this
  // wrapper. No assumptions about the GL defaults or an old EGL context.
  static constexpr std::array<GLenum, 9> kTrackedCaps = {
      GL_BLEND, GL_DEPTH_TEST, GL_SCISSOR_TEST, GL_CULL_FACE,
      GL_DITHER, GL_POLYGON_OFFSET_FILL, GL_STENCIL_TEST,
      GL_SAMPLE_ALPHA_TO_COVERAGE, GL_SAMPLE_COVERAGE};
  bool capKnown[kTrackedCaps.size()] = {};
  bool capEnabled[kTrackedCaps.size()] = {};
  bool blendKnown = false;
  GLenum blendSrc = 0, blendDst = 0;
  bool depthMaskKnown = false;
  GLboolean depthMask = GL_TRUE;
  bool depthFuncKnown = false;
  GLenum depthFunc = 0;
  bool viewportKnown = false;
  GLint viewport[4] = {};
  bool scissorKnown = false;
  GLint scissor[4] = {};
  bool clearColorKnown = false;
  GLfloat clearColor[4] = {};
};
inline PyraGLBindingCache gPyraGLBindingCache;
inline void PyraGLResetBindingCache() { gPyraGLBindingCache = {}; }

// New Performance 8 state cache is independently switchable, retaining
// Performance 7's existing texture/program/buffer no-op suppression.
inline bool PyraGLStateCacheEnabled() {
  static const bool enabled = [] {
    const char *env = std::getenv("PYRA_STATE_CACHE");
    return !(env && env[0] == '0' && env[1] == '\0');
  }();
  return enabled;
}

enum PyraNoopReason : unsigned {
  kNoopActive, kNoopProgram, kNoopTexture, kNoopBuffer, kNoopFBO,
  kNoopEnable, kNoopDisable, kNoopBlendFunc, kNoopDepthMask,
  kNoopDepthFunc, kNoopViewport, kNoopScissor, kNoopCount
};
inline std::array<std::uint64_t, kNoopCount> gPyraNoopCounts{};
inline bool PyraNoopDetected(PyraNoopReason reason) {
  // Trace counters are optional; avoid adding a timing-dependent log in
  // the driver hot path.
  static const bool profile = [] {
    const char *env = std::getenv("PYRA_PERF");
    return env && env[0] && env[0] != '0';
  }();
  if (profile) ++gPyraNoopCounts[reason];
  return true;
}

// These are the only commands which may be skipped. All other mutations
// preserve their ordering via PyraBatchBarrier() below.
inline bool PyraGLSkipNoop(std::string_view name, GLenum value) {
  auto &c = gPyraGLBindingCache;
  if (name == "glActiveTexture") {
    if (c.activeKnown && c.activeUnit == value) return PyraNoopDetected(kNoopActive);
    c.activeUnit = value;
    c.activeKnown = true;
  } else if (name == "glUseProgram") {
    if (c.programKnown && c.program == value) return PyraNoopDetected(kNoopProgram);
    c.program = value;
    c.programKnown = true;
  } else if (PyraGLStateCacheEnabled() && (name == "glEnable" || name == "glDisable")) {
    // Track only legal GLES2 capability enums. Other values still reach the
    // driver so GL_INVALID_ENUM/GL_INVALID_OPERATION behavior is preserved.
    for (unsigned i = 0; i < c.kTrackedCaps.size(); ++i) {
      if (c.kTrackedCaps[i] != value) continue;
      const bool enable = (name == "glEnable");
      if (c.capKnown[i] && c.capEnabled[i] == enable)
        return PyraNoopDetected(enable ? kNoopEnable : kNoopDisable);
      c.capKnown[i] = true;
      c.capEnabled[i] = enable;
      break;
    }
  } else if (PyraGLStateCacheEnabled() && name == "glDepthFunc" &&
             (value == GL_NEVER || value == GL_LESS || value == GL_EQUAL ||
              value == GL_LEQUAL || value == GL_GREATER || value == GL_NOTEQUAL ||
              value == GL_GEQUAL || value == GL_ALWAYS)) {
    if (c.depthFuncKnown && c.depthFunc == value)
      return PyraNoopDetected(kNoopDepthFunc);
    c.depthFuncKnown = true;
    c.depthFunc = value;
  }
  return false;
}
inline bool PyraGLSkipNoop(std::string_view name, GLenum target, GLuint id) {
  auto &c = gPyraGLBindingCache;
  if (name == "glBindTexture" && target == GL_TEXTURE_2D && c.activeKnown &&
      c.activeUnit >= GL_TEXTURE0 && c.activeUnit < GL_TEXTURE0 + PyraGLBindingCache::kTrackedTextureUnits) {
    const unsigned unit = unsigned(c.activeUnit - GL_TEXTURE0);
    if (c.textureKnown[unit] && c.texture2D[unit] == id) return PyraNoopDetected(kNoopTexture);
    c.textureKnown[unit] = true;
    c.texture2D[unit] = id;
  } else if (name == "glBindBuffer" && target == GL_ARRAY_BUFFER) {
    if (c.bufferKnown && c.arrayBuffer == id) return PyraNoopDetected(kNoopBuffer);
    c.bufferKnown = true;
    c.arrayBuffer = id;
  } else if (name == "glBindFramebuffer" && target == GL_FRAMEBUFFER) {
    if (c.framebufferKnown && c.framebuffer == id) return PyraNoopDetected(kNoopFBO);
    c.framebufferKnown = true;
    c.framebuffer = id;
  } else if (PyraGLStateCacheEnabled() && name == "glBlendFunc" &&
             // glBlendFunc source/destination legal enums (ES2 core).
             (target == GL_ZERO || target == GL_ONE || target == GL_SRC_COLOR ||
              target == GL_ONE_MINUS_SRC_COLOR || target == GL_DST_COLOR ||
              target == GL_ONE_MINUS_DST_COLOR || target == GL_SRC_ALPHA ||
              target == GL_ONE_MINUS_SRC_ALPHA || target == GL_DST_ALPHA ||
              target == GL_ONE_MINUS_DST_ALPHA || target == GL_CONSTANT_COLOR ||
              target == GL_ONE_MINUS_CONSTANT_COLOR || target == GL_CONSTANT_ALPHA ||
              target == GL_ONE_MINUS_CONSTANT_ALPHA || target == GL_SRC_ALPHA_SATURATE) &&
             (id == GL_ZERO || id == GL_ONE || id == GL_SRC_COLOR ||
              id == GL_ONE_MINUS_SRC_COLOR || id == GL_DST_COLOR ||
              id == GL_ONE_MINUS_DST_COLOR || id == GL_SRC_ALPHA ||
              id == GL_ONE_MINUS_SRC_ALPHA || id == GL_DST_ALPHA ||
              id == GL_ONE_MINUS_DST_ALPHA || id == GL_CONSTANT_COLOR ||
              id == GL_ONE_MINUS_CONSTANT_COLOR || id == GL_CONSTANT_ALPHA ||
              id == GL_ONE_MINUS_CONSTANT_ALPHA)) {
    if (c.blendKnown && c.blendSrc == target && c.blendDst == id)
      return PyraNoopDetected(kNoopBlendFunc);
    c.blendKnown = true;
    c.blendSrc = target;
    c.blendDst = id;
  }
  return false;
}
inline bool PyraGLSkipNoop(std::string_view name, GLboolean value) {
  auto &c = gPyraGLBindingCache;
  if (PyraGLStateCacheEnabled() && name == "glDepthMask" &&
      (value == GL_TRUE || value == GL_FALSE)) {
    if (c.depthMaskKnown && c.depthMask == value)
      return PyraNoopDetected(kNoopDepthMask);
    c.depthMaskKnown = true;
    c.depthMask = value;
  }
  return false;
}
inline bool PyraGLSkipNoop(std::string_view name, GLint x, GLint y, GLsizei w, GLsizei h) {
  if (!PyraGLStateCacheEnabled()) return false;
  auto &c = gPyraGLBindingCache;
  bool *known = nullptr;
  GLint *saved = nullptr;
  PyraNoopReason reason = kNoopViewport;
  if (name == "glViewport") {
    known = &c.viewportKnown;
    saved = c.viewport;
  } else if (name == "glScissor") {
    known = &c.scissorKnown;
    saved = c.scissor;
    reason = kNoopScissor;
  } else {
    return false;
  }
  if (w < 0 || h < 0) return false;
  if (*known && saved[0] == x && saved[1] == y && saved[2] == w && saved[3] == h)
    return PyraNoopDetected(reason);
  *known = true;
  saved[0] = x; saved[1] = y; saved[2] = w; saved[3] = h;
  return false;
}
template <typename... Args>
inline bool PyraGLSkipNoop(std::string_view, Args...) { return false; }

inline bool PyraGLReadOnly(std::string_view name) {
  // No queued polygon depends on these queries or on allocating object names.
  return name == "glGetError" || name == "glGetIntegerv" ||
         name == "glGetFloatv" || name == "glGetVertexAttribiv" ||
         name == "glGetProgramInfoLog" || name == "glGetProgramiv" ||
         name == "glGetShaderInfoLog" || name == "glGetShaderiv" ||
         name == "glGetString" || name == "glGetUniformLocation" ||
         name == "glIsEnabled" || name == "glCheckFramebufferStatus" ||
         name == "glGenBuffers" || name == "glGenTextures" ||
         name == "glGenFramebuffers" || name == "glGenRenderbuffers";
}
inline void PyraGLAfterMutation(std::string_view name) {
  // Deleting a bound object, or changing contexts, must not leave cached
  // names that could cause us to omit a required rebind later.
  if (name == "glDeleteTextures" || name == "glDeleteBuffers" ||
      name == "glDeleteFramebuffers" || name == "glDeleteProgram") {
    PyraGLResetBindingCache();
  }
}

// The renderer owns a single ordered CPU geometry queue. Non-redundant state
// mutations must flush it first. Passing the exact function name also gives
// the frame profiler a breakdown of actual flush causes.
inline void (*gPyraBatchFlushCallback)(const char *) = nullptr;
inline bool gPyraBatchFlushActive = false;
inline void PyraBatchBarrier(const char *reason = "explicit") {
  if (gPyraBatchFlushCallback && !gPyraBatchFlushActive)
    gPyraBatchFlushCallback(reason);
}
#endif

template <typename T> struct FnPtr;
template <typename Ret, typename... Args>
struct FnPtr<Ret GLFUNCCALL(Args...)> {
  explicit FnPtr(std::string_view name, bool optional = false);

  Ret operator()(Args... args) const {
#if defined(PYRA_GLES2)
    if constexpr (std::is_void_v<Ret>) {
      if (PyraSmartEnabled() && PyraGLSkipNoop(name_, args...)) return;
    }
    if (!PyraSmartEnabled() || !PyraGLReadOnly(name_)) PyraBatchBarrier(name_.data());
#endif
    if constexpr (std::is_same_v<Ret, void>) {
      fn_(args...);
#if defined(PYRA_GLES2)
      PyraGLAfterMutation(name_);
      PyraTraceGLError(name_);
#endif
      CheckError();
    } else {
      auto ret = fn_(args...);
#if defined(PYRA_GLES2)
      PyraGLAfterMutation(name_);
      PyraTraceGLError(name_);
#endif
      CheckError();
      return ret;
    }
  }

  operator bool() const { return fn_; }

private:
  Ret (GLFUNCCALL *fn_)(Args...);
  std::string_view name_;
};

#if defined(DECLARE_OPENGL)
extern char loadedLibrary[_MAX_PATH];
static module OpenGLDLLInst;


static std::vector<std::tuple<void **, std::string_view, bool>> inits_;
static void LoadGLFnPtrs() {
#if defined(PYRA_GLES2)
  if (!gPyraGLESHandle) {
    throw std::runtime_error("PYRA: PowerVR GLES2 library not loaded");
  }
#endif
  for (auto &[ptr, name, optional] : inits_) {
#if defined(PYRA_GLES2)
    // Do not accidentally resolve functions from GLVND's libGLESv2.so.2.
    // Native PowerVR exports all GLES2 core entry points directly.
    *ptr = dlsym(gPyraGLESHandle, name.data());
#else
    *ptr = SDL_GL_GetProcAddress(name.data());
#endif
    if (!*ptr && !optional) {
      throw std::runtime_error(std::string{"failed to find "} + name.data());
    }
  }
  inits_.clear();
#if defined(PYRA_GLES2)
  LOG_INFO << "PYRA: Native PowerVR GLES2 functions loaded";
#endif
}

template<typename Ret, typename... Args>
FnPtr<Ret GLFUNCCALL(Args...)>::FnPtr(std::string_view name, bool optional) : fn_{}, name_{name} {
  inits_.push_back(std::make_tuple(reinterpret_cast<void **>(&fn_), name, optional));
}

static module *LoadOpenGLDLL(const char *dllname) {
  LOG_INFO << "Loading OpenGL dll...";
#if defined(PYRA_GLES2)
  constexpr const char *pvrEGL =
      "/opt/omap5-sgx-ddk-um-linux/lib/libEGL.so.1";
  constexpr const char *pvrGLES =
      "/opt/omap5-sgx-ddk-um-linux/lib/libGLESv2.so.1";

  // Load EGL before GLES so native EGL symbols can be resolved by the
  // PowerVR driver even on systems with an additional GLVND installation.
  if (!gPyraEGLHandle) {
    gPyraEGLHandle = dlopen(pvrEGL, RTLD_NOW | RTLD_GLOBAL);
    if (!gPyraEGLHandle) {
      LOG_ERROR.printf("PYRA: Failed to load native EGL: %s", dlerror());
      return nullptr;
    }
  }
  LOG_INFO << "PYRA: Native PowerVR EGL library loaded";
#endif

  // SDL3 returns bool. SDL2's previous 'rc < 0' error check was incorrect.
  // For the X11/EGL backend, 'dllname' is the EGL library path.
  if (!SDL_GL_LoadLibrary(dllname && dllname[0] ? dllname : nullptr)) {
    LOG_ERROR.printf("OpenGL: SDL_GL_LoadLibrary failed: %s", SDL_GetError());
    return nullptr;
  }

#if defined(PYRA_GLES2)
  if (!gPyraGLESHandle) {
    gPyraGLESHandle = dlopen(pvrGLES, RTLD_NOW | RTLD_LOCAL);
    if (!gPyraGLESHandle) {
      LOG_ERROR.printf("PYRA: Failed to load native GLES2: %s", dlerror());
      SDL_GL_UnloadLibrary();
      return nullptr;
    }
  }
  LOG_INFO << "PYRA: Native PowerVR GLES2 library loaded";
#endif

  strcpy(loadedLibrary, dllname ? dllname : "");
  LOG_INFO << "OpenGL dll loading successful.";
  return &OpenGLDLLInst;
}

#define DYNAEXTERN_OPT(Name, Opt) FnPtr<decltype(Name)> const d##Name{#Name, Opt}
#define DYNAEXTERN(Name) DYNAEXTERN_OPT(Name, false)
#else
#define DYNAEXTERN(Name) extern FnPtr<decltype(Name)> const d##Name
#endif

DYNAEXTERN(glAttachShader);
DYNAEXTERN(glBindAttribLocation);
DYNAEXTERN(glBindBuffer);
DYNAEXTERN(glBindTexture);
#if !defined(PYRA_GLES2)
DYNAEXTERN(glBindVertexArray);
#endif
DYNAEXTERN(glBlendFunc);
DYNAEXTERN(glBufferData);
DYNAEXTERN(glBufferSubData);
DYNAEXTERN(glClear);
DYNAEXTERN(glClearColor);
DYNAEXTERN(glCompileShader);
DYNAEXTERN(glCreateProgram);
DYNAEXTERN(glCreateShader);
DYNAEXTERN(glDeleteBuffers);
DYNAEXTERN(glDeleteProgram);
DYNAEXTERN(glDeleteShader);
DYNAEXTERN(glDeleteTextures);
#if !defined(PYRA_GLES2)
DYNAEXTERN(glDeleteVertexArrays);
#endif
DYNAEXTERN(glDepthFunc);
DYNAEXTERN(glDepthMask);
DYNAEXTERN(glDisable);
DYNAEXTERN(glDisableVertexAttribArray);
DYNAEXTERN(glDrawArrays);
DYNAEXTERN(glEnable);
DYNAEXTERN(glEnableVertexAttribArray);
DYNAEXTERN(glFlush);
DYNAEXTERN(glFinish);
DYNAEXTERN(glGenBuffers);
DYNAEXTERN(glGenTextures);
#if !defined(PYRA_GLES2)
DYNAEXTERN(glGenVertexArrays);
#endif
DYNAEXTERN(glGetError);
DYNAEXTERN(glGetIntegerv);
DYNAEXTERN(glGetFloatv);
DYNAEXTERN(glGetVertexAttribiv);
DYNAEXTERN(glGetProgramInfoLog);
DYNAEXTERN(glGetProgramiv);
DYNAEXTERN(glGetShaderInfoLog);
DYNAEXTERN(glGetShaderiv);
DYNAEXTERN(glGetString);
DYNAEXTERN(glIsEnabled);
#if !defined(PYRA_GLES2)
DYNAEXTERN(glGetStringi);
#endif
DYNAEXTERN(glGetUniformLocation);
DYNAEXTERN(glLinkProgram);
#if !defined(PYRA_GLES2)
DYNAEXTERN(glMapBufferRange);
#endif
DYNAEXTERN(glPixelStorei);
DYNAEXTERN(glPolygonOffset);
DYNAEXTERN(glReadPixels);
DYNAEXTERN(glScissor);
DYNAEXTERN(glShaderSource);
DYNAEXTERN(glTexImage2D);
DYNAEXTERN(glTexParameteri);
DYNAEXTERN(glTexSubImage2D);
DYNAEXTERN(glUniform1f);
DYNAEXTERN(glUniform1i);
DYNAEXTERN(glUniform4f);
DYNAEXTERN(glUniformMatrix4fv);
#if !defined(PYRA_GLES2)
DYNAEXTERN(glUnmapBuffer);
#endif
DYNAEXTERN(glUseProgram);
DYNAEXTERN(glVertexAttribPointer);
DYNAEXTERN(glViewport);

// FBO entry points for render-to-texture ...
DYNAEXTERN(glBindFramebuffer);
DYNAEXTERN(glBindRenderbuffer);
#if !defined(PYRA_GLES2)
DYNAEXTERN(glBlitFramebuffer);
#endif
DYNAEXTERN(glCheckFramebufferStatus);
DYNAEXTERN(glDeleteFramebuffers);
DYNAEXTERN(glDeleteRenderbuffers);
DYNAEXTERN(glFramebufferRenderbuffer);
DYNAEXTERN(glFramebufferTexture2D);
DYNAEXTERN(glGenFramebuffers);
DYNAEXTERN(glGenRenderbuffers);
DYNAEXTERN(glRenderbufferStorage);

DYNAEXTERN(glActiveTexture);
#if !defined(PYRA_GLES2)
DYNAEXTERN(glMultiTexCoord4f);
#endif

#if defined(WIN32)
DYNAEXTERN(wglCreateContext);
DYNAEXTERN(wglDeleteContext);
DYNAEXTERN(wglGetProcAddress);
DYNAEXTERN(wglMakeCurrent);
#endif
