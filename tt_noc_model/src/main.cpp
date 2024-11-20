#include <fmt/printf.h>

#include "ScopedTimer.hpp"
#include "nocPE.hpp"
#include "nocWorkload.hpp"

using namespace fmt;

inline tt_npe::nocWorkload genTestWorkload(const std::string &device_name) {
  tt_npe::nocWorkload wl;

  size_t grid_xdim = 0, grid_ydim = 0;
  if (device_name == "test") {
    fmt::println("setup workload for 'test' device");
    grid_xdim = 10;
    grid_ydim = 12;
  }

  tt_npe::nocWorkloadPhase ph;
  size_t total_bytes_overall = 0;
  constexpr int NUM_TRANSFERS = 512;
  ph.transfers.reserve(NUM_TRANSFERS);
  for (int i = 0; i < NUM_TRANSFERS; i++) {
    constexpr size_t PACKET_SIZE = 8192;
    auto src = tt_npe::Coord{(rand() % 10), (rand() % 12)};
    auto dst = tt_npe::Coord{(rand() % 10), (rand() % 12)};
    CycleCount startup_latency =
        (src.x == dst.x) || (src.y == dst.y) ? 155 : 260;
    auto bytes = ((rand() % 8) + 2) * PACKET_SIZE;
    total_bytes_overall += bytes;
    ph.transfers.push_back({.bytes = bytes,
                            .packet_size = PACKET_SIZE,
                            .src = src,
                            .dst = dst,
                            .cycle_offset = startup_latency});
  }
  fmt::println("{} total bytes in all transfers; {} per Tensix",
               total_bytes_overall, total_bytes_overall / 120);

  int tr_id = 0;
  for (const auto &tr : ph.transfers) {
    if (!tr.validate(grid_xdim, grid_ydim)) {
      fmt::println("Could not validate transfer #{}!", tr_id);
    }
    tr_id++;
  }

  wl.addPhase(std::move(ph));

  return wl;
}

int main() {
  srand(10);

  // construct a nocWorkload to feed to nocPE
  tt_npe::printDiv("Build Workload");
  tt_npe::nocWorkload wl = genTestWorkload("test");

  // run perf estimation
  tt_npe::printDiv("Run NPE");
  tt_npe::nocPE npe("test");

  for (auto cycles_per_timestep : {4,16,32,64,128,512}) {
    ScopedTimer timer(fmt::sprintf("perf estimation gran: " +
                                   std::to_string(cycles_per_timestep)));
    auto stats = npe.runPerfEstimation(wl, cycles_per_timestep);
    fmt::println("estimated cycles: {}", stats.total_cycles);
  }

  return 0;
}