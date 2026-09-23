# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
# AI tests: integrity (source scan + sentinel views), determinism (direct and
# through the worker-thread hooks), equity accuracy against exact
# enumeration, legality over random views, and timing.
bpl_add_test(ai_source_scan     test_ai_source_scan.c bpl_ai)
target_compile_definitions(ai_source_scan PRIVATE BPL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
bpl_add_test(ai_integrity       test_ai_integrity.c   bpl_ai)
bpl_add_test(ai_determinism     test_ai_determinism.c bpl_ai Threads::Threads)
bpl_add_test(ai_equity_accuracy test_ai_equity.c      bpl_ai)
bpl_add_test(ai_legality        test_ai_legality.c    bpl_ai)
bpl_add_test(ai_timing          test_ai_timing.c      bpl_ai)
# Timing numbers mean nothing with other tests competing for the cores.
set_tests_properties(ai_timing PROPERTIES RUN_SERIAL TRUE)
