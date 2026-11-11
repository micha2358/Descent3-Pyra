cmake_path(SET GENERATED_HEADERS NORMALIZE ${CMAKE_CURRENT_BINARY_DIR}/generated)
cmake_path(SET SHADERS_H NORMALIZE ${GENERATED_HEADERS}/shaders.h)
set(SHADERS
  shaders/fragment.glsl
  shaders/vertex.glsl
)
add_custom_command(
  OUTPUT ${SHADERS_H}
  COMMAND ${CMAKE_COMMAND} -Dshaders_h=${SHADERS_H} -P ../cmake/shaders.cmake -- ${SHADERS}
  DEPENDS ../cmake/shaders.cmake ${SHADERS}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

set(HEADERS
  dyna_gl.h
  PyraRenderScale.h
  PyraUIResolution.h
  HardwareInternal.h
  ShaderProgram.h
  ${SHADERS_H}
)
set(CPPS
  HardwareClipper.cpp
  HardwareDraw.cpp
  HardwareGlobalVars.cpp
  HardwareInstance.cpp
  HardwareOpenGL.cpp
  HardwareBaseGPU.cpp
  HardwarePoints.cpp
  HardwareSetup.cpp
  HardwareTransforms.cpp
)

add_library(renderer STATIC ${HEADERS} ${CPPS})
target_include_directories(renderer PRIVATE ${GENERATED_HEADERS} ${SDL3_INCLUDE_DIRS})
target_link_libraries(renderer PRIVATE
  glm::glm
  SDL3::SDL3
  bitmap
  ddebug
  ddio
  logger
  mem
  misc
  module
  rtperformance
)

if(PYRA_GLES2)
  find_library(PYRA_GLES2_LIBRARY NAMES GLESv2 PATHS /opt/omap5-sgx-ddk-um-linux/lib NO_DEFAULT_PATH REQUIRED)
  find_library(PYRA_EGL_LIBRARY NAMES EGL PATHS /opt/omap5-sgx-ddk-um-linux/lib NO_DEFAULT_PATH REQUIRED)
  # dlsym/dlopen are used to avoid GLVND's libGLESv2.so.2 dispatch.
  target_link_libraries(renderer PRIVATE ${PYRA_GLES2_LIBRARY} ${PYRA_EGL_LIBRARY} ${CMAKE_DL_LIBS})
endif()
