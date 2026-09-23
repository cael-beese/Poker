# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
# bpl_draw: video poker rules, paytables, the exact-EV strategy hint, the
# strategy tables and the Draw Poker state machine. Pure C11 + pthreads.
file(GLOB BPL_DRAW_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
add_library(bpl_draw STATIC ${BPL_DRAW_SOURCES})
target_link_libraries(bpl_draw PUBLIC bpl_engine Threads::Threads)

# This module's offline tools live in tools/ (CONTRACT section 1) but are
# built from here, so the module stays self-contained and the shared
# tools/CMakeLists.txt is not needed.
add_executable(draw_strategy_gen ${CMAKE_SOURCE_DIR}/tools/draw_strategy_gen.c)
target_link_libraries(draw_strategy_gen PRIVATE bpl_draw)
add_executable(draw_cli ${CMAKE_SOURCE_DIR}/tools/draw_cli.c)
target_link_libraries(draw_cli PRIVATE bpl_draw)
