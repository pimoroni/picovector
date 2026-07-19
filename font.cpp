#include <algorithm>

#include "font.hpp"
#include "image.hpp"
#include "picovector.hpp"
#include "rasteriser.hpp" // render_begin / render_add_path / render_flush
#include "brush.hpp"
#include "mat3.hpp"

using std::sort;

namespace picovector {

  vec2_t glyph_path_point_t::transform(mat3_t *transform) {
    return vec2_t(
      transform->v00 * float(x) + transform->v01 * float(y) + transform->v02,
      transform->v10 * float(x) + transform->v11 * float(y) + transform->v12
    );
  }

  rect_t glyph_t::bounds(mat3_t *transform) {
    vec2_t p1(x, -y);
    vec2_t p2(x + w, -y);
    vec2_t p3(x + w, -y - h);
    vec2_t p4(x, -y);

    p1 = p1.transform(transform);
    p2 = p2.transform(transform);
    p3 = p3.transform(transform);
    p4 = p4.transform(transform);

    float minx = min(p1.x, min(p2.x, min(p3.x, p4.x)));
    float miny = min(p1.y, min(p2.y, min(p3.y, p4.y)));
    float maxx = max(p1.x, max(p2.x, max(p3.x, p4.x)));
    float maxy = max(p1.y, max(p2.y, max(p3.y, p4.y)));

    return rect_t(minx, miny, ceil(maxx) - minx, ceil(maxy) - miny);
  }

  static uint16_t get_utf8_char(const char *text, const char *end);
  static inline uint8_t utf8_seq_len(uint8_t b0);

  rect_t font_t::measure(image_t *target, const char *text, float size) {
    (void)target;
    float x = 0.0f, max_w = 0.0f;
    int lines = 1;
    const float s = size / 128.0f;

    // Walk UTF-8 codepoints so widths match draw() for non-ASCII glyphs.
    const char *p = text;
    const char *end = text + strlen(text);
    while(p != end) {
      uint16_t cp = get_utf8_char(p, end);
      if(cp == '\n') { if(x > max_w) max_w = x; x = 0.0f; lines++; p += 1; continue; }
      if(cp == '\r') { p += 1; continue; }
      for(int j = 0; j < this->glyph_count; j++) {
        if(this->glyphs[j].codepoint == cp) {
          x += float(this->glyphs[j].advance) * s;
          break;
        }
      }
      uint8_t len = utf8_seq_len(*p);
      p += len ? len : 1;  // never stall on a malformed lead byte
    }
    if(x > max_w) max_w = x;

    rect_t r = { 0, 0, max_w, size * lines };
    return r;
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

  // Scratch for converting one glyph contour's compact int8 points to vec2_t
  // before handing them to the geometry-agnostic renderer.
  static vec2_t glyph_point_buf[256];

  // Draw a single glyph through the retained renderer (begin / add_path / flush).
  // This is the old render_glyph, hoisted out of picovector so the renderer
  // stays agnostic of the font's point representation.
  static void draw_glyph(glyph_t *glyph, image_t *target, mat3_t *transform, brush_t *brush) {
    if(!glyph->path_count) return;

    render_begin();
    for(int i = 0; i < glyph->path_count; i++) {
      glyph_path_t &path = glyph->paths[i];
      int count = path.point_count;
      if(count < 2) continue;
      if(count > (int)(sizeof(glyph_point_buf) / sizeof(glyph_point_buf[0]))) continue; // too detailed to fit
      for(int k = 0; k < count; k++) {
        glyph_point_buf[k] = vec2_t((float)path.points[k].x, (float)path.points[k].y);
      }
      render_add_path(glyph_point_buf, count, transform);
    }
    render_flush(target, brush);
  }

  void font_t::draw(image_t *target, const char *text, float size) {
    // Draw from the image's text caret, advancing it per glyph and honouring
    // '\n' (return to origin_x, drop one line). image.text() sets up the caret
    // (x, y, origin_x, valid) before calling; line_height for a vector font is
    // the point size.
    text_cursor_t *c = target->text_cursor_state();
    c->line_height = size;
    const float s = size / 128.0f;

    // A glyph's compact int8 contour is drawn through `transform`, which places
    // the baseline at (x, y) and scales the 128-unit em to `size`. It's rebuilt
    // from the caret whenever the position jumps (start / newline).
    auto build_transform = [size, s](float x, float y) {
      mat3_t t;
      t = t.translate(x, y);
      t = t.translate(0, size);
      t = t.scale(s, s);
      return t;
    };
    mat3_t transform = build_transform(c->x, c->y);

    const char *end = text + strlen(text);

    while(text != end) {
      uint16_t codepoint = get_utf8_char(text, end);

      if(codepoint == '\n') {
        c->x = c->origin_x;
        c->y += c->line_height;
        transform = build_transform(c->x, c->y);
        text += 1;
        continue;
      }
      if(codepoint == '\r') { text += 1; continue; }

      // find the glyph
      for(int j = 0; j < this->glyph_count; j++) {
        if(this->glyphs[j].codepoint == codepoint) {
          draw_glyph(&this->glyphs[j], target, &transform, target->brush());
          float a = this->glyphs[j].advance;
          transform = transform.translate(a, 0);
          c->x += a * s;
          break;
        }
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
