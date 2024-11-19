#pragma once

#include <vector>

#include "nocCommon.hpp"
#include "util.hpp"

namespace tt_npe {

struct nocWorkloadTransfer {
  size_t bytes;
  size_t packet_size;
  Coord src, dst;
  Cycle rel_start_time = 0;

  // returns true if the transfer appears well formed
  bool validate(size_t grid_xdim, size_t grid_ydim) const {
    bool valid_bytes = bytes > 0;
    bool valid_packet_size = packet_size <= bytes;
    bool valid_src =
        (src.x >= 0 && src.x < grid_xdim) && (src.y >= 0 && src.y < grid_ydim);
    bool valid_dst =
        (dst.x >= 0 && dst.x < grid_xdim) && (dst.y >= 0 && dst.y < grid_ydim);
    bool valid_rel_start_time = rel_start_time >= 0;

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

inline nocWorkload genTestWorkload(const std::string &device_name) {
  nocWorkload wl;

  size_t grid_xdim = 0, grid_ydim = 0;
  if (device_name == "test") {
    fmt::println("setup workload for 'test' device");
    grid_xdim = 4;
    grid_ydim = 4;
  }

  nocWorkloadPhase ph = {{
      {.bytes = 8192, .packet_size = 8192, .src = {1, 1}, .dst = {1, 0}},
      {.bytes = 8192, .packet_size = 8192, .src = {1, 1}, .dst = {0, 1}},
      {.bytes = 8192, .packet_size = 8192, .src = {1, 1}, .dst = {1, 2}},
      {.bytes = 8192, .packet_size = 8192, .src = {1, 1}, .dst = {2, 1}},
  }};

  int tr_id = 0;
  for (const auto &tr : ph.transfers) {
    if (!tr.validate(grid_xdim, grid_ydim)) {
      fmt::println("Could not validate transfer #{}!", tr_id);
    }
    tr_id++;
  }

  wl.addPhase(ph);

  return wl;
}

} // namespace tt_npe