#pragma once

#include <vector>

#include "fmt/base.h"
#include "nocCommon.hpp"
#include "util.hpp"

namespace tt_npe {

struct nocWorkloadTransfer {
  size_t bytes;
  size_t packet_size;
  Coord src, dst;
  float injection_rate = 28; // how many GB/cycle the source can inject
  CycleCount cycle_offset =
      0; // when this transfer can start relative to beginning of its phase

  // returns true if the transfer appears well formed
  bool validate(size_t grid_xdim, size_t grid_ydim) const {
    bool valid_bytes = bytes > 0;
    bool valid_packet_size = packet_size <= bytes;
    bool valid_src =
        (src.x >= 0 && src.x < grid_xdim) && (src.y >= 0 && src.y < grid_ydim);
    bool valid_dst =
        (dst.x >= 0 && dst.x < grid_xdim) && (dst.y >= 0 && dst.y < grid_ydim);
    bool valid_rel_start_time = cycle_offset >= 0;

    return valid_bytes && valid_packet_size && valid_src && valid_dst &&
           valid_rel_start_time;
  }
};

struct nocWorkloadPhase {
  std::vector<nocWorkloadTransfer> transfers;
};

class nocWorkload {
public:
  void addPhase(nocWorkloadPhase phase) { phases.push_back(phase); }
  const auto &getPhases() const { return phases; };

private:
  std::vector<nocWorkloadPhase> phases;
};

} // namespace tt_npe