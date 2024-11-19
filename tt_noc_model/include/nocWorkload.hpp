#pragma once

#include "util.hpp"
#include <vector>

namespace tt_npe {

class nocWorkloadTransfer {
  size_t bytes;
  size_t packet_size;
  Coord src, dest;
  float injection_rate;
  Cycle rel_start_time = 0;
};

class nocWorkloadPhase {
  std::vector<nocWorkloadTransfer> transfers;
  std::vector<const nocWorkloadPhase *> dependencies;
  bool done = false;
};
} // namespace tt_npe