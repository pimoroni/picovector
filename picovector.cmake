add_library(usermod_picovector INTERFACE)

# pico3d rasteriser backend:
#   "float"         - pico3d_raster.cpp           (reference, float hot path)
#   "int"           - pico3d_raster_int.cpp       (integer fixed-point hot path)
#   "int_templated" - pico3d_raster_int_templated.cpp (int + per-feature template
#                     specialisation + normal-map support)
set(PICO3D_RASTER "int_templated")
if(PICO3D_RASTER STREQUAL "int")
  set(PICO3D_RASTER_SRC ${CMAKE_CURRENT_LIST_DIR}/pico3d_raster_int.cpp)
elseif(PICO3D_RASTER STREQUAL "int_templated")
  set(PICO3D_RASTER_SRC ${CMAKE_CURRENT_LIST_DIR}/pico3d_raster_int_templated.cpp)
else()
  set(PICO3D_RASTER_SRC ${CMAKE_CURRENT_LIST_DIR}/pico3d_raster.cpp)
endif()

list(APPEND SOURCES
  # PicoVector C/C++ core library (MicroPython-agnostic)
  ${CMAKE_CURRENT_LIST_DIR}/picovector.cpp
  ${CMAKE_CURRENT_LIST_DIR}/rasteriser.cpp
  ${CMAKE_CURRENT_LIST_DIR}/picovector_working_buffer.cpp
  ${CMAKE_CURRENT_LIST_DIR}/shape.cpp
  ${CMAKE_CURRENT_LIST_DIR}/font.cpp
  ${CMAKE_CURRENT_LIST_DIR}/font_parse.cpp
  ${CMAKE_CURRENT_LIST_DIR}/gif_parse.cpp
  ${CMAKE_CURRENT_LIST_DIR}/pixel_font.cpp
  ${CMAKE_CURRENT_LIST_DIR}/image.cpp
  ${CMAKE_CURRENT_LIST_DIR}/blit.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brush.cpp
  ${CMAKE_CURRENT_LIST_DIR}/color.cpp
  ${CMAKE_CURRENT_LIST_DIR}/primitive.cpp
  ${PICO3D_RASTER_SRC}
  ${CMAKE_CURRENT_LIST_DIR}/pico3d_draw.cpp
  ${CMAKE_CURRENT_LIST_DIR}/algorithms/geometry.cpp
  ${CMAKE_CURRENT_LIST_DIR}/algorithms/dda.cpp
  ${CMAKE_CURRENT_LIST_DIR}/tween/easing.cpp
  ${CMAKE_CURRENT_LIST_DIR}/tween/tween.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/pattern.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/color.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/transparent.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/image.cpp
  ${CMAKE_CURRENT_LIST_DIR}/blit.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/gradient.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/fractal.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/pixelate.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/blur.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/brightness.cpp
  ${CMAKE_CURRENT_LIST_DIR}/filters/blur.cpp
  ${CMAKE_CURRENT_LIST_DIR}/filters/bloom.cpp
  ${CMAKE_CURRENT_LIST_DIR}/filters/edgeglow.cpp
  ${CMAKE_CURRENT_LIST_DIR}/filters/wave.cpp
  ${CMAKE_CURRENT_LIST_DIR}/filters/zoom.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/dither.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/monochrome.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/invert.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/threshold.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/saturation.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/contrast.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/duotone.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/crt.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/grid.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/vignette.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/noise.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/glitch.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/oilpaint.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/palette_dither.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/phosphor.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/nightvision.cpp
  ${CMAKE_CURRENT_LIST_DIR}/brushes/chromatic.cpp

  # Vendored third-party, see lib/qrcodegen/README.md
  ${CMAKE_CURRENT_LIST_DIR}/lib/qrcodegen/qrcodegen.c
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

  # pico3d coverage backend. The default (neither define) is scalar coverage with
  # exact integer edge functions, so shared edges are watertight. Enable at most
  # one of these:
  #   PICO3D_USE_INTERP=1       INTERP0 edge-function coverage (exact integer, faster)
  #   PICO3D_USE_INTERP_BARY=1  INTERP0 normalised-barycentric + OVERF coverage;
  #                             fastest, but normalises through float, so sub-pixel
  #                             coverage error opens hairline cracks on shared edges
  #target_compile_definitions(usermod_picovector INTERFACE PICO3D_USE_INTERP=1)
  #target_compile_definitions(usermod_picovector INTERFACE PICO3D_USE_INTERP_BARY=1)
  target_link_libraries(usermod_picovector INTERFACE hardware_interp)

  set_source_files_properties(
    ${SOURCES}
    PROPERTIES COMPILE_OPTIONS
    "-O2;-fgcse-after-reload;-floop-interchange;-fpeel-loops;-fpredictive-commoning;-fsplit-paths;-ftree-loop-distribute-patterns;-ftree-loop-distribution;-ftree-vectorize;-ftree-partial-pre;-funswitch-loops"
  )
endif()
