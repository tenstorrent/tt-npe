#include <fmt/printf.h>
#include <yaml-cpp/yaml.h>

#include "ScopedTimer.hpp"
#include "genWorkload.hpp"
#include "nocPE.hpp"
#include "nocWorkload.hpp"
#include "util.hpp"
#include "yaml-cpp/node/parse.h"

using namespace fmt;

int main(int argc, char **argv) {
    srand(10);

    // load config file
    YAML::Node cfg;
    if (argc < 1) {
        tt_npe::error("usage: noc_model config/test_cfg_file.yaml");
    } else if (argc > 1) {
        fmt::println("Loading yaml config file '{}'", argv[1]);
        cfg = YAML::LoadFile(argv[1]);
    }

    // init device
    std::string device_name = "wormhole_b0";
    tt_npe::nocPE npe(device_name);

    // construct a nocWorkload to feed to nocPE
    tt_npe::printDiv("Build Workload");
    tt_npe::nocWorkload wl = genTestWorkload(npe.getModel(), cfg);

    tt_npe::printDiv("Run NPE");
    for (auto cycles_per_timestep : {128}) {
        ScopedTimer timer;
        fmt::println("");
        auto stats = npe.runPerfEstimation(wl, cycles_per_timestep);
        timer.printDelta();
        fmt::println(
            "gran: {:4d} cycles: {:5d}, sim_cyc: {:5d} timesteps: {:5d}",
            cycles_per_timestep,
            stats.total_cycles,
            stats.simulated_cycles,
            stats.num_timesteps);
    }

    return 0;
}
