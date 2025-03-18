// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include <boost/container/small_vector.hpp>

#include "npeCommon.hpp"
#include "npeDependencyTracker.hpp"
#include "npeDeviceModelIface.hpp"
#include "npeWorkload.hpp"

namespace bc = boost::container;

namespace tt_npe {

using PETransferID = int;

struct PETransferState {
    PETransferState() = default;
    PETransferState(
        const npeWorkloadTransfer &wl_transfer, CycleCount start_cycle, nocRoute &&route) :
        params(wl_transfer), start_cycle(start_cycle), end_cycle(0), route(std::move(route)) {}

    npeWorkloadTransfer params;
    bc::small_vector<npeCheckpointID, 2> required_by;
    npeCheckpointID depends_on = npeTransferDependencyTracker::UNDEFINED_CHECKPOINT;
    nocRoute route;
    CycleCount start_cycle = 0;
    CycleCount end_cycle = 0;

    float curr_bandwidth = 0;
    size_t total_bytes_transferred = 0;

    bool operator<(const auto &rhs) const { return start_cycle < rhs.start_cycle; }
    bool operator>(const auto &rhs) const { return start_cycle > rhs.start_cycle; }
};
}  // namespace tt_npe