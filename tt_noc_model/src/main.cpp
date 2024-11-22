#include <fmt/printf.h>
#include <yaml-cpp/yaml.h>

#include "ScopedTimer.hpp"
#include "genWorkload.hpp"
#include "nocPE.hpp"
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
    if (not wl.validate(npe.getModel())) {
        tt_npe::error("Failed to validate workload; see errors above.");
        return 1;
    }

    tt_npe::printDiv("Run NPE");
    for (auto cycles_per_timestep : {64}) {
        fmt::println("");
        for (bool enable_cong_model : {true}) {
            ScopedTimer timer;
            auto stats = npe.runPerfEstimation(wl, cycles_per_timestep, enable_cong_model, true);
            timer.stop();
            fmt::print("{}", stats.to_string(true /*verbose*/));
            auto etime_us = timer.getElapsedTimeMicroSeconds();
            fmt::println("etime: {} us ({:.0f} ns/timestep)", etime_us, 1000. * float(etime_us) / stats.num_timesteps);
        }
    }

    return 0;
}
