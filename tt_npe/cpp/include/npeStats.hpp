// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include "nlohmann/json.hpp"

#include "grid.hpp"
#include "npeConfig.hpp"
#include "npeUtil.hpp"

namespace tt_npe {

class npeDeviceModel;
class npeWorkload;
class PETransferState;

inline constexpr float TIMELINE_DEMAND_SIGNIFICANCE_THRESHOLD = 0.001f;

struct SparseDemandEntry {
    uint32_t id;
    float demand;
};

struct TimestepStats {
    Cycle start_cycle = 0;
    Cycle end_cycle = 0;
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
    // multicast write stats (absolute util over all NoC links)
    double avg_mcast_write_link_util = 0;

    std::vector<SparseDemandEntry> significant_link_demands;
    std::vector<SparseDemandEntry> significant_niu_demands;
};

struct TimestepSummaryAccumulator {
    double link_demand_sum = 0;
    double max_link_demand = 0;
    double link_util_sum = 0;
    double max_link_util = 0;
    double niu_demand_sum = 0;
    double max_niu_demand = 0;
    double noc0_link_demand_sum = 0;
    double noc0_link_util_sum = 0;
    double max_noc0_link_demand = 0;
    double noc1_link_demand_sum = 0;
    double noc1_link_util_sum = 0;
    double max_noc1_link_demand = 0;
    double mcast_write_link_util_sum = 0;

    void add(const TimestepStats& timestep);
};

// various results from npe simulation
struct npeStats {
    struct deviceStats {
        bool completed = false;
        Cycle estimated_cycles = 0;
        Cycle estimated_cong_free_cycles = 0;
        Cycle golden_cycles = 0;
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
        // multicast write stats (absolute util over all NoC links)
        double overall_avg_mcast_write_link_util = 0;

        double dram_bw_util = 0;
        double dram_bw_util_sim = 0;
        std::unordered_map<Coord, double> eth_bw_util_per_core;
        std::unordered_map<uint32_t, double> dram_bw_util_per_controller;
        std::vector<TimestepStats> per_timestep_stats;
        std::optional<TimestepStats> current_timestep_stats;
        TimestepSummaryAccumulator running_summary;
        TimestepSummaryAccumulator committed_summary;
        Timestep summary_timestep_count = 0;

        std::string to_string(bool verbose = false) const;

        // populates summary stat fields from per-timestep stats
        void computeSummaryStats(const npeWorkload& wl, const npeDeviceModel& device_model, DeviceID device_id);

        // returns congestion impact as percentage of estimated runtime recoverable without congestion
        double getCongestionImpact() const;

        // returns ETH BW util per core as a formatted string
        std::string getEthBwUtilPerCoreStr() const;

        // returns DRAM BW util per controller as a formatted string
        std::string getDramBwUtilPerControllerStr() const;

        // returns aggregate (average) ETH BW util across all cores
        double getAggregateEthBwUtil() const;

        private:
        // just for computing estimated_cycles, not to be reported in output
        Cycle worst_case_transfer_end_cycle = 0;
        friend npeStats;
    };

    const npeDeviceModel* device_model = nullptr;
    std::unordered_map<DeviceID, deviceStats> per_device_stats;

    npeStats() = default;
    npeStats(const npeDeviceModel* device_model);
    
    void insertTimestep(
        Cycle start_cycle,
        Cycle end_cycle,
        const npeWorkload& wl,
        bool retain_mesh_timeline_details);
    TimestepStats* currentTimestepStats(DeviceID device_id);
    void accumulateCurrentTimestepStats();

    std::string to_string(bool verbose = false) const;

    // populates summary stat fields from per-timestep stats
    void computeSummaryStats(const npeWorkload& wl);

    void finishSimulation(size_t getElapsedTimeMicroSeconds, Cycle cycles_per_timestep, const npeWorkload &wl);

    void updateWorstCaseTransferEndCycle(DeviceID device_id, PETransferState& tr, std::pair<Cycle, Cycle> golden_cycles);
    void updateWorstCaseTransferEndCycle(
        DeviceID device_id,
        Cycle phase_cycle_offset,
        Cycle end_cycle,
        std::pair<Cycle, Cycle> golden_cycles);

    // emit all simulation stats to a file; used for visualization
    void emitSimTimelineToFile(
        const std::vector<PETransferState> &transfer_state,
        const npeWorkload &wl,
        const npeConfig &cfg) const;

    static constexpr const char* CURRENT_TIMELINE_SCHEMA_VERSION = "1.0.0";

private:
    bool retain_mesh_timeline_details_ = false;
};

}  // namespace tt_npe
