#include <fmt/printf.h>

#include "ScopedTimer.hpp"
#include "nocPE.hpp"
#include "nocWorkload.hpp"
#include "genWorkload.hpp"

using namespace fmt;

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
