// The GIF container parser.
//
// Written from the GIF89a specification (W3C, 31 July 1990); the variable-width
// LZW is the decompression procedure of its Appendix F. Not derived from an
// existing decoder.
//
// Why this exists at all, when bitbank2's AnimatedGIF is proven, is by the same
// author as the vendored PNG and JPEG decoders, and would drop straight in: a
// vendored decoder lives under lib/, which the host test build excludes, so it
// would carry no sanitiser coverage - and LZW over untrusted bytes is exactly
// the code that wants some. It is also built around playing frames onto its own
// canvas, where what we want is raw indices to composite ourselves. gif_reader_t
// and gif_status_t are the seam, so swapping a library in later reaches no
// further than this file.
//
// Layout, everything little-endian:
//
//   "GIF87a" or "GIF89a"
//   logical screen descriptor    u16 width, u16 height, packed, background, aspect
//   global colour table          3 bytes per entry, when the packed flag says so
//   blocks, until the trailer:
//     0x21 extension             label, then length-prefixed sub-blocks
//     0x2c image descriptor      rect, packed, optional local colour table,
//                                LZW minimum code size, then data sub-blocks
//     0x3b trailer
//
// The 0xf9 graphic control extension carries the delay, the transparent index
// and the disposal method, and applies to the image block that follows it.

#include <string.h>

#include "gif.hpp"
#include "image.hpp"
#include "picovector.hpp"

namespace picovector {

  namespace {

    static const uint8_t GIF_BLOCK_EXTENSION = 0x21;
    static const uint8_t GIF_BLOCK_IMAGE     = 0x2c;
    static const uint8_t GIF_BLOCK_TRAILER   = 0x3b;
    static const uint8_t GIF_EXT_GRAPHIC_CONTROL = 0xf9;

    // Little-endian reads over a gif_reader_t. As in the .af parser, a short
    // read latches `truncated` and every read after it yields zero, so the walk
    // finishes without wandering off the data and reports once, at the end.
    struct gif_read_t {
      gif_reader_t source;
      bool truncated;

      void bytes(void *dest, size_t len) {
        if(!truncated && source.read(source.handle, dest, len) == len) return;
        truncated = true;
        memset(dest, 0, len);
      }

      uint8_t u8() { uint8_t b; bytes(&b, 1); return b; }
      uint16_t u16() { uint8_t b[2]; bytes(b, 2); return (uint16_t)(b[0] | (b[1] << 8)); }

      void skip(size_t len) {
        uint8_t discard[64];
        while(len > 0 && !truncated) {
          size_t chunk = len < sizeof(discard) ? len : sizeof(discard);
          bytes(discard, chunk);
          len -= chunk;
        }
      }

      // Length-prefixed sub-blocks, terminated by a zero length.
      void skip_sub_blocks() {
        for(uint8_t len = u8(); len > 0 && !truncated; len = u8()) skip(len);
      }
    };

    struct memory_source_t {
      const uint8_t *data;
      size_t size;
      size_t pos;
    };

    size_t memory_read(void *handle, void *dest, size_t len) {
      memory_source_t *source = (memory_source_t *)handle;
      size_t available = source->size - source->pos;
      if(len > available) len = available;
      memcpy(dest, source->data + source->pos, len);
      source->pos += len;
      return len;
    }

    bool memory_rewind(void *handle) {
      ((memory_source_t *)handle)->pos = 0;
      return true;
    }

    struct gif_header_t {
      uint16_t width, height;
      uint16_t palette_size;   // global colour table entries, 0 when absent
      uint8_t background;
    };

    // Reads as far as the end of the global colour table, which lands in
    // scratch->palette. Shared by both passes so they agree on where the block
    // stream starts.
    gif_status_t read_header(gif_read_t &r, gif_scratch_t *scratch, gif_header_t *header) {
      char marker[6];
      r.bytes(marker, sizeof(marker));
      if(r.truncated || memcmp(marker, "GIF8", 4) != 0) return GIF_BAD_MAGIC;
      // 87a and 89a differ only in which extensions are allowed, and those are
      // skipped by length either way.
      if((marker[4] != '7' && marker[4] != '9') || marker[5] != 'a') return GIF_BAD_MAGIC;

      header->width = r.u16();
      header->height = r.u16();
      uint8_t packed = r.u8();
      header->background = r.u8();
      r.u8();  // pixel aspect ratio, unused

      header->palette_size = (packed & 0x80) ? (uint16_t)(2 << (packed & 0x07)) : 0;
      if(header->palette_size) r.bytes(scratch->palette, (size_t)header->palette_size * 3);

      if(r.truncated) return GIF_TRUNCATED;
      if(header->width == 0 || header->height == 0) return GIF_BAD_DATA;
      return GIF_OK;
    }

    // ── LZW ───────────────────────────────────────────────────────────────────
    // Codes are packed least-significant bit first and run across the data
    // sub-block boundaries, so the bit reader refills from the next sub-block
    // without the code stream noticing.
    struct lzw_read_t {
      gif_read_t *r;
      gif_scratch_t *scratch;
      int block_len;
      int block_pos;
      uint32_t bits;
      int bit_count;
      bool ended;

      bool next_block() {
        if(ended) return false;
        block_len = r->u8();
        if(block_len == 0 || r->truncated) { ended = true; return false; }
        r->bytes(scratch->block, (size_t)block_len);
        block_pos = 0;
        return !r->truncated;
      }

      // The next `width`-bit code, or -1 if the data ran out first.
      int code(int width) {
        while(bit_count < width) {
          if(block_pos == block_len && !next_block()) return -1;
          bits |= (uint32_t)scratch->block[block_pos++] << bit_count;
          bit_count += 8;
        }
        int value = (int)(bits & ((1u << width) - 1));
        bits >>= width;
        bit_count -= width;
        return value;
      }

      // Walk to the terminator so the block stream stays aligned for the caller.
      void drain() { while(next_block()) { } }
    };

    // Writes decoded indices into one cell of the sheet, in the frame's own
    // rect. Pixels outside the canvas are consumed and dropped: a frame is
    // allowed to declare a rect larger than the screen, and the stream still
    // carries every pixel of it.
    struct frame_writer_t {
      uint8_t *base;      // top-left of the destination cell
      size_t stride;
      int canvas_w, canvas_h;
      int x, y, w, h;     // the frame's rect, as declared
      bool interlaced;
      int transparent;      // -1 when the frame declares none
      const uint8_t *remap; // null unless the frame brought its own colour table
      int max_index;
      int col, row, pass;

      void put(uint8_t index) {
        if(row >= h) return;   // more pixels than the rect can hold
        int px = x + col, py = y + row;
        if((int)index != transparent && px < canvas_w && py < canvas_h) {
          uint8_t value = remap ? remap[index]
                                : (index > max_index ? (uint8_t)max_index : index);
          base[(size_t)py * stride + px] = value;
        }
        if(++col == w) { col = 0; advance_row(); }
      }

      // Interlaced rows arrive in four passes: every eighth row from 0, then
      // from 4, then every fourth from 2, then every second from 1.
      void advance_row() {
        if(!interlaced) { row++; return; }
        static const int start[4] = { 0, 4, 2, 1 };
        static const int step[4] = { 8, 8, 4, 2 };
        row += step[pass];
        while(row >= h && pass < 3) { pass++; row = start[pass]; }
      }
    };

    gif_status_t decode_frame(gif_read_t &r, gif_scratch_t *scratch, frame_writer_t &writer) {
      int min_code_size = r.u8();
      if(min_code_size < 2 || min_code_size > 8) return GIF_BAD_DATA;

      const int clear_code = 1 << min_code_size;
      const int end_code = clear_code + 1;

      lzw_read_t in = { &r, scratch, 0, 0, 0, 0, false };
      int width = min_code_size + 1;
      int next_code = end_code + 1;
      int previous = -1;
      int first_byte = 0;

      for(;;) {
        int code = in.code(width);
        if(code < 0) return GIF_TRUNCATED;
        if(code == end_code) break;
        if(code == clear_code) {
          width = min_code_size + 1;
          next_code = end_code + 1;
          previous = -1;
          continue;
        }

        // Walk the code back to its first byte, stacking the string in reverse.
        int top = 0;
        int walk = code;
        if(code >= next_code) {
          // A code used in the same step that defines it: its string is the
          // previous one followed by that string's own first byte.
          if(code > next_code || previous < 0) return GIF_BAD_DATA;
          scratch->stack[top++] = (uint8_t)first_byte;
          walk = previous;
        }
        while(walk >= clear_code) {
          if(top >= 4095) return GIF_BAD_DATA;   // a cycle in the table
          scratch->stack[top++] = scratch->suffix[walk];
          walk = scratch->prefix[walk];
        }
        first_byte = walk;
        scratch->stack[top++] = (uint8_t)walk;

        while(top > 0) writer.put(scratch->stack[--top]);

        if(previous >= 0 && next_code < 4096) {
          scratch->prefix[next_code] = (uint16_t)previous;
          scratch->suffix[next_code] = (uint8_t)first_byte;
          next_code++;
          // The code width grows with the table and holds at 12 once it is full;
          // an encoder may go on emitting 12-bit codes without ever clearing.
          if(next_code == (1 << width) && width < 12) width++;
        }
        previous = code;
      }

      in.drain();
      return r.truncated ? GIF_TRUNCATED : GIF_OK;
    }

    struct clipped_t { int x, y, w, h; };

    clipped_t clip_to_canvas(int x, int y, int w, int h, int canvas_w, int canvas_h) {
      int right = x + w, bottom = y + h;
      if(right > canvas_w) right = canvas_w;
      if(bottom > canvas_h) bottom = canvas_h;
      return { x, y, right > x ? right - x : 0, bottom > y ? bottom - y : 0 };
    }

    // Folds a frame's own colour table into the sheet's, filling `remap` with
    // where each of its indices landed. Encoders that optimise per frame emit a
    // table per frame, but the colours across them nearly always fit one table
    // between them, which is what keeps the sheet indexed. Returns false when
    // the union outgrows what a byte can address.
    //
    // The base table is never merged into itself: duplicate entries in it are
    // kept, because a frame without a table of its own indexes it directly.
    bool merge_palette(uint8_t *merged, int *merged_size, const uint8_t *table, int entries,
                       int transparent, uint8_t *remap) {
      memset(remap, 0, 256);
      for(int i = 0; i < entries; i++) {
        if(i == transparent) continue;   // its colour goes unused, so it needn't fit
        const uint8_t *c = &table[i * 3];
        int found = -1;
        for(int j = 0; j < *merged_size; j++) {
          if(merged[j * 3] == c[0] && merged[j * 3 + 1] == c[1] && merged[j * 3 + 2] == c[2]) {
            found = j;
            break;
          }
        }
        if(found < 0) {
          if(*merged_size >= 256) return false;
          found = (*merged_size)++;
          merged[found * 3] = c[0];
          merged[found * 3 + 1] = c[1];
          merged[found * 3 + 2] = c[2];
        }
        remap[i] = (uint8_t)found;
      }
      return true;
    }

    void write_palette(image_t *sheet, const gif_scratch_t *scratch, int from, int to) {
      for(int i = from; i < to; i++) {
        const uint8_t *c = &scratch->palette[i * 3];
        sheet->palette((uint8_t)i, rgb_color_t(c[0], c[1], c[2], 255)._p);
      }
    }

  }

  gif_status_t gif_survey(gif_reader_t reader, gif_scratch_t *scratch, gif_info_t *info,
                          gif_frame_t *frames, int max_frames) {
    gif_read_t r = { reader, false };

    gif_header_t header;
    gif_status_t status = read_header(r, scratch, &header);
    if(status != GIF_OK) return status;

    memset(info, 0, sizeof(gif_info_t));
    info->width = header.width;
    info->height = header.height;

    // Graphic control state, which applies to the next image block and resets
    // once it has been used.
    uint8_t disposal = GIF_DISPOSE_NONE;
    int frame_transparent = -1;
    uint16_t delay = 0;

    int merged_size = header.palette_size;
    bool have_palette = header.palette_size != 0;
    int count = 0;
    bool done = false;

    while(!done && !r.truncated) {
      switch(r.u8()) {
        case GIF_BLOCK_EXTENSION: {
          if(r.u8() == GIF_EXT_GRAPHIC_CONTROL) {
            if(r.u8() != 4) return GIF_BAD_DATA;   // fixed block size
            uint8_t packed = r.u8();
            uint32_t ms = (uint32_t)r.u16() * 10;  // the file counts hundredths
            uint8_t index = r.u8();
            r.u8();                                // block terminator
            delay = (uint16_t)(ms > 65535 ? 65535 : ms);
            disposal = (packed >> 2) & 0x07;
            frame_transparent = (packed & 0x01) ? index : -1;
          } else {
            r.skip_sub_blocks();
          }
        } break;

        case GIF_BLOCK_IMAGE: {
          gif_frame_t frame;
          frame.x = r.u16();
          frame.y = r.u16();
          frame.w = r.u16();
          frame.h = r.u16();
          uint8_t packed = r.u8();
          frame.interlaced = (packed & 0x40) != 0;
          frame.local_palette = (packed & 0x80) != 0;
          frame.disposal = disposal;
          frame.transparent = (int16_t)frame_transparent;
          frame.delay = delay;

          if(frame.local_palette) {
            uint16_t entries = (uint16_t)(2 << (packed & 0x07));
            r.bytes(scratch->local, (size_t)entries * 3);
            if(!have_palette) {
              // No global table: the first frame's own becomes the sheet's base.
              memcpy(scratch->palette, scratch->local, (size_t)entries * 3);
              merged_size = entries;
              have_palette = true;
            } else if(!merge_palette(scratch->palette, &merged_size, scratch->local, entries,
                                     frame_transparent, scratch->remap)) {
              info->needs_true_color = true;
            }
          }

          r.u8();   // LZW minimum code size
          r.skip_sub_blocks();

          if(frame.w == 0 || frame.h == 0) return GIF_BAD_DATA;
          // Nothing precedes the first frame, so its dispose-to-previous has
          // nothing to put back and needs no buffer.
          if(frame.disposal == GIF_DISPOSE_PREVIOUS && count > 0) {
            uint32_t bytes = (uint32_t)frame.w * frame.h;
            if(bytes > info->restore_bytes) info->restore_bytes = bytes;
          }
          if(frames && count < max_frames) frames[count] = frame;
          if(count == 65535) return GIF_BAD_DATA;
          count++;

          disposal = GIF_DISPOSE_NONE;
          frame_transparent = -1;
          delay = 0;
        } break;

        case GIF_BLOCK_TRAILER: done = true; break;

        default: return GIF_BAD_DATA;
      }
    }

    if(r.truncated) return GIF_TRUNCATED;
    if(count == 0) return GIF_BAD_DATA;
    if(merged_size == 0) return GIF_BAD_DATA;   // no colours anywhere

    info->frame_count = (uint16_t)count;
    info->palette_size = (uint16_t)merged_size;
    // Held to the table: the index is a byte from the file and the table may be
    // far shorter, and this one is painted, not just compared against.
    info->background_index = header.background < merged_size ? header.background : 0;

    // One index has to mean "nothing here", for the initial fill and for a frame
    // that disposes to background. An index a frame declared transparent looks
    // like a free one to reuse, and isn't: another frame may declare a different
    // one, or none, and paint that colour meaning it.
    if(merged_size < 256) {
      info->clear_index = (uint8_t)merged_size;
      info->palette_size++;
    } else {
      // A full table: nothing can be cleared, and with 256 opaque colours in use
      // nothing much needs to be. The background index addresses the base table,
      // which is the file's own only when it declared one.
      info->clear_index = have_palette && header.palette_size ? header.background : 0;
      info->clear_is_opaque = true;
    }

    if(frames && count > max_frames) return GIF_TOO_MANY_FRAMES;
    return GIF_OK;
  }

  gif_status_t gif_decode(gif_reader_t reader, gif_scratch_t *scratch, const gif_info_t *info,
                          image_t *sheet) {
    if(info->needs_true_color) return GIF_UNSUPPORTED;
    if(!sheet || !sheet->has_palette()) return GIF_NO_BUFFER;
    if(sheet->palette_size() < info->palette_size) return GIF_NO_BUFFER;

    const int cell_w = info->width, cell_h = info->height;
    rect_t bounds = sheet->bounds();
    if((int)bounds.w != cell_w * (int)info->frame_count || (int)bounds.h != cell_h) return GIF_NO_BUFFER;
    if(info->restore_bytes > 0 &&
       (scratch->restore == nullptr || scratch->restore_size < info->restore_bytes)) return GIF_NO_BUFFER;

    gif_read_t r = { reader, false };
    gif_header_t header;
    gif_status_t status = read_header(r, scratch, &header);
    if(status != GIF_OK) return status;
    if(header.width != info->width || header.height != info->height) return GIF_BAD_DATA;

    // The colour table is rebuilt exactly as the survey built it - the same file
    // walked the same way - so a frame's indices mean the same thing in both.
    int merged_size = header.palette_size;
    bool have_palette = header.palette_size != 0;
    if(have_palette) write_palette(sheet, scratch, 0, merged_size);
    if(!info->clear_is_opaque) sheet->palette(info->clear_index, 0);

    const size_t stride = sheet->row_stride();
    const int max_index = info->palette_size - 1;

    // The previous frame's disposal is applied to the next frame's cell, and
    // `saved` is what a dispose-to-previous frame put aside on its way in.
    int previous_disposal = GIF_DISPOSE_NONE;
    int previous_transparent = -1;
    clipped_t previous_rect = { 0, 0, 0, 0 };
    clipped_t saved = { 0, 0, 0, 0 };

    int transparent = -1;
    uint8_t disposal = GIF_DISPOSE_NONE;
    int frame_index = 0;
    bool done = false;

    while(!done && !r.truncated) {
      switch(r.u8()) {
        case GIF_BLOCK_EXTENSION: {
          if(r.u8() == GIF_EXT_GRAPHIC_CONTROL) {
            if(r.u8() != 4) return GIF_BAD_DATA;
            uint8_t packed = r.u8();
            r.u16();                    // delay, collected by the survey
            uint8_t index = r.u8();
            r.u8();                     // block terminator
            disposal = (packed >> 2) & 0x07;
            transparent = (packed & 0x01) ? index : -1;
          } else {
            r.skip_sub_blocks();
          }
        } break;

        case GIF_BLOCK_IMAGE: {
          if(frame_index >= (int)info->frame_count) return GIF_BAD_DATA;

          int x = r.u16(), y = r.u16(), w = r.u16(), h = r.u16();
          uint8_t packed = r.u8();
          bool interlaced = (packed & 0x40) != 0;
          const uint8_t *remap = nullptr;
          if(packed & 0x80) {
            uint16_t entries = (uint16_t)(2 << (packed & 0x07));
            r.bytes(scratch->local, (size_t)entries * 3);
            if(!have_palette) {
              // No global table: the sheet takes its base from the first frame
              // that carries one, so its indices need no remapping.
              memcpy(scratch->palette, scratch->local, (size_t)entries * 3);
              merged_size = entries;
              write_palette(sheet, scratch, 0, merged_size);
              have_palette = true;
            } else {
              int before = merged_size;
              if(!merge_palette(scratch->palette, &merged_size, scratch->local, entries,
                                transparent, scratch->remap)) {
                return GIF_UNSUPPORTED;
              }
              write_palette(sheet, scratch, before, merged_size);
              remap = scratch->remap;
            }
          }
          if(!have_palette) return GIF_BAD_DATA;
          if(r.truncated) return GIF_TRUNCATED;

          uint8_t *cell = (uint8_t *)sheet->ptr(frame_index * cell_w, 0);

          // Cell n is the finished canvas for frame n, so it starts as cell n-1
          // with that frame's disposal applied. Nothing else has to be kept.
          if(frame_index == 0) {
            for(int row = 0; row < cell_h; row++) memset(cell + row * stride, info->clear_index, cell_w);
          } else {
            const uint8_t *previous = (const uint8_t *)sheet->ptr((frame_index - 1) * cell_w, 0);
            for(int row = 0; row < cell_h; row++) {
              memcpy(cell + row * stride, previous + row * stride, cell_w);
            }
            if(previous_disposal == GIF_DISPOSE_BACKGROUND) {
              // "Restore to background" means the screen's background colour,
              // and only means transparent when the frame declared an index for
              // it - which is how an animation that clears back to nothing is
              // actually written.
              uint8_t fill = previous_transparent >= 0 && !info->clear_is_opaque
                           ? info->clear_index : info->background_index;
              for(int row = 0; row < previous_rect.h; row++) {
                memset(cell + (previous_rect.y + row) * stride + previous_rect.x,
                       fill, previous_rect.w);
              }
            } else if(previous_disposal == GIF_DISPOSE_PREVIOUS && frame_index > 1) {
              // Nothing precedes the first frame, so its dispose-to-previous has
              // nothing to put back and is left alone.
              for(int row = 0; row < saved.h; row++) {
                memcpy(cell + (saved.y + row) * stride + saved.x,
                       scratch->restore + (size_t)row * saved.w, saved.w);
              }
            }
          }

          clipped_t rect = clip_to_canvas(x, y, w, h, cell_w, cell_h);
          // Saved only when it could be put back, which is the same condition
          // the survey sized the buffer against.
          if(disposal == GIF_DISPOSE_PREVIOUS && frame_index > 0) {
            saved = rect;
            for(int row = 0; row < rect.h; row++) {
              memcpy(scratch->restore + (size_t)row * rect.w,
                     cell + (rect.y + row) * stride + rect.x, rect.w);
            }
          }

          frame_writer_t writer = {
            cell, stride, cell_w, cell_h, x, y, w, h,
            interlaced, transparent, remap, max_index, 0, 0, 0
          };
          status = decode_frame(r, scratch, writer);
          if(status != GIF_OK) return status;

          previous_disposal = disposal;
          previous_transparent = transparent;
          previous_rect = rect;
          frame_index++;

          disposal = GIF_DISPOSE_NONE;
          transparent = -1;
        } break;

        case GIF_BLOCK_TRAILER: done = true; break;

        default: return GIF_BAD_DATA;
      }
    }

    if(r.truncated) return GIF_TRUNCATED;
    if(frame_index != (int)info->frame_count) return GIF_BAD_DATA;
    return GIF_OK;
  }

  gif_status_t gif_survey(const uint8_t *data, size_t size, gif_scratch_t *scratch,
                          gif_info_t *info, gif_frame_t *frames, int max_frames) {
    memory_source_t source = { data, size, 0 };
    gif_reader_t reader = { memory_read, memory_rewind, &source };
    return gif_survey(reader, scratch, info, frames, max_frames);
  }

  gif_status_t gif_decode(const uint8_t *data, size_t size, gif_scratch_t *scratch,
                          const gif_info_t *info, image_t *sheet) {
    memory_source_t source = { data, size, 0 };
    gif_reader_t reader = { memory_read, memory_rewind, &source };
    return gif_decode(reader, scratch, info, sheet);
  }

}
