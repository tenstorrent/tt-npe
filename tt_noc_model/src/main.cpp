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
  constexpr int NUM_TRANSFERS = 256;
  ph.transfers.reserve(NUM_TRANSFERS);
  for (int i = 0; i < NUM_TRANSFERS; i++) {
    constexpr size_t PACKET_SIZE = 8192;
    auto src = tt_npe::Coord{(rand() % 8), (rand() % 8)};
    auto dst = tt_npe::Coord{(rand() % 8), (rand() % 8)};
    CycleCount startup_latency =
        (src.row == dst.row) || (src.col == dst.col) ? 155 : 260;
    startup_latency += rand() % 512;
    auto bytes = ((rand() % 8) + 2) * PACKET_SIZE;
    total_bytes_overall += bytes;
    tt_npe::nocWorkloadTransfer tr{.bytes = bytes,
                                   .packet_size = PACKET_SIZE,
                                   .src = src,
                                   .dst = dst,
                                   .cycle_offset = startup_latency};
    ph.transfers.push_back(tr);
  }
  fmt::println("{} total bytes in all transfers; {} per Tensix",
               total_bytes_overall, total_bytes_overall / 120);

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

  for (auto cycles_per_timestep : {64,128,256}) {
    ScopedTimer timer;
    auto stats = npe.runPerfEstimation(wl, cycles_per_timestep);
    fmt::println("\n\ngran: {:4d} cycles: {:5d}, sim_cyc: {:5d} timesteps: {:5d}", cycles_per_timestep,
                 stats.total_cycles, stats.simulated_cycles, stats.num_timesteps);
  }

  return 0;
}
