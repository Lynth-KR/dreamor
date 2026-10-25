# SPDX-License-Identifier: Apache-2.0
# Checks the installed package the way a user would take it: find_package, link, build, run.
# CI installs dreamor to a prefix and configures this with CMAKE_PREFIX_PATH pointing there.
cmake_minimum_required(VERSION 3.20)
project(dreamor_consumer LANGUAGES CXX)

find_package(dreamor 0.2 CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE dreamor::dreamor)
