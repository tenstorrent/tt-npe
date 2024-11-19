#pragma once

#include <cassert>
#include <vector>

namespace tt_npe {

template <typename T> class Grid2D {
private:
  std::vector<T> data;
  size_t kRows;
  size_t kCols;

public:
  Grid2D() : kRows(0), kCols(0) {}
  Grid2D(size_t num_rows, size_t num_cols, const T &default_value = T())
      : data(num_rows * num_cols, default_value), kRows(num_rows),
        kCols(num_cols) {}

  // returns true if row and col are in bounds
  bool inBounds(size_t row, size_t col) const {
    return row < kRows && col < kCols;
  }

  // returns ref to elem at [row][col]
  T &operator()(size_t row, size_t col) {
    assert(inBounds(row, col));
    return data[row * kCols + col];
  }

  // returns const ref to elem at [row][col]
  const T &operator()(size_t row, size_t col) const {
    assert(inBounds(row, col));
    return data[row * kCols + col];
  }

  // Get dimensions
  size_t getRows() const { return kRows; }
  size_t getCols() const { return kCols; }
  size_t size() const { return kRows * kCols; }

  // iterators
  typename std::vector<T>::iterator begin() { return data.begin(); }
  typename std::vector<T>::iterator end() { return data.end(); }
  typename std::vector<T>::const_iterator begin() const { return data.begin(); }
  typename std::vector<T>::const_iterator end() const { return data.end(); }
};

} // namespace tt_npe