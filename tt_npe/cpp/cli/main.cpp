// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

// sole header needed for libtt_npe
#include "npeAPI.hpp"

// includes for building CLI
#include "cliOptions.hpp"
#include "genWorkload.hpp"

int main(int argc, char **argv) {
    srand(10);

    tt_npe::npeConfig cfg;
    if (not tt_npe::parse_options(cfg, argc, argv)) {
        return 1;
    }

    try {
        bool verbose = cfg.verbosity != tt_npe::VerbosityLevel::Normal;

        // setup API handle; this may throw npeException!
        tt_npe::npeAPI npe_api(cfg);

        // construct and validate an npeWorkload to feed to npeEngine
        tt_npe::printDiv("Build Workload");
        tt_npe::npeWorkload wl;
        if (cfg.test_config_yaml != "") {
            wl = genTestWorkload(npe_api.getDeviceModel(), cfg.test_config_yaml);
        } else {
            auto maybe_wl = tt_npe::createWorkloadFromJSON(cfg.workload_json, cfg.workload_is_noc_trace, verbose);
            if (maybe_wl.has_value()) {
                wl = maybe_wl.value();
            } else {
                tt_npe::log_error("Failed to ingest workload from file '{}'", cfg.workload_json);
                return 1;
            }
        }

        // Run simulation (always validates workload before)
        tt_npe::printDiv("Run Perf Estimation");
        tt_npe::npeResult result = npe_api.runNPE(wl);

        // Handle errors if they bubble up
        std::visit(
            tt_npe::overloaded{
                [&](const tt_npe::npeStats &stats) {
                    tt_npe::printDiv("Stats");
                    fmt::print("{}", stats.to_string(verbose));
                },
                [&](const tt_npe::npeException &err) {
                    fmt::println(stderr, "E: {}\n", err.what());
                }},
            result);

    } catch (const tt_npe::npeException &exp) {
        fmt::println(stderr, "{}", exp.what());
        return 1;
    } catch (const std::exception &exp) {
        fmt::println(stderr, "{}", exp.what());
        return 1;
    }

    return 0;
}
