# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# dreamor's tests: what CMakePresets.json's presets build and CTest runs. Against the Concert stub (stub/), and
# with the solvers whose directories are given - each backend's own in its folder (mathopt/, highs/, cplex/):
#
#   DREAMOR_ORTOOLS_DIR  an OR-Tools C++ prebuilt: the MathOpt backend's tests, its example and the samples
#   DREAMOR_HIGHS_DIR    a HiGHS install: the HiGHS backend's (an OR-Tools prebuilt is one)
#   DREAMOR_CPLEX_DIR    CPLEX Studio: the tests that solve with CPLEX, and compile_fail on its real headers
#
#   DREAMOR_TESTS      the kinds to build and run, a list (default: all of them):
#                        unit           one part each (unit/)
#                        stress         the set layer against a reference: one chain over many data, the engine's
#                                       situations over shapes; lookups against hints (stress/)
#                        docs           the code of the guide and the README, as written there (docs/)
#                        examples       the example's model against the same model written by hand (examples/)
#                        syntax         cplex/*.cpp and the Concert example, checked against the stub
#                        compile_fail   code that must not compile, each case its own test
#                        contract_fail  code that breaks a contract, for a debug build to stop
#                      syntax and compile_fail depend on the compiler alone; docs runs too, but what it runs is
#                      covered by unit and stress, so a configuration that repeats a compiler leaves the three out.
#   DREAMOR_SANITIZE   address (AddressSanitizer, UndefinedBehaviorSanitizer, LeakSanitizer) or thread
#
# Every test builds with the strict warnings: a header that warns here would warn in a user's strict build. The
# compile_fail cases and the ported samples are lenient (-Wall -Wextra): a warning must not become a case's first
# error, and the samples are Google's code. The code users read - the docs' code and examples/ - builds as a user
# would build it, with -Wall -Wextra and no warning (-Werror); the library's tests that include examples/ take it
# as a system header, so the strict warnings stay on dreamor's headers and the tests themselves.

set(DREAMOR_TESTS "unit;stress;docs;examples;syntax;compile_fail;contract_fail" CACHE STRING "The kinds of test to build")
set(DREAMOR_SANITIZE "" CACHE STRING "address or thread, or empty")
set(DREAMOR_ORTOOLS_DIR "" CACHE PATH "An OR-Tools C++ prebuilt, for the MathOpt backend's tests")
set(DREAMOR_HIGHS_DIR "" CACHE PATH "A HiGHS install, for the HiGHS backend's tests")
set(DREAMOR_CPLEX_DIR "" CACHE PATH "CPLEX Studio, for the tests that solve with CPLEX")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(tests ${CMAKE_CURRENT_SOURCE_DIR})
set(examples ${PROJECT_SOURCE_DIR}/examples)
set(clang_like FALSE)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(clang_like TRUE)
endif()
set(wasm FALSE)
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  set(wasm TRUE)
endif()

if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  # cl.exe and clang-cl take MSVC's options. /W4 is the strict level a Windows user builds at: /Wall, like clang-cl's
  # -Wall, is every warning the compiler has. cl.exe also needs /permissive- for the standard's rules.
  set(strict /W4 /WX)
  if(NOT clang_like)
    list(APPEND strict /permissive-)
  endif()
  set(lenient /W4)
  set(user ${strict})
  # tests/cases.py's options for a case: the standard, and a system header, as MSVC's front ends take them.
  set(cases_std /std:c++20)
  set(isystem /external:I)
  set(cases_msvc true)
else()
  set(strict -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Wundef
      -Wnull-dereference -Wdouble-promotion -Wformat-security -Wmissing-declarations -Wfloat-equal -Wcast-align
      -Wnon-virtual-dtor -Werror)
  if(clang_like)
    list(APPEND strict -Wmissing-prototypes)  # C only in GCC
  endif()
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    list(APPEND strict -Wuseless-cast)  # GCC's alone: a cast to the type a value already has, which a user's GCC build may warn of
  endif()
  set(lenient -Wall -Wextra)
  set(user -Wall -Wextra -Wfloat-equal -Werror)  # -Wfloat-equal: the docs' checks compare doubles as tests do (testing.md)
  set(cases_std -std=c++20)
  set(isystem -isystem)
  set(cases_msvc false)
endif()
# What a case compiles with besides the warnings, for tests/cases.py: the headers (examples/ as a system header,
# as the programs take it), and the stub where a folder builds on it.
set(includes -I${PROJECT_SOURCE_DIR}/include ${isystem} ${examples} -I${tests})
if(cases_msvc)
  list(APPEND includes /external:W0)  # cl.exe warns in a system header unless told its level there
endif()
set(common "")
set(link "")
if(wasm)
  # Emscripten's -Wall turns on its own link warnings too, and one says that growing a memory threads share is
  # slow - which the tests' speed under WASM is not about.
  list(APPEND strict -Wno-pthreads-mem-growth)
  list(APPEND lenient -Wno-pthreads-mem-growth)
  list(APPEND user -Wno-pthreads-mem-growth)
  # The tests throw and catch (-fwasm-exceptions); some run 8 threads, Web Workers on one shared memory,
  # started before main (a pool), since a worker cannot start while main waits for it. NODERAWFS: the files a
  # test writes are the machine's. A stack of 8 MB, workers' of 2: HiGHS's simplex and MIP overflow
  # Emscripten's 64 KB, unchecked, into the memory around it.
  list(APPEND common -fwasm-exceptions -pthread)
  set(link -fwasm-exceptions -pthread -sPTHREAD_POOL_SIZE=8 -sALLOW_MEMORY_GROWTH -sNODERAWFS -sSTACK_SIZE=8MB
      -sDEFAULT_PTHREAD_STACK_SIZE=2MB)
endif()
string(TOUPPER "${CMAKE_BUILD_TYPE}" build_type)
separate_arguments(build_flags UNIX_COMMAND "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${build_type}}")

# A sanitizer instruments the tests and their run; the compile_fail cases and syntax checks see none.
set(sanitize "")
set(run_with "")
set(environment "")
if(DREAMOR_SANITIZE STREQUAL "address")
  set(sanitize -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  set(environment "ASAN_OPTIONS=detect_leaks=1")
elseif(DREAMOR_SANITIZE STREQUAL "thread")
  set(sanitize -fsanitize=thread -fno-omit-frame-pointer)
  set(environment "TSAN_OPTIONS=halt_on_error=1")
  # GCC 13's TSan stops before main when the kernel randomizes addresses more widely than its fixed shadow layout
  # allows, as Ubuntu's kernels have since 2024. Newer runtimes, Clang 18's among them, run the program again without
  # randomization themselves. setarch -R turns it off for every compiler, which costs the others nothing.
  set(run_with setarch ${CMAKE_HOST_SYSTEM_PROCESSOR} -R)
elseif(NOT DREAMOR_SANITIZE STREQUAL "")
  message(FATAL_ERROR "DREAMOR_SANITIZE is address, thread or empty, not ${DREAMOR_SANITIZE}")
endif()


find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
set(cases ${Python3_EXECUTABLE} ${tests}/cases.py)

# A test's program. Strict warnings unless LENIENT (the ported samples, Google's code) or USER (the code users
# read: the docs' code and examples/); against the stub unless NO_STUB (the real Concert); LIBRARIES to link (a
# solver's target). examples/ is a system header to the tests that include it.
function(dreamor_program target source)
  cmake_parse_arguments(PARSE_ARGV 2 arg "LENIENT;USER;NO_STUB" "" "LIBRARIES;DEFINITIONS")
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE dreamor::dreamor ${arg_LIBRARIES})
  target_include_directories(${target} PRIVATE ${tests})
  target_include_directories(${target} SYSTEM PRIVATE ${examples})
  if(NOT arg_NO_STUB)
    target_include_directories(${target} PRIVATE ${tests}/stub)
  endif()
  if(arg_LENIENT)
    target_compile_options(${target} PRIVATE ${lenient})
  elseif(arg_USER)
    target_compile_options(${target} PRIVATE ${user})
  else()
    target_compile_options(${target} PRIVATE ${strict})
  endif()
  target_compile_options(${target} PRIVATE ${sanitize} ${common})
  target_compile_definitions(${target} PRIVATE ${arg_DEFINITIONS})
  target_link_options(${target} PRIVATE ${sanitize} ${link})
endfunction()

# A test that runs TARGET, with ARGS and ENVIRONMENT beside the sanitizer's. A command that is the target itself
# runs under the cross-compiling emulator (Node.js for wasm32); one run under setarch names the program's file.
function(dreamor_run name target label)
  cmake_parse_arguments(PARSE_ARGV 3 arg "" "" "ARGS;ENVIRONMENT")
  if(run_with)
    add_test(NAME ${name} COMMAND ${run_with} $<TARGET_FILE:${target}> ${arg_ARGS})
  else()
    add_test(NAME ${name} COMMAND ${target} ${arg_ARGS})
  endif()
  set(env ${environment} ${arg_ENVIRONMENT})
  set_tests_properties(${name} PROPERTIES LABELS ${label})
  if(env)
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "${env}")
  endif()
endfunction()

# A JSON string of each item, its backslashes and quotes escaped.
function(dreamor_json_strings out)
  set(items "")
  foreach(item IN LISTS ARGN)
    string(REPLACE "\\" "\\\\" item "${item}")
    string(REPLACE "\"" "\\\"" item "${item}")
    list(APPEND items "\"${item}\"")
  endforeach()
  list(JOIN items ", " joined)
  set(${out} "${joined}" PARENT_SCOPE)
endfunction()

# What tests/cases.py compiles the cases and syntax checks of NAME with: cases_NAME.json, its path in OUT. FLAGS
# are the headers and definitions beside the warnings.
function(dreamor_cases_config name out)
  dreamor_json_strings(cxx_json ${CMAKE_CXX_COMPILER})
  dreamor_json_strings(pch_json ${CMAKE_CURRENT_BINARY_DIR}/pch/${name})
  dreamor_json_strings(lenient_json ${cases_std} ${build_flags} ${common} ${ARGN} ${lenient})
  dreamor_json_strings(strict_json ${cases_std} ${build_flags} ${common} ${ARGN} ${strict})
  dreamor_json_strings(user_json ${cases_std} ${build_flags} ${common} ${ARGN} ${user})
  if(clang_like)
    set(clang_pch true)
  else()
    set(clang_pch false)
  endif()
  set(config ${CMAKE_CURRENT_BINARY_DIR}/cases_${name}.json)
  file(WRITE ${config} "{\"cxx\": ${cxx_json}, \"msvc\": ${cases_msvc}, \"clang_pch\": ${clang_pch}, \"pch_dir\": ${pch_json},\n"
                       " \"lenient\": [${lenient_json}],\n \"strict\": [${strict_json}],\n"
                       " \"user\": [${user_json}]}\n")
  set(${out} ${config} PARENT_SCOPE)
endfunction()

# The cases of FOLDER, by tests/cases.py list: NAME<tab>FILE each. A change to a topic file configures again.
function(dreamor_cases folder out)
  file(GLOB topics CONFIGURE_DEPENDS ${tests}/${folder}/*.cpp)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${topics})
  execute_process(COMMAND ${cases} list ${tests}/${folder} OUTPUT_VARIABLE listed COMMAND_ERROR_IS_FATAL ANY)
  string(REPLACE "\n" ";" listed "${listed}")
  list(FILTER listed EXCLUDE REGEX "^$")
  set(${out} "${listed}" PARENT_SCOPE)
endfunction()

# FOLDER's compile_fail cases, named PREFIX/<case>: the control first (and the prelude precompiled), as a fixture
# every case requires - a control that fails makes them meaningless.
function(dreamor_compile_fail prefix folder config)
  add_test(NAME ${prefix}/control COMMAND ${cases} prepare ${config} ${tests}/${folder})
  set_tests_properties(${prefix}/control PROPERTIES LABELS compile_fail FIXTURES_SETUP ${prefix})
  dreamor_cases(${folder} listed)
  foreach(entry IN LISTS listed)
    string(REPLACE "\t" ";" entry "${entry}")
    list(GET entry 0 name)
    list(GET entry 1 topic)
    add_test(NAME ${prefix}/${name} COMMAND ${cases} rejects ${config} ${tests}/${folder} ${topic} ${name})
    set_tests_properties(${prefix}/${name} PROPERTIES LABELS compile_fail FIXTURES_REQUIRED ${prefix})
  endforeach()
endfunction()

# FOLDER's contract_fail cases, each a program that must stop with its message. A broken contract stops a debug
# build alone: under NDEBUG the checks compile out, and ThreadSanitizer has nothing to find in a program that stops
# on purpose. LIBRARIES as for dreamor_program.
function(dreamor_contract_fail folder)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "LIBRARIES")
  if(build_flags MATCHES "[-/]DNDEBUG")
    message(STATUS "${folder}: left out (NDEBUG compiles the checks out)")
    return()
  elseif(DREAMOR_SANITIZE STREQUAL "thread")
    message(STATUS "${folder}: left out (ThreadSanitizer has nothing to find in a program that stops)")
    return()
  endif()
  string(REPLACE "/" "_" tag ${folder})
  dreamor_cases(${folder} listed)
  foreach(entry IN LISTS listed)
    string(REPLACE "\t" ";" entry "${entry}")
    list(GET entry 0 name)
    list(GET entry 1 topic)
    set(target ${tag}_${name})
    dreamor_program(${target} ${topic} LIBRARIES ${arg_LIBRARIES} DEFINITIONS DREAMOR_CASE_${name})
    target_include_directories(${target} PRIVATE ${tests}/${folder})
    add_test(NAME ${folder}/${name}
             COMMAND ${cases} stops ${topic} ${name} ${CMAKE_CROSSCOMPILING_EMULATOR} $<TARGET_FILE:${target}>)
    set_tests_properties(${folder}/${name} PROPERTIES LABELS contract_fail)
    if(environment)
      set_tests_properties(${folder}/${name} PROPERTIES ENVIRONMENT "${environment}")
    endif()
  endforeach()
endfunction()

# --- Against the stub ---------------------------------------------------------------------------------------

# test_*.cpp of unit/, stress/ and examples/, each a program and a test, labelled by its folder.
foreach(kind IN ITEMS unit stress examples)
  if(NOT kind IN_LIST DREAMOR_TESTS)
    continue()
  endif()
  file(GLOB sources CONFIGURE_DEPENDS ${tests}/${kind}/test_*.cpp)
  foreach(source IN LISTS sources)
    get_filename_component(name ${source} NAME_WE)
    # ThreadSanitizer intercepts pthread_once, which libstdc++'s std::call_once calls, and its interceptor leaves the
    # flag marked as running when the callable throws, so the next call on that flag waits forever (GCC 13 and
    # Clang 18 alike; libc++'s std::call_once does not go through it). test_allocation_failure makes the first build
    # of a set's lookup index throw std::bad_alloc and then looks the set up again, as the standard allows, and so
    # hangs there. The test starts no thread, so ThreadSanitizer has nothing to find in it; the address presets run it.
    if(name STREQUAL "test_allocation_failure" AND DREAMOR_SANITIZE STREQUAL "thread")
      message(STATUS "${kind}/${name}: left out (ThreadSanitizer's std::call_once hangs after the callable throws)")
      continue()
    endif()
    dreamor_program(${name} ${source})
    # A release build runs the differential tests' large cases too, sets of tens of thousands of rows: past the
    # 16384 entries from which an index tags its slots, through the release build's own way of indexing a table's
    # rows. A debug build leaves them out, where they take a run from a second to a minute.
    if(kind STREQUAL "stress" AND name MATCHES "^test_differential" AND CMAKE_BUILD_TYPE STREQUAL "Release")
      dreamor_run(${kind}/${name} ${name} ${kind} ENVIRONMENT DREAMOR_DIFF_LARGE=1)
    else()
      dreamor_run(${kind}/${name} ${name} ${kind})
    endif()
    # ASan holds freed memory back (a quarantine), so no set is made where one gone was, and a hint it left
    # never lies inside a set made after it: the stress tests run once more with memory handed back at once.
    if(kind STREQUAL "stress" AND DREAMOR_SANITIZE STREQUAL "address")
      add_test(NAME ${kind}/${name}/no_quarantine COMMAND ${name})
      set_tests_properties(${kind}/${name}/no_quarantine PROPERTIES LABELS ${kind}
                           ENVIRONMENT "${environment}:quarantine_size_mb=0")
    endif()
  endforeach()
endforeach()

# The guide's code that needs no solver (docs/guide/), a test a page, built as a user's.
if("docs" IN_LIST DREAMOR_TESTS)
  file(GLOB guide_sources CONFIGURE_DEPENDS ${tests}/docs/guide/*.cpp)
  foreach(source IN LISTS guide_sources)
    get_filename_component(name ${source} NAME_WE)
    dreamor_program(guide_${name} ${source} USER)
    dreamor_run(docs/guide/${name} guide_${name} docs)
  endforeach()
endif()

dreamor_cases_config(stub stub_config ${includes} -I${tests}/stub)
if("syntax" IN_LIST DREAMOR_TESTS)
  file(GLOB syntax_sources CONFIGURE_DEPENDS ${tests}/cplex/*.cpp)
  foreach(source IN LISTS syntax_sources)
    get_filename_component(name ${source} NAME_WE)
    add_test(NAME syntax/${name} COMMAND ${cases} compiles ${stub_config} ${source})
    set_tests_properties(syntax/${name} PROPERTIES LABELS syntax)
  endforeach()
  add_test(NAME syntax/production_planning_concert
           COMMAND ${cases} compiles ${stub_config} ${examples}/production_planning_concert.cpp user)
  set_tests_properties(syntax/production_planning_concert PROPERTIES LABELS syntax)
endif()
if("compile_fail" IN_LIST DREAMOR_TESTS)
  dreamor_compile_fail(compile_fail compile_fail ${stub_config})
endif()
if("contract_fail" IN_LIST DREAMOR_TESTS)
  dreamor_contract_fail(contract_fail)
endif()

# --- With the solvers ---------------------------------------------------------------------------------------

if(DREAMOR_ORTOOLS_DIR)
  add_subdirectory(mathopt)
endif()
if(DREAMOR_HIGHS_DIR)
  add_subdirectory(highs)
endif()
if(DREAMOR_CPLEX_DIR)
  add_subdirectory(cplex)
endif()
