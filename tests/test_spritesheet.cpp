// spritesheet_t: a grid, and the numbering that turns a cell number into a cell.
//
// Row-major against column-major, and the clamping that keeps a caller from
// naming a cell outside the grid - the arithmetic an off-by-one hides in.

#include "test.hpp"
#include "spritesheet.hpp"

using namespace picovector;

namespace {

  // The cell at number n, as a packed pair so a walk reads as a list.
  int at(const spritesheet_t &s, int n) {
    int x, y;
    s.locate(n, &x, &y);
    return x * 100 + y;
  }
  int xy(int x, int y) { return x * 100 + y; }

}

void test_spritesheet() {
  printf("sheet: a grid reports its shape\n");
  {
    spritesheet_t s(4, 1);
    CHECK(s.cols() == 4 && s.rows() == 1);
    CHECK(s.frames() == 4);
    // A degenerate grid is one cell, so dividing an image by it is safe.
    spritesheet_t d(0, -3);
    CHECK(d.cols() == 1 && d.rows() == 1 && d.frames() == 1);
    CHECK(at(d, 0) == xy(0, 0));
  }

  printf("sheet: cells are numbered along the rows by default\n");
  {
    spritesheet_t s(4, 3);
    CHECK(s.frames() == 12);
    CHECK(at(s, 0) == xy(0, 0));
    CHECK(at(s, 3) == xy(3, 0));      // along the first row
    CHECK(at(s, 4) == xy(0, 1));      // then wrap to the next
    CHECK(at(s, 11) == xy(3, 2));
  }

  printf("sheet: COLUMNS numbers down the columns instead\n");
  {
    // Which is what makes a column of a tall sheet a contiguous run of numbers.
    spritesheet_t s(2, 16);
    s.direction(SHEET_COLUMNS);
    CHECK(at(s, 0) == xy(0, 0));
    CHECK(at(s, 15) == xy(0, 15));    // down the first column
    CHECK(at(s, 16) == xy(1, 0));     // then wrap to the next
    CHECK(at(s, 31) == xy(1, 15));
  }

  printf("sheet: a negative number counts from the end\n");
  {
    // As a Python index does, and as palette[-1] does.
    spritesheet_t s(4, 2);
    CHECK(at(s, -1) == xy(3, 1));
    CHECK(at(s, -4) == xy(0, 1));
    CHECK(at(s, -8) == xy(0, 0));
  }

  printf("sheet: a number outside the grid clamps into it\n");
  {
    // No number can name a cell that would window outside the buffer.
    spritesheet_t s(4, 2);
    CHECK(at(s, 8) == xy(3, 1));
    CHECK(at(s, 9999) == xy(3, 1));
    CHECK(at(s, -9999) == xy(0, 0));
  }

  printf("sheet: number_of inverts locate\n");
  {
    for(int dir = 0; dir < 2; dir++) {
      spritesheet_t s(5, 3);
      s.direction((sheet_direction_t)dir);
      for(int n = 0; n < s.frames(); n++) {
        int x, y;
        s.locate(n, &x, &y);
        CHECK_MSG(s.number_of(x, y) == n, "number_of did not invert locate");
      }
    }
    // A coordinate off the grid has no number, distinguishable from cell zero.
    spritesheet_t s(5, 3);
    CHECK(s.number_of(0, 0) == 0);
    CHECK(s.number_of(-1, 0) == -1);
    CHECK(s.number_of(5, 0) == -1);
    CHECK(s.number_of(0, 3) == -1);
  }

  printf("sheet: every number maps to a cell inside the grid\n");
  {
    // What the binding relies on: locate() never hands back a coordinate that
    // window() would read outside the buffer.
    for(int cols = 1; cols <= 5; cols++) {
      for(int rows = 1; rows <= 5; rows++) {
        for(int dir = 0; dir < 2; dir++) {
          spritesheet_t s(cols, rows);
          s.direction((sheet_direction_t)dir);
          for(int n = -3; n < cols * rows + 3; n++) {
            int x, y;
            s.locate(n, &x, &y);
            CHECK_MSG(x >= 0 && x < cols && y >= 0 && y < rows, "cell left the grid");
          }
        }
      }
    }
  }

  printf("sheet: the numbering visits every cell exactly once\n");
  {
    for(int dir = 0; dir < 2; dir++) {
      spritesheet_t s(3, 4);
      s.direction((sheet_direction_t)dir);
      bool seen[12] = { false };
      for(int n = 0; n < s.frames(); n++) {
        int x, y;
        s.locate(n, &x, &y);
        int k = y * 3 + x;
        CHECK_MSG(!seen[k], "a cell was visited twice");
        seen[k] = true;
      }
      for(int k = 0; k < 12; k++) CHECK_MSG(seen[k], "a cell was never visited");
    }
  }
}
