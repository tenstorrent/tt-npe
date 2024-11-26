#pragma once

#include "grid.hpp"
#include "npeDeviceModel.hpp"
#include "npeWorkload.hpp"
#include "npeConfig.hpp"
#include "util.hpp"

namespace tt_npe {

// returns various results from noc simulation
struct npeStats {
    bool completed = false;
    size_t estimated_cycles = 0;
    size_t simulated_cycles = 0;
    size_t num_timesteps = 0;
    size_t wallclock_runtime_us = 0;
    std::string to_string(bool verbose = false) const;
};

class npeEngine {
   public:
    npeEngine() = default;
    npeEngine(const std::string &device_name) {
        // initialize noc model
        model = nocModel(device_name);
    }

    npeStats runPerfEstimation(const nocWorkload &wl, const npeConfig &cfg);

    const nocModel &getModel() { return model; }

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

    nocModel model;
    static constexpr size_t MAX_CYCLE_LIMIT = 100000000;
};

}  // namespace tt_npe
