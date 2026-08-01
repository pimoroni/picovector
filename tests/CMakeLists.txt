# Host tests for the PicoVector core.
#
# The core is portable C++ with no MicroPython or Pico SDK dependency, so it
# builds and runs on a desktop. That is what makes these tests cheap enough to
# run on every push, where the on-device tests need hardware.
#
#   cmake -S tests -B build/tests && cmake --build build/tests && ./build/tests/pv_tests

cmake_minimum_required(VERSION 3.16)
project(picovector_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PV_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

# The core library sources. lib/ (the PNG/JPEG decoders) is deliberately not
# here - it belongs to the embedder, not the core.
file(GLOB PV_SOURCES CONFIGURE_DEPENDS
  ${PV_ROOT}/*.cpp
  ${PV_ROOT}/brushes/*.cpp
  ${PV_ROOT}/filters/*.cpp
  ${PV_ROOT}/algorithms/*.cpp
  ${PV_ROOT}/tween/*.cpp
)

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/test_*.cpp)

add_executable(pv_tests ${TEST_SOURCES} ${CMAKE_CURRENT_LIST_DIR}/main.cpp ${PV_SOURCES})
target_include_directories(pv_tests PRIVATE ${PV_ROOT} ${CMAKE_CURRENT_LIST_DIR})

# Warnings are errors here: this build is where a portability slip surfaces, and
# the whole point is to catch it before it reaches a board.
# -Wunknown-pragmas: the core carries #pragma GCC optimize hints for the
# firmware's -Os build, which clang doesn't recognise. A toolchain difference,
# not a defect.
target_compile_options(pv_tests PRIVATE
  -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unknown-pragmas)

enable_testing()
add_test(NAME pv_tests COMMAND pv_tests)
