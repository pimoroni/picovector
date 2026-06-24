add_library(usermod_picovector INTERFACE)

set(PNGDEC_DIR "${CMAKE_CURRENT_LIST_DIR}/lib/pngdec")
set(JPEGDEC_DIR "${CMAKE_CURRENT_LIST_DIR}/lib/jpegdec")

find_package(PNGDEC CONFIG REQUIRED)
find_package(JPEGDEC CONFIG REQUIRED)

# pico3d rasteriser backend (A/B):
#   "float"         - pico3d_raster.cpp                (original)
#   "int"           - pico3d_raster_int.cpp            (integer fixed-point hot path)
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
  ${CMAKE_CURRENT_LIST_DIR}/micropython/picovector_bindings.c
  ${CMAKE_CURRENT_LIST_DIR}/micropython/picovector.cpp
  ${CMAKE_CURRENT_LIST_DIR}/picovector.cpp
  ${CMAKE_CURRENT_LIST_DIR}/shape.cpp
  ${CMAKE_CURRENT_LIST_DIR}/font.cpp
  ${CMAKE_CURRENT_LIST_DIR}/pixel_font.cpp
  ${CMAKE_CURRENT_LIST_DIR}/image.cpp
  ${PICO3D_RASTER_SRC}
  ${CMAKE_CURRENT_LIST_DIR}/pico3d_draw.cpp
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
  ${CMAKE_CURRENT_LIST_DIR}/micropython/brush.cpp
  ${CMAKE_CURRENT_LIST_DIR}/micropython/color.cpp
  ${CMAKE_CURRENT_LIST_DIR}/micropython/font.cpp
  ${CMAKE_CURRENT_LIST_DIR}/micropython/image_jpeg.cpp
  ${CMAKE_CURRENT_LIST_DIR}/micropython/image_png.cpp
  ${CMAKE_CURRENT_LIST_DIR}/micropython/image.cpp
  ${CMAKE_CURRENT_LIST_DIR}/micropython/mat3.cpp
  ${CMAKE_CURRENT_LIST_DIR}/micropython/pixel_font.cpp
  ${CMAKE_CURRENT_LIST_DIR}/micropython/shape.cpp
  ${CMAKE_CURRENT_LIST_DIR}/micropython/rect.cpp
  ${CMAKE_CURRENT_LIST_DIR}/micropython/vec2.cpp
  ${CMAKE_CURRENT_LIST_DIR}/micropython/algorithm.cpp
  ${CMAKE_CURRENT_LIST_DIR}/micropython/pico3d.cpp
)

target_sources(usermod_picovector INTERFACE
  ${SOURCES}
)

target_include_directories(usermod_picovector INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(usermod INTERFACE usermod_picovector pngdec jpegdec)

set_source_files_properties(
  ${SOURCES}
  PROPERTIES COMPILE_FLAGS
  "-Wno-unused-variable"
)

if(DEFINED PICO_BOARD)
  # Build jpegdec for Pico
  target_compile_definitions(jpegdec PRIVATE PICO_BUILD)

  # Build picovector for Pico
  target_compile_definitions(usermod_picovector INTERFACE PICO=1)

  # pico3d rasteriser COVERAGE backend (A/B — enable exactly ONE; attributes are
  # always float-linear regardless):
  #   PICO3D_USE_INTERP=1       INTERP0 edge-function coverage (exact integer; faster)
  #   PICO3D_USE_INTERP_BARY=1  INTERP0 normalized-barycentric + OVERF coverage
  #                             (fastest ~18.3fps, BUT normalizes via float -> ±sub-
  #                             pixel coverage -> tiny cracks on shared edges)
  #   (neither)                 scalar coverage: EXACT integer edge functions, so
  #                             shared edges are watertight (covered by both tris,
  #                             never neither) -> no cracks. Active for correctness.
  # To A/B, enable exactly one of the two lines below (else scalar), then rebuild.
  #target_compile_definitions(usermod_picovector INTERFACE PICO3D_USE_INTERP=1)
  #target_compile_definitions(usermod_picovector INTERFACE PICO3D_USE_INTERP_BARY=1)
  #   (+ PICO3D_BARY_AUTOSTEP=1 is an on-hardware experiment: auto-steps via the
  #    24-bit add_raw. If add_raw carries — as the datasheet implies — it false-
  #    trips OVERF and the model nearly vanishes; if your silicon renders it
  #    cleanly, add_raw does NOT carry and the fast auto-stepped form is viable.)
  target_link_libraries(usermod_picovector INTERFACE hardware_interp)

  set_source_files_properties(
    ${SOURCES}
    PROPERTIES COMPILE_OPTIONS
    "-O2;-fgcse-after-reload;-floop-interchange;-fpeel-loops;-fpredictive-commoning;-fsplit-paths;-ftree-loop-distribute-patterns;-ftree-loop-distribution;-ftree-vectorize;-ftree-partial-pre;-funswitch-loops"
  )
endif()