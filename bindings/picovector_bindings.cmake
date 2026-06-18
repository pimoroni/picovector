# picovector_bindings.cmake — wire the generated PicoVector bindings into the
# firmware build, in place of the hand-written micropython/*.cpp.
#
# Drop-in for the `micropython/...` entries in ../picovector.cmake: include this
# file from there (or append PV_GENERATED_BINDINGS_SOURCES to SOURCES). Paths
# are relative to this file (bindings/).

set(_PV_BINDINGS_DIR ${CMAKE_CURRENT_LIST_DIR})

set(PV_GENERATED_BINDINGS_SOURCES
    # generated module table + per-type bindings
    ${_PV_BINDINGS_DIR}/generated/picovector_bindings.c
    ${_PV_BINDINGS_DIR}/generated/vec2.cpp
    ${_PV_BINDINGS_DIR}/generated/rect.cpp
    ${_PV_BINDINGS_DIR}/generated/mat3.cpp
    ${_PV_BINDINGS_DIR}/generated/color.cpp
    ${_PV_BINDINGS_DIR}/generated/brush.cpp
    ${_PV_BINDINGS_DIR}/generated/shape.cpp
    ${_PV_BINDINGS_DIR}/generated/image.cpp
    ${_PV_BINDINGS_DIR}/generated/font.cpp
    ${_PV_BINDINGS_DIR}/generated/pixel_font.cpp
    ${_PV_BINDINGS_DIR}/generated/algorithm.cpp
    # shared glue + hand-written (native) bodies + companion image decoders
    ${_PV_BINDINGS_DIR}/runtime/pv_support.cpp
    ${_PV_BINDINGS_DIR}/native/font_native.cpp
    ${_PV_BINDINGS_DIR}/native/pixel_font_native.cpp
    ${_PV_BINDINGS_DIR}/native/image_native.cpp
    ${_PV_BINDINGS_DIR}/native/algorithm_native.cpp
    ${_PV_BINDINGS_DIR}/native/image_png.cpp
    ${_PV_BINDINGS_DIR}/native/image_jpeg.cpp
)

# The bindings tree is self-contained. Sources include "pv_bindings.hpp",
# "types.h" and "pv_objs.hpp" (the obj-struct header) from the generated/runtime
# dirs below; the picovector library headers (shape.hpp, blend.hpp, …) and
# PNGdec/JPEGDEC come from the picovector root + decoder libs already on the
# usermod include path.
set(PV_GENERATED_BINDINGS_INCLUDES
    ${_PV_BINDINGS_DIR}/generated
    ${_PV_BINDINGS_DIR}/runtime
)

# Match the original per-source relaxation (generated code has intentional
# unused locals, e.g. the running arg index in simple methods).
set_source_files_properties(${PV_GENERATED_BINDINGS_SOURCES}
    PROPERTIES COMPILE_FLAGS "-Wno-unused-variable")
