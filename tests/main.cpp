// Host test runner for the PicoVector core. Each suite is a plain function.

#include "test.hpp"

void test_geometry();
void test_raster();
void test_clip();
void test_blit();
void test_blend();
void test_alpha();
void test_image_brush();
void test_font();
void test_text_transform();
void test_gif();
void test_robustness();
void test_palette();
void test_color();
void test_color_readback();
void test_color_palette();
void test_gradient();
void test_stroke();
void test_combine();
void test_spritesheet();
void test_qr();
void test_pico3d_math();
void test_pico3d_raster();
void test_pico3d_draw();

int main() {
  test_geometry();
  test_raster();
  test_clip();
  test_blit();
  test_blend();
  test_alpha();
  test_image_brush();
  test_font();
  test_text_transform();
  test_gif();
  test_robustness();
  test_palette();
  test_color();
  test_color_readback();
  test_color_palette();
  test_gradient();
  test_stroke();
  test_combine();
  test_spritesheet();
  test_qr();
  test_pico3d_math();
  test_pico3d_raster();
  test_pico3d_draw();
  return pvtest::summary();
}
