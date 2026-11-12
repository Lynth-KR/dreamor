# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Real CPLEX Studio (DREAMOR_CPLEX_DIR), which CI does not have: the tests that use only public Concert API
# (unit/test_index_set and cplex/*.cpp, which solve), the Concert example built and run, and the compile_fail cases
# against the real headers. Against the stub, cplex/*.cpp are syntax checks (tests/CMakeLists.txt). IL_STD selects
# Concert's std:: API; CPLEX's headers are not ours to warn about.
set(cplex_arch x86-64_linux)
add_library(dreamor_cplex INTERFACE)
target_include_directories(dreamor_cplex SYSTEM INTERFACE ${DREAMOR_CPLEX_DIR}/concert/include
                                                          ${DREAMOR_CPLEX_DIR}/cplex/include)
target_compile_definitions(dreamor_cplex INTERFACE IL_STD)
target_link_directories(dreamor_cplex INTERFACE ${DREAMOR_CPLEX_DIR}/cplex/lib/${cplex_arch}/static_pic
                        ${DREAMOR_CPLEX_DIR}/concert/lib/${cplex_arch}/static_pic)
target_link_libraries(dreamor_cplex INTERFACE ilocplex concert cplex m pthread dl)

file(GLOB sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
foreach(source IN LISTS sources ITEMS ${tests}/unit/test_index_set.cpp)
  get_filename_component(name ${source} NAME_WE)
  dreamor_program(cplex_${name} ${source} NO_STUB LIBRARIES dreamor_cplex)
  dreamor_run(cplex/${name} cplex_${name} cplex)
endforeach()
dreamor_program(example_concert ${examples}/production_planning_concert.cpp USER NO_STUB LIBRARIES dreamor_cplex)
dreamor_run(cplex/example example_concert cplex)

dreamor_cases_config(cplex config ${includes} -DIL_STD ${isystem} ${DREAMOR_CPLEX_DIR}/concert/include
                     ${isystem} ${DREAMOR_CPLEX_DIR}/cplex/include)
dreamor_compile_fail(cplex/compile_fail compile_fail ${config})
