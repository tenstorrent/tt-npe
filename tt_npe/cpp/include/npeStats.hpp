// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

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

    LinkDemandGrid link_demand_grid;
    NIUDemandGrid niu_demand_grid;
    // Per-DRAM-controller demand for this timestep, in bytes/cycle. Empty unless the DRAM
    // controller model is enabled. Unlike the link/NIU grids this one is retained for every
    // device (not just MESH_DEVICE) because it is tiny -- 6-8 floats per chip.
    DramDemandGrid dram_demand_grid;
    std::vector<int> live_transfer_ids;
};

// A DRAM controller whose aggregate demand approached or exceeded its bandwidth. This is
// the signal the per-NIU view cannot produce: several DRAM NIUs share one controller, so
// each can look unsaturated while the controller behind them is oversubscribed.
struct DramHotspot {
    uint32_t controller_id = 0;
    DeviceID device_id = 0;
    // peak / mean per-timestep demand as a percentage of controller bandwidth
    double peak_demand_pct = 0;
    double mean_demand_pct = 0;
    // fraction of simulated timesteps in which demand met or exceeded capacity
    double saturated_frac = 0;
    // saturated_frac expressed in estimated cycles
    double saturated_cycles = 0;

    std::string to_string() const;
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

        //---- per-DRAM-controller congestion stats -----------------------------------
        // All of these are simulator outputs derived from the per-timestep DRAM demand
        // grid; they are zero/empty unless the DRAM controller model was enabled. This is
        // the distinction from dram_bw_util* above, which are post-hoc quantities computed
        // from the static workload divided by a cycle count.
        //
        // Map keys are the DRAM controller ID for a single-device deviceStats, and the
        // flattened grid slot (device_id * num_controllers + controller_id) for
        // MESH_DEVICE. The two coincide on single-chip parts.
        double overall_max_dram_controller_demand = 0;  // % of controller bandwidth
        double overall_avg_dram_controller_demand = 0;  // % of controller bandwidth
        std::unordered_map<uint32_t, double> dram_controller_peak_demand;
        std::unordered_map<uint32_t, double> dram_controller_mean_demand;
        std::unordered_map<uint32_t, double> dram_controller_saturated_frac;
        // congestion-model capacity per controller in bytes/cycle (0 if model disabled)
        double dram_controller_capacity = 0;
        // number of DRAM controllers per chip, and the device these stats belong to;
        // together these decompose a MESH_DEVICE map key back into (device, controller)
        size_t dram_num_controllers = 0;
        DeviceID dram_stats_device_id = 0;

        std::vector<TimestepStats> per_timestep_stats;

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

        // returns DRAM controllers whose peak demand reached threshold_pct of controller
        // bandwidth, sorted by saturated_frac then peak demand (both descending).
        // Always empty when the DRAM controller model is disabled.
        std::vector<DramHotspot> getDRAMHotspots(double threshold_pct = 90.0) const;

        // returns DRAM hotspots as a single formatted line
        std::string getDRAMHotspotStr() const;

        // true if any DRAM controller was saturated for more than half the runtime, i.e.
        // the workload is DRAM-bandwidth-bound rather than NoC-fabric-bound
        bool isDRAMBound() const;

        private:
        // reduces the per-timestep DRAM demand grids into the per-controller summary
        // fields above; no-op when the DRAM controller model was disabled
        void computeDramControllerStats(const npeDeviceModel& device_model, DeviceID device_id);

        // just for computing estimated_cycles, not to be reported in output
        Cycle worst_case_transfer_end_cycle = 0;
        friend npeStats;
    };

    const npeDeviceModel* device_model = nullptr;
    std::unordered_map<DeviceID, deviceStats> per_device_stats;

    npeStats() = default;
    npeStats(const npeDeviceModel* device_model);
    
    void insertTimestep(Cycle start_cycle, Cycle end_cycle, const npeWorkload& wl);

    std::string to_string(bool verbose = false) const;

    // populates summary stat fields from per-timestep stats
    void computeSummaryStats(const npeWorkload& wl);

    void finishSimulation(size_t getElapsedTimeMicroSeconds, Cycle cycles_per_timestep, const npeWorkload &wl);

    void updateWorstCaseTransferEndCycle(DeviceID device_id, PETransferState& tr, std::pair<Cycle, Cycle> golden_cycles);

    // emit all simulation stats to a file; used for visualization
    void emitSimTimelineToFile(
        const std::vector<PETransferState> &transfer_state,
        const npeWorkload &wl,
        const npeConfig &cfg) const;

    static constexpr const char* CURRENT_TIMELINE_SCHEMA_VERSION = "1.0.0";
};

}  // namespace tt_npe
