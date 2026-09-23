# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
# Presentation smoke tests. Unlike the rules tests these link the renderer
# (and so raylib), but they open no window and touch no GL: only the
# update side of a presentation runs, which must not need a display.
if(TARGET bpl_render)
  bpl_add_test(holdem_present test_holdem_present.c bpl_render bpl_holdem)
  # Count allocations made while the presentation updates.
  target_link_options(holdem_present PRIVATE
    "LINKER:--wrap=malloc" "LINKER:--wrap=calloc" "LINKER:--wrap=realloc")
endif()
