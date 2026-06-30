// picovector_working_buffer.cpp — the shared rasterisation scratch pool.
//
// Split out of picovector.cpp so its size is a single configurable knob
// (PV_WORKING_BUFFER_SIZE, see config_default.hpp) and so the one buffer can be
// shared with embedders. The core rasteriser carves its tile/node/edge buffers
// out of it; the MicroPython bindings additionally use it as scratch for the
// PNG/JPEG decoders (new(PicoVector_working_buffer) PNG()/JPEGDEC()), which is
// why the MicroPython build enlarges PV_WORKING_BUFFER_SIZE to fit PNGDEC state.
//
// On RP2 it *must* be 32-bit aligned (learned the hard way).

#include "picovector.hpp"   // PV_WORKING_BUFFER_SIZE + the extern declarations

extern "C" {
  const size_t working_buffer_size = PV_WORKING_BUFFER_SIZE;
  char __attribute__((aligned(4))) PicoVector_working_buffer[PV_WORKING_BUFFER_SIZE];
}
