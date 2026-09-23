# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
cmake_minimum_required(VERSION 3.18)
project(BeesePokerLounge C)

# The whole tree is portable C11 plus POSIX, for aarch64 (the Pi) and x86-64
# (development under WSL). Extensions are off so nothing GNU-only sneaks in;
# _DEFAULT_SOURCE exposes the POSIX 2008 and BSD interfaces the code relies on
# (clock_gettime, fsync, getrandom's header) without per-file feature macros.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
add_compile_definitions(_DEFAULT_SOURCE)

set(BPL_PLATFORM "DESKTOP" CACHE STRING "Target platform: DRM (the Pi, KMS/DRM, GLES) or DESKTOP (development)")
set_property(CACHE BPL_PLATFORM PROPERTY STRINGS DRM DESKTOP)
if(NOT BPL_PLATFORM MATCHES "^(DRM|DESKTOP)$")
  message(FATAL_ERROR "BPL_PLATFORM must be DRM or DESKTOP, not '${BPL_PLATFORM}'")
endif()

# A plain `cmake -B build` gives an optimised build, because the tests (10M
# seven-card hands, 1M shuffles) and the benchmarks are meaningless at -O0.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "Release flags" FORCE)

# The Pi build turns this on to get -O3 tuned for the Cortex-A72 it runs on.
option(BPL_NATIVE "Tune for the build machine (-O3 and -mcpu/-march=native)" OFF)
if(BPL_NATIVE)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|arm")
    add_compile_options(-O3 -mcpu=native)
  else()
    add_compile_options(-O3 -march=native)
  endif()
endif()

# No warnings, ever, on our code. raylib is built separately by
# tools/fetch_raylib.sh, so these global flags never reach it; a module that
# ever pulls third-party source in with add_subdirectory must strip them there.
add_compile_options(-Wall -Wextra -Werror)

find_package(Threads REQUIRED)
enable_testing()

add_subdirectory(engine)

# Other modules are owned by other people; each only has to drop its own
# CMakeLists.txt in place and it is picked up here without editing this file.
foreach(dir platform games/draw games/holdem ai audio render)
  if(EXISTS ${CMAKE_SOURCE_DIR}/${dir}/CMakeLists.txt)
    add_subdirectory(${dir})
  endif()
endforeach()

add_subdirectory(tests)

# Evaluator benchmark, so eval speed can be measured on the Pi itself.
add_executable(bench_eval tools/bench_eval.c)
target_link_libraries(bench_eval PRIVATE bpl_engine)

if(EXISTS ${CMAKE_SOURCE_DIR}/tools/CMakeLists.txt)
  add_subdirectory(tools)
endif()
