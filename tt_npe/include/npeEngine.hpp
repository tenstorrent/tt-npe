#pragma once

#include "grid.hpp"
#include "npeCommon.hpp"
#include "npeConfig.hpp"
#include "npeDeviceModel.hpp"
#include "npeStats.hpp"
#include "npeWorkload.hpp"
#include "util.hpp"

namespace tt_npe {

class npeEngine {
   public:
    // construct an engine; if anything fails during setup return empty variant
    static std::optional<npeEngine> makeEngine(const std::string &device_name);

    // run a performance estimation sim and reports back stats
    npeResult runPerfEstimation(const npeWorkload &wl, const npeConfig &cfg) const;

    const npeDeviceModel &getDeviceModel() const { return model; }

    // sanity check config values
    bool validateConfig(const npeConfig &cfg) const;

   private:
    using PETransferID = int;
    using LinkUtilGrid = Grid3D<float>;
    using NIUUtilGrid = Grid3D<float>;

    struct CongestionStats {
        std::vector<float> avg_link_utilization;
        std::vector<float> max_link_utilization;
    };

    struct PETransferState {
        PETransferState() = default;
        PETransferState(const npeWorkloadTransfer &wl_transfer, CycleCount start_cycle, nocRoute &&route) :
            params(wl_transfer), start_cycle(start_cycle), route(std::move(route)) {}

        npeWorkloadTransfer params;
        nocRoute route;
        CycleCount start_cycle;

        float curr_bandwidth = 0;
        size_t total_bytes_transferred = 0;

        bool operator<(const auto &rhs) const { return start_cycle < rhs.start_cycle; }
        bool operator>(const auto &rhs) const { return start_cycle > rhs.start_cycle; }
    };

    // used to sort transfers by start time 
    struct TransferQueuePair {
        CycleCount start_cycle;
        PETransferID id;
    };

    std::vector<PETransferState> initTransferState(const npeWorkload& wl) const;

    std::vector<TransferQueuePair> createTransferQueue(const std::vector<PETransferState>& transfer_state) const;

    float interpolateBW(const TransferBandwidthTable &tbt, size_t packet_size, size_t num_packets) const;

    void updateTransferBandwidth(
        std::vector<PETransferState> *transfers, const std::vector<PETransferID> &live_transfer_ids) const;

    void modelCongestion(
        CycleCount start_timestep,
        CycleCount end_timestep,
        std::vector<PETransferState> &transfers,
        const std::vector<PETransferID> &live_transfer_ids,
        NIUUtilGrid &niu_util_grid,
        LinkUtilGrid &link_util_grid,
        CongestionStats &cong_stats) const;

    npeDeviceModel model;
    static constexpr size_t MAX_CYCLE_LIMIT = 1000000;
};

}  // namespace tt_npe
