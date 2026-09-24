# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
# platform/CMakeLists.txt - bpl_platform (library) and beese-poker (executable).
#
# Included by the top-level CMakeLists.txt with add_subdirectory(platform),
# after it has defined bpl_engine. Selects the raylib build that
# tools/fetch_raylib.sh produced for BPL_PLATFORM:
#   DESKTOP          -> third_party/raylib/desktop    (GLFW, OpenGL 3.3)
#   DRM              -> third_party/raylib/drm        (KMS/DRM, OpenGL ES 2.0)
#   DRM + BPL_GLES3  -> third_party/raylib/drm-gles3  (KMS/DRM, OpenGL ES 3.0)

get_filename_component(BPL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

if(NOT DEFINED BPL_PLATFORM)
  set(BPL_PLATFORM "DESKTOP")
endif()
option(BPL_GLES3 "DRM only: link the raylib build made for OpenGL ES 3.0" OFF)

if(BPL_PLATFORM STREQUAL "DRM")
  if(BPL_GLES3)
    set(BPL_RAYLIB_VARIANT "drm-gles3")
  else()
    set(BPL_RAYLIB_VARIANT "drm")
  endif()
elseif(BPL_PLATFORM STREQUAL "DESKTOP")
  set(BPL_RAYLIB_VARIANT "desktop")
else()
  message(FATAL_ERROR "BPL_PLATFORM must be DRM or DESKTOP, not '${BPL_PLATFORM}'")
endif()

set(BPL_RAYLIB_DIR "${BPL_ROOT}/third_party/raylib/${BPL_RAYLIB_VARIANT}")
if(NOT EXISTS "${BPL_RAYLIB_DIR}/lib/libraylib.a")
  message(FATAL_ERROR "raylib '${BPL_RAYLIB_VARIANT}' not built: run tools/fetch_raylib.sh ${BPL_RAYLIB_VARIANT}")
endif()

add_library(bpl_raylib STATIC IMPORTED GLOBAL)
set_target_properties(bpl_raylib PROPERTIES
  IMPORTED_LOCATION "${BPL_RAYLIB_DIR}/lib/libraylib.a"
  INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${BPL_RAYLIB_DIR}/include"
  INTERFACE_INCLUDE_DIRECTORIES "${BPL_RAYLIB_DIR}/include")

find_package(Threads REQUIRED)
if(BPL_PLATFORM STREQUAL "DRM")
  find_library(BPL_GLES GLESv2 REQUIRED)
  find_library(BPL_EGL EGL REQUIRED)
  find_library(BPL_GBM gbm REQUIRED)
  find_library(BPL_DRM drm REQUIRED)
  find_path(BPL_DRM_INCLUDE xf86drmMode.h REQUIRED)
  find_path(BPL_LIBDRM_INCLUDE drm.h PATH_SUFFIXES libdrm REQUIRED)
  set_property(TARGET bpl_raylib APPEND PROPERTY INTERFACE_LINK_LIBRARIES
    ${BPL_GLES} ${BPL_EGL} ${BPL_GBM} ${BPL_DRM} Threads::Threads m dl rt atomic)
else()
  find_package(OpenGL REQUIRED)
  find_package(X11 REQUIRED)
  set_property(TARGET bpl_raylib APPEND PROPERTY INTERFACE_LINK_LIBRARIES
    OpenGL::GL ${X11_LIBRARIES} Threads::Threads m dl rt)
endif()

add_library(bpl_platform STATIC
  app.c
  modes_placeholder.c
  mode_attract.c
  mode_menu.c
  mode_draw.c
  mode_holdem.c
  service.c
  selftest.c
  session.c
  placeholder_ui.c
  placeholder_draw.c
  placeholder_holdem.c
  fx_settings.c
  gputest.c
  ini.c
  input.c
  joy_evdev.c
  screen.c
  texreg.c
  gpustats.c
  perf.c
  overlay.c
  options.c
  display_probe.c
)
# Credits, stats and settings on disk: pure C, no raylib, so tests/platform
# can link it on its own.
add_library(bpl_persist STATIC save.c settings.c)
target_include_directories(bpl_persist PUBLIC "${BPL_ROOT}")
target_link_libraries(bpl_persist PUBLIC Threads::Threads)
target_compile_options(bpl_persist PRIVATE -Wall -Wextra -Werror)

target_include_directories(bpl_platform PUBLIC "${BPL_ROOT}")
target_link_libraries(bpl_platform PUBLIC bpl_engine bpl_raylib)
# The real game modes (systems milestone): the games, the AI, audio, the save file.
# bpl_audio calls raylib but does not link it, so the raylib archive has to come
# after it again; named by path, as CMake would drop a repeat of the target.
target_link_libraries(bpl_platform PUBLIC bpl_persist bpl_draw bpl_holdem bpl_ai bpl_audio
  "${BPL_RAYLIB_DIR}/lib/libraylib.a")
target_compile_options(bpl_platform PRIVATE -Wall -Wextra -Werror)
# _GNU_SOURCE for clock_gettime / getrandom / ioctl helpers under -std=c11.
target_compile_definitions(bpl_platform PUBLIC _GNU_SOURCE)

if(BPL_PLATFORM STREQUAL "DRM")
  target_compile_definitions(bpl_platform PUBLIC BPL_PLATFORM_DRM=1)
  if(BPL_GLES3)
    target_compile_definitions(bpl_platform PUBLIC BPL_GLES3=1)
  endif()
  target_include_directories(bpl_platform PRIVATE "${BPL_DRM_INCLUDE}" "${BPL_LIBDRM_INCLUDE}")
  # Draw calls are counted by wrapping the GLES entry points raylib calls
  # directly (see gpustats.c). --wrap has to be on the final link.
  # --undefined makes the linker take gpustats.o out of the archive even in a
  # program that never calls into it (the presentation smoke tests link
  # raylib, whose draw calls are wrapped, but nothing else from gpustats.c),
  # or the wrapped calls would have no __wrap_ definition to land on.
  target_link_options(bpl_platform INTERFACE
    "LINKER:--wrap=glDrawElements" "LINKER:--wrap=glDrawArrays"
    "LINKER:--undefined=__wrap_glDrawElements" "LINKER:--undefined=__wrap_glDrawArrays")
else()
  target_compile_definitions(bpl_platform PUBLIC BPL_PLATFORM_DESKTOP=1)
endif()

# app_modes.c (the mode table) belongs to the executable: it names modes from
# libraries that depend on bpl_platform, which a static bpl_platform could
# not reference without a link cycle.
add_executable(beese-poker main.c app_modes.c)
target_link_libraries(beese-poker PRIVATE bpl_platform)
# The renderer (render/, added after this directory) brings the RENDERTEST mode.
if(EXISTS "${BPL_ROOT}/render/CMakeLists.txt")
  target_link_libraries(beese-poker PRIVATE bpl_render)
  target_compile_definitions(beese-poker PRIVATE BPL_HAVE_RENDER=1)
endif()
target_compile_options(beese-poker PRIVATE -Wall -Wextra -Werror)

# The binary looks for config.ini next to itself first, so keep a copy there.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/config.ini" "${CMAKE_CURRENT_BINARY_DIR}/config.ini" COPYONLY)
