# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
# PLACEHOLDER top-level build, written on the platform branch so the platform
# can be built before the engine branch lands. The engine agent owns the real
# top-level CMakeLists.txt; the lead keeps theirs at merge and only has to make
# sure it does add_subdirectory(platform) after defining bpl_engine.
cmake_minimum_required(VERSION 3.18)
project(BeesePokerLounge C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(BPL_PLATFORM "DESKTOP" CACHE STRING "Target platform: DRM (the Pi) or DESKTOP")
set_property(CACHE BPL_PLATFORM PROPERTY STRINGS DRM DESKTOP)

# Our own code is warning-free; raylib is prebuilt and not affected.
set(BPL_WARNINGS -Wall -Wextra -Werror)

# Placeholder engine library: only what the platform needs (event queue).
add_library(bpl_engine STATIC engine/event.c)
target_include_directories(bpl_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(bpl_engine PRIVATE ${BPL_WARNINGS})

add_subdirectory(platform)
