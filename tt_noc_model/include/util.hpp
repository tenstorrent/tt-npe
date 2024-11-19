#pragma once

#include <cstddef>

using Timestep = size_t;
using Cycle = size_t;

namespace tt_npe {

struct Coord {
  Coord(size_t x_, size_t y_) : x(x_), y(y_) {}
  size_t x, y;
};
} // namespace tt_npe