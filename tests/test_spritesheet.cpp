// spritesheet_t: a grid, a range of cells within it, and optionally their timings.
//
// The arithmetic that used to live in the bindings as a GIF-only frame_at, which
// assumed the sheet was one row and could only describe one sequence per image.
// Everything here is the sequence walk; turning a cell into a view belongs to the
// binding, and the pixels belong to the image.

#include "test.hpp"
#include "spritesheet.hpp"

using namespace picovector;

namespace {

  // The cell at sequence position i, as a packed pair so a walk reads as a list.
  int at(const spritesheet_t &s, int i) {
    int x, y;
    s.cell(i, &x, &y);
    return x * 100 + y;
  }
  int xy(int x, int y) { return x * 100 + y; }

}

void test_spritesheet() {
  printf("sheet: an untimed grid is an atlas\n");
  {
    // What most spritesheet() callers want: indexing, no playback.
    spritesheet_t s(4, 1);
    CHECK(s.cols() == 4 && s.rows() == 1);
    CHECK(s.frames() == 4);
    CHECK(s.duration() == 0);
    // Nothing timed, so every moment is the first cell rather than a division
    // by zero or a frame that races the draw loop.
    CHECK(s.index_at(0) == 0);
    CHECK(s.index_at(100000) == 0);
    CHECK(!s.finished(100000));
  }

  printf("sheet: a range along a row walks that row\n");
  {
    spritesheet_t s(7, 2);
    s.range(0, 1, 4, 1).interval(100);        // the chicken's death animation
    CHECK(s.frames() == 5);
    CHECK(s.duration() == 500);
    CHECK(at(s, 0) == xy(0, 1));
    CHECK(at(s, 4) == xy(4, 1));
    // The two unused cells of row 1 are outside the range and never reached.
    CHECK(at(s, 99) == xy(4, 1));
  }

  printf("sheet: a range down a column walks that column\n");
  {
    spritesheet_t s(2, 16);
    s.range(1, 0, 1, 15).interval(50);
    CHECK(s.frames() == 16);
    CHECK(at(s, 0) == xy(1, 0));
    CHECK(at(s, 7) == xy(1, 7));
    CHECK(at(s, 15) == xy(1, 15));
  }

  printf("sheet: a rectangular range needs its direction\n");
  {
    // The only case where origin and dest do not imply the walk on their own.
    spritesheet_t s(4, 4);
    s.range(0, 0, 3, 3).interval(10);
    CHECK(s.frames() == 16);

    s.direction(SHEET_ROWS);
    CHECK(at(s, 0) == xy(0, 0));
    CHECK(at(s, 3) == xy(3, 0));      // along the first row
    CHECK(at(s, 4) == xy(0, 1));      // then wrap to the next
    CHECK(at(s, 15) == xy(3, 3));

    s.direction(SHEET_COLUMNS);
    CHECK(at(s, 0) == xy(0, 0));
    CHECK(at(s, 3) == xy(0, 3));      // down the first column
    CHECK(at(s, 4) == xy(1, 0));      // then wrap to the next
    CHECK(at(s, 15) == xy(3, 3));
  }

  printf("sheet: a range authored backwards plays backwards\n");
  {
    // Reversing an animation is a thing a caller means to do, so it is a range
    // rather than an error - and it must not walk off the other end of the grid.
    spritesheet_t s(8, 1);
    s.range(5, 0, 2, 0).interval(100);
    CHECK(s.frames() == 4);
    CHECK(at(s, 0) == xy(5, 0));
    CHECK(at(s, 1) == xy(4, 0));
    CHECK(at(s, 3) == xy(2, 0));
  }

  printf("sheet: a range outside the grid is clamped into it\n");
  {
    spritesheet_t s(4, 2);
    s.range(-5, -5, 99, 99);
    CHECK(s.frames() == 8);
    CHECK(at(s, 0) == xy(0, 0));
    CHECK(at(s, 7) == xy(3, 1));
    // ...and a single cell is a legitimate range of one.
    s.range(2, 1, 2, 1).interval(30);
    CHECK(s.frames() == 1);
    CHECK(s.duration() == 30);
    CHECK(at(s, 0) == xy(2, 1));
    CHECK(s.index_at(0) == 0 && s.index_at(29) == 0 && s.index_at(30) == 0);
  }

  printf("sheet: a degenerate grid is still one cell\n");
  {
    spritesheet_t s(0, -3);
    CHECK(s.cols() == 1 && s.rows() == 1);
    CHECK(s.frames() == 1);
    CHECK(at(s, 0) == xy(0, 0));
  }

  printf("sheet: a uniform interval divides the timeline evenly\n");
  {
    spritesheet_t s(4, 1);
    s.interval(100);
    CHECK(s.duration() == 400);
    CHECK(s.index_at(0) == 0);
    CHECK(s.index_at(99) == 0);
    CHECK(s.index_at(100) == 1);
    CHECK(s.index_at(399) == 3);
    // ...and wraps, so a free-running clock can be handed straight over.
    CHECK(s.index_at(400) == 0);
    CHECK(s.index_at(450) == 0);      // 50ms into the second pass
    CHECK(s.index_at(550) == 1);
    CHECK(s.index_at(4000) == 0);
  }

  printf("sheet: per-cell timings are honoured\n");
  {
    // A GIF's own delays. The one case that varies per frame.
    static const uint16_t d[4] = { 60, 90, 120, 150 };
    spritesheet_t s(4, 1);
    s.delays(d, 4);
    CHECK(s.duration() == 420);
    CHECK(s.index_at(0) == 0);
    CHECK(s.index_at(59) == 0);
    CHECK(s.index_at(60) == 1);
    CHECK(s.index_at(149) == 1);
    CHECK(s.index_at(150) == 2);
    CHECK(s.index_at(419) == 3);
    CHECK(s.index_at(420) == 0);
    CHECK(s.index_at(480) == 1);
  }

  printf("sheet: a short timing list falls back to the interval\n");
  {
    // Rather than reading off the end of the array it was given.
    static const uint16_t d[2] = { 100, 100 };
    spritesheet_t s(4, 1);
    s.interval(50).delays(d, 2);
    CHECK(s.delay_at(0) == 100 && s.delay_at(1) == 100);
    CHECK(s.delay_at(2) == 50 && s.delay_at(3) == 50);
    CHECK(s.duration() == 300);
    CHECK(s.index_at(200) == 2);
    CHECK(s.index_at(250) == 3);
  }

  printf("sheet: setting an interval clears per-cell timings\n");
  {
    static const uint16_t d[2] = { 500, 500 };
    spritesheet_t s(2, 1);
    s.delays(d, 2);
    CHECK(s.duration() == 1000);
    s.interval(10);
    CHECK(s.delay_count() == 0);
    CHECK(s.duration() == 20);
  }

  printf("sheet: a one-shot holds its last cell and reports done\n");
  {
    spritesheet_t s(5, 1);
    s.interval(100).loop(false);
    CHECK(s.duration() == 500);
    CHECK(s.index_at(0) == 0);
    CHECK(s.index_at(450) == 4);
    // Past the end it stays on the last frame rather than snapping back.
    CHECK(s.index_at(500) == 4);
    CHECK(s.index_at(100000) == 4);
    CHECK(!s.finished(499));
    CHECK(s.finished(500));
    CHECK(s.finished(100000));
    // A looping range is never done.
    s.loop(true);
    CHECK(!s.finished(100000));
    CHECK(s.index_at(500) == 0);
  }

  printf("sheet: a clock that ran backwards lands inside the loop\n");
  {
    spritesheet_t s(4, 1);
    s.interval(100);
    for(int ms = -1; ms > -1000; ms -= 37) {
      int i = s.index_at(ms);
      CHECK_MSG(i >= 0 && i < 4, "a negative ms left the range");
    }
    CHECK(s.index_at(-100) == 3);       // one interval before the loop point
    CHECK(s.index_at(-400) == 0);       // exactly a loop back
    // A one-shot has no loop to land in, so it holds at the start instead.
    s.loop(false);
    CHECK(s.index_at(-100) == 0);
  }

  printf("sheet: a huge ms stays in range\n");
  {
    static const uint16_t d[3] = { 1, 1, 1 };
    spritesheet_t s(3, 1);
    s.delays(d, 3);
    for(int ms : { 100000, 1000000, 0x7fff0000 }) {
      int i = s.index_at(ms);
      CHECK_MSG(i >= 0 && i < 3, "a large ms left the range");
    }
  }

  printf("sheet: every position maps to a cell inside the grid\n");
  {
    // The property that matters for the binding: cell() can never hand back a
    // coordinate that window() would read outside the buffer.
    for(int cols = 1; cols <= 5; cols++) {
      for(int rows = 1; rows <= 5; rows++) {
        for(int dir = 0; dir < 2; dir++) {
          spritesheet_t s(cols, rows);
          s.range(cols - 1, rows - 1, 0, 0);        // backwards, both axes
          s.direction((sheet_direction_t)dir).interval(10);
          CHECK(s.frames() == cols * rows);
          for(int i = -3; i < cols * rows + 3; i++) {
            int x, y;
            s.cell(i, &x, &y);
            CHECK_MSG(x >= 0 && x < cols && y >= 0 && y < rows, "cell left the grid");
          }
        }
      }
    }
  }

  printf("sheet: index_of inverts cell\n");
  {
    // What narrowing a timed sheet relies on: the child's frame 0 is some other
    // position in the parent's sequence, so its delay is looked up by cell.
    for(int dir = 0; dir < 2; dir++) {
      spritesheet_t s(5, 3);
      s.direction((sheet_direction_t)dir);
      for(int i = 0; i < s.frames(); i++) {
        int x, y;
        s.cell(i, &x, &y);
        CHECK_MSG(s.index_of(x, y) == i, "index_of did not invert cell");
      }
    }
    // A cell outside the range has no position in the sequence.
    spritesheet_t s(5, 3);
    s.range(1, 1, 3, 1);
    CHECK(s.index_of(1, 1) == 0);
    CHECK(s.index_of(3, 1) == 2);
    CHECK(s.index_of(0, 1) == -1);
    CHECK(s.index_of(4, 1) == -1);
    CHECK(s.index_of(2, 0) == -1);
    // ...and it inverts a backwards range too.
    s.range(3, 1, 1, 1);
    CHECK(s.index_of(3, 1) == 0);
    CHECK(s.index_of(1, 1) == 2);
  }

  printf("sheet: a sequence visits every cell exactly once\n");
  {
    for(int dir = 0; dir < 2; dir++) {
      spritesheet_t s(3, 4);
      s.direction((sheet_direction_t)dir).interval(10);
      bool seen[12] = { false };
      for(int i = 0; i < s.frames(); i++) {
        int x, y;
        s.cell(i, &x, &y);
        int k = y * 3 + x;
        CHECK_MSG(!seen[k], "a cell was visited twice");
        seen[k] = true;
      }
      for(int k = 0; k < 12; k++) CHECK_MSG(seen[k], "a cell was never visited");
    }
  }

  printf("sheet: self-timing tracks the clock like a tween\n");
  {
    spritesheet_t s(4, 1);
    s.interval(100);
    // Not started: elapsed holds at 0 and nothing is done, matching tween_t.
    CHECK(!s.running());
    CHECK(s.elapsed() == 0);
    CHECK(!s.done());

    s.start(0);
    CHECK(s.running());
    s.stop();
    CHECK(!s.running());
    CHECK(s.elapsed() == 0);

    // An explicit start time makes elapsed a pure function of PV_TICKS, which is
    // 0 on the host - so starting in the past is how a test moves the clock.
    // The range is 4 cells at 100, so it runs out at 400.
    s.loop(false).start(-350);
    CHECK(s.elapsed() == 350);
    CHECK(s.index_now() == 3);
    CHECK(!s.done());
    s.start(-400);
    CHECK(s.done());
    CHECK(s.index_now() == 3);        // and holds on the last cell
  }
}
