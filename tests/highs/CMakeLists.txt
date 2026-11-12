# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# The HiGHS backend against a HiGHS install (DREAMOR_HIGHS_DIR; an OR-Tools prebuilt is one): its tests (solving,
# the README's program taken out of it, and the example built on Concert, on the stub,
# beside HiGHS, the two models' constraints compared), the guide's code that solves (tests/docs/highs/), its example, and the HiGHS
# compile_fail and contract_fail cases.
list(APPEND CMAKE_PREFIX_PATH ${DREAMOR_HIGHS_DIR})
# CMAKE_FIND_ROOT_PATH_BOTH: a cross-compiling toolchain (Emscripten) looks in its sysroot alone, and the
# directory given is outside it.
find_package(highs CONFIG REQUIRED PATHS ${DREAMOR_HIGHS_DIR} NO_DEFAULT_PATH CMAKE_FIND_ROOT_PATH_BOTH)

file(GLOB sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
foreach(source IN LISTS sources)
  get_filename_component(name ${source} NAME_WE)
  dreamor_program(highs_${name} ${source} LIBRARIES highs::highs)
  dreamor_run(highs/${name} highs_${name} highs)
endforeach()
# The guide's code that solves (tests/docs/highs/), a test a file, built as a user's.
file(GLOB guide_sources CONFIGURE_DEPENDS ${tests}/docs/highs/*.cpp)
foreach(source IN LISTS guide_sources)
  get_filename_component(name ${source} NAME_WE)
  dreamor_program(highs_guide_${name} ${source} USER LIBRARIES highs::highs)
  dreamor_run(highs/guide/${name} highs_guide_${name} highs)
endforeach()
# The README's program, taken out of README.md as written there (tests/docs/readme.py), built as a user's, and run:
# it prints the lines the README shows, in that order. A README that changes takes it out again. The program is
# an argument here, so the emulator (Node.js for wasm32) is put before it by hand.
set(readme_source ${CMAKE_CURRENT_BINARY_DIR}/readme_program.cpp)
add_custom_command(
  OUTPUT ${readme_source}
  COMMAND ${Python3_EXECUTABLE} ${tests}/docs/readme.py program ${PROJECT_SOURCE_DIR}/README.md ${readme_source}
  DEPENDS ${PROJECT_SOURCE_DIR}/README.md ${tests}/docs/readme.py
  VERBATIM)
dreamor_program(highs_readme ${readme_source} USER LIBRARIES highs::highs)
add_test(NAME highs/readme COMMAND ${Python3_EXECUTABLE} ${tests}/docs/readme.py output ${PROJECT_SOURCE_DIR}/README.md
                                   ${CMAKE_CROSSCOMPILING_EMULATOR} ${run_with} $<TARGET_FILE:highs_readme>)
set_tests_properties(highs/readme PROPERTIES LABELS highs)
if(environment)
  set_tests_properties(highs/readme PROPERTIES ENVIRONMENT "${environment}")
endif()
dreamor_program(example_highs ${examples}/production_planning_highs.cpp USER LIBRARIES highs::highs)
dreamor_run(highs/example example_highs highs)

dreamor_cases_config(highs config ${includes} -I${tests}/stub ${isystem} ${DREAMOR_HIGHS_DIR}/include/highs)
dreamor_compile_fail(highs/compile_fail highs/compile_fail ${config})
dreamor_contract_fail(highs/contract_fail LIBRARIES highs::highs)
