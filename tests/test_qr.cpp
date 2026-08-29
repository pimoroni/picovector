// image_t::qr() — the QR factory over the vendored qrcodegen.
//
// The assertions are about the parts of a QR code the standard fixes, so they
// hold whatever qrcodegen chooses for mask or version: the quiet zone is clear,
// the three finder patterns sit in their corners, and the image is exactly the
// module count plus two borders with a two-entry palette.

#include <cstdio>

#include "test.hpp"
#include "picovector.hpp"
#include "image.hpp"
#include "color.hpp"

using namespace picovector;
using namespace pvtest;

static const int BORDER = 4;

static uint8_t module_at(image_t *img, int x, int y) {
  return *((uint8_t *)img->ptr(x, y));
}

// A finder pattern is a 7x7 dark ring around a light ring around a 3x3 dark
// core, at each of three corners. Checked on the diagonal through its centre,
// which crosses all three bands.
static bool finder_at(image_t *img, int ox, int oy) {
  return module_at(img, ox + 0, oy + 0) == 1   // outer ring
      && module_at(img, ox + 1, oy + 1) == 0   // light ring
      && module_at(img, ox + 3, oy + 3) == 1   // dark core
      && module_at(img, ox + 5, oy + 5) == 0   // light ring, far side
      && module_at(img, ox + 6, oy + 6) == 1;  // outer ring, far side
}

void test_qr() {
  suite() = "qr";

  printf("qr: text encodes to an image\n");
  image_t *img = image_t::qr("HELLO WORLD");
  CHECK(img != nullptr);
  if(!img) return;

  printf("qr: the grid is square and a valid module count\n");
  // Square, and a whole number of modules once both borders come off.
  const int dimension = (int)img->bounds().w;
  CHECK(img->bounds().h == img->bounds().w);
  const int modules = dimension - BORDER * 2;
  // Version 1 is 21 modules, version 40 is 177, always 4n+17.
  CHECK(modules >= 21 && modules <= 177);
  CHECK((modules - 17) % 4 == 0);

  printf("qr: two-entry palette, one byte per pixel\n");
  // One byte per pixel through a palette, not 4 bytes of RGBA.
  CHECK(img->has_palette());
  CHECK(img->bytes_per_pixel() == 1);
  CHECK(img->palette(0) == rgb_color_t(255, 255, 255, 255)._p);
  CHECK(img->palette(1) == rgb_color_t(0, 0, 0, 255)._p);

  printf("qr: the quiet zone is clear on all four sides\n");
  // The quiet zone is light the whole way round, which is what a scanner needs
  // to find the code at all.
  bool quiet = true;
  for(int i = 0; i < dimension; i++) {
    for(int b = 0; b < BORDER; b++) {
      quiet &= module_at(img, i, b) == 0;                  // top
      quiet &= module_at(img, i, dimension - 1 - b) == 0;  // bottom
      quiet &= module_at(img, b, i) == 0;                  // left
      quiet &= module_at(img, dimension - 1 - b, i) == 0;  // right
    }
  }
  CHECK(quiet);

  printf("qr: finder patterns sit in three corners\n");
  // Finder patterns: top-left, top-right, bottom-left. Never bottom-right.
  CHECK(finder_at(img, BORDER, BORDER));
  CHECK(finder_at(img, BORDER + modules - 7, BORDER));
  CHECK(finder_at(img, BORDER, BORDER + modules - 7));

  // Some modules are dark, so we are not just reading a cleared buffer.
  int dark = 0;
  for(int y = 0; y < dimension; y++) {
    for(int x = 0; x < dimension; x++) dark += module_at(img, x, y);
  }
  CHECK(dark > modules);

  free_scratch_image(img);

  printf("qr: more error correction is never a smaller code\n");
  // More error correction needs at least as many modules for the same text.
  image_t *low = image_t::qr("HELLO WORLD", QR_LOW);
  image_t *high = image_t::qr("HELLO WORLD", QR_HIGH);
  CHECK(low != nullptr && high != nullptr);
  if(low && high) CHECK(high->bounds().w >= low->bounds().w);
  if(low) free_scratch_image(low);
  if(high) free_scratch_image(high);

  printf("qr: border 0 is the module grid exactly\n");
  // A zero border is the module grid exactly, with a dark top-left corner.
  image_t *tight = image_t::qr("HELLO WORLD", QR_MEDIUM, 0);
  CHECK(tight != nullptr);
  if(tight) {
    CHECK((int)tight->bounds().w == modules);
    CHECK(module_at(tight, 0, 0) == 1);
    free_scratch_image(tight);
  }

  printf("qr: text too long to encode fails cleanly\n");
  // Too much text for even version 40 fails rather than truncating.
  static char oversized[8192];
  for(size_t i = 0; i < sizeof(oversized) - 1; i++) oversized[i] = 'A';
  oversized[sizeof(oversized) - 1] = '\0';
  CHECK(image_t::qr(oversized, QR_HIGH) == nullptr);
}
