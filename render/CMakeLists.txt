# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
# render/CMakeLists.txt - bpl_render: atlas, procedural cards, text, bloom,
# particles, the effects / UI kit and the RENDERTEST mode.
#
# Added by the top-level CMakeLists.txt after platform/ and audio/, so
# bpl_platform (texreg, screen, settings; raylib) and bpl_audio exist.

add_library(bpl_render STATIC
  art.c
  atlas.c
  cards.c
  celebrate.c
  fx.c
  gfx.c
  jobs.c
  particles.c
  post.c
  raster.c
  render.c
  rendertest.c
  sprites.c
  text.c
  tween.c
  ui.c
)
target_link_libraries(bpl_render PUBLIC bpl_platform bpl_engine Threads::Threads m)
if(TARGET bpl_audio)
  target_link_libraries(bpl_render PRIVATE bpl_audio)
  # bpl_audio is only compiled against raylib.h and does not name the raylib
  # library; give it the link dependency so raylib comes after it.
  if(TARGET bpl_raylib)
    target_link_libraries(bpl_audio INTERFACE bpl_raylib)
  endif()
  target_compile_definitions(bpl_render PRIVATE BPL_HAVE_AUDIO=1)
endif()
target_compile_options(bpl_render PRIVATE -Wall -Wextra -Werror)
