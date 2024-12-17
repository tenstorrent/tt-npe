#include <fmt/core.h>

#include <variant>

#include "cliOptions.hpp"
#include "fmt/base.h"
#include "genWorkload.hpp"
#include "ingestWorkload.hpp"
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

    try {
        // setup API handle; this may throw npeException!
        tt_npe::npeAPI npe_api(cfg);

        // construct and validate an npeWorkload to feed to npeEngine
        tt_npe::printDiv("Build Workload");
        tt_npe::npeWorkload wl;
        if (cfg.test_config_yaml != "") {
            wl = genTestWorkload(npe_api.getDeviceModel(), cfg.test_config_yaml);
        } else {
            wl = tt_npe::ingestYAMLWorkload(cfg.workload_yaml);
        }

        // Run simulation (always validates workload before) 
        tt_npe::printDiv("Run Perf Estimation");
        tt_npe::npeResult result = npe_api.runNPE(wl);

        // Handle errors if they bubble up
        std::visit(
            tt_npe::overloaded{
                [&](const tt_npe::npeStats &stats) {
                    tt_npe::printDiv("Stats");
                    fmt::print("{}", stats.to_string(cfg.verbosity != tt_npe::VerbosityLevel::Normal));
                },
                [&](const tt_npe::npeException &err) { fmt::println(stderr, "{}\n", err.what()); }},
            result);

    } catch (const tt_npe::npeException& exp) {
        fmt::println(stderr, "{}", exp.what());
        return 1;
    } catch (const std::exception& exp) {
        fmt::println(stderr, "{}", exp.what());
        return 1;
    }

    return 0;
}
