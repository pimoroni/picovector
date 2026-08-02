// The GIF container: the block walk, LZW, interlace, and the frame-to-frame
// compositing that turns a delta format into a spritesheet.

#include <vector>

#include "test.hpp"
#include "picovector.hpp"
#include "gif.hpp"
#include "image.hpp"
#include "fixture_gif.hpp"

using namespace picovector;

namespace {

  // Packs codes into a GIF LZW stream, mirroring the decoder's table
  // bookkeeping so the code width changes at the same points. It only ever
  // emits literals, which compresses nothing but exercises everything about the
  // stream except multi-byte dictionary strings.
  struct lzw_writer_t {
    std::vector<uint8_t> packed;
    uint32_t bits = 0;
    int bit_count = 0;
    int min_code_size;
    int width;
    int next_code;
    bool have_previous = false;

    explicit lzw_writer_t(int min_code_size) : min_code_size(min_code_size) {
      width = min_code_size + 1;
      next_code = (1 << min_code_size) + 2;
      put(1 << min_code_size);   // a stream opens with a clear
    }

    void put(int code) {
      bits |= (uint32_t)code << bit_count;
      bit_count += width;
      while(bit_count >= 8) {
        packed.push_back((uint8_t)(bits & 0xff));
        bits >>= 8;
        bit_count -= 8;
      }
    }

    void literal(int value) {
      put(value);
      if(have_previous && next_code < 4096) {
        next_code++;
        if(next_code == (1 << width) && width < 12) width++;
      }
      have_previous = true;
    }

    void clear() {
      put(1 << min_code_size);
      width = min_code_size + 1;
      next_code = (1 << min_code_size) + 2;
      have_previous = false;
    }

    std::vector<uint8_t> finish() {
      put((1 << min_code_size) + 1);   // end of information
      if(bit_count > 0) packed.push_back((uint8_t)(bits & 0xff));
      bit_count = 0;
      return packed;
    }
  };

  // Builds a GIF in memory, so the tests don't depend on a file.
  struct gif_builder_t {
    std::vector<uint8_t> out;

    void u8(int v) { out.push_back((uint8_t)v); }
    void u16(int v) { u8(v & 0xff); u8((v >> 8) & 0xff); }

    // Deterministic colours, so a local table can be built to match the global
    // one exactly or to differ from it.
    void colours(int entries, int bias = 0) {
      for(int i = 0; i < entries; i++) { u8(i + bias); u8(i * 2); u8(i * 3); }
    }

    // gct_bits < 0 builds a file with no global colour table.
    explicit gif_builder_t(int w = 8, int h = 4, int gct_bits = 1) {
      const char marker[6] = { 'G', 'I', 'F', '8', '9', 'a' };
      out.insert(out.end(), marker, marker + 6);
      u16(w); u16(h);
      u8(gct_bits < 0 ? 0x00 : (0x80 | gct_bits));
      u8(0);   // background index
      u8(0);   // pixel aspect ratio
      if(gct_bits >= 0) colours(2 << gct_bits);
    }

    void graphic_control(int delay_hundredths, int disposal, int transparent) {
      u8(0x21); u8(0xf9); u8(4);
      u8((disposal << 2) | (transparent >= 0 ? 1 : 0));
      u16(delay_hundredths);
      u8(transparent >= 0 ? transparent : 0);
      u8(0);
    }

    void comment(const char *text) {
      u8(0x21); u8(0xfe);
      size_t len = strlen(text);
      u8((int)len);
      out.insert(out.end(), text, text + len);
      u8(0);
    }

    void descriptor(int x, int y, int w, int h, int lct_bits, bool interlaced, int lct_bias) {
      u8(0x2c);
      u16(x); u16(y); u16(w); u16(h);
      u8((lct_bits >= 0 ? (0x80 | lct_bits) : 0) | (interlaced ? 0x40 : 0));
      if(lct_bits >= 0) colours(2 << lct_bits, lct_bias);
    }

    void sub_blocks(const std::vector<uint8_t> &data) {
      size_t pos = 0;
      while(pos < data.size()) {
        size_t n = data.size() - pos;
        if(n > 255) n = 255;
        u8((int)n);
        out.insert(out.end(), data.begin() + pos, data.begin() + pos + n);
        pos += n;
      }
      u8(0);
    }

    // A frame whose payload is filler: enough for the survey, which only follows
    // the sub-block lengths.
    void image(int x, int y, int w, int h, int payload = 4, int lct_bits = -1,
               bool interlaced = false, int lct_bias = 0) {
      descriptor(x, y, w, h, lct_bits, interlaced, lct_bias);
      u8(8);   // LZW minimum code size
      std::vector<uint8_t> data((size_t)payload, 0x00);
      sub_blocks(data);
    }

    // A frame carrying real pixels, one index per entry of `indices`.
    void pixels(int x, int y, int w, int h, const std::vector<uint8_t> &indices,
                int min_code_size = 4, bool interlaced = false, int clear_every = 0) {
      descriptor(x, y, w, h, -1, interlaced, 0);
      u8(min_code_size);
      lzw_writer_t writer(min_code_size);
      for(size_t i = 0; i < indices.size(); i++) {
        if(clear_every > 0 && i > 0 && (i % clear_every) == 0) writer.clear();
        writer.literal(indices[i]);
      }
      sub_blocks(writer.finish());
    }

    void trailer() { u8(0x3b); }
  };

  gif_scratch_t &scratch() {
    static gif_scratch_t s;
    return s;
  }

  // A deterministic PRNG, so a failure here is reproducible rather than a story
  // about one unlucky CI run.
  uint32_t rnd_state = 6180339;
  uint32_t rnd() {
    rnd_state = rnd_state * 1664525u + 1013904223u;
    return rnd_state >> 8;
  }

  gif_status_t survey(const gif_builder_t &b, gif_info_t *info,
                      gif_frame_t *frames = nullptr, int max_frames = 0) {
    scratch().restore = nullptr;
    scratch().restore_size = 0;
    return gif_survey(b.out.data(), b.out.size(), &scratch(), info, frames, max_frames);
  }

  // A decoded sheet, with the sizing the survey asked for.
  struct sheet_t {
    gif_info_t info;
    std::vector<uint8_t> restore;
    image_t image;
    gif_status_t status;

    explicit sheet_t(const gif_builder_t &b) : image() {
      status = survey(b, &info);
      if(status != GIF_OK) return;
      restore.assign(info.restore_bytes ? info.restore_bytes : 1, 0);
      scratch().restore = restore.data();
      scratch().restore_size = restore.size();
      new (&image) image_t((int)info.width * info.frame_count, (int)info.height,
                           1, (int)info.frame_count, RGBA8888, true, info.palette_size);
      status = gif_decode(b.out.data(), b.out.size(), &scratch(), &info, &image);
    }

    // The index stored for frame `frame` at (x, y).
    uint8_t at(int frame, int x, int y) {
      return ((uint8_t *)image.ptr(frame * (int)info.width + x, y))[0];
    }
  };

}

void test_gif() {
  printf("gif: a file that isn't one is handed back, not diagnosed\n");
  {
    gif_info_t info;
    gif_builder_t png;
    png.out = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
    CHECK(survey(png, &info) == GIF_BAD_MAGIC);

    gif_builder_t truncated_marker;
    truncated_marker.out = { 'G', 'I', 'F' };
    CHECK(survey(truncated_marker, &info) == GIF_BAD_MAGIC);

    gif_builder_t wrong_version;
    wrong_version.out = { 'G', 'I', 'F', '8', '5', 'a' };
    CHECK(survey(wrong_version, &info) == GIF_BAD_MAGIC);
  }

  printf("gif: the survey sizes what the caller has to allocate\n");
  {
    gif_builder_t b(16, 8, 1);   // 4 colours
    b.image(0, 0, 16, 8);
    b.trailer();

    gif_info_t info;
    CHECK(survey(b, &info) == GIF_OK);
    CHECK(info.width == 16);
    CHECK(info.height == 8);
    CHECK(info.frame_count == 1);
    // No frame declared transparency, so a clear entry is appended to the four.
    CHECK(info.palette_size == 5);
    CHECK(info.clear_index == 4);
    CHECK(info.clear_is_opaque == false);
    CHECK(info.restore_bytes == 0);
    CHECK(info.needs_true_color == false);
  }

  printf("gif: the colour table is sized by the header's three bits\n");
  {
    for(int bits = 0; bits < 8; bits++) {
      gif_builder_t b(4, 4, bits);
      b.image(0, 0, 4, 4);
      b.trailer();

      gif_info_t info;
      CHECK(survey(b, &info) == GIF_OK);
      int entries = 2 << bits;
      // 256 entries leaves no room to append a clear index.
      CHECK_MSG(info.palette_size == (entries < 256 ? entries + 1 : 256), "palette size");
      CHECK_MSG(info.clear_is_opaque == (entries == 256), "clear entry");
    }
  }

  printf("gif: a graphic control block times and disposes the frame after it\n");
  {
    gif_builder_t b(8, 8, 2);
    b.graphic_control(7, GIF_DISPOSE_BACKGROUND, 3);
    b.image(1, 2, 4, 4);
    b.comment("skipped by length");
    b.graphic_control(0, GIF_DISPOSE_PREVIOUS, -1);
    b.image(0, 0, 8, 8);
    b.trailer();

    gif_info_t info;
    gif_frame_t frames[2];
    CHECK(survey(b, &info, frames, 2) == GIF_OK);
    CHECK(info.frame_count == 2);
    CHECK(frames[0].delay == 70);           // hundredths in the file, ms out
    CHECK(frames[0].disposal == GIF_DISPOSE_BACKGROUND);
    CHECK(frames[0].transparent == 3);
    CHECK(frames[0].x == 1 && frames[0].y == 2);
    CHECK(frames[0].w == 4 && frames[0].h == 4);
    // The control block applies to one frame only.
    CHECK(frames[1].delay == 0);
    CHECK(frames[1].transparent == -1);
    CHECK(frames[1].disposal == GIF_DISPOSE_PREVIOUS);
    // The clear index is appended rather than borrowed from the frame that
    // declared one: another frame may paint that index meaning its colour.
    CHECK(info.clear_index == 8);
    CHECK(info.palette_size == 9);
    // Sized for the largest rect any dispose-to-previous frame will save.
    CHECK(info.restore_bytes == 64);
  }

  printf("gif: a frame table too small still reports the true count\n");
  {
    gif_builder_t b(4, 4, 1);
    for(int i = 0; i < 5; i++) b.image(0, 0, 4, 4);
    b.trailer();

    gif_info_t info;
    gif_frame_t frames[2];
    CHECK(survey(b, &info, frames, 2) == GIF_TOO_MANY_FRAMES);
    CHECK(info.frame_count == 5);
    CHECK(survey(b, &info) == GIF_OK);   // no table, no complaint
    CHECK(info.frame_count == 5);
  }

  printf("gif: truncation is reported wherever it lands\n");
  {
    gif_builder_t b(8, 8, 2);
    b.graphic_control(5, GIF_DISPOSE_KEEP, -1);
    b.image(0, 0, 8, 8, 40);
    b.trailer();

    gif_info_t info;
    for(size_t cut = 6; cut < b.out.size(); cut++) {
      gif_builder_t part;
      part.out.assign(b.out.begin(), b.out.begin() + cut);
      gif_status_t status = survey(part, &info);
      CHECK_MSG(status == GIF_TRUNCATED || status == GIF_BAD_DATA, "truncated survey");
    }
  }

  printf("gif: structurally broken files are refused\n");
  {
    gif_info_t info;

    gif_builder_t no_frames(8, 8, 1);
    no_frames.trailer();
    CHECK(survey(no_frames, &info) == GIF_BAD_DATA);

    gif_builder_t bad_block(8, 8, 1);
    bad_block.u8(0x7f);
    CHECK(survey(bad_block, &info) == GIF_BAD_DATA);

    gif_builder_t zero_rect(8, 8, 1);
    zero_rect.image(0, 0, 0, 4);
    zero_rect.trailer();
    CHECK(survey(zero_rect, &info) == GIF_BAD_DATA);

    gif_builder_t no_colours(8, 8, -1);
    no_colours.image(0, 0, 8, 8);
    no_colours.trailer();
    CHECK(survey(no_colours, &info) == GIF_BAD_DATA);
  }

  printf("gif: per-frame colour tables are folded into one\n");
  {
    gif_info_t info;

    // A local table repeating the global one adds nothing to it.
    gif_builder_t same(8, 8, 2);
    same.image(0, 0, 8, 8, 4, 2);
    same.trailer();
    CHECK(survey(same, &info) == GIF_OK);
    CHECK(info.needs_true_color == false);
    CHECK(info.palette_size == 9);         // eight colours, plus the clear entry

    // One that really differs contributes its colours to the sheet's table,
    // which is what keeps a frame-optimised file indexed.
    gif_builder_t different(8, 8, 2);
    different.image(0, 0, 8, 8, 4, 2, false, 9);
    different.trailer();
    CHECK(survey(different, &info) == GIF_OK);
    CHECK(info.needs_true_color == false);
    CHECK(info.palette_size == 17);        // eight and eight, plus the clear entry

    // Same colours, more of them: only the new ones cost anything.
    gif_builder_t sized_apart(8, 8, 2);
    sized_apart.image(0, 0, 8, 8, 4, 3);
    sized_apart.trailer();
    CHECK(survey(sized_apart, &info) == GIF_OK);
    CHECK(info.palette_size == 17);        // sixteen, the first eight shared

    // With no global table, the first frame's own becomes the sheet's base.
    gif_builder_t adopted(8, 8, -1);
    adopted.image(0, 0, 8, 8, 4, 2);
    adopted.image(0, 0, 8, 8, 4, 2);
    adopted.trailer();
    CHECK(survey(adopted, &info) == GIF_OK);
    CHECK(info.palette_size == 9);
    CHECK(info.needs_true_color == false);

    // Two full tables with nothing in common want 512 colours between them,
    // which is more than an index can reach.
    gif_builder_t overflowing(8, 8, 7);
    overflowing.image(0, 0, 8, 8, 4, 7, false, 1);
    overflowing.trailer();
    CHECK(survey(overflowing, &info) == GIF_OK);
    CHECK(info.needs_true_color == true);
  }

  printf("gif: a frame's own indices are remapped into the sheet's table\n");
  {
    // Frame 1 brings a table whose colours are new, so its index 1 has to land
    // somewhere else in the sheet's table and still resolve to its own colour.
    gif_builder_t b(2, 1, 1);
    b.pixels(0, 0, 2, 1, { 1, 2 });
    b.descriptor(0, 0, 2, 1, 1, false, 40);
    b.u8(4);
    lzw_writer_t writer(4);
    writer.literal(1);
    writer.literal(3);
    b.sub_blocks(writer.finish());
    b.trailer();

    sheet_t sheet(b);
    CHECK(sheet.status == GIF_OK);
    CHECK(sheet.info.palette_size == 9);   // four global, four local, one clear

    // The global table's own indices are untouched.
    CHECK(sheet.at(0, 0, 0) == 1);
    CHECK(sheet.image.palette(1) == rgb_color_t(1, 2, 3, 255)._p);
    // The local table's are not, but they still name their own colours.
    CHECK(sheet.at(1, 0, 0) != 1);
    CHECK(sheet.image.palette(sheet.at(1, 0, 0)) == rgb_color_t(41, 2, 3, 255)._p);
    CHECK(sheet.image.palette(sheet.at(1, 1, 0)) == rgb_color_t(43, 6, 9, 255)._p);
  }

  printf("gif: pixels survive the LZW round trip\n");
  {
    std::vector<uint8_t> indices;
    for(int i = 0; i < 8 * 4; i++) indices.push_back((uint8_t)(i % 13));

    gif_builder_t b(8, 4, 3);   // 16 colours
    b.pixels(0, 0, 8, 4, indices);
    b.trailer();

    sheet_t sheet(b);
    CHECK(sheet.status == GIF_OK);
    CHECK((int)sheet.image.bounds().w == 8);
    CHECK(sheet.image.has_palette());

    bool matched = true;
    for(int y = 0; y < 4; y++) {
      for(int x = 0; x < 8; x++) matched &= sheet.at(0, x, y) == indices[y * 8 + x];
    }
    CHECK(matched);

    // The palette arrives premultiplied, from the file's RGB triples.
    CHECK(sheet.image.palette(1) == rgb_color_t(1, 2, 3, 255)._p);
    CHECK(sheet.image.palette(sheet.info.clear_index) == 0);
  }

  printf("gif: the code width grows with the table, and holds when it is full\n");
  {
    // 4096 pixels at a minimum code size of 8 fills the table part way through,
    // after which the encoder keeps emitting 12-bit codes without ever clearing.
    std::vector<uint8_t> indices;
    for(int i = 0; i < 64 * 64; i++) indices.push_back((uint8_t)(i * 7));

    gif_builder_t b(64, 64, 7);
    b.pixels(0, 0, 64, 64, indices, 8);
    b.trailer();

    sheet_t sheet(b);
    CHECK(sheet.status == GIF_OK);
    bool matched = true;
    for(int y = 0; y < 64; y++) {
      for(int x = 0; x < 64; x++) matched &= sheet.at(0, x, y) == indices[y * 64 + x];
    }
    CHECK(matched);
  }

  printf("gif: a mid-stream clear resets the table\n");
  {
    std::vector<uint8_t> indices;
    for(int i = 0; i < 32 * 8; i++) indices.push_back((uint8_t)(i % 5));

    gif_builder_t b(32, 8, 2);
    b.pixels(0, 0, 32, 8, indices, 4, false, 17);
    b.trailer();

    sheet_t sheet(b);
    CHECK(sheet.status == GIF_OK);
    bool matched = true;
    for(int y = 0; y < 8; y++) {
      for(int x = 0; x < 32; x++) matched &= sheet.at(0, x, y) == indices[y * 32 + x];
    }
    CHECK(matched);
  }

  printf("gif: a code can be used in the step that defines it\n");
  {
    // The self-referential case: clear, literal 1, then the code that is about
    // to be defined, whose string is the previous one plus its own first byte.
    // Three pixels of index 1 come out.
    gif_builder_t b(3, 1, 1);
    b.descriptor(0, 0, 3, 1, -1, false, 0);
    b.u8(2);                       // minimum code size: clear 4, end 5
    lzw_writer_t writer(2);
    writer.put(1);                 // literal
    writer.put(6);                 // the code being defined this step
    writer.put(5);                 // end of information
    if(writer.bit_count > 0) writer.packed.push_back((uint8_t)(writer.bits & 0xff));
    b.sub_blocks(writer.packed);
    b.trailer();

    sheet_t sheet(b);
    CHECK(sheet.status == GIF_OK);
    CHECK(sheet.at(0, 0, 0) == 1);
    CHECK(sheet.at(0, 1, 0) == 1);
    CHECK(sheet.at(0, 2, 0) == 1);
  }

  printf("gif: interlaced rows land in the right order\n");
  {
    // One index per row, so a row that arrives out of order is obvious. The
    // passes are rows 0 and 8, then 4 and 12, then 2, 6, 10, 14, then the odds.
    const int order[16] = { 0, 8, 4, 12, 2, 6, 10, 14, 1, 3, 5, 7, 9, 11, 13, 15 };
    std::vector<uint8_t> indices;
    for(int i = 0; i < 16; i++) {
      for(int x = 0; x < 4; x++) indices.push_back((uint8_t)order[i]);
    }

    gif_builder_t b(4, 16, 3);
    b.pixels(0, 0, 4, 16, indices, 4, true);
    b.trailer();

    sheet_t sheet(b);
    CHECK(sheet.status == GIF_OK);
    bool matched = true;
    for(int y = 0; y < 16; y++) {
      for(int x = 0; x < 4; x++) matched &= sheet.at(0, x, y) == (uint8_t)y;
    }
    CHECK(matched);
  }

  printf("gif: a transparent pixel leaves what was underneath\n");
  {
    gif_builder_t b(4, 1, 2);
    b.pixels(0, 0, 4, 1, { 1, 2, 3, 4 });
    b.graphic_control(0, GIF_DISPOSE_KEEP, 7);
    b.pixels(0, 0, 4, 1, { 7, 7, 5, 7 });
    b.trailer();

    sheet_t sheet(b);
    CHECK(sheet.status == GIF_OK);
    CHECK(sheet.at(1, 0, 0) == 1);   // transparent: frame 0 shows through
    CHECK(sheet.at(1, 1, 0) == 2);
    CHECK(sheet.at(1, 2, 0) == 5);   // opaque: painted over
    CHECK(sheet.at(1, 3, 0) == 4);
  }

  printf("gif: disposal decides what the next frame starts from\n");
  {
    // Frame 0 paints the row. Frame 1 covers the middle two pixels and disposes
    // to background; frame 2 covers them again and disposes to previous; frame 3
    // shows what each left behind. Checked against what PIL and ffmpeg both make
    // of the same construction.
    gif_builder_t b(4, 1, 2);
    b.pixels(0, 0, 4, 1, { 1, 1, 1, 1 });
    b.graphic_control(0, GIF_DISPOSE_BACKGROUND, -1);
    b.pixels(1, 0, 2, 1, { 2, 2 });
    b.graphic_control(0, GIF_DISPOSE_PREVIOUS, -1);
    b.pixels(1, 0, 2, 1, { 3, 3 });
    b.graphic_control(0, GIF_DISPOSE_KEEP, -1);
    b.pixels(0, 0, 1, 1, { 4 });
    b.trailer();

    sheet_t sheet(b);
    CHECK(sheet.status == GIF_OK);
    CHECK(sheet.info.frame_count == 4);
    CHECK(sheet.info.restore_bytes == 2);

    // Each cell is the finished canvas for its frame.
    CHECK(sheet.at(0, 0, 0) == 1 && sheet.at(0, 3, 0) == 1);
    CHECK(sheet.at(1, 1, 0) == 2 && sheet.at(1, 0, 0) == 1);
    // Frame 1 disposed to background and declared no transparency, so frame 2
    // starts from the screen's background colour and paints over it.
    CHECK(sheet.at(2, 1, 0) == 3 && sheet.at(2, 2, 0) == 3);
    CHECK(sheet.at(2, 0, 0) == 1);
    // Frame 2 disposed to previous: the middle goes back to what it held before
    // frame 2 painted, which is frame 1's background fill, not frame 0's colour.
    CHECK(sheet.at(3, 0, 0) == 4);
    CHECK(sheet.at(3, 1, 0) == sheet.info.background_index);
    CHECK(sheet.at(3, 2, 0) == sheet.info.background_index);
    CHECK(sheet.at(3, 3, 0) == 1);
  }

  printf("gif: dispose-to-background clears to nothing only if asked to\n");
  {
    // The spec says a disposed area goes back to the background colour, and an
    // animation that means "back to nothing" says so with a transparent index.
    // Both PIL and ffmpeg read it this way.
    gif_builder_t opaque(4, 1, 1);
    opaque.graphic_control(0, GIF_DISPOSE_BACKGROUND, -1);
    opaque.pixels(0, 0, 4, 1, { 1, 1, 1, 1 });
    opaque.graphic_control(0, GIF_DISPOSE_KEEP, 3);
    opaque.pixels(0, 0, 4, 1, { 3, 3, 2, 2 });
    opaque.trailer();

    sheet_t a(opaque);
    CHECK(a.status == GIF_OK);
    CHECK(a.at(1, 0, 0) == a.info.background_index);
    CHECK(a.info.background_index == 0);
    CHECK(a.at(1, 2, 0) == 2);

    gif_builder_t transparent(4, 1, 1);
    transparent.graphic_control(0, GIF_DISPOSE_BACKGROUND, 1);
    transparent.pixels(0, 0, 4, 1, { 0, 0, 0, 0 });
    transparent.graphic_control(0, GIF_DISPOSE_KEEP, 3);
    transparent.pixels(0, 0, 4, 1, { 3, 3, 2, 2 });
    transparent.trailer();

    sheet_t t(transparent);
    CHECK(t.status == GIF_OK);
    CHECK(t.at(1, 0, 0) == t.info.clear_index);
    CHECK(t.image.palette(t.info.clear_index) == 0);
    CHECK(t.at(1, 2, 0) == 2);
  }

  printf("gif: the first frame has no previous to be restored to\n");
  {
    // A dispose-to-previous on frame 0 has nothing to put back, so it is left
    // alone and frame 1 composites over frame 0.
    gif_builder_t b(4, 1, 1);
    b.graphic_control(0, GIF_DISPOSE_PREVIOUS, -1);
    b.pixels(0, 0, 4, 1, { 1, 1, 1, 1 });
    b.graphic_control(0, GIF_DISPOSE_KEEP, 3);
    b.pixels(0, 0, 4, 1, { 3, 3, 2, 2 });
    b.trailer();

    sheet_t sheet(b);
    CHECK(sheet.status == GIF_OK);
    CHECK(sheet.info.restore_bytes == 0);   // and needs no buffer for it
    CHECK(sheet.at(1, 0, 0) == 1);
    CHECK(sheet.at(1, 2, 0) == 2);
  }

  printf("gif: an untouched pixel is transparent, not black\n");
  {
    gif_builder_t b(4, 2, 2);
    b.pixels(1, 0, 2, 1, { 1, 2 });
    b.trailer();

    sheet_t sheet(b);
    CHECK(sheet.status == GIF_OK);
    CHECK(sheet.at(0, 0, 0) == sheet.info.clear_index);
    CHECK(sheet.at(0, 0, 1) == sheet.info.clear_index);
    // Which is what makes it draw as nothing at all.
    CHECK(sheet.image.palette(sheet.info.clear_index) == 0);
  }

  printf("gif: the sheet is a spritesheet, so a frame is just a sub-view\n");
  {
    gif_builder_t b(4, 2, 2);
    b.pixels(0, 0, 4, 2, { 1, 1, 1, 1, 1, 1, 1, 1 });
    b.pixels(0, 0, 4, 2, { 2, 2, 2, 2, 2, 2, 2, 2 });
    b.trailer();

    sheet_t sheet(b);
    CHECK(sheet.status == GIF_OK);

    image_t frame = sheet.image.sprite(1, 0);
    CHECK((int)frame.bounds().w == 4);
    CHECK((int)frame.bounds().h == 2);
    CHECK(frame.palette_data() == sheet.image.palette_data());   // shared, not copied
    CHECK(((uint8_t *)frame.ptr(0, 0))[0] == 2);
    CHECK(((uint8_t *)frame.ptr(3, 1))[0] == 2);

    // The sheet knows how many frames it has, which is what bounds a loop.
    CHECK(sheet.image.cols() == 2);
    CHECK(sheet.image.rows() == 1);
    // A single frame is not itself a sheet, so it does not claim its parent's
    // grid - otherwise one sprite would report the whole animation's length.
    CHECK(frame.cols() == 1);
    CHECK(frame.rows() == 1);
    CHECK(sheet.image.window(rect_t(0, 0, 4, 2)).cols() == 1);
  }

  printf("gif: a frame hanging off the canvas is consumed, not written\n");
  {
    // The rect overhangs on both axes; the stream still carries every pixel of
    // it, and the ones with nowhere to go are dropped.
    std::vector<uint8_t> indices;
    for(int i = 0; i < 6 * 6; i++) indices.push_back(3);

    gif_builder_t b(4, 4, 2);
    b.pixels(2, 2, 6, 6, indices);
    b.trailer();

    const uint32_t guard = 0xdeadbeefu;
    gif_info_t info;
    CHECK(survey(b, &info) == GIF_OK);

    std::vector<uint32_t> arena(1024 + (size_t)info.width * info.height / 4 + 1024, guard);
    scratch().restore = nullptr;
    scratch().restore_size = 0;
    image_t sheet(&arena[1024], (int)info.width * info.frame_count, (int)info.height,
                  1, (int)info.frame_count, RGBA8888, true, info.palette_size);
    CHECK(gif_decode(b.out.data(), b.out.size(), &scratch(), &info, &sheet) == GIF_OK);

    bool guards_intact = true;
    for(size_t i = 0; i < 1024; i++) guards_intact &= arena[i] == guard;
    for(size_t i = arena.size() - 1024; i < arena.size(); i++) guards_intact &= arena[i] == guard;
    CHECK(guards_intact);

    CHECK(((uint8_t *)sheet.ptr(2, 2))[0] == 3);
    CHECK(((uint8_t *)sheet.ptr(3, 3))[0] == 3);
    CHECK(((uint8_t *)sheet.ptr(0, 0))[0] == info.clear_index);
  }

  printf("gif: an index past the colour table is clamped, not followed\n");
  {
    // Two colours plus a clear entry, and a stream naming index 12.
    gif_builder_t b(2, 1, 0);
    b.pixels(0, 0, 2, 1, { 12, 1 });
    b.trailer();

    sheet_t sheet(b);
    CHECK(sheet.status == GIF_OK);
    CHECK(sheet.at(0, 0, 0) < sheet.info.palette_size);
    CHECK(sheet.at(0, 1, 0) == 1);
  }

  printf("gif: decoding checks the sheet it was handed\n");
  {
    gif_builder_t b(4, 2, 2);
    b.pixels(0, 0, 4, 2, { 1, 1, 1, 1, 1, 1, 1, 1 });
    b.trailer();

    gif_info_t info;
    CHECK(survey(b, &info) == GIF_OK);
    scratch().restore = nullptr;
    scratch().restore_size = 0;

    image_t wrong_size(8, 8, RGBA8888, true, info.palette_size);
    CHECK(gif_decode(b.out.data(), b.out.size(), &scratch(), &info, &wrong_size) == GIF_NO_BUFFER);

    image_t not_indexed((int)info.width, (int)info.height);
    CHECK(gif_decode(b.out.data(), b.out.size(), &scratch(), &info, &not_indexed) == GIF_NO_BUFFER);

    image_t small_palette((int)info.width, (int)info.height, RGBA8888, true, 2);
    CHECK(gif_decode(b.out.data(), b.out.size(), &scratch(), &info, &small_palette) == GIF_NO_BUFFER);

    // A dispose-to-previous frame needs somewhere to put the rect it saves.
    gif_builder_t restores(4, 2, 2);
    restores.pixels(0, 0, 4, 2, { 1, 1, 1, 1, 1, 1, 1, 1 });
    restores.graphic_control(0, GIF_DISPOSE_PREVIOUS, -1);
    restores.pixels(0, 0, 4, 2, { 2, 2, 2, 2, 2, 2, 2, 2 });
    restores.pixels(0, 0, 4, 2, { 3, 3, 3, 3, 3, 3, 3, 3 });
    restores.trailer();
    CHECK(survey(restores, &info) == GIF_OK);
    CHECK(info.restore_bytes == 8);
    image_t sheet((int)info.width * info.frame_count, (int)info.height,
                  1, (int)info.frame_count, RGBA8888, true, info.palette_size);
    CHECK(gif_decode(restores.out.data(), restores.out.size(), &scratch(), &info, &sheet) == GIF_NO_BUFFER);
  }

  printf("gif: a truncated stream doesn't yield half a frame\n");
  {
    std::vector<uint8_t> indices((size_t)16 * 16, 5);
    gif_builder_t b(16, 16, 3);
    b.pixels(0, 0, 16, 16, indices);
    b.trailer();

    gif_info_t info;
    CHECK(survey(b, &info) == GIF_OK);

    for(size_t cut = b.out.size() - 12; cut < b.out.size(); cut++) {
      gif_builder_t part;
      part.out.assign(b.out.begin(), b.out.begin() + cut);
      scratch().restore = nullptr;
      scratch().restore_size = 0;
      image_t sheet((int)info.width, (int)info.height, RGBA8888, true, info.palette_size);
      gif_status_t status = gif_decode(part.out.data(), part.out.size(), &scratch(), &info, &sheet);
      CHECK_MSG(status != GIF_OK, "truncated decode");
    }
  }

  printf("gif: a file wanting more than 256 colours can't share a sheet\n");
  {
    gif_builder_t b(8, 8, 7);
    b.image(0, 0, 8, 8, 4, 7, false, 1);
    b.trailer();

    gif_info_t info;
    CHECK(survey(b, &info) == GIF_OK);
    CHECK(info.needs_true_color == true);

    scratch().restore = nullptr;
    scratch().restore_size = 0;
    image_t sheet((int)info.width, (int)info.height, RGBA8888, true, info.palette_size);
    CHECK(gif_decode(b.out.data(), b.out.size(), &scratch(), &info, &sheet) == GIF_UNSUPPORTED);
  }

  printf("gif: a real encoder's output, pixel for pixel\n");
  {
    // Everything else here is built by a literal-only encoder, which never
    // exercises a multi-symbol dictionary entry. This one came out of PIL, and
    // the expected pixels are what PIL and ffmpeg both render it as.
    gif_info_t info;
    gif_frame_t frames[4];
    scratch().restore = nullptr;
    scratch().restore_size = 0;
    CHECK(gif_survey(fixture_gif, sizeof(fixture_gif), &scratch(), &info, frames, 4) == GIF_OK);
    CHECK(info.width == 10 && info.height == 8);
    CHECK(info.frame_count == 4);

    bool timed = true;
    for(int f = 0; f < 4; f++) timed &= frames[f].delay == fixture_delays[f];
    CHECK(timed);

    std::vector<uint8_t> restore(info.restore_bytes ? info.restore_bytes : 1, 0);
    scratch().restore = restore.data();
    scratch().restore_size = restore.size();
    image_t sheet((int)info.width * info.frame_count, (int)info.height,
                  1, (int)info.frame_count, RGBA8888, true, info.palette_size);
    CHECK(gif_decode(fixture_gif, sizeof(fixture_gif), &scratch(), &info, &sheet) == GIF_OK);

    bool matched = true;
    for(int f = 0; f < 4 && matched; f++) {
      for(int y = 0; y < 8; y++) {
        for(int x = 0; x < 10; x++) {
          uint8_t index = ((uint8_t *)sheet.ptr(f * (int)info.width + x, y))[0];
          rgb_color_t got = color_from_premul(sheet.palette(index));
          uint32_t packed = got.a() == 0 ? 0u
                          : ((uint32_t)got.a() << 24) | ((uint32_t)got.r() << 16)
                            | ((uint32_t)got.g() << 8) | got.b();
          uint32_t want = fixture_colours[fixture_pixels[(f * 8 + y) * 10 + x]];
          if(packed != want) {
            if(matched) printf("  frame %d (%d,%d): %08x, expected %08x\n", f, x, y, packed, want);
            matched = false;
          }
        }
      }
    }
    CHECK(matched);
  }

  printf("gif: one reader serves both passes\n");
  {
    // Everything else here goes through the memory overloads, which build a
    // fresh source per call and so always start at byte zero. An embedder with
    // an open file has one reader and hands it to both passes - the survey
    // leaves it at the end of the file, and decode has to get back to the start
    // by itself. Reading the file twice from one reader is the case the
    // bindings actually have, and nothing was covering it.
    gif_builder_t b(4, 2, 2);
    b.pixels(0, 0, 4, 2, { 1, 1, 1, 1, 2, 2, 2, 2 });
    b.pixels(0, 0, 4, 2, { 3, 3, 3, 3, 4, 4, 4, 4 });
    b.trailer();

    struct counted_t {
      const uint8_t *data;
      size_t size, pos;
      int rewinds;
    } source = { b.out.data(), b.out.size(), 0, 0 };

    gif_reader_t reader = {
      [](void *handle, void *dest, size_t len) -> size_t {
        counted_t *s = (counted_t *)handle;
        size_t available = s->size - s->pos;
        if(len > available) len = available;
        memcpy(dest, s->data + s->pos, len);
        s->pos += len;
        return len;
      },
      [](void *handle) -> bool {
        counted_t *s = (counted_t *)handle;
        s->pos = 0;
        s->rewinds++;
        return true;
      },
      &source
    };

    gif_info_t info;
    scratch().restore = nullptr;
    scratch().restore_size = 0;
    CHECK(gif_survey(reader, &scratch(), &info, nullptr, 0) == GIF_OK);
    CHECK(info.frame_count == 2);
    CHECK_MSG(source.pos == source.size, "the survey should have read the whole file");

    std::vector<uint8_t> restore(info.restore_bytes ? info.restore_bytes : 1, 0);
    scratch().restore = restore.data();
    scratch().restore_size = restore.size();
    image_t sheet((int)info.width * info.frame_count, (int)info.height,
                  1, (int)info.frame_count, RGBA8888, true, info.palette_size);

    // Handed the same reader, sitting at the end of the file.
    CHECK(gif_decode(reader, &scratch(), &info, &sheet) == GIF_OK);
    CHECK_MSG(source.rewinds > 0, "decode should rewind rather than assume");
    CHECK(((uint8_t *)sheet.ptr(0, 0))[0] == 1);
    CHECK(((uint8_t *)sheet.ptr(4, 0))[0] == 3);
  }

  printf("gif: decode says so when it cannot re-read the file\n");
  {
    gif_builder_t b(4, 2, 2);
    b.pixels(0, 0, 4, 2, { 1, 1, 1, 1, 2, 2, 2, 2 });
    b.trailer();

    gif_info_t info;
    scratch().restore = nullptr;
    scratch().restore_size = 0;
    CHECK(gif_survey(b.out.data(), b.out.size(), &scratch(), &info, nullptr, 0) == GIF_OK);

    // A null buffer reads short rather than reaching memcpy. Only Linux CI
    // reports the difference; Darwin's memcpy carries no nonnull attribute.
    CHECK(gif_survey(nullptr, 0, &scratch(), &info, nullptr, 0) == GIF_BAD_MAGIC);
    CHECK(gif_survey(b.out.data(), 0, &scratch(), &info, nullptr, 0) == GIF_BAD_MAGIC);

    // A reader that cannot go back cannot be decoded from, and that is reported
    // rather than read as a file with no header.
    struct once_t { const uint8_t *data; size_t size, pos; } source =
      { b.out.data(), b.out.size(), 0 };
    gif_reader_t stubborn = {
      [](void *handle, void *dest, size_t len) -> size_t {
        once_t *s = (once_t *)handle;
        size_t available = s->size - s->pos;
        if(len > available) len = available;
        memcpy(dest, s->data + s->pos, len);
        s->pos += len;
        return len;
      },
      [](void *) -> bool { return false; },
      &source
    };

    image_t sheet((int)info.width * info.frame_count, (int)info.height,
                  1, (int)info.frame_count, RGBA8888, true, info.palette_size);
    CHECK(gif_decode(stubborn, &scratch(), &info, &sheet) == GIF_NO_BUFFER);
  }

  printf("gif: the parser against 20000 mutated files\n");
  {
    // Every field comes off a filesystem, so all of it is corruption- or
    // attacker-controlled. Accept or reject, but never run off the buffer, and
    // never hand back a sheet holding an index its own table can't answer.
    gif_builder_t good(8, 4, 2);
    good.graphic_control(5, GIF_DISPOSE_BACKGROUND, 3);
    std::vector<uint8_t> indices;
    for(int i = 0; i < 8 * 4; i++) indices.push_back((uint8_t)(i % 7));
    good.pixels(0, 0, 8, 4, indices);
    good.trailer();

    int accepted = 0, rejected = 0, decoded = 0;
    bool sane = true;
    for(int i = 0; i < 20000; i++) {
      std::vector<uint8_t> m = good.out;
      int muts = 1 + (int)(rnd() % 4);
      for(int k = 0; k < muts && !m.empty(); k++) {
        uint32_t op = rnd() % 3;
        if(op == 0) m[rnd() % m.size()] = (uint8_t)rnd();
        else if(op == 1) m.resize(1 + rnd() % m.size());
        else m.push_back((uint8_t)rnd());
      }

      gif_info_t info;
      scratch().restore = nullptr;
      scratch().restore_size = 0;
      if(gif_survey(m.data(), m.size(), &scratch(), &info, nullptr, 0) != GIF_OK) {
        rejected++;
        continue;
      }
      accepted++;
      if(info.frame_count == 0 || info.width == 0 || info.height == 0) sane = false;
      if(info.palette_size == 0 || info.clear_index >= info.palette_size) sane = false;

      // A mutated header can ask for a sheet far larger than the file could
      // fill; refusing that is the caller's job, and here the caller is a test
      // with a budget.
      size_t bytes = (size_t)info.width * info.height * info.frame_count;
      if(bytes == 0 || bytes > 65536) continue;

      std::vector<uint8_t> restore(info.restore_bytes ? info.restore_bytes : 1, 0);
      scratch().restore = restore.data();
      scratch().restore_size = restore.size();
      image_t sheet((int)info.width * info.frame_count, (int)info.height,
                    1, (int)info.frame_count, RGBA8888, true, info.palette_size);
      if(gif_decode(m.data(), m.size(), &scratch(), &info, &sheet) != GIF_OK) continue;

      decoded++;
      int sheet_w = (int)info.width * info.frame_count;
      for(int y = 0; y < (int)info.height && sane; y++) {
        const uint8_t *row = (const uint8_t *)sheet.ptr(0, y);
        for(int x = 0; x < sheet_w; x++) {
          if(row[x] >= info.palette_size) sane = false;
        }
      }
    }
    CHECK(accepted > 0);   // the mutations aren't all fatal
    CHECK(rejected > 0);   // nor all harmless
    CHECK(decoded > 0);    // and some survive all the way to pixels
    CHECK_MSG(sane, "an accepted file described itself inconsistently");
  }
}
