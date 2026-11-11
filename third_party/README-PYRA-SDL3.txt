SDL3 3.2.6 is vendored in third_party/SDL and built from source as part of the
Pyra build. No system libSDL3.so is required.

Pyra configuration intentionally keeps Linux/X11, ALSA, joystick/gamepad,
haptic and OpenGL ES support. Desktop OpenGL, Vulkan, Wayland, KMSDRM,
PulseAudio, PipeWire, JACK, sndio, SDL tests/examples and SDL installation are
disabled by the top-level CMakeLists.txt when PYRA_GLES2=ON.

SDL license: see SDL/LICENSE.txt.
