# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
# bpl_engine: cards, RNG, deck, evaluator, events, replay, fixed step.
# Pure C11 + POSIX, no raylib, so tests and every game module can link it.
file(GLOB BPL_ENGINE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
add_library(bpl_engine STATIC ${BPL_ENGINE_SOURCES})

# Headers are included by path from the source root: #include "engine/eval.h".
target_include_directories(bpl_engine PUBLIC ${CMAKE_SOURCE_DIR})

# eval_init() uses pthread_once so concurrent first calls are safe.
target_link_libraries(bpl_engine PUBLIC Threads::Threads)
