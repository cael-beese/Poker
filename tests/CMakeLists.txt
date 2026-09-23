# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 - see LICENSE.md
# Tests link only the pure libraries (engine, draw, holdem, ai) and need no
# display. bpl_add_test(name source libs...) registers one with ctest.
function(bpl_add_test name src)
  add_executable(${name} ${src})
  target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/tests)
  target_link_libraries(${name} PRIVATE ${ARGN} m)
  add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endfunction()

bpl_add_test(eval_exhaustive      test_eval_exhaustive.c    bpl_engine)
bpl_add_test(eval7_vs_bruteforce  test_eval7_bruteforce.c   bpl_engine)
bpl_add_test(eval7_exhaustive     test_eval7_exhaustive.c   bpl_engine)
bpl_add_test(shuffle_uniformity   test_shuffle_uniformity.c bpl_engine)
bpl_add_test(rng_replay_determinism test_rng_replay.c       bpl_engine)
bpl_add_test(engine_basics        test_engine_basics.c      bpl_engine Threads::Threads)

# Other modules add their tests in their own tests/<module>/CMakeLists.txt,
# using bpl_add_test, without editing this file.
foreach(sub draw holdem ai)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${sub}/CMakeLists.txt)
    add_subdirectory(${sub})
  endif()
endforeach()
