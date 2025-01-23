// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include "ingestWorkload.hpp"

#include <filesystem>

#include "yaml-cpp/yaml.h"

#include "ScopedTimer.hpp"
#include "npeWorkload.hpp"
#include "npeUtil.hpp"

namespace tt_npe {

std::optional<npeWorkload> ingestYAMLWorkload(const std::string &yaml_wl_filename, bool verbose) {
    ScopedTimer st;
    npeWorkload wl;

    if (not std::filesystem::exists(yaml_wl_filename)) {
        log_error("Provided YAML workload file '{}' is not a valid file!", yaml_wl_filename);
        return {};
    } else if (not yaml_wl_filename.ends_with(".yaml")) {
        if (not promptUser("Provided workload file does not have .yaml file extension; are you "
                           "sure you want to load this?")) {
            return {};
        }
    }

    // load config file
    YAML::Node yaml_data;
    try {
        yaml_data = YAML::LoadFile(yaml_wl_filename);
    } catch (const YAML::Exception &exp) {
        log_error("{}", exp.what());
        return {};
    }

    // YAML will somtimes invalid files as empty, even with invalid contents
    // this includes JSON files (!) somehow (-_-)*
    // so sanity check that a dict called phases is defined
    if (not yaml_data["phases"].IsDefined()) {
        log_error("No workload phases declared within workload file '{}'!", yaml_wl_filename);
        return {};
    }

    YAML::Node golden_result = yaml_data["golden_result"];
    if (golden_result.IsMap()) {
        auto golden_cycles = golden_result["cycles"].as<int>(0);
        wl.setGoldenResultCycles(golden_cycles);
    }

    for (const auto &phase : yaml_data["phases"]) {
        assert(phase.second.IsMap());
        npeWorkloadPhase ph;
        for (const auto &transfer : phase.second["transfers"]) {
            auto packet_size = transfer.second["packet_size"].as<int>(0);
            auto num_packets = transfer.second["num_packets"].as<int>(0);
            auto src_x = transfer.second["src_x"].as<int>(0);
            auto src_y = transfer.second["src_y"].as<int>(0);
            auto dst_x = transfer.second["dst_x"].as<int>(0);
            auto dst_y = transfer.second["dst_y"].as<int>(0);
            auto injection_rate = transfer.second["injection_rate"].as<float>(0.0f);
            auto phase_cycle_offset = transfer.second["phase_cycle_offset"].as<int>(0);
            auto noc_type = transfer.second["noc_type"].as<std::string>("NOC_0");

            ph.transfers.emplace_back(
                packet_size,
                num_packets,
                // note: row is y position, col is x position!
                Coord{src_y, src_x},
                Coord{dst_y, dst_x},
                injection_rate,
                phase_cycle_offset,
                (noc_type == "NOC_0") ? nocType::NOC0 : nocType::NOC1);
        }
        wl.addPhase(ph);
    }

    if (verbose)
        fmt::println("workload ingestion took {} us", st.getElapsedTimeMicroSeconds());
    return wl;
}
}  // namespace tt_npe
