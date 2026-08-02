#pragma once

#include <stddef.h>
#include <stdint.h>

namespace picovector {

  class image_t;

  // ── GIF parsing ─────────────────────────────────────────────────────────────
  // A GIF frame is a sub-rectangle composited onto a canvas, and what the canvas
  // held beforehand depends on the previous frame's disposal, so frame N depends
  // on potentially every frame before it. The parser settles that once, at load:
  // it composites every frame into its own cell of a spritesheet, after which a
  // frame is an ordinary sub-view and costs nothing to reach.
  //
  // A byte source, as font_reader_t but with a rewind: the file is read twice,
  // once to count frames and size the sheet and once to decode into it. A read
  // returning fewer than `len` bytes is a truncated file.
  struct gif_reader_t {
    size_t (*read)(void *handle, void *dest, size_t len);
    bool (*rewind)(void *handle);
    void *handle;
  };

  enum gif_status_t {
    GIF_OK = 0,
    GIF_BAD_MAGIC,    // not a GIF at all - the caller may try another format
    GIF_TRUNCATED,    // ran out of bytes mid-parse
    GIF_UNSUPPORTED,  // a feature this parser doesn't implement
    GIF_TOO_MANY_FRAMES, // more frames than the caller's table holds
    GIF_TOO_BIG,      // more memory than the embedder allows (PV_GIF_MAX_BYTES)
    GIF_NO_BUFFER,    // a buffer the caller had to supply is missing or too small
    GIF_BAD_DATA      // structurally a GIF, but the contents don't work
  };

  // What becomes of a frame's area before the next frame is drawn.
  enum gif_disposal_t {
    GIF_DISPOSE_NONE = 0,
    GIF_DISPOSE_KEEP = 1,
    GIF_DISPOSE_BACKGROUND = 2,  // clear the frame's rect
    GIF_DISPOSE_PREVIOUS = 3     // put back what the rect held before the frame
  };

  struct gif_frame_t {
    uint16_t x, y, w, h;
    uint16_t delay;          // milliseconds, from the file's hundredths
    uint8_t disposal;
    int16_t transparent;     // palette index, -1 if the frame declares none
    bool interlaced;
    bool local_palette;
  };

  struct gif_info_t {
    uint16_t width, height;  // logical screen: the size of one cell
    uint16_t frame_count;
    uint16_t palette_size;   // entries the sheet's colour table needs
    uint8_t clear_index;     // what an untouched pixel holds
    uint8_t background_index; // the screen's background colour, for opaque disposal
    bool clear_is_opaque;    // true when there was no room for a transparent entry
    bool needs_true_color;   // the frames want more colours between them than a byte indexes
    uint32_t restore_bytes;  // scratch a dispose-to-previous frame needs, 0 if none
  };

  // Working storage, placed by the caller: on a microcontroller this belongs in
  // whatever scratch pool the embedder already reserves, not on the stack. The
  // LZW tables are per-frame state, so nothing here has to survive a call.
  struct gif_scratch_t {
    uint16_t prefix[4096];
    uint8_t suffix[4096];
    uint8_t stack[4096];
    uint8_t block[256];      // one LZW data sub-block
    uint8_t palette[768];    // the sheet's colour table, as read
    uint8_t local[768];      // a frame's own table, to be folded into the sheet's
    uint8_t remap[256];      // where that table's indices landed in the sheet's
    // Only needed when a frame disposes to previous; gif_info_t::restore_bytes
    // says how much, and is zero when no frame asks for it.
    uint8_t *restore;
    size_t restore_size;
  };

  // Walk the file without decoding pixels, sizing everything the caller has to
  // allocate. `frames` may be null, and is filled up to `max_frames`; either way
  // info->frame_count is the true count, so a caller with a table too small can
  // resize and survey again.
  gif_status_t gif_survey(gif_reader_t reader, gif_scratch_t *scratch, gif_info_t *info,
                          gif_frame_t *frames, int max_frames);

  // Decode every frame into `sheet`, which must be an indexed image
  // info->frame_count cells wide and one cell tall, each cell info->width by
  // info->height, with room for info->palette_size colours. The colour table is
  // written too. Frame n ends up at sheet->sprite(n, 0).
  gif_status_t gif_decode(gif_reader_t reader, gif_scratch_t *scratch, const gif_info_t *info,
                          image_t *sheet);

  // Both passes over a blob already in memory.
  gif_status_t gif_survey(const uint8_t *data, size_t size, gif_scratch_t *scratch,
                          gif_info_t *info, gif_frame_t *frames, int max_frames);
  gif_status_t gif_decode(const uint8_t *data, size_t size, gif_scratch_t *scratch,
                          const gif_info_t *info, image_t *sheet);

}
