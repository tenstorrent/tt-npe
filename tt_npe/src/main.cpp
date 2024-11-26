#include <fmt/core.h>
#include <yaml-cpp/yaml.h>

#include "ScopedTimer.hpp"
#include "cli_options.hpp"
#include "genWorkload.hpp"
#include "nocPE.hpp"
#include "npeConfig.hpp"
#include "util.hpp"

using namespace fmt;

int main(int argc, char **argv) {
    srand(10);

    npeConfig cfg;
    if (not parse_options(cfg, argc, argv)) {
        return 1;
    }

    // init device
    std::string device_name = "wormhole_b0";
    tt_npe::nocPE npe(device_name);

    // construct a nocWorkload to feed to nocPE and validate it
    tt_npe::printDiv("Build Workload");
    tt_npe::nocWorkload wl = genTestWorkload(npe.getModel(), cfg.yaml_workload_config);
    if (not wl.validate(npe.getModel())) {
        tt_npe::error("Failed to validate workload; see errors above.");
        return 1;
    }

    tt_npe::printDiv("Run Perf Estimation");
    auto stats = npe.runPerfEstimation(wl, cfg);

    tt_npe::printDiv("Stats");
    fmt::print("{}", stats.to_string(cfg.verbosity != VerbosityLevel::Normal));

    return 0;
}
