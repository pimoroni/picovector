#include <algorithm>

#include "pixel_font.hpp"
#include "image.hpp"
#include "picovector.hpp"
#include "brush.hpp"
#include "mat3.hpp"

using std::min;
using std::max;

namespace picovector {

  static inline uint8_t utf8_seq_len(uint8_t b0); // defined below; used by measure() above its definition

  int pixel_font_t::glyph_index(int codepoint) {

    uint32_t low = 0;
    uint32_t high = this->glyph_count;

    while(low < high) {
      uint32_t mid = low + (high - low) / 2;
      uint32_t compare = this->glyphs[mid].codepoint;
      if (compare == (uint32_t)codepoint) {
        return mid;
      }

      if(compare < (uint32_t)codepoint) {
        low = mid + 1;
      } else {
        high = mid;
      }
    }

    return -1;  // not found
  }

  static uint16_t get_utf8_char(const char *text, const char *end);
  static inline uint8_t utf8_seq_len(uint8_t b0);

  rect_t pixel_font_t::measure(image_t *target, const char *text, int scale) {
    return this->measure(target, text, text + strlen(text), scale);
  }

  rect_t pixel_font_t::measure(image_t *target, const char *text, const char *end, int scale) {
    (void)target;
    if(scale < 1) scale = 1;
    float x = 0, max_w = 0;
    int lines = 1;

    // Walk UTF-8 codepoints so widths match draw() for non-ASCII glyphs.
    while(text < end) {
      if(*text == '\n') { if(x > max_w) max_w = x; x = 0; lines++; text++; continue; }
      if(*text == '\r') { text++; continue; }
      // special case for "space"
      if(*text == 32) {
        x += (this->width / 3) * scale;
        text++;
        continue;
      }

      int glyph_index = this->glyph_index(get_utf8_char(text, end));
      if(glyph_index != -1) {
        x += (this->glyphs[glyph_index].width + 1) * scale;
      }

      uint8_t len = utf8_seq_len(*text);
      text += len ? len : 1;  // never stall on a malformed lead byte
    }
    if(x > max_w) max_w = x;

    return rect_t(0, 0, max_w, this->height * scale * lines);
  }

  void pixel_font_t::draw_glyph(image_t *target, const pixel_font_glyph_t *glyph, uint8_t *data, brush_t *brush, const rect_t &bounds, int x, int y, int scale) {
    if(scale < 1) scale = 1;
    uint32_t bytes_per_row = (this->width + 7) >> 3;

    _reset_spans();

    // bounds is the target clip rect. The span func writes without clipping, so
    // we clip each emitted run here. Walk source pixels (not dest), coalescing
    // horizontal runs of set bits into a single scale-wide span per dest row —
    // fewer span-func calls than one per lit pixel, at any scale.
    int bx0 = int(bounds.x), bx1 = int(bounds.x + bounds.w);
    int by0 = int(bounds.y), by1 = int(bounds.y + bounds.h);

    for(int gy = 0; gy < int(this->height); gy++) {
      uint8_t *row = data + gy * bytes_per_row;
      int dy = y + gy * scale;
      if(dy + scale <= by0 || dy >= by1) continue; // whole source row off-screen

      int gx = 0;
      while(gx < int(glyph->width)) {
        if(row[gx >> 3] & (0x80u >> (gx & 7))) {
          // extend the run over consecutive set bits
          int run = 1;
          while(gx + run < int(glyph->width) &&
                (row[(gx + run) >> 3] & (0x80u >> ((gx + run) & 7)))) run++;

          // dest span for this run, clipped to bounds in x
          int dx0 = x + gx * scale;
          int cx0 = dx0 < bx0 ? bx0 : dx0;
          int cx1 = (dx0 + run * scale) > bx1 ? bx1 : (dx0 + run * scale);
          if(cx1 > cx0) {
            for(int r = 0; r < scale; r++) {          // scale dest rows per source row
              int ry = dy + r;
              if(ry < by0 || ry >= by1) continue;
              _add_span(cx0, ry, cx1 - cx0);
            }
          }
          gx += run;
        } else {
          gx++;
        }
      }
    }
    _blend_spans(target, brush);
  }

  static uint16_t get_utf8_char(const char *text, const char *end) {
    uint16_t codepoint;
    if((*text & 0x80) == 0x00) {
      codepoint = *text; // ASCII, codepoints U+0000...U007F
    }
    else if( ((*text & 0xE0) == 0xC0) && (text+1 <= end) && ((*(text+1) & 0xC0) == 0x80) ) {
      codepoint = ((uint16_t)(*text & 0x1F) << 6) + (*(text+1) & 0x3F); //codepoints U+0080...U+07FF
    }
    else if( ((*text & 0xF0) == 0xE0) && (text+2 <= end) && ((*(text+1) & 0xC0) == 0x80) && ((*(text+2) & 0xC0) == 0x80) ) {
      codepoint = ((uint16_t)(*text & 0x0F) << 12) + ((uint16_t)(*(text+1) & 0x3F) << 6) + (*(text+2) & 0x3F); // codepoints U+0800...U+FFFF
    }
    else {
      codepoint = 0xFFFF; // malformed UTF-8 sequences or unsupported codepoints starting at U+10000
    }
    return codepoint;
  }

  static inline uint8_t utf8_seq_len(uint8_t b0) {
    if ((b0 & 0x80) == 0x00) return 1;
    if ((b0 & 0xE0) == 0xC0) return 2;
    if ((b0 & 0xF0) == 0xE0) return 3;
    if ((b0 & 0xF8) == 0xF0) return 4;
    return 0; // invalid
  }

  void pixel_font_t::draw(image_t *target, const char *text, int scale) {
    this->draw(target, text, text + strlen(text), scale);
  }

  void pixel_font_t::draw(image_t *target, const char *text, const char *end, int scale) {
    if(scale < 1) scale = 1;

    // Draw from the image's text caret, advancing it per glyph and honouring
    // '\n' (return to origin_x, drop one line). image.text() sets up the caret
    // (x, y, origin_x, valid) before calling; line_height is height * scale.
    text_cursor_t *c = target->text_cursor_state();
    c->line_height = this->height * scale;

    // Coarse whole-text visibility gate: if the multi-line box doesn't meet the
    // clip at all we still advance the caret but skip the per-glyph draws.
    rect_t text_bounds = this->measure(target, text, end, scale);
    text_bounds.x = c->x;
    text_bounds.y = c->y;
    bool visible = text_bounds.intersects(target->clip());

    rect_t bounds = target->clip();
    brush_t *brush = target->brush();

    while(text < end) {
      if(*text == '\n') {
        c->x = c->origin_x;
        c->y += c->line_height;
        text++;
        continue;
      }
      if(*text == '\r') { text++; continue; }
      // special case for "space"
      if(*text == 32) {
        c->x += (this->width / 3) * scale;
        text++;
        continue;
      }

      int codepoint = get_utf8_char(text, end);
      int glyph_index = this->glyph_index(codepoint);
      if(glyph_index != -1) {
        pixel_font_glyph_t *glyph = &this->glyphs[glyph_index];
        uint8_t *data = &this->glyph_data[this->glyph_data_size * glyph_index];

        if(visible) {
          draw_glyph(target, glyph, data, brush, bounds, (int)c->x, (int)c->y, scale);
        }

        c->x += (glyph->width + 1) * scale;
      }

      uint8_t len = utf8_seq_len(*text);
      text += len ? len : 1;  // never stall on a malformed lead byte
    }

    // Advance the caret as if the text ended with a newline, so a following
    // text() with no position starts on the next line.
    c->x = c->origin_x;
    c->y += c->line_height;
  }

}