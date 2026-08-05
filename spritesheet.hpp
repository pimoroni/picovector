#pragma once

#include "types.hpp"

// A grid over a pixel buffer, holding no pixels of its own: the caller pairs it
// with a buffer and turns a cell into a view.
//
// Cells are numbered 0..cols*rows-1, which is what a frame number is. `direction`
// chooses the numbering: ROWS counts along each row in turn, COLUMNS down each
// column, which makes a column of a tall sheet a contiguous run.
//
// There is no clock here. A played sequence is a tween over cell numbers - tween_t
// carries the duration and the easing, this carries the space.

namespace picovector {

  enum sheet_direction_t {
    SHEET_ROWS = 0,     // number along each row in turn (default)
    SHEET_COLUMNS = 1   // number down each column in turn
  };

  class spritesheet_t {
  public:
    spritesheet_t() {}

    spritesheet_t(int cols, int rows)
      : _cols(cols < 1 ? 1 : cols), _rows(rows < 1 ? 1 : rows) {}

    spritesheet_t &direction(sheet_direction_t d) { _direction = d; return *this; }

    int cols() const { return _cols; }
    int rows() const { return _rows; }
    sheet_direction_t direction() const { return _direction; }

    // Cells in the grid, and so a tween's endpoint when playing them.
    int frames() const { return _cols * _rows; }

    // A cell number to grid coordinates. Clamped, and a negative number counts
    // from the end as a Python index does, so no number names a cell outside the
    // grid.
    void locate(int n, int *x, int *y) const {
      int total = frames();
      if(n < 0) n += total;
      if(n < 0) n = 0;
      if(n >= total) n = total - 1;
      if(_direction == SHEET_COLUMNS) { *x = n / _rows; *y = n % _rows; }
      else                            { *x = n % _cols; *y = n / _cols; }
    }

    // ...and back. -1 for a coordinate off the grid, which is distinguishable
    // from cell zero.
    int number_of(int x, int y) const {
      if(x < 0 || x >= _cols || y < 0 || y >= _rows) return -1;
      return _direction == SHEET_COLUMNS ? x * _rows + y : y * _cols + x;
    }

  private:
    int _cols = 1, _rows = 1;
    sheet_direction_t _direction = SHEET_ROWS;
  };

}
