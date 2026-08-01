// Every drawing operation must stay inside image.clip().
//
// This suite exists because several did not: the three blit overloads clipped to
// the image bounds instead, bloom/edgeglow/wave/zoom ignored the clip entirely,
// and blit_hspan/blit_vspan clipped only along the axis they travelled - so a
// span whose row lay outside the image wrote past the end of the buffer.

#include "test.hpp"
#include "helpers.hpp"

using namespace picovector;
using namespace pvtest;

static const rect_t CLIP_AREA(16, 16, 16, 16);

// Runs `op` on a canvas with CLIP_AREA set and reports pixels written outside it.
template<typename Op>
static int escapes(Op op) {
  canvas_t c;
  c.img.clip(CLIP_AREA);
  c.img.antialias(X4);
  rgb_color_t red(255, 0, 0, 255);
  color_brush_t brush(red);
  c.img.brush(&brush);
  c.snapshot();
  op(c);
  return c.changed_outside(CLIP_AREA);
}

#define CLIPS(name, body) CHECK_MSG(escapes([&](canvas_t &c) body) == 0, name)

void test_clip() {
  printf("clip: primitives\n");
  CLIPS("clear",      { c.img.clear(); });
  CLIPS("rectangle",  { c.img.rectangle(rect_t(0, 0, 64, 64)); });
  CLIPS("circle",     { c.img.circle(vec2_t(32, 32), 30); });
  CLIPS("triangle",   { c.img.triangle(vec2_t(0, 0), vec2_t(63, 0), vec2_t(32, 63)); });
  CLIPS("line",       { c.img.line(vec2_t(0, 0), vec2_t(63, 63)); });
  CLIPS("hspan",      { c.img.hspan(0, 32, 64); });
  CLIPS("vspan",      { c.img.vspan(32, 0, 64); });
  CLIPS("span",       { c.img.span(0, 8, 64); });
  CLIPS("put",        { for(int k = 0; k < 64; k++) c.img.put(k, k); });
  CLIPS("shape",      { mat3_t t; shape_t *s = circle(32, 32, 30); render(s, &c.img, &t, c.img.brush()); });

  printf("clip: blits\n");
  canvas_t src_c(32, 32);
  src_c.flat(0xff00ff00u);
  image_t *src = &src_c.img;
  CLIPS("blit(point)",     { src->blit(&c.img, vec2_t(0, 0)); });
  CLIPS("blit(rect)",      { src->blit(&c.img, rect_t(0, 0, 64, 64)); });
  CLIPS("blit(src, dst)",  { src->blit(&c.img, rect_t(0, 0, 32, 32), rect_t(0, 0, 64, 64)); });
  CLIPS("blit_hspan",      { src->blit_hspan(&c.img, vec2_t(0, 32), 64, vec2_t(0, 0), vec2_t(31, 31)); });
  CLIPS("blit_vspan",      { src->blit_vspan(&c.img, vec2_t(32, 0), 64, vec2_t(0, 0), vec2_t(31, 31)); });

  printf("clip: filters\n");
  CLIPS("blur",       { c.img.blur(3.0f); });
  CLIPS("bloom",      { c.img.bloom(100, 150, 4.0f); });
  CLIPS("edgeglow",   { c.img.edgeglow(220); });
  CLIPS("wave",       { c.img.wave(4, 4); });
  CLIPS("zoom",       { c.img.zoom(200); });
  CLIPS("invert",     { c.img.invert(); });
  CLIPS("monochrome", { c.img.monochrome(); });
  CLIPS("dither",     { c.img.dither(); });
  CLIPS("onebit",     { c.img.onebit(); });
  CLIPS("vignette",   { c.img.vignette(200); });
  CLIPS("oilpaint",   { c.img.oilpaint(2, 128); });
  CLIPS("chromatic",  { c.img.chromatic(3); });
  CLIPS("crt",        { c.img.crt(3, 60); });
  CLIPS("grid",       { c.img.grid(8, 60); });
  CLIPS("noise",      { c.img.noise(40, 0); });
  CLIPS("glitch",     { c.img.glitch(60); });
  CLIPS("saturation", { c.img.saturation(200); });
  CLIPS("contrast",   { c.img.contrast(200); });
  CLIPS("threshold",  { c.img.threshold(128, rgb_color_t(0,0,0,255), rgb_color_t(255,255,255,255)); });
  CLIPS("duotone",    { c.img.duotone(rgb_color_t(0,0,64,255), rgb_color_t(255,220,180,255)); });
  CLIPS("phosphor",   { c.img.phosphor(rgb_color_t(0,255,120,255)); });
  CLIPS("gameboy",    { c.img.gameboy(); });
  CLIPS("cga",        { c.img.cga(); });
  CLIPS("c64",        { c.img.c64(); });
  CLIPS("synthwave",  { c.img.synthwave(); });
  CLIPS("nightvision",{ c.img.nightvision(); });

  printf("clip: still draws what it should\n");
  {
    // A clip must not be so eager that it drops the visible part too.
    canvas_t c;
    c.img.clip(CLIP_AREA);
    rgb_color_t red(255, 0, 0, 255);
    color_brush_t brush(red);
    c.img.brush(&brush);
    c.snapshot();
    c.img.rectangle(rect_t(0, 0, 64, 64));
    CHECK(c.changed_total() == (int)(CLIP_AREA.w * CLIP_AREA.h));
  }
  {
    canvas_t src2(32, 32);
    src2.flat(0xff00ff00u);
    canvas_t c;
    c.img.clip(CLIP_AREA);
    c.snapshot();
    src2.img.blit(&c.img, vec2_t(0, 0));
    CHECK(c.changed_total() > 0);           // the overlap is still drawn
    CHECK(c.changed_outside(CLIP_AREA) == 0);
  }
  {
    // A span along the clipped row still draws its visible run.
    canvas_t src3(32, 32);
    src3.flat(0xff00ff00u);
    canvas_t c;
    c.img.clip(CLIP_AREA);
    c.snapshot();
    src3.img.blit_hspan(&c.img, vec2_t(0, 20), 64, vec2_t(0, 0), vec2_t(31, 31));
    CHECK(c.changed_total() == (int)CLIP_AREA.w);
  }

  printf("clip: a clip outside the image draws nothing\n");
  {
    canvas_t c;
    c.img.clip(rect_t(100, 100, 20, 20));   // setter intersects with bounds -> empty
    c.snapshot();
    c.img.clear();
    c.img.rectangle(rect_t(0, 0, 64, 64));
    c.img.blur(2.0f);
    CHECK(c.changed_total() == 0);
  }
}
