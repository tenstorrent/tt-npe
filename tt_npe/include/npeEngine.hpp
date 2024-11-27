#pragma once

#include "grid.hpp"
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
    npeStats runPerfEstimation(const npeWorkload &wl, const npeConfig &cfg);

    const npeDeviceModel &getDeviceModel() const { return model; }

   private:
    using PETransferID = int;
    using BytesPerCycle = float;
    using TransferBandwidthTable = std::vector<std::pair<size_t, BytesPerCycle>>;
    using LinkUtilGrid = Grid3D<float>;

    struct CongestionStats {
        std::vector<float> avg_link_utilization;
        std::vector<float> max_link_utilization;
    };

    struct PETransferState {
        size_t packet_size;
        size_t num_packets;
        Coord src, dst;
        float injection_rate;  // how many GB/cycle the source can inject
        CycleCount cycle_offset;

        nocRoute route;
        CycleCount start_cycle = 0;
        float curr_bandwidth = 0;
        size_t total_bytes = 0;
        size_t total_bytes_transferred = 0;

        bool operator<(const auto &rhs) const { return start_cycle < rhs.start_cycle; }
        bool operator>(const auto &rhs) const { return start_cycle > rhs.start_cycle; }
    };

    float interpolateBW(const TransferBandwidthTable &tbt, size_t packet_size, size_t num_packets) const;

    void updateTransferBandwidth(
        std::vector<PETransferState> *transfers, const std::vector<PETransferID> &live_transfer_ids) const;

    void modelCongestion(
        CycleCount start_timestep,
        CycleCount end_timestep,
        std::vector<PETransferState> &transfers,
        const std::vector<PETransferID> &live_transfer_ids,
        LinkUtilGrid &link_util_grid,
        CongestionStats &cong_stats) const;

    npeDeviceModel model;
    static constexpr size_t MAX_CYCLE_LIMIT = 10000;
};

}  // namespace tt_npe
