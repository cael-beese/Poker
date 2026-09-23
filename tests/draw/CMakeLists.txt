# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
# Draw Poker tests. Each runs in well under a minute on x86 by default; the
# environment variables named in each file select the full-length runs.
set(BPL_DRAW_TESTS
  draw_classify_exhaustive test_draw_classify.c
  draw_return              test_draw_return.c
  draw_sim_return          test_draw_sim.c
  draw_hint                test_draw_hint.c
  draw_game                test_draw_game.c)
while(BPL_DRAW_TESTS)
  list(POP_FRONT BPL_DRAW_TESTS name src)
  bpl_add_test(${name} ${src} bpl_draw Threads::Threads)
  # The strategy tables are read from the source tree's assets/.
  target_compile_definitions(${name} PRIVATE BPL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endwhile()
