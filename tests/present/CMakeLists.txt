# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
# Presentation smoke tests. The exception to "tests link only the pure
# libraries": these link render/ to drive its update functions, which make no
# GL calls, so they still need no display. malloc and friends are wrapped at
# link time so the test can check nothing is allocated per frame.
if(TARGET bpl_render)
  bpl_add_test(draw_present_smoke test_draw_present.c bpl_render)
  target_link_options(draw_present_smoke PRIVATE
    "LINKER:--wrap=malloc" "LINKER:--wrap=calloc" "LINKER:--wrap=realloc")
endif()
