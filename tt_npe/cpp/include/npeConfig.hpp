// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once
#include <string>

#include "fmt/core.h"
#include "magic_enum.hpp"

namespace tt_npe {

// Enum for verbosity levels
enum class VerbosityLevel { Normal = 0, Verbose = 1, MoreVerbose = 2, MostVerbose = 3 };

// common config fields (populated from cli options or via API)
struct npeConfig {
    std::string device_name = "wormhole_b0";
    std::string congestion_model_name = "fast";
    std::string workload_json;
    std::string test_config_yaml;
    uint32_t cycles_per_timestep = 128;
    VerbosityLevel verbosity = VerbosityLevel::Normal;
    bool enable_visualizations = false;
    bool infer_injection_rate_from_src = true;
    bool emit_stats_as_json = false;
    bool estimate_cong_impact = true;
    bool workload_is_noc_trace = false;
    bool remove_localized_unicast_transfers = false;
    std::string stats_json_filepath = "";
    float scale_workload_schedule = 0.0f;

    void setVerbosityLevel(int vlvl) {
        vlvl = std::clamp(vlvl, 0, 3);
        verbosity = VerbosityLevel(vlvl);
    };
    std::string to_string() const {
        std::string repr;
        repr += fmt::format("Config {{");
        repr += fmt::format("\n  device_name           = {}", device_name);
        repr += fmt::format("\n  congestion_model_name = {}", congestion_model_name);
        repr += fmt::format("\n  workload_yaml         = \"{}\"", workload_json);
        repr += fmt::format("\n  test_config_yaml      = \"{}\"", test_config_yaml);
        repr += fmt::format("\n  cycles_per_timestep   = {}", cycles_per_timestep);
        repr += fmt::format("\n  verbosity             = {}", magic_enum::enum_name(verbosity));
        repr += fmt::format("\n  enable_visualizations = {}", enable_visualizations);
        repr += "\n}";
        return repr;
    }
};

}  // namespace tt_npe
