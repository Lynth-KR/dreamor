# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Checks the installed package the way a user would take it: find_package, link, build, run.
# CI installs dreamor to a prefix and configures this with CMAKE_PREFIX_PATH pointing there.
cmake_minimum_required(VERSION 3.20)
project(dreamor_consumer LANGUAGES CXX)

find_package(dreamor 0.7 CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE dreamor::dreamor)
