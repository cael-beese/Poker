# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
# Platform tests that need no display: the power-cut-safe save file.
if(TARGET bpl_persist)
  bpl_add_test(save_persistence test_save.c bpl_persist)
endif()
# The control panel layer: wiring, the bindings it builds, panel.ini.
if(TARGET bpl_platform)
  bpl_add_test(panel test_panel.c bpl_platform)
endif()
