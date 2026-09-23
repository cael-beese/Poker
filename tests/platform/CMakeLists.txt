# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
# Platform tests that need no display: the power-cut-safe save file.
if(TARGET bpl_persist)
  bpl_add_test(save_persistence test_save.c bpl_persist)
endif()
