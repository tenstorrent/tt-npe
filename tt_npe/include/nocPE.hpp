#pragma once

#include <algorithm>
#include <boost/container/small_vector.hpp>

#include "ScopedTimer.hpp"
#include "fmt/base.h"
#include "fmt/core.h"
#include "grid.hpp"
#include "nocModel.hpp"
#include "nocNode.hpp"
#include "nocWorkload.hpp"
#include "npeConfig.hpp"
#include "util.hpp"

namespace tt_npe {

// returns various results from noc simulation
struct nocPEStats {
    bool completed = false;
    size_t estimated_cycles = 0;
    size_t simulated_cycles = 0;
    size_t num_timesteps = 0;
    size_t wallclock_runtime_us = 0;
    std::string to_string(bool verbose = false) const {
        std::string output;
        output.append(fmt::format("  estimated cycles:  {:5d}\n", estimated_cycles));
        if (verbose) {
            output.append(fmt::format("  simulation cycles: {:5d}\n", simulated_cycles));
            output.append(fmt::format("  num timesteps:     {:5d}\n", num_timesteps));
            output.append(fmt::format("  wallclock runtime: {:5d} us\n", wallclock_runtime_us));
        }
        return output;
    }
};

using PETransferID = int;

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

struct CongestionStats {
    std::vector<float> avg_link_utilization;
    std::vector<float> max_link_utilization;
};

class nocPE {
   public:
    nocPE() = default;
    nocPE(const std::string &device_name) {
        // initialize noc model
        model = nocModel(device_name);
    }

    using BytesPerCycle = float;
    using TransferBandwidthTable = std::vector<std::pair<size_t, BytesPerCycle>>;
    using LinkUtilGrid = Grid3D<float>;

    float interpolateBW(const TransferBandwidthTable &tbt, size_t packet_size, size_t num_packets) {
        for (int fst = 0; fst < tbt.size() - 1; fst++) {
            size_t start_range = tbt[fst].first;
            size_t end_range = tbt[fst + 1].first;
            if (packet_size >= start_range && packet_size <= end_range) {
                float delta = end_range - start_range;
                float pct = (packet_size - start_range) / delta;
                float val_delta = tbt[fst + 1].second - tbt[fst].second;
                float steady_state_bw = (val_delta * pct) + tbt[fst].second;

                float first_transfer_bw = 28.1;
                float steady_state_ratio = float(num_packets - 1) / num_packets;
                float first_transfer_ratio = 1.0 - steady_state_ratio;
                assert(steady_state_ratio + first_transfer_ratio < 1.0001);
                assert(steady_state_ratio + first_transfer_ratio > 0.999);
                float average_bw = (first_transfer_ratio * first_transfer_bw) + (steady_state_ratio * steady_state_bw);

                // fmt::println("interp bw for ps={} num_packets={} is {:5.2f} {}",
                // packet_size,
                //              num_packets, average_bw, steady_state_bw);
                return average_bw;
            }
        }
        assert(0);
    }

    void updateTransferBandwidth(
        std::vector<PETransferState> *transfers, const std::vector<PETransferID> &live_transfer_ids) {
        static const TransferBandwidthTable tbt = {
            {0, 0}, {128, 5.5}, {256, 10.1}, {512, 18.0}, {1024, 27.4}, {2048, 30.0}, {8192, 30.0}};

        for (auto &ltid : live_transfer_ids) {
            auto &lt = (*transfers)[ltid];
            auto noc_limited_bw = interpolateBW(tbt, lt.packet_size, lt.num_packets);
            lt.curr_bandwidth = std::fmin(lt.injection_rate, noc_limited_bw);
        }
    }

    void modelCongestion(
        CycleCount start_timestep,
        CycleCount end_timestep,
        std::vector<PETransferState> &transfers,
        const std::vector<PETransferID> &live_transfer_ids,
        LinkUtilGrid &link_util_grid,
        CongestionStats &cong_stats) {
        const float LINK_BANDWIDTH = 28.1;

        size_t cycles_per_timestep = end_timestep - start_timestep;

        // Note: for now doing gradient descent to determine link bandwidth doesn't appear necessary. Base algorithm
        // devolves to running just a
        constexpr int NUM_ITERS = 1;
        constexpr float grad_fac = 1.0;
        for (int iter = 0; iter < NUM_ITERS; iter++) {
            // determine effective demand through each link
            link_util_grid.reset(0.0f);
            for (auto ltid : live_transfer_ids) {
                auto &lt = transfers[ltid];

                // account for transfers starting mid-way into timestep, and derate effective utilization accordingly
                CycleCount predicted_start = std::max(start_timestep, lt.start_cycle);
                float effective_util = float(end_timestep - predicted_start) / float(cycles_per_timestep);
                effective_util *= lt.curr_bandwidth;
                for (const auto &link : lt.route) {
                    auto [r, c] = link.coord;
                    link_util_grid(r, c, size_t(link.type)) += effective_util;
                }
            }

            // find highest contention link to set bandwidth
            for (auto ltid : live_transfer_ids) {
                auto &lt = transfers[ltid];
                float max_link_util_on_route = 0.f;
                for (const auto &link : lt.route) {
                    auto [r, c] = link.coord;
                    float util = link_util_grid(r, c, size_t(link.type));
                    if (util > max_link_util_on_route) {
                        max_link_util_on_route = util;
                    }
                }

                // TODO: this should take into account actual total demand through a node
                // this is fine if all transfers have similar bandwidth
                if (max_link_util_on_route > LINK_BANDWIDTH) {
                    float bw_derate = LINK_BANDWIDTH / max_link_util_on_route;
                    lt.curr_bandwidth *= 1.0 - (grad_fac * (1.0f - bw_derate));
                    if (iter == NUM_ITERS - 1) {
                        // fmt::println(
                        //   "  Transfer {:3d} rate is {:.2f} link bw: {:.2f} derate by {:.3f}",
                        //   ltid,
                        //   lt.curr_bandwidth,
                        //   max_link_util_on_route,
                        //   bw_derate);
                    }
                }
            }
        }

        // track statistics
        float avg = 0;
        size_t active_links = 0;
        float max_link_util = 0;
        for (const auto &link_util : link_util_grid) {
            if (link_util > 0.0) {
                avg += link_util;
                active_links++;
                max_link_util = std::fmax(max_link_util, link_util);
            }
        }
        avg /= (active_links ? active_links : 1);
        cong_stats.avg_link_utilization.push_back(avg);
    }

    nocPEStats runPerfEstimation(const nocWorkload &wl, const npeConfig &cfg) {
        ScopedTimer timer;

        nocPEStats stats;

        bool enable_congestion_model = cfg.congestion_model_name != "fast";

        // setup link util grid
        LinkUtilGrid link_util_grid =
            Grid3D<float>(model.getRows(), model.getCols(), size_t(nocLinkType::NUM_LINK_TYPES));
        CongestionStats cong_stats;

        // construct flat vector of all transfers from workload
        std::vector<PETransferState> transfers;
        size_t num_transfers = 0;
        for (const auto &ph : wl.getPhases()) {
            num_transfers += ph.transfers.size();
        }

        transfers.resize(num_transfers);
        for (const auto &ph : wl.getPhases()) {
            for (const auto &wl_transfer : ph.transfers) {
                assert(wl_transfer.id < num_transfers);
                transfers[wl_transfer.id] = PETransferState{
                    .packet_size = wl_transfer.packet_size,
                    .num_packets = wl_transfer.num_packets,
                    .src = wl_transfer.src,
                    .dst = wl_transfer.dst,
                    .injection_rate = wl_transfer.injection_rate,
                    .cycle_offset = wl_transfer.cycle_offset,
                    .total_bytes = wl_transfer.packet_size * wl_transfer.num_packets};
            }
        }

        // TODO: Determine all phases that have satisfied dependencies
        auto ready_phases = wl.getPhases();

        // Insert start_cycle,transfer pairs into a sorted queue for dispatching in
        // main sim loop
        struct TPair {
            CycleCount start_cycle;
            PETransferID id;
        };
        std::vector<TPair> tq;
        for (const auto &ph : ready_phases) {
            for (const auto &tr : ph.transfers) {
                // start time is phase start (curr_cycle) + offset within phase
                // (cycle_offset)
                auto adj_start_cycle = tr.cycle_offset;
                transfers[tr.id].start_cycle = adj_start_cycle;
                transfers[tr.id].route = model.route(nocType::NOC0, tr.src, tr.dst);

                tq.push_back({adj_start_cycle, tr.id});
            }
        }
        std::stable_sort(tq.begin(), tq.end(), [](const TPair &lhs, const TPair &rhs) {
            return std::make_pair(lhs.start_cycle, lhs.id) > std::make_pair(rhs.start_cycle, rhs.id);
        });

        // main simulation loop
        std::vector<PETransferID> live_transfer_ids;
        live_transfer_ids.reserve(transfers.size());
        size_t timestep = 0;
        CycleCount curr_cycle = cfg.cycles_per_timestep;
        while (true) {
            size_t start_of_timestep = (curr_cycle - cfg.cycles_per_timestep);

            // transfer now-active transfers to live_transfers
            while (tq.size() && tq.back().start_cycle <= curr_cycle) {
                auto id = tq.back().id;
                live_transfer_ids.push_back(id);
                tq.pop_back();
            }

            // Compute bandwidth for this timestep for all live transfers
            updateTransferBandwidth(&transfers, live_transfer_ids);

            // model congestion and derate bandwidth
            if (enable_congestion_model) {
                modelCongestion(
                    start_of_timestep, curr_cycle, transfers, live_transfer_ids, link_util_grid, cong_stats);
            }

            // Update all live transfer state
            size_t worst_case_transfer_end_cycle = 0;
            for (auto ltid : live_transfer_ids) {
                auto &lt = transfers[ltid];

                size_t remaining_bytes = lt.total_bytes - lt.total_bytes_transferred;
                size_t cycles_active_in_curr_timestep = std::min(cfg.cycles_per_timestep, curr_cycle - lt.start_cycle);
                size_t max_transferrable_bytes = cycles_active_in_curr_timestep * lt.curr_bandwidth;

                // bytes actually transferred may be limited by remaining bytes in the
                // transfer
                size_t bytes_transferred = std::min(remaining_bytes, max_transferrable_bytes);
                lt.total_bytes_transferred += bytes_transferred;

                // compute cycle where transfer ended
                if (lt.total_bytes == lt.total_bytes_transferred) {
                    float cycles_transferring = std::ceil(bytes_transferred / float(lt.curr_bandwidth));
                    // account for situations when transfer starts and ends within a
                    // single timestep!
                    size_t start_cycle_of_transfer_within_timestep =
                        std::max(size_t(lt.start_cycle), start_of_timestep);
                    size_t transfer_end_cycle = start_cycle_of_transfer_within_timestep + cycles_transferring;

                    // fmt::println(
                    //     "Transfer {} ended on cycle {} cycles_active_in_timestep = {}",
                    //     ltid, transfer_end_cycle, cycles_active_in_curr_timestep);
                    worst_case_transfer_end_cycle = std::max(worst_case_transfer_end_cycle, transfer_end_cycle);
                }
            }

            // compact live transfer list, removing completed transfers
            auto transfer_complete = [&transfers](const PETransferID id) {
                return transfers[id].total_bytes_transferred == transfers[id].total_bytes;
            };
            live_transfer_ids.erase(
                std::remove_if(live_transfer_ids.begin(), live_transfer_ids.end(), transfer_complete),
                live_transfer_ids.end());

            // TODO: if new phase is unlocked, add phase transfer's to tr_queue

            // end sim loop if all transfers have been completed
            if (live_transfer_ids.size() == 0 and tq.size() == 0) {
                timer.stop();
                stats.completed = true;
                stats.estimated_cycles = worst_case_transfer_end_cycle;
                stats.num_timesteps = timestep + 1;
                stats.simulated_cycles = stats.num_timesteps * cfg.cycles_per_timestep;
                stats.wallclock_runtime_us = timer.getElapsedTimeMicroSeconds();

                break;
            }

            if (curr_cycle > MAX_CYCLE_LIMIT) {
                error("Exceeded max cycle limit!");
                stats.completed = false;
                stats.estimated_cycles = MAX_CYCLE_LIMIT;
                stats.num_timesteps = timestep + 1;
                stats.simulated_cycles = stats.num_timesteps * cfg.cycles_per_timestep;
                break;
            }

            // Advance time step
            curr_cycle += cfg.cycles_per_timestep;
            timestep++;
        }

        // visualize link congestion
        if (cfg.enable_visualizations) {
            printDiv("Link Utilization");
            fmt::println("* unused links not included");
            size_t ts = 0;
            auto max_cong =
                *std::max_element(cong_stats.avg_link_utilization.begin(), cong_stats.avg_link_utilization.end());
            float bar_scale = 80.f / max_cong;
            for (const auto &congestion : cong_stats.avg_link_utilization) {
                std::string bar;
                bar.insert(0, bar_scale * congestion, '=');
                bar.append(fmt::format(" {:.2f}", congestion));
                ts++;
                fmt::println("{:3d}|{}", ts, bar);
            }
        }

        return stats;
    }
    const nocModel &getModel() { return model; }

   private:
    nocModel model;
    static constexpr size_t MAX_CYCLE_LIMIT = 100000000;
};

}  // namespace tt_npe
