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

inline nocWorkload genTestWorkload(const std::string &device_name) {
  nocWorkload wl;

  size_t grid_xdim = 0, grid_ydim = 0;
  if (device_name == "test") {
    fmt::println("setup workload for 'test' device");
    grid_xdim = 10;
    grid_ydim = 12;
  }

  nocWorkloadPhase ph;
  size_t total_bytes_overall = 0;
  for (int i = 0; i < 2000; i++) {
    {
      constexpr size_t PACKET_SIZE = 8192;
      auto src = Coord{(rand() % 10), (rand() % 12)};
      auto dst = Coord{(rand() % 10), (rand() % 12)};
      CycleCount startup_latency =
          (src.x == dst.x) || (src.y == dst.y) ? 155 : 260;
      auto bytes = ((rand() % 8) + 2) * PACKET_SIZE;
      total_bytes_overall+=bytes;
      ph.transfers.push_back({.bytes = bytes,
                              .packet_size = PACKET_SIZE,
                              .src = src,
                              .dst = dst,
                              .cycle_offset = startup_latency});
    }
  }
  fmt::println("{} total bytes in all transfers; {} per Tensix",total_bytes_overall,total_bytes_overall/120);

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