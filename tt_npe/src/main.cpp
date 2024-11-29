#include <fmt/core.h>

#include <variant>

#include "cliOptions.hpp"
#include "fmt/base.h"
#include "genWorkload.hpp"
#include "npeAPI.hpp"
#include "npeCommon.hpp"
#include "npeStats.hpp"
#include "util.hpp"

int main(int argc, char **argv) {
    srand(10);

    tt_npe::npeConfig cfg;
    if (not tt_npe::parse_options(cfg, argc, argv)) {
        return 1;
    }

    // setup API handle
    tt_npe::npeAPI npe_api;
    if (auto optional_npe_api = tt_npe::npeAPI::makeAPI(cfg)) {
        npe_api = optional_npe_api.value();
    } else {
        return 1;
    }

    // construct and validate an npeWorkload to feed to npeEngine
    tt_npe::printDiv("Build Workload");
    tt_npe::npeWorkload wl = genTestWorkload(npe_api.getDeviceModel(), cfg.yaml_workload_config);

    // actually run validation then simulation
    tt_npe::printDiv("Run Perf Estimation");
    tt_npe::npeResult result = npe_api.runNPE(wl);

    // handle errors if they bubble up
    if (auto optional_stat = tt_npe::getStatsFromNPEResult(result)) {
        tt_npe::npeStats stats = optional_stat.value();
        tt_npe::printDiv("Stats");
        fmt::print("{}", stats.to_string(cfg.verbosity != tt_npe::VerbosityLevel::Normal));
    } else {
        tt_npe::npeException err = tt_npe::getErrorFromNPEResult(result).value();
        fmt::println("{}\n", err.what());
    }

    return 0;
}
