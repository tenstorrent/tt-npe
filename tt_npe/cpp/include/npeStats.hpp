// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include <string>

#include "grid.hpp"

namespace tt_npe {

using LinkUtilGrid = Grid3D<float>;
using NIUUtilGrid = Grid3D<float>;
struct TimestepStats {
    size_t start_cycle = 0;
    size_t end_cycle = 0;
    float avg_link_util = 0;
    float max_link_util = 0;
    LinkUtilGrid link_util_grid;
    NIUUtilGrid niu_util_grid;
    std::vector<int> live_transfer_ids;
};

// various results from npe simulation
struct npeStats {
    bool completed = false;
    size_t estimated_cycles = 0;
    size_t golden_cycles = 0;
    size_t num_timesteps = 0;
    size_t wallclock_runtime_us = 0;
    std::vector<TimestepStats> per_timestep_stats;
    std::string to_string(bool verbose = false) const;
};

}  // namespace tt_npe