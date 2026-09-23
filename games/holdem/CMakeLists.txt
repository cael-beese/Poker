# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
# bpl_holdem: the No-Limit Hold'em table, betting rules, pots and showdown.
# Pure C11 on top of bpl_engine; from ai/ it uses only the header ai/ai_view.h
# and never links the AI library (the app wires the AI in through hooks).
file(GLOB BPL_HOLDEM_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
add_library(bpl_holdem STATIC ${BPL_HOLDEM_SOURCES})
target_include_directories(bpl_holdem PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(bpl_holdem PUBLIC bpl_engine)
