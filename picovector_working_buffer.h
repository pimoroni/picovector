#pragma once

// Shared rasterisation scratch pool — public declaration.
//
// Defined in picovector_working_buffer.cpp and sized by PV_WORKING_BUFFER_SIZE.
// The core rasteriser carves its tile/node/edge buffers out of it; embedders may
// borrow it as scratch while the rasteriser is idle (the MicroPython bindings
// reuse it for PNG/JPEG decode, and the sleep module uses it as the parser
// arena). Kept in a small, dependency-free, C-compatible header so any module —
// including C ones — can declare the buffer by including this, rather than
// hand-writing the externs or pulling in the whole C++ picovector.hpp.

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const size_t working_buffer_size;
extern char PicoVector_working_buffer[];

#ifdef __cplusplus
}
#endif
