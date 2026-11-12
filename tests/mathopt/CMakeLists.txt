# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# The MathOpt backend against an OR-Tools C++ prebuilt (DREAMOR_ORTOOLS_DIR), solving with HiGHS: its tests (one
# of them builds the example on Concert, on the stub, and compares the two models' constraints), its example, the
# OR-Tools samples ported to dreamor (examples/mathopt_samples/, lenient: Google's code as ported), and the
# MathOpt compile_fail cases. ortools::ortools brings OR-Tools' headers, its definitions and its libraries.
list(APPEND CMAKE_PREFIX_PATH ${DREAMOR_ORTOOLS_DIR})
find_package(ortools CONFIG REQUIRED PATHS ${DREAMOR_ORTOOLS_DIR} NO_DEFAULT_PATH)
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  # The Windows prebuilt's target links with /WHOLEARCHIVE:ortools, but its ortools.lib is a DLL's import library,
  # which has nothing to keep whole: lld-link (clang-cl's) opens the bare name as a file, and given the path, Visual
  # Studio 2022's (LLVM 19) fails with a .data section past 4 GiB. Linked as any import library, it serves both.
  set_property(TARGET ortools::ortools PROPERTY INTERFACE_LINK_OPTIONS "")
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  # cl.exe warns C4702 (unreachable code) in abseil's container_memory.h, which the prebuilt ships, as it generates the
  # code: the warning of an external header that /external does not reach.
  set_property(TARGET ortools::ortools APPEND PROPERTY INTERFACE_COMPILE_OPTIONS /wd4702)
endif()

file(GLOB sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
foreach(source IN LISTS sources)
  get_filename_component(name ${source} NAME_WE)
  dreamor_program(mathopt_${name} ${source} LIBRARIES ortools::ortools)
  dreamor_run(mathopt/${name} mathopt_${name} mathopt)
endforeach()
dreamor_program(example_mathopt ${examples}/production_planning_mathopt.cpp USER LIBRARIES ortools::ortools)
dreamor_run(mathopt/example example_mathopt mathopt)

# Each sample on its small instance: time_indexed_scheduling on its fixed test data, not a random one.
file(GLOB samples CONFIGURE_DEPENDS ${examples}/mathopt_samples/*.cc)
foreach(source IN LISTS samples)
  get_filename_component(name ${source} NAME_WE)
  set(args "")
  if(name STREQUAL "time_indexed_scheduling")
    set(args --use_test_data)
  endif()
  dreamor_program(sample_${name} ${source} LENIENT LIBRARIES ortools::ortools)
  dreamor_run(mathopt/sample/${name} sample_${name} mathopt ARGS ${args} ENVIRONMENT NO_COLOR=1)
endforeach()

dreamor_cases_config(mathopt config ${includes} -I${tests}/stub -DOR_PROTO_DLL= ${isystem} ${DREAMOR_ORTOOLS_DIR}/include)
dreamor_compile_fail(mathopt/compile_fail mathopt/compile_fail ${config})
