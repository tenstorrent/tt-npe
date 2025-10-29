// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include <string>
#include "nlohmann/json.hpp"

#include "grid.hpp"
#include "npeConfig.hpp"

namespace tt_npe {

class npeDeviceModel;
class npeWorkload;
class PETransferState;

struct TimestepStats {
    size_t start_cycle = 0;
    size_t end_cycle = 0;
    // NB: link/niu _demand_ expresses the summed demand over the timestep; it
    // can exceed 100% if multiple NoC packet routes overlap in time
    double avg_link_demand = 0;
    // In contrast to link demand, link _util_ is the number of cycles in a
    // timestep a link(s) is used; this cannot exceed 100%.
    double max_link_demand = 0;
    double avg_link_util = 0;
    double avg_niu_demand = 0;
    double max_niu_demand = 0;

    // noc0 stats
    double avg_noc0_link_demand = 0;
    double avg_noc0_link_util = 0;
    double max_noc0_link_demand = 0;
    // noc1 stats
    double avg_noc1_link_demand = 0;
    double avg_noc1_link_util = 0;
    double max_noc1_link_demand = 0;

    LinkDemandGrid link_demand_grid;
    NIUDemandGrid niu_demand_grid;
    std::vector<int> live_transfer_ids;
};

// various results from npe simulation
struct npeStats {
    bool completed = false;
    size_t estimated_cycles = 0;
    size_t estimated_cong_free_cycles = 0;
    size_t golden_cycles = 0;
    double cycle_prediction_error = 0.0;
    size_t wallclock_runtime_us = 0;
    double overall_avg_link_demand = 0;
    double overall_max_link_demand = 0;
    double overall_avg_link_util = 0;
    double overall_max_link_util = 0;
    double overall_avg_niu_demand = 0;
    double overall_max_niu_demand = 0;

    // noc0 stats
    double overall_avg_noc0_link_demand = 0;
    double overall_avg_noc0_link_util = 0;
    double overall_max_noc0_link_demand = 0;
    // noc1 stats
    double overall_avg_noc1_link_demand = 0;
    double overall_avg_noc1_link_util = 0;
    double overall_max_noc1_link_demand = 0;

    double dram_bw_util = 0;
    double dram_bw_util_sim = 0;
    std::vector<TimestepStats> per_timestep_stats;

    TimestepStats& insertTimestep(size_t start_cycle, size_t end_cycle) {
        per_timestep_stats.push_back({});
        TimestepStats &timestep_stats = per_timestep_stats.back();
        timestep_stats.start_cycle = start_cycle;
        timestep_stats.end_cycle = end_cycle;
        return timestep_stats;
    }

    std::string to_string(bool verbose = false) const;

    // populates summary stat fields from per-timestep stats
    void computeSummaryStats(const npeWorkload& wl, const npeDeviceModel& device_model);

    // returns congestion impact as percentage of estimated runtime recoverable without congestion
    double getCongestionImpact() const;

    // emit all simulation stats to a file; used for visualization
    void emitSimTimelineToFile(
        const std::vector<PETransferState> &transfer_state,
        const npeDeviceModel& model,
        const npeWorkload &wl,
        const npeConfig &cfg) const;

    private:
    static constexpr const char* CURRENT_TIMELINE_SCHEMA_VERSION = "1.0.0";
    nlohmann::json v0TimelineSerialization(
        const npeConfig &cfg,
        const npeDeviceModel &model,
        const npeWorkload &wl,
        const std::vector<PETransferState> &transfer_state) const;
    nlohmann::json v1TimelineSerialization(
        const npeConfig &cfg,
        const npeDeviceModel &model,
        const npeWorkload &wl,
        const std::vector<PETransferState> &transfer_state) const;
};

}  // namespace tt_npe
