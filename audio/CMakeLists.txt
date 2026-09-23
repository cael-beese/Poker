# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
#
# bpl_audio - sound effects, music and ducking on raylib's audio.
#
# bpl_audio only needs raylib's header to compile; the executable links the
# raylib library. raylib is found, in order, as:
#   1. a `raylib` target (make it IMPORTED GLOBAL if it is defined in another
#      directory, or it is not visible here),
#   2. RAYLIB_INCLUDE_DIR (a directory holding raylib.h),
#   3. the tree tools/fetch_raylib.sh builds: third_party/raylib/<drm|desktop>,
#   4. find_package(raylib).
# If none is found the tree still configures: bpl_audio is then built from
# audio_stub.c, the same API with no sound.
cmake_minimum_required(VERSION 3.18)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  project(bpl_audio C)
  set(CMAKE_C_STANDARD 11)
  set(CMAKE_C_STANDARD_REQUIRED ON)
  set(CMAKE_C_EXTENSIONS OFF)
  add_compile_definitions(_DEFAULT_SOURCE)
  set(_bpl_root ${CMAKE_CURRENT_SOURCE_DIR}/..)
else()
  set(_bpl_root ${CMAKE_SOURCE_DIR})
endif()

set(_bpl_audio_raylib "")
if(TARGET raylib)
  set(_bpl_audio_raylib target)
elseif(RAYLIB_INCLUDE_DIR AND EXISTS "${RAYLIB_INCLUDE_DIR}/raylib.h")
  set(_bpl_audio_raylib include)
else()
  if(BPL_PLATFORM STREQUAL "DRM")
    set(_bpl_variant drm)
  else()
    set(_bpl_variant desktop)
  endif()
  if(EXISTS "${_bpl_root}/third_party/raylib/${_bpl_variant}/include/raylib.h")
    set(RAYLIB_INCLUDE_DIR "${_bpl_root}/third_party/raylib/${_bpl_variant}/include")
    set(_bpl_audio_raylib include)
  else()
    find_package(raylib QUIET)
    if(TARGET raylib)
      set(_bpl_audio_raylib target)
    endif()
  endif()
endif()

if(_bpl_audio_raylib)
  add_library(bpl_audio STATIC audio.c)
  if(_bpl_audio_raylib STREQUAL "target")
    target_link_libraries(bpl_audio PUBLIC raylib)
  else()
    target_include_directories(bpl_audio PRIVATE "${RAYLIB_INCLUDE_DIR}")
  endif()
  message(STATUS "bpl_audio: raylib (${_bpl_audio_raylib} ${RAYLIB_INCLUDE_DIR})")
else()
  add_library(bpl_audio STATIC audio_stub.c)
  message(STATUS "bpl_audio: raylib not found - building the silent stub")
endif()
target_include_directories(bpl_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(bpl_audio PRIVATE -Wall -Wextra -Werror)
target_link_libraries(bpl_audio PUBLIC m)

# A manual check on a machine with raylib and (any) audio device, not a ctest:
# loads every asset, measures memory and load time, and exercises voice
# pooling, streaming and ducking through raylib itself. It needs the raylib
# library to link, from the `raylib` target or RAYLIB_LIBRARY.
option(BPL_AUDIO_SELFTEST "Build audio_selftest" OFF)
if(BPL_AUDIO_SELFTEST AND _bpl_audio_raylib)
  add_executable(audio_selftest audio_selftest.c)
  target_compile_options(audio_selftest PRIVATE -Wall -Wextra -Werror)
  target_link_libraries(audio_selftest PRIVATE bpl_audio)
  if(NOT _bpl_audio_raylib STREQUAL "target")
    if(NOT RAYLIB_LIBRARY)
      get_filename_component(_rl_prefix "${RAYLIB_INCLUDE_DIR}" DIRECTORY)
      set(RAYLIB_LIBRARY "${_rl_prefix}/lib/libraylib.a")
    endif()
    target_include_directories(audio_selftest PRIVATE "${RAYLIB_INCLUDE_DIR}")
    if(BPL_PLATFORM STREQUAL "DRM")
      set(_rl_sys EGL GLESv2 drm gbm)
    else()
      set(_rl_sys GL X11)
    endif()
    target_link_libraries(audio_selftest PRIVATE "${RAYLIB_LIBRARY}" ${_rl_sys} pthread dl rt m)
  endif()
endif()
