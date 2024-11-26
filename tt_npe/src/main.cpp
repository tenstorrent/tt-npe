#include <fmt/core.h>

#include "cli_options.hpp"
#include "genWorkload.hpp"
#include "npeEngine.hpp"
#include "npeConfig.hpp"
#include "util.hpp"

using namespace fmt;

int main(int argc, char **argv) {
    srand(10);

    tt_npe::npeConfig cfg;
    if (not parse_options(cfg, argc, argv)) {
        return 1;
    }

    // init device
    std::string device_name = "wormhole_b0";
    tt_npe::npeEngine npe(device_name);

    // construct and validate an npeWorkload to feed to npeEngine
    tt_npe::printDiv("Build Workload");
    tt_npe::npeWorkload wl = genTestWorkload(npe.getDeviceModel(), cfg.yaml_workload_config);
    if (not wl.validate(npe.getDeviceModel())) {
        tt_npe::error("Failed to validate workload; see errors above.");
        return 1;
    }

    tt_npe::printDiv("Run Perf Estimation");
    auto stats = npe.runPerfEstimation(wl, cfg);

    tt_npe::printDiv("Stats");
    fmt::print("{}", stats.to_string(cfg.verbosity != tt_npe::VerbosityLevel::Normal));

    return 0;
}
