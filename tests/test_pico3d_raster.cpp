// Host tests for the pico3d triangle rasteriser.

#include "test.hpp"

#include <cstring>
#include <vector>

#include "pico3d.hpp"

using namespace picovector;

static const int W = 64, H = 64;
static std::vector<uint32_t> color;
static std::vector<uint16_t> depth;

static pico3d_target_t make_target() {
  color.assign(W * H, 0);
  depth.assign(W * H, 0xFFFF);
  pico3d_target_t t{};
  t.color = color.data(); t.depth = depth.data();
  t.width = W; t.height = H; t.color_stride = W; t.depth_stride = W;
  t.clip_x0 = 0; t.clip_y0 = 0; t.clip_x1 = W; t.clip_y1 = H;
  return t;
}

// Build a triangle from three screen-space points at a fixed NDC depth. The
// rasteriser takes vertices already projected (pico3d_draw_mesh does that pass),
// so screen position goes in directly and w = 1 makes the interpolation affine.
static pico3d_tri_t screen_tri(float ax, float ay, float bx, float by,
                            float cx, float cy, float ndcz,
                            uint32_t c0, uint32_t c1, uint32_t c2) {
  pico3d_tri_t tri{};
  tri.sx[0] = ax; tri.sy[0] = ay;
  tri.sx[1] = bx; tri.sy[1] = by;
  tri.sx[2] = cx; tri.sy[2] = cy;
  for (int i = 0; i < 3; i++) { tri.z[i] = ndcz; tri.iw[i] = 1.0f; }
  tri.uv_[0] = vec3_t(0,0,0); tri.uv_[1] = vec3_t(1,0,0); tri.uv_[2] = vec3_t(0,1,0);
  tri.rgb[0] = c0; tri.rgb[1] = c1; tri.rgb[2] = c2;
  return tri;
}

void test_pico3d_raster() {
  pico3d_material_t flat{}; flat.texture = nullptr; flat.color = 0xffffff;
  flat.filter = PICO3D_NEAREST; flat.double_sided = false;

  // 1) A CCW (in screen space) solid triangle fills a sane pixel count and
  //    paints its colour; the centroid pixel is set, a far-outside pixel isn't.
  {
    pico3d_target_t t = make_target();
    uint32_t red = pico3d_rgb(255, 0, 0);
    // screen-space CCW: (10,10)->(54,10)->(10,54) is clockwise with y-down,
    // so order to get a front face. Try both; one is culled, one draws.
    pico3d_tri_t tri = screen_tri(10,10, 10,54, 54,10, 0.0f, red, red, red);
    int n = pico3d_raster_triangle(&t, &tri, &flat, nullptr);
    if (n == 0) { // wrong winding for front face; swap two verts
      tri = screen_tri(10,10, 54,10, 10,54, 0.0f, red, red, red);
      n = pico3d_raster_triangle(&t, &tri, &flat, nullptr);
    }
    CHECK_MSG(n > 700 && n < 1100, "triangle pixel count ~ area (968)");
    CHECK_MSG(color[20 * W + 20] == red, "interior pixel painted red");
    CHECK_MSG(color[2 * W + 2] == 0, "exterior pixel untouched");
  }

  // 2) a back-facing winding (positive screen-space area) is culled when
  //    single-sided and drawn when double-sided. Fresh targets each time.
  {
    // screen-CW (= back face under our convention): positive denom
    pico3d_tri_t back = screen_tri(10,10, 54,10, 10,54, 0.0f, 0xffffff,0xffffff,0xffffff);
    pico3d_material_t single = flat;                 // double_sided = false
    pico3d_material_t doubles = flat; doubles.double_sided = true;
    pico3d_target_t ta = make_target();
    int culled = pico3d_raster_triangle(&ta, &back, &single, nullptr);
    pico3d_target_t tb = make_target();
    int drawn  = pico3d_raster_triangle(&tb, &back, &doubles, nullptr);
    CHECK_MSG(culled == 0, "single-sided culls back face");
    CHECK_MSG(drawn > 0, "double-sided draws back face");
  }

  // 3) depth test: a near triangle occludes a far one regardless of draw order.
  {
    pico3d_target_t t = make_target();
    uint32_t far_c = pico3d_rgb(0, 0, 255), near_c = pico3d_rgb(0, 255, 0);
    auto mkfront = [&](float z, uint32_t col, pico3d_tri_t &out)->bool{
      out = screen_tri(10,10, 54,10, 10,54, z, col,col,col);
      pico3d_material_t mm = flat;
      return pico3d_raster_triangle(&t, &out, &mm, nullptr) > 0;
    };
    pico3d_tri_t a, b;
    // draw FAR first then NEAR; near must overwrite at a shared interior pixel
    pico3d_material_t mm = flat;
    pico3d_tri_t farT = screen_tri(10,10, 54,10, 10,54,  0.6f, far_c,far_c,far_c);
    if (pico3d_raster_triangle(&t, &farT, &mm, nullptr) == 0)
      farT = screen_tri(10,10, 10,54, 54,10, 0.6f, far_c,far_c,far_c), pico3d_raster_triangle(&t, &farT, &mm, nullptr);
    int interior = 18 * W + 18;
    bool far_painted = color[interior] == far_c;
    pico3d_tri_t nearT = farT; for (int i=0;i<3;i++) { nearT.z[i] = -0.6f; nearT.rgb[i] = near_c; }
    pico3d_raster_triangle(&t, &nearT, &mm, nullptr);
    CHECK_MSG(far_painted, "far triangle painted first");
    CHECK_MSG(color[interior] == near_c, "near triangle occludes far (depth test)");
    // now draw far again: must NOT overwrite the near pixel
    for (int i=0;i<3;i++) farT.rgb[i]=far_c;
    pico3d_raster_triangle(&t, &farT, &mm, nullptr);
    CHECK_MSG(color[interior] == near_c, "far triangle rejected behind near");
    (void)a;(void)b;(void)mkfront;
  }

  // 4) small textured triangle uses the affine UV path and still samples right.
  //    (bbox <= PICO3D_AFFINE_MAX(24) -> affine; w is constant here so affine
  //    and perspective are identical, which is exactly why it's safe for small.)
  {
    pico3d_target_t t = make_target();
    uint32_t blue = pico3d_rgb(0,0,255);
    uint32_t tex[4] = { blue, blue, blue, blue };          // all blue
    pico3d_texture_t texture{ tex, 2, 2 };
    pico3d_material_t m{}; m.texture = &texture; m.color = 0xffffff; m.double_sided = true; m.filter = PICO3D_NEAREST;
    // ~10px triangle near centre -> small bbox -> affine path
    pico3d_tri_t tri = screen_tri(28,28, 38,28, 28,38, 0.0f, 0xffffff,0xffffff,0xffffff);
    int n = pico3d_raster_triangle(&t, &tri, &m, nullptr);
    CHECK_MSG(n > 0 && n < 120, "small textured triangle rasterised (affine path)");
    uint32_t px = color[31 * W + 30];
    CHECK_MSG(((px >> 16) & 0xff) > 200 && (px & 0xff) < 60, "affine path samples the texture (blue)");
  }

  // 5) normal map drives PER-PIXEL lighting. World N=(0,0,1), T=(1,0,0); light
  //    comes from +z. A flat tangent normal (0,0,1) faces the light (bright); a
  //    tangent normal pointing along T maps to world (1,0,0), perpendicular to
  //    the light (ambient only).
  {
    pico3d_light_t lit{}; lit.color = pico3d_rgb(255,255,255); lit.ambient = pico3d_rgb(20,20,20);
    lit.direction = vec3_t(0,0,-1);            // L (toward light) = +z
    auto shade = [&](uint8_t r, uint8_t g, uint8_t b) -> int {
      pico3d_target_t t = make_target();
      uint32_t texel = pico3d_rgb(r, g, b);
      pico3d_texture_t nm{ &texel, 1, 1 };
      pico3d_material_t m{}; m.color = pico3d_rgb(255,255,255); m.normal_map = &nm;
      m.double_sided = true; m.filter = PICO3D_NEAREST;
      pico3d_tri_t tri = screen_tri(10,10, 54,10, 10,54, 0.0f, 0xffffff,0xffffff,0xffffff);
      for (int i = 0; i < 3; i++) { tri.n[i] = vec3_t(0,0,1); tri.tan[i] = vec3_t(1,0,0); }
      pico3d_raster_triangle(&t, &tri, &m, &lit);
      uint32_t c = color[20 * W + 20];
      return (c & 0xff) + ((c >> 8) & 0xff) + ((c >> 16) & 0xff);
    };
    int flat_n = shade(128,128,255);   // ts normal ~ (0,0,1) -> world (0,0,1), faces light
    int tilt_n = shade(255,128,128);   // ts normal ~ (1,0,0) -> world (1,0,0), perpendicular
    CHECK_MSG(flat_n > 600, "normal map: forward normal is lit");
    CHECK_MSG(tilt_n < 200, "normal map: sideways normal is in shadow (ambient)");
    CHECK_MSG(flat_n > tilt_n, "normal map drives per-pixel lighting");
  }

  // --- depth clear ----------------------------------------------------------
  // It fills by 32-bit words, so the interesting cases are an odd width, a
  // stride wider than the width (rows not contiguous), and an odd start
  // alignment - each of which leaves a halfword tail for the scalar path.
  {
    auto clear_check = [&](int w, int h, int stride, int off, uint16_t v) -> bool {
      std::vector<uint16_t> buf((size_t)stride * h + off + 1, 0x1234);
      pico3d_target_t t{};
      t.depth = buf.data() + off;
      t.width = w; t.height = h; t.depth_stride = stride;
      t.clip_x1 = w; t.clip_y1 = h;
      pico3d_depth_clear(&t, v);
      for (int y = 0; y < h; y++)
        for (int x = 0; x < stride; x++) {
          uint16_t got = buf[(size_t)off + (size_t)y * stride + x];
          // Inside the width it must be the value; the gap up to the stride,
          // and the guard word past the end, must be untouched.
          if (x < w ? got != v : got != 0x1234) return false;
        }
      return buf.back() == 0x1234;
    };
    CHECK_MSG(clear_check(64, 8, 64, 0, 0xFFFF), "depth clear: the contiguous far-plane case");
    CHECK_MSG(clear_check(63, 5, 63, 0, 0xFFFF), "depth clear: an odd width leaves no tail behind");
    CHECK_MSG(clear_check(30, 4, 37, 0, 0xFFFF), "depth clear: a wider stride leaves the gap alone");
    CHECK_MSG(clear_check(31, 3, 31, 1, 0xFFFF), "depth clear: an unaligned start still fills");
    CHECK_MSG(clear_check(31, 3, 31, 1, 0x1357), "depth clear: a value with unequal bytes");
    CHECK_MSG(clear_check(1, 1, 1, 1, 0x0F0E), "depth clear: a single unaligned pixel");

    pico3d_target_t none{};                      // no depth buffer: a no-op, not a crash
    none.width = 8; none.height = 8; none.depth_stride = 8;
    none.clip_x1 = 8; none.clip_y1 = 8;
    pico3d_depth_clear(&none, 0xFFFF);
    CHECK_MSG(true, "depth clear: a target with no depth buffer is a no-op");

    // The clip bounds it, which is what makes clearing one band of a banded
    // render possible - and a degenerate clip clears nothing at all.
    {
      std::vector<uint16_t> buf(8 * 8, 0x1234);
      pico3d_target_t t{};
      t.depth = buf.data(); t.width = 8; t.height = 8; t.depth_stride = 8;
      t.clip_x0 = 2; t.clip_x1 = 6; t.clip_y0 = 3; t.clip_y1 = 5;
      pico3d_depth_clear(&t, 0xABCD);
      bool ok = true;
      for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
          bool inside = (x >= 2 && x < 6 && y >= 3 && y < 5);
          if (buf[y * 8 + x] != (inside ? 0xABCD : 0x1234)) ok = false;
        }
      CHECK_MSG(ok, "depth clear: only the clip rect is cleared");

      std::vector<uint16_t> buf2(8 * 8, 0x1234);
      pico3d_target_t e{};
      e.depth = buf2.data(); e.width = 8; e.height = 8; e.depth_stride = 8;
      pico3d_depth_clear(&e, 0xABCD);
      bool untouched = true;
      for (uint16_t got : buf2) if (got != 0x1234) untouched = false;
      CHECK_MSG(untouched, "depth clear: a degenerate clip clears nothing");
    }
  }

}
