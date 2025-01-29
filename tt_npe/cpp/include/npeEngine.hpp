// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include <boost/container/small_vector.hpp>

#include "npeCommon.hpp"
#include "npeConfig.hpp"
#include "npeDependencyTracker.hpp"
#include "npeDeviceModel.hpp"
#include "npeWorkload.hpp"
#include "npeTransferState.hpp"

namespace tt_npe {

class npeEngine {
   public:
    npeEngine() = default;

    // throws an npeException if device model cannot be built for device_name
    npeEngine(const std::string &device_name);

    // run a performance estimation sim and reports back stats
    npeResult runPerfEstimation(const npeWorkload &wl, const npeConfig &cfg) const;

    const npeDeviceModel &getDeviceModel() const { return model; }

    // sanity check config values
    bool validateConfig(const npeConfig &cfg) const;

   private:

    // used to sort transfers by start time
    struct TransferQueuePair {
        CycleCount start_cycle;
        PETransferID id;
    };

    std::vector<PETransferState> initTransferState(const npeWorkload &wl) const;

    std::vector<TransferQueuePair> createTransferQueue(
        const std::vector<PETransferState> &transfer_state) const;

    npeTransferDependencyTracker genDependencies(
        std::vector<PETransferState> &transfer_state) const;

    float interpolateBW(
        const TransferBandwidthTable &tbt, size_t packet_size, size_t num_packets) const;

    void updateTransferBandwidth(
        std::vector<PETransferState> *transfers,
        const std::vector<PETransferID> &live_transfer_ids) const;

    void modelCongestion(
        CycleCount start_timestep,
        CycleCount end_timestep,
        std::vector<PETransferState> &transfers,
        const std::vector<PETransferID> &live_transfer_ids,
        NIUDemandGrid &niu_demand_grid,
        LinkDemandGrid &link_demand_grid,
        TimestepStats &sim_stats) const;

    void visualizeTransferSources(
        const std::vector<PETransferState> &transfer_state,
        const std::vector<PETransferID> &live_transfer_ids,
        size_t curr_cycle) const;

    void computeSummaryStats(npeStats &stats) const;

    void emitSimStats(
        const std::string &filepath,
        const std::vector<PETransferState> &transfer_state,
        const npeStats &stats,
        const npeConfig &cfg) const;

    npeDeviceModel model;
    static constexpr size_t MAX_CYCLE_LIMIT = 50000000;
};

}  // namespace tt_npe
