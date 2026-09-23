# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
# Hold'em rules tests. They link bpl_holdem (and so bpl_engine) only.
bpl_add_test(holdem_pots      test_holdem_pots.c      bpl_holdem)
bpl_add_test(holdem_betting   test_holdem_betting.c   bpl_holdem)
bpl_add_test(holdem_showdown  test_holdem_showdown.c  bpl_holdem)
bpl_add_test(holdem_sng       test_holdem_sng.c       bpl_holdem)
bpl_add_test(holdem_integrity test_holdem_integrity.c bpl_holdem)

# The include-direction check reads the source tree.
target_compile_definitions(holdem_integrity PRIVATE BPL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
