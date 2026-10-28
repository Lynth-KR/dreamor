# SPDX-License-Identifier: Apache-2.0
# dreamor is header-only: one INTERFACE target, dreamor::dreamor, with the include path and
# C++20. The solver is the user's to bring - the backend headers include it, so link it next
# to dreamor::dreamor:
#
#   find_package(dreamor CONFIG REQUIRED)
#   target_link_libraries(app PRIVATE dreamor::dreamor ortools::ortools)   # dreamor/mathopt.hpp
#   target_link_libraries(app PRIVATE dreamor::dreamor <your CPLEX target>) # dreamor/concert.hpp
#
# Also usable without installing: add_subdirectory(dreamor) or FetchContent.
# The tests do not go through CMake; see tests/run_tests.sh.
cmake_minimum_required(VERSION 3.20)
project(dreamor VERSION 0.3.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(dreamor INTERFACE)
add_library(dreamor::dreamor ALIAS dreamor)
target_include_directories(dreamor INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(dreamor INTERFACE cxx_std_20)

# Install only when dreamor is the top-level project, so add_subdirectory / FetchContent users
# do not get dreamor's files installed along with their own.
if(PROJECT_IS_TOP_LEVEL)
  set(DREAMOR_CMAKE_DIR ${CMAKE_INSTALL_DATADIR}/cmake/dreamor)

  install(DIRECTORY include/dreamor DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(TARGETS dreamor EXPORT dreamorTargets)
  install(EXPORT dreamorTargets NAMESPACE dreamor:: DESTINATION ${DREAMOR_CMAKE_DIR})

  configure_package_config_file(cmake/dreamorConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/dreamorConfig.cmake
    INSTALL_DESTINATION ${DREAMOR_CMAKE_DIR})
  write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/dreamorConfigVersion.cmake
    COMPATIBILITY SameMinorVersion ARCH_INDEPENDENT)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/dreamorConfig.cmake
                ${CMAKE_CURRENT_BINARY_DIR}/dreamorConfigVersion.cmake
          DESTINATION ${DREAMOR_CMAKE_DIR})
  install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/dreamor)
endif()
