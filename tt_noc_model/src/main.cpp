#include "fmt/base.h"
#include "nocPE.hpp"
#include <chrono>
#include <fmt/printf.h>

using namespace fmt;

int main() {
  srand(10);

  // construct a nocWorkload to feed to nocPE
  tt_npe::printDiv("Build Workload");
  tt_npe::nocWorkload wl = tt_npe::genTestWorkload("test");

  // run perf estimation
  tt_npe::printDiv("Run NPE");
  tt_npe::nocPE npe("test");

  for (auto cycles_per_timestep : {1, 2, 4, 8, 16, 32, 64, 128}) {
    auto start = std::chrono::high_resolution_clock().now();
    npe.runPerfEstimation(wl, cycles_per_timestep);
    auto end = std::chrono::high_resolution_clock().now();
    auto delta =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start)
            .count();
    fmt::println("took {} us", delta);
  }

  return 0;
}