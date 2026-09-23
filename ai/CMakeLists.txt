# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
# bpl_ai: the Hold'em opponents. Pure C11 + pthreads; depends only on
# bpl_engine and ai/ai_view.h (never on the Hold'em module).
file(GLOB BPL_AI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
add_library(bpl_ai STATIC ${BPL_AI_SOURCES})
target_include_directories(bpl_ai PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(bpl_ai PUBLIC bpl_engine Threads::Threads m)

# The AI's tools live in tools/ but are built from here, so the AI module
# does not have to edit a shared CMakeLists.txt.
add_executable(ai_gen_preflop ${CMAKE_SOURCE_DIR}/tools/ai_gen_preflop.c)
target_link_libraries(ai_gen_preflop PRIVATE bpl_ai)

if(EXISTS ${CMAKE_SOURCE_DIR}/tools/ai_selfplay.c)
  add_executable(ai_selfplay ${CMAKE_SOURCE_DIR}/tools/ai_selfplay.c)
  target_link_libraries(ai_selfplay PRIVATE bpl_ai)
endif()
