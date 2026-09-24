# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
# Presentation smoke tests. The exception to "tests link only the pure
# libraries": these link render/ (and so raylib) to drive its update
# functions, which make no GL calls, so they open no window and need no
# display. malloc and friends are wrapped at link time so each test can check
# that nothing is allocated per frame.
if(TARGET bpl_render)
  bpl_add_test(draw_present_smoke test_draw_present.c bpl_render)
  target_link_options(draw_present_smoke PRIVATE
    "LINKER:--wrap=malloc" "LINKER:--wrap=calloc" "LINKER:--wrap=realloc")
  bpl_add_test(holdem_present test_holdem_present.c bpl_render bpl_holdem)
  target_link_options(holdem_present PRIVATE
    "LINKER:--wrap=malloc" "LINKER:--wrap=calloc" "LINKER:--wrap=realloc")
endif()
