// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include <string>

#include "grid.hpp"
#include "npeConfig.hpp"
#include "npeTransferState.hpp"

namespace tt_npe {

using LinkDemandGrid = Grid3D<float>;
using NIUDemandGrid = Grid3D<float>;
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
    LinkDemandGrid link_demand_grid;
    NIUDemandGrid niu_demand_grid;
    std::vector<int> live_transfer_ids;
};

// various results from npe simulation
struct npeStats {
    bool completed = false;
    size_t estimated_cycles = 0;
    size_t golden_cycles = 0;
    double cycle_prediction_error = 0.0;
    size_t num_timesteps = 0;
    size_t wallclock_runtime_us = 0;
    double overall_avg_link_demand = 0;
    double overall_max_link_demand = 0;
    double overall_avg_link_util = 0;
    double overall_max_link_util = 0;
    double overall_avg_niu_demand = 0;
    double overall_max_niu_demand = 0;
    std::vector<TimestepStats> per_timestep_stats;

    std::string to_string(bool verbose = false) const;

    // populates summary stat fields from per-timestep stats
    void computeSummaryStats();

    // emit all simulation stats to a file; used for visualization
    void emitSimStatsToFile(
        const std::string &filepath,
        const std::vector<PETransferState> &transfer_state,
        const npeConfig &cfg) const;
};

}  // namespace tt_npe
