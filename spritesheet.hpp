#pragma once

#include "config.hpp"   // PV_TICKS (clock source; see config_default.hpp)
#include "types.hpp"

// A sheet is a grid over a pixel buffer, a range of cells within that grid, and
// optionally how long each cell is shown for. It holds no pixels: the caller
// pairs it with a buffer and turns a cell into a view.
//
// The range is a pair of inclusive grid cells. When one coordinate is fixed the
// two of them imply the axis on their own, which covers every real case - a row
// of frames, or a column of them. `direction` only decides the walk when both
// coordinates vary and the range is a rectangle, where a sequence has to wrap:
// ROWS runs along each row in turn, COLUMNS down each column.
//
// Timing is either one interval for every cell or a per-cell array the caller
// owns (a GIF's own delays). Time is in whatever unit PV_TICKS counts; on the
// badge that is milliseconds.
//
// The optional start()/now()/done() helpers save a start time so a sheet can work
// out its own elapsed time, exactly as tween_t does, and for the same reason:
// the clock comes from PV_TICKS so the core stays host-clean.

namespace picovector {

  enum sheet_direction_t {
    SHEET_ROWS = 0,     // along each row in turn (default)
    SHEET_COLUMNS = 1   // down each column in turn
  };

  class spritesheet_t {
  public:
    spritesheet_t() {}

    // The whole grid, untimed: what an atlas is.
    spritesheet_t(int cols, int rows)
      : _cols(cols < 1 ? 1 : cols), _rows(rows < 1 ? 1 : rows) {
      _x1 = _cols - 1;
      _y1 = _rows - 1;
    }

    // --- configuration (chainable) -----------------------------------------
    // An inclusive cell range, clamped to the grid. Handed the two corners in
    // either order, since a range authored backwards is a range, not an error -
    // it just plays the other way round, which is what a caller reversing an
    // animation would write.
    spritesheet_t &range(int x0, int y0, int x1, int y1) {
      _x0 = clamp_x(x0); _y0 = clamp_y(y0);
      _x1 = clamp_x(x1); _y1 = clamp_y(y1);
      return *this;
    }

    spritesheet_t &direction(sheet_direction_t d) { _direction = d; return *this; }
    spritesheet_t &loop(bool on)                  { _loop = on; return *this; }

    // One interval for every cell. Clears any per-cell timings.
    spritesheet_t &interval(int ms) {
      _interval = ms < 0 ? 0 : ms;
      _delays = nullptr;
      _delay_count = 0;
      return *this;
    }

    // Per-cell timings, in sequence order. The array belongs to the caller and
    // has to outlive the sheet. Short arrays fall back to the interval for the
    // cells they do not cover, so a partial list cannot read off the end.
    spritesheet_t &delays(const uint16_t *ms, int count) {
      _delays = count > 0 ? ms : nullptr;
      _delay_count = _delays ? count : 0;
      return *this;
    }

    // --- the grid ----------------------------------------------------------
    int cols() const { return _cols; }
    int rows() const { return _rows; }

    // How many cells the range covers. Always at least 1.
    int frames() const {
      int w = span_x(), h = span_y();
      return w * h;
    }

    // The cell at sequence position `i`, clamped into the range. Writes grid
    // coordinates, which is what turns into a view.
    void cell(int i, int *x, int *y) const {
      int n = frames();
      if(i < 0) i = 0;
      if(i >= n) i = n - 1;
      int w = span_x(), h = span_y();
      // Along the range's own axes, so a range authored backwards walks
      // backwards rather than jumping to the other end of the grid.
      int cx, cy;
      if(_direction == SHEET_COLUMNS) { cx = i / h; cy = i % h; }
      else                            { cx = i % w; cy = i / w; }
      *x = _x0 + cx * step_x();
      *y = _y0 + cy * step_y();
    }

    // Where a grid cell sits in this sheet's sequence - the inverse of cell().
    // -1 when the cell is outside the range. Narrowing a sheet that carries
    // per-frame timings needs this: the child's frame 0 is some other position in
    // the parent's sequence, so its delay has to be looked up by cell rather than
    // by index, or every frame gets the timing of a different one.
    int index_of(int x, int y) const {
      int dx = (x - _x0) * step_x();
      int dy = (y - _y0) * step_y();
      int w = span_x(), h = span_y();
      if(dx < 0 || dx >= w || dy < 0 || dy >= h) return -1;
      return _direction == SHEET_COLUMNS ? dx * h + dy : dy * w + dx;
    }

    // --- timing ------------------------------------------------------------
    // How long one pass through the range takes. Zero when nothing is timed,
    // which is a sheet asking to be indexed rather than played.
    int duration() const {
      int n = frames();
      if(_delays) {
        int total = 0;
        for(int i = 0; i < n; i++) total += delay_at(i);
        return total;
      }
      return _interval * n;
    }

    // Which sequence position covers `ms`. Wraps when looping, and holds on the
    // last cell when not - so a one-shot leaves the end of its animation on
    // screen rather than snapping back to the start.
    int index_at(int ms) const {
      int n = frames();
      int total = duration();
      // Nothing timed: every moment is the first cell. A caller who wanted a
      // frame per draw is asking about their own frame rate, not about the sheet.
      if(total <= 0) return 0;

      if(ms < 0) {
        // A clock that has run backwards still lands inside the loop.
        if(!_loop) return 0;
        ms = total - ((-ms) % total);
        if(ms == total) ms = 0;
      } else if(ms >= total) {
        if(!_loop) return n - 1;
        ms %= total;
      }

      int elapsed = 0;
      for(int i = 0; i < n; i++) {
        elapsed += delay_at(i);
        if(ms < elapsed) return i;
      }
      return n - 1;
    }

    // True once a non-looping range has run out. A looping one is never done.
    bool finished(int ms) const {
      if(_loop) return false;
      int total = duration();
      return total > 0 && ms >= total;
    }

    // --- optional self-timing (reads the PV_TICKS clock) -------------------
    spritesheet_t &start()      { _start = (int)(PV_TICKS); _running = true; return *this; }
    spritesheet_t &start(int t) { _start = t; _running = true; return *this; }
    void stop()                 { _running = false; }

    int  elapsed() const { return _running ? (int)(PV_TICKS) - _start : 0; }
    int  index_now() const { return index_at(elapsed()); }
    bool done() const    { return _running && finished(elapsed()); }
    bool running() const { return _running; }

    int  interval() const { return _interval; }
    bool loop() const     { return _loop; }
    sheet_direction_t direction() const { return _direction; }
    int  delay_count() const { return _delay_count; }

    // The range, as the caller gave it.
    int x0() const { return _x0; }
    int y0() const { return _y0; }
    int x1() const { return _x1; }
    int y1() const { return _y1; }

    // How long cell `i` of the sequence is shown for.
    int delay_at(int i) const {
      if(_delays && i >= 0 && i < _delay_count) return _delays[i];
      return _interval;
    }

  private:
    int clamp_x(int v) const { return v < 0 ? 0 : (v > _cols - 1 ? _cols - 1 : v); }
    int clamp_y(int v) const { return v < 0 ? 0 : (v > _rows - 1 ? _rows - 1 : v); }

    int span_x() const { return (_x1 >= _x0 ? _x1 - _x0 : _x0 - _x1) + 1; }
    int span_y() const { return (_y1 >= _y0 ? _y1 - _y0 : _y0 - _y1) + 1; }
    int step_x() const { return _x1 >= _x0 ? 1 : -1; }
    int step_y() const { return _y1 >= _y0 ? 1 : -1; }

    int _cols = 1, _rows = 1;
    int _x0 = 0, _y0 = 0, _x1 = 0, _y1 = 0;
    sheet_direction_t _direction = SHEET_ROWS;
    int _interval = 0;
    const uint16_t *_delays = nullptr;
    int _delay_count = 0;
    bool _loop = true;
    int _start = 0;
    bool _running = false;
  };

}
