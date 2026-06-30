add_library(usermod_picovector INTERFACE)

list(APPEND SOURCES
  # PicoVector C/C++ core library (MicroPython-agnostic)
  ${CMAKE_CURRENT_LIST_DIR}/picovector.cpp
  ${CMAKE_CURRENT_LIST_DIR}/picovector_working_buffer.cpp
  ${CMAKE_CURRENT_LIST_DIR}/shape.cpp
  ${CMAKE_CURRENT_LIST_DIR}/font.cpp
  ${CMAKE_CURRENT_LIST_DIR}/pixel_font.cpp
  ${CMAKE_CURRENT_LIST_DIR}/image.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brush.cpp
  ${CMAKE_CURRENT_LIST_DIR}/color.cpp
  ${CMAKE_CURRENT_LIST_DIR}/primitive.cpp
  ${CMAKE_CURRENT_LIST_DIR}/algorithms/geometry.cpp
  ${CMAKE_CURRENT_LIST_DIR}/algorithms/dda.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/pattern.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/color.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/image.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/gradient.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/pixelate.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/blur.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/brightness.cpp
  ${CMAKE_CURRENT_LIST_DIR}/filters/blur.cpp
  ${CMAKE_CURRENT_LIST_DIR}/filters/dither.cpp
  ${CMAKE_CURRENT_LIST_DIR}/filters/monochrome.cpp
  ${CMAKE_CURRENT_LIST_DIR}/filters/onebit.cpp
)

# The MicroPython bindings, decoders, allocator config and build knobs live in
# the sibling picovector-micropython component, wired in separately (see
# board/usermodules.cmake). This file is the core library only.

target_sources(usermod_picovector INTERFACE
  ${SOURCES}
)

target_include_directories(usermod_picovector INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(usermod INTERFACE usermod_picovector)

set_source_files_properties(
  ${SOURCES}
  PROPERTIES COMPILE_FLAGS
  "-Wno-unused-variable"
)

if(DEFINED PICO_BOARD)
  # Build picovector for Pico
  target_compile_definitions(usermod_picovector INTERFACE PICO=1)

  set_source_files_properties(
    ${SOURCES}
    PROPERTIES COMPILE_OPTIONS
    "-O2;-fgcse-after-reload;-floop-interchange;-fpeel-loops;-fpredictive-commoning;-fsplit-paths;-ftree-loop-distribute-patterns;-ftree-loop-distribution;-ftree-vectorize;-ftree-partial-pre;-funswitch-loops"
  )
endif()
