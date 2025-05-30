// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025 Tenstorrent AI ULC

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
    uint32_t cycles_per_timestep = 128;
    VerbosityLevel verbosity = VerbosityLevel::Normal;
    bool enable_visualizations = false;
    bool infer_injection_rate_from_src = true;
    bool emit_timeline_file = false;
    bool estimate_cong_impact = true;
    bool workload_is_noc_trace = false;
    bool remove_localized_unicast_transfers = false;
    bool compress_timeline_output_file = false;
    bool use_legacy_timeline_format = false;
    std::string timeline_filepath = "";
    float scale_workload_schedule = 0.0f;
    std::string cluster_coordinates_json; // Path to cluster coordinates JSON file

    void setVerbosityLevel(int vlvl) {
        vlvl = std::clamp(vlvl, 0, 3);
        verbosity = VerbosityLevel(vlvl);
    };
    std::string to_string() const {
        std::string repr;
        repr += fmt::format("Config {{");
        repr += fmt::format(
            "\n  verbosity                          = {}", std::string(uint32_t(verbosity), 'v'));
        repr += fmt::format("\n  device_name                        = {}", device_name);
        repr += "\n";
        repr += fmt::format("\n  workload_is_noc_trace              = {}", workload_is_noc_trace);
        repr += fmt::format("\n  workload_json                      = \"{}\"", workload_json);
        repr += fmt::format("\n  emit_timeline_file                 = {}", emit_timeline_file);
        repr += fmt::format("\n  timeline_filepath                  = \"{}\"", timeline_filepath);
        repr += "\n";
        repr += fmt::format("\n  congestion_model_name              = {}", congestion_model_name);
        repr += fmt::format("\n  estimate_cong_impact               = {}", estimate_cong_impact);
        repr += fmt::format("\n  cycles_per_timestep                = {}", cycles_per_timestep);
        repr += fmt::format(
            "\n  infer_injection_rate_from_src      = {}", infer_injection_rate_from_src);
        repr += "\n";
        repr += fmt::format(
            "\n  compress_timeline_output_file      = {}", compress_timeline_output_file);
        repr += fmt::format("\n  enable_visualizations              = {}", enable_visualizations);
        repr += fmt::format(
            "\n  remove_localized_unicast_transfers = {}", remove_localized_unicast_transfers);
        repr += fmt::format("\n  scale_workload_schedule            = {}", scale_workload_schedule);
        repr += fmt::format("\n  use_legacy_timeline_format         = {}", use_legacy_timeline_format);
        repr += fmt::format("\n  cluster_coordinates_json           = \"{}\"", cluster_coordinates_json);
        repr += "\n}";
        return repr;
    }
};

}  // namespace tt_npe
