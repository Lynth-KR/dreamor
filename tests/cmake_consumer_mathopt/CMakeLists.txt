# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# The MathOpt combination README documents: find_package(dreamor) next to the OR-Tools
# prebuilt's own find_package(ortools), building the MathOpt example. ortools::ortools brings
# OR-Tools' include path, its definitions (OR_PROTO_DLL) and its libraries.
cmake_minimum_required(VERSION 3.20)
project(dreamor_consumer_mathopt LANGUAGES CXX)

find_package(dreamor 0.6 CONFIG REQUIRED)
find_package(ortools CONFIG REQUIRED)
add_executable(lot_sizing ${CMAKE_CURRENT_SOURCE_DIR}/../../examples/production_planning_mathopt.cpp)
target_include_directories(lot_sizing PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../examples)
target_link_libraries(lot_sizing PRIVATE dreamor::dreamor ortools::ortools)
