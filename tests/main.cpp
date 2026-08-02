// Host test runner for the PicoVector core. Each suite is a plain function.

#include "test.hpp"

void test_geometry();
void test_raster();
void test_clip();
void test_blit();
void test_blend();
void test_font();
void test_gif();
void test_robustness();
void test_palette();
void test_color();
void test_color_readback();
void test_gradient();

int main() {
  test_geometry();
  test_raster();
  test_clip();
  test_blit();
  test_blend();
  test_font();
  test_gif();
  test_robustness();
  test_palette();
  test_color();
  test_color_readback();
  test_gradient();
  return pvtest::summary();
}
