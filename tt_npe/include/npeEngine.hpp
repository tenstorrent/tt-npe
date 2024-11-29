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
    npeResult runPerfEstimation(const npeWorkload &wl, const npeConfig &cfg);

    const npeDeviceModel &getDeviceModel() const { return model; }

    // sanity check config values
    bool validateConfig(const npeConfig &cfg) const;

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
        PETransferState() = default;
        PETransferState(
            size_t packet_size,
            size_t num_packets,
            Coord src,
            Coord dst,
            float injection_rate,
            CycleCount phase_cycle_offset,
            nocType noc_type,
            size_t total_bytes) :
            packet_size(packet_size),
            num_packets(num_packets),
            src(src),
            dst(dst),
            injection_rate(injection_rate),
            phase_cycle_offset(phase_cycle_offset),
            noc_type(noc_type),
            total_bytes(total_bytes) {}

        size_t packet_size;
        size_t num_packets;
        Coord src, dst;
        float injection_rate;           // how many GB/cycle the source can inject
        CycleCount phase_cycle_offset;  // start cycle relative to phase start
        nocType noc_type;
        size_t total_bytes = 0;

        nocRoute route;
        CycleCount start_cycle = 0;
        float curr_bandwidth = 0;
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
    static constexpr size_t MAX_CYCLE_LIMIT = 1000000;
};

}  // namespace tt_npe
