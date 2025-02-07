// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include "npeEngine.hpp"

#include <map>

#include "ScopedTimer.hpp"
#include "fmt/base.h"
#include "grid.hpp"
#include "npeAssert.hpp"
#include "npeCommon.hpp"
#include "npeDeviceModel.hpp"
#include "npeDeviceNode.hpp"
#include "npeUtil.hpp"
#include "npeWorkload.hpp"

namespace tt_npe {

npeEngine::npeEngine(const std::string &device_name) : model(device_name) {}

float npeEngine::interpolateBW(
    const TransferBandwidthTable &tbt, size_t packet_size, size_t num_packets) const {
    TT_ASSERT(packet_size > 0);
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
            TT_ASSERT(steady_state_ratio + first_transfer_ratio < 1.0001);
            TT_ASSERT(steady_state_ratio + first_transfer_ratio > 0.999);
            float interpolated_bw =
                (first_transfer_ratio * first_transfer_bw) + (steady_state_ratio * steady_state_bw);

            return interpolated_bw;
        }
    }
    // if packet is larger than table, assume it has same peak bw as last table entry
    auto max_table_packet_size = tbt.back().first;
    if (packet_size >= max_table_packet_size) {
        return tbt.back().second;
    }

    TT_ASSERT(false, "interpolation of bandwidth failed");
    return 0;
}

void npeEngine::updateTransferBandwidth(
    std::vector<PETransferState> *transfers,
    const std::vector<PETransferID> &live_transfer_ids) const {
    const auto &tbt = model.getTransferBandwidthTable();
    for (auto &ltid : live_transfer_ids) {
        auto &lt = (*transfers)[ltid];
        auto noc_limited_bw = interpolateBW(tbt, lt.params.packet_size, lt.params.num_packets);
        lt.curr_bandwidth = std::fmin(lt.params.injection_rate, noc_limited_bw);
    }
}

void npeEngine::modelCongestion(
    CycleCount start_timestep,
    CycleCount end_timestep,
    std::vector<PETransferState> &transfers,
    const std::vector<PETransferID> &live_transfer_ids,
    NIUDemandGrid &niu_demand_grid,
    LinkDemandGrid &link_demand_grid,
    TimestepStats &sim_stats) const {
    size_t cycles_per_timestep = end_timestep - start_timestep;

    // assume all links have identical bandwidth
    float LINK_BANDWIDTH = model.getLinkBandwidth({{0, 0}, nocLinkType::NOC0_EAST});
    static auto worker_sink_absorption_rate =
        model.getSinkAbsorptionRateByCoreType(CoreType::WORKER);

    // Note: for now doing gradient descent to determine link bandwidth doesn't
    // appear necessary. Base algorithm devolves to running just a single
    // iteration (first order congestion only).
    constexpr int NUM_ITERS = 1;
    constexpr float grad_fac = 1.0;

    for (int iter = 0; iter < NUM_ITERS; iter++) {
        // determine effective demand through each link
        link_demand_grid.reset(0.0f);
        niu_demand_grid.reset(0.0f);
        for (auto ltid : live_transfer_ids) {
            auto &lt = transfers[ltid];

            // account for transfers starting mid-way into timestep, and derate effective
            // utilization accordingly
            CycleCount predicted_start = std::max(start_timestep, lt.start_cycle);
            float effective_demand =
                float(end_timestep - predicted_start) / float(cycles_per_timestep);
            effective_demand *= lt.curr_bandwidth;

            // track demand at src and sink NIU
            auto src_niu_idx = size_t(
                lt.params.noc_type == nocType::NOC0 ? nocNIUType::NOC0_SRC : nocNIUType::NOC1_SRC);
            niu_demand_grid(lt.params.src.row, lt.params.src.col, src_niu_idx) += effective_demand;
            auto sink_niu_idx = size_t(
                lt.params.noc_type == nocType::NOC0 ? nocNIUType::NOC0_SINK
                                                    : nocNIUType::NOC1_SINK);

            if (std::holds_alternative<Coord>(lt.params.dst)) {
                const auto &dst = std::get<Coord>(lt.params.dst);
                niu_demand_grid(dst.row, dst.col, sink_niu_idx) += effective_demand;
            } else {
                const auto &mcast_dst = std::get<MCastCoordPair>(lt.params.dst);
                for (auto c : mcast_dst) {
                    // multicast only loads on WORKER NIUs; other NIUS ignore traffic
                    if (model.getCoreType(c) == CoreType::WORKER) {
                        niu_demand_grid(c.row, c.col, sink_niu_idx) += effective_demand;
                    }
                }
            }

            for (const auto &link : lt.route) {
                auto [r, c] = link.coord;
                link_demand_grid(r, c, size_t(link.type)) += effective_demand;
            }
        }

        // find highest demand resource on each route to set bandwidth
        for (auto ltid : live_transfer_ids) {
            auto &lt = transfers[ltid];

            // find max link demand on route
            float max_link_demand_on_route = 0;
            auto update_max_link_demand = [&max_link_demand_on_route](float demand) -> bool {
                if (demand > max_link_demand_on_route) {
                    max_link_demand_on_route = demand;
                    return true;
                } else {
                    return false;
                }
            };
            for (const auto &link : lt.route) {
                auto [r, c] = link.coord;
                float link_demand = link_demand_grid(r, c, size_t(link.type));
                update_max_link_demand(link_demand);
            }
            auto min_link_bw_derate = LINK_BANDWIDTH / max_link_demand_on_route;

            // compute bottleneck (min derate factor) for source and sink NIUs
            auto src_niu_idx = size_t(
                lt.params.noc_type == nocType::NOC0 ? nocNIUType::NOC0_SRC : nocNIUType::NOC1_SRC);
            auto src_bw_demand = niu_demand_grid(lt.params.src.row, lt.params.src.col, src_niu_idx);
            auto src_bw_derate = lt.params.injection_rate / src_bw_demand;

            auto sink_niu_idx = size_t(
                lt.params.noc_type == nocType::NOC0 ? nocNIUType::NOC0_SINK
                                                    : nocNIUType::NOC1_SINK);

            float sink_bw_derate = 1;
            if (std::holds_alternative<Coord>(lt.params.dst)) {
                const auto &dst = std::get<Coord>(lt.params.dst);
                auto sink_bw_demand = niu_demand_grid(dst.row, dst.col, sink_niu_idx);
                sink_bw_derate = model.getSinkAbsorptionRate(dst) / sink_bw_demand;
            } else {
                // multicast transfer speed is set by the slowest sink NIU
                const auto &mcast_dst = std::get<MCastCoordPair>(lt.params.dst);
                float sink_demand = 0;
                for (const auto &loc : mcast_dst) {
                    if (model.getCoreType(loc) == CoreType::WORKER) {
                        sink_demand =
                            std::min(sink_demand, niu_demand_grid(loc.row, loc.col, sink_niu_idx));
                    }
                }
                sink_bw_derate = worker_sink_absorption_rate / sink_demand;
            }

            auto min_niu_bw_derate = std::min(src_bw_derate, sink_bw_derate);

            if (min_link_bw_derate < 1.0 || min_niu_bw_derate < 1.0) {
                float overall_bw_derate = std::min(min_link_bw_derate, min_niu_bw_derate);

                lt.curr_bandwidth *= 1.0 - (grad_fac * (1.0f - overall_bw_derate));
            }
        }
    }

    // track statistics; see npeStats.hpp for an explanation of how util and demand differ
    float avg_link_demand = 0;
    float avg_link_util = 0;
    float max_link_demand = 0;
    for (const auto &link_demand : link_demand_grid) {
        avg_link_demand += link_demand;
        avg_link_util += std::min(link_demand, LINK_BANDWIDTH);
        max_link_demand = std::fmax(max_link_demand, link_demand);
    }
    avg_link_demand *= 100. / (LINK_BANDWIDTH * link_demand_grid.size());
    avg_link_util *= 100. / (LINK_BANDWIDTH * link_demand_grid.size());
    max_link_demand *= 100. / LINK_BANDWIDTH;

    float avg_niu_demand = 0;
    float max_niu_demand = 0;
    for (const auto &niu_demand : niu_demand_grid) {
        avg_niu_demand += niu_demand;
        max_niu_demand = std::fmax(max_niu_demand, niu_demand);
    }
    // Hack: LINK_BANDWIDTH is not always a good approximation of NIU bandwidth
    avg_niu_demand *= 100. / (LINK_BANDWIDTH * niu_demand_grid.size());
    max_niu_demand *= 100. / LINK_BANDWIDTH;

    sim_stats.avg_link_demand = avg_link_demand;
    sim_stats.max_link_demand = max_link_demand;
    sim_stats.avg_link_util = avg_link_util;
    sim_stats.avg_niu_demand = avg_niu_demand;
    sim_stats.max_niu_demand = max_niu_demand;

    // NOTE: copying these is a 10% runtime overhead
    sim_stats.link_demand_grid = link_demand_grid;
    sim_stats.niu_demand_grid = niu_demand_grid;
}

std::vector<PETransferState> npeEngine::initTransferState(const npeWorkload &wl) const {
    // construct flat vector of all transfers from workload
    size_t num_transfers = 0;
    for (const auto &ph : wl.getPhases()) {
        num_transfers += ph.transfers.size();
    }

    std::vector<PETransferState> transfer_state;
    transfer_state.resize(num_transfers);

    for (const auto &ph : wl.getPhases()) {
        for (const auto &wl_transfer : ph.transfers) {
            TT_ASSERT(wl_transfer.getID() < num_transfers);
            transfer_state[wl_transfer.getID()] = PETransferState(
                wl_transfer,
                // assume all phases start at cycle 0
                wl_transfer.phase_cycle_offset,
                model.route(wl_transfer.noc_type, wl_transfer.src, wl_transfer.dst));
        }
    }

    return transfer_state;
}

std::vector<npeEngine::TransferQueuePair> npeEngine::createTransferQueue(
    const std::vector<PETransferState> &transfer_state) const {
    std::vector<TransferQueuePair> transfer_queue;
    for (auto &tr : transfer_state) {
        transfer_queue.push_back({tr.params.phase_cycle_offset, tr.params.getID()});
    }
    // sort transfer_queue from greatest to least cycle count
    // sim loop will pop transfers that are ready off the end
    std::stable_sort(
        transfer_queue.begin(),
        transfer_queue.end(),
        [](const TransferQueuePair &lhs, const TransferQueuePair &rhs) {
            return std::make_pair(lhs.start_cycle, lhs.id) >
                   std::make_pair(rhs.start_cycle, rhs.id);
        });
    return transfer_queue;
}

npeTransferDependencyTracker npeEngine::genDependencies(
    std::vector<PETransferState> &transfer_state) const {
    npeTransferDependencyTracker dep_tracker;

    std::map<std::tuple<nocType, int, int, int>, std::vector<PETransferID>> bucketed_transfers;

    // the categorization here is either a nocLinkType, or a special int type
    // that specifies a pure-local transfer within a Tensix. This leads to more
    // realistic behavior when debugging, even if it costs a little correlation
    // accuracy.
    constexpr int LOCAL_NOC0_TRANSFER_TYPE = int(nocLinkType::NUM_LINK_TYPES) * 2;
    constexpr int LOCAL_NOC1_TRANSFER_TYPE = int(nocLinkType::NUM_LINK_TYPES) * 3;
    for (auto &tr : transfer_state) {
        int link_type = (tr.route.size() > 0)
                            ? int(tr.route[0].type)
                            : (tr.params.noc_type == nocType::NOC0 ? LOCAL_NOC0_TRANSFER_TYPE
                                                                   : LOCAL_NOC1_TRANSFER_TYPE);

        bucketed_transfers[{tr.params.noc_type, tr.params.src.row, tr.params.src.col, link_type}]
            .push_back(tr.params.getID());
    }

    for (auto &[niu, transfers] : bucketed_transfers) {
        auto &[id, col, row, link_type] = niu;
        // fmt::println("---- {} {} {} ----",magic_enum::enum_name(id),col,row);

        std::stable_sort(
            transfers.begin(),
            transfers.end(),
            [&transfer_state](const auto &lhs, const auto &rhs) {
                return transfer_state[lhs].start_cycle < transfer_state[rhs].start_cycle;
            });

        // depending on n-2 transfer is roughly approximate to 2-VC effects
        int stride = 2;
        for (int i = stride; i < transfers.size(); i++) {
            auto id = transfers[i];
            npeCheckpointID chkpt_id = dep_tracker.createCheckpoint(1);
            transfer_state[id].depends_on = chkpt_id;
            int dependency_id = i - stride;
            TT_ASSERT(dependency_id >= 0);
            transfer_state[transfers[dependency_id]].required_by.push_back(chkpt_id);
        }
    }

    // check dependencies are exactly satisfied by iterating over all transfers required_by
    for (auto tr : transfer_state) {
        for (auto chkpt_id : tr.required_by) {
            dep_tracker.updateCheckpoint(chkpt_id, 0);
        }
    }
    if (!dep_tracker.sanityCheck() || !dep_tracker.allComplete()) {
        log_error("Found inconsistency in dependencies!");
        exit(1);
    }

    // ensure that dep_tracker is reset after sanity check
    dep_tracker.reset();

    return dep_tracker;
}

npeResult npeEngine::runPerfEstimation(const npeWorkload &wl, const npeConfig &cfg) const {
    if (cfg.estimate_cong_impact) {
        // first run with default cfg settings; return early if a failure occurs
        auto cong_result = runSinglePerfSim(wl, cfg);
        if (std::holds_alternative<npeException>(cong_result)) {
            return cong_result;
        }
        auto stats = std::get<npeStats>(cong_result);

        // run congestion-free simulation just to estimate the number of cycles
        auto tmp_cfg = cfg;
        tmp_cfg.congestion_model_name = "none";
        tmp_cfg.emit_stats_as_json = false;
        npeResult cong_free_result = runSinglePerfSim(wl, tmp_cfg);
        if (std::holds_alternative<npeException>(cong_free_result)) {
            return cong_result;
        }

        // combine results from congestion and congestion-free simulations and return
        stats.estimated_cong_free_cycles = std::get<npeStats>(cong_free_result).estimated_cycles;
        return stats;

    } else {
        return runSinglePerfSim(wl, cfg);
    }
}

npeResult npeEngine::runSinglePerfSim(const npeWorkload &wl, const npeConfig &cfg) const {
    ScopedTimer timer("");
    npeStats stats;

    // setup congestion tracking data structures
    bool enable_congestion_model = cfg.congestion_model_name != "none";
    NIUDemandGrid niu_demand_grid =
        Grid3D<float>(model.getRows(), model.getCols(), size_t(nocNIUType::NUM_NIU_TYPES));
    LinkDemandGrid link_demand_grid =
        Grid3D<float>(model.getRows(), model.getCols(), size_t(nocLinkType::NUM_LINK_TYPES));

    // create flattened list of transfers from workload
    auto transfer_state = initTransferState(wl);

    // create sorted queue of transfers to dispatch in main sim loop
    std::vector<TransferQueuePair> transfer_queue = createTransferQueue(transfer_state);

    // setup transfer dependencies within a single NIU
    npeTransferDependencyTracker dep_tracker = genDependencies(transfer_state);

    // main simulation loop
    std::vector<PETransferID> live_transfer_ids;
    live_transfer_ids.reserve(transfer_state.size());
    size_t timestep = 0;
    CycleCount curr_cycle = cfg.cycles_per_timestep;
    while (true) {
        size_t start_of_timestep = (curr_cycle - cfg.cycles_per_timestep);
        size_t prev_start_of_timestep = start_of_timestep - cfg.cycles_per_timestep;
        auto in_prev_timestep = [&](size_t cycle) {
            return cycle >= prev_start_of_timestep && cycle < start_of_timestep;
        };

        stats.per_timestep_stats.push_back({});
        TimestepStats &timestep_stats = stats.per_timestep_stats.back();
        timestep_stats.start_cycle = start_of_timestep;
        timestep_stats.end_cycle = curr_cycle;

        // transfer now-active transfers to live_transfers
        int transfers_activated = 0;
        size_t swap_pos = transfer_queue.size() - 1;
        for (int i = swap_pos; i >= 0 && transfer_queue[i].start_cycle <= curr_cycle; i--) {
            const auto &transfer = transfer_state[transfer_queue[i].id];
            // fmt::println("checking transfer #{} with depends_on
            // {}",transfer.params.getID(),transfer.depends_on);
            if (dep_tracker.done(transfer.depends_on)) {
                // fmt::println("activating transfer #{}",transfer_queue[i].id);
                live_transfer_ids.push_back(transfer_queue[i].id);

                // move inserted element to the end of the transfer_queue to be discarded afterwards
                std::swap(transfer_queue[swap_pos--], transfer_queue[i]);
                transfers_activated++;
            }
        }

        // discard now inactive transfers from the end of the queue
        transfer_queue.resize(transfer_queue.size() - transfers_activated);

        // save list of live transfers
        timestep_stats.live_transfer_ids = live_transfer_ids;

        // Compute bandwidth for this timestep for all live transfers
        updateTransferBandwidth(&transfer_state, live_transfer_ids);

        // model congestion and derate bandwidth
        if (enable_congestion_model) {
            modelCongestion(
                start_of_timestep,
                curr_cycle,
                transfer_state,
                live_transfer_ids,
                niu_demand_grid,
                link_demand_grid,
                timestep_stats);
        }

        // if (cfg.enable_visualizations) {
        //     visualizeTransferSources(transfer_state, live_transfer_ids, curr_cycle);
        // }

        // Update all live transfer state
        size_t worst_case_transfer_end_cycle = 0;
        for (auto ltid : live_transfer_ids) {
            auto &lt = transfer_state[ltid];
            TT_ASSERT(dep_tracker.done(lt.depends_on));

            size_t remaining_bytes = lt.params.total_bytes - lt.total_bytes_transferred;
            size_t cycles_active_in_curr_timestep =
                std::min(cfg.cycles_per_timestep, curr_cycle - lt.start_cycle);
            if (lt.depends_on != npeTransferDependencyTracker::UNDEFINED_CHECKPOINT) {
                auto dep_end_cycle = dep_tracker.end_cycle(lt.depends_on);
                if (lt.start_cycle < start_of_timestep && in_prev_timestep(dep_end_cycle)) {
                    auto adjusted_start = std::max(lt.start_cycle, dep_end_cycle);
                    cycles_active_in_curr_timestep = curr_cycle - adjusted_start;
                    TT_ASSERT(cycles_active_in_curr_timestep >= cfg.cycles_per_timestep);
                    TT_ASSERT(cycles_active_in_curr_timestep <= 2 * cfg.cycles_per_timestep);
                }
            }
            size_t max_transferrable_bytes = cycles_active_in_curr_timestep * lt.curr_bandwidth;

            // bytes actually transferred may be limited by remaining bytes in the
            // transfer
            size_t bytes_transferred = std::min(remaining_bytes, max_transferrable_bytes);
            lt.total_bytes_transferred += bytes_transferred;

            // compute cycle where transfer ended
            if (lt.params.total_bytes == lt.total_bytes_transferred) {
                float cycles_transferring = std::ceil(bytes_transferred / float(lt.curr_bandwidth));
                // account for situations when transfer starts and ends within a
                // single timestep!
                size_t start_cycle_of_transfer_within_timestep =
                    std::max(size_t(lt.start_cycle), start_of_timestep);
                size_t transfer_end_cycle =
                    start_cycle_of_transfer_within_timestep + cycles_transferring;
                lt.end_cycle = transfer_end_cycle;

                for (auto chkpt_id : lt.required_by) {
                    dep_tracker.updateCheckpoint(chkpt_id, transfer_end_cycle);
                }

                // fmt::println(
                //     "Transfer {} ended on cycle {} cycles_active_in_timestep = {}",
                //     ltid, transfer_end_cycle, cycles_active_in_curr_timestep);
                worst_case_transfer_end_cycle =
                    std::max(worst_case_transfer_end_cycle, transfer_end_cycle);
            }
        }

        // compact live transfer list, removing completed transfers
        auto transfer_complete = [&transfer_state](const PETransferID id) {
            return transfer_state[id].total_bytes_transferred ==
                   transfer_state[id].params.total_bytes;
        };
        live_transfer_ids.erase(
            std::remove_if(live_transfer_ids.begin(), live_transfer_ids.end(), transfer_complete),
            live_transfer_ids.end());

        // TODO: if new phase is unlocked, add phase transfer's to tr_queue

        // end sim loop if all transfers have been completed
        if (live_transfer_ids.size() == 0 and transfer_queue.size() == 0) {
            if (!dep_tracker.sanityCheck() || !dep_tracker.allComplete()) {
                log_error("Some dependencies not satisfied!");
            }

            timer.stop();
            stats.completed = true;
            stats.estimated_cycles = worst_case_transfer_end_cycle;
            stats.num_timesteps = timestep + 1;
            stats.wallclock_runtime_us = timer.getElapsedTimeMicroSeconds();
            stats.golden_cycles = wl.getGoldenResultCycles();

            break;
        }

        if (curr_cycle > MAX_CYCLE_LIMIT) {
            return npeException(npeErrorCode::EXCEEDED_SIM_CYCLE_LIMIT);
        }

        // Advance time step
        curr_cycle += cfg.cycles_per_timestep;
        timestep++;
    }

    stats.computeSummaryStats();
    auto dram_traffic_stats = wl.getDRAMTrafficStats(model);
    stats.dram_bw_util = dram_traffic_stats.dram_utilization_pct;

    // visualize link congestion
    if (cfg.enable_visualizations) {
        printDiv("Average Link Utilization");
        fmt::println("* unused links not included");
        size_t ts = 0;
        auto max_cong_stats = *std::max_element(
            stats.per_timestep_stats.begin(),
            stats.per_timestep_stats.end(),
            [](const auto &lhs, const auto &rhs) {
                return lhs.avg_link_demand < rhs.avg_link_demand;
            });
        float bar_scale = 80.f / max_cong_stats.avg_link_demand;

        for (const auto &ts_stat : stats.per_timestep_stats) {
            std::string bar;
            auto congestion = ts_stat.avg_link_demand;
            bar.insert(0, bar_scale * congestion, '=');
            bar.append(fmt::format(" {:.2f}", congestion));
            ts++;
            fmt::println("{:3d}|{}", ts, bar);
        }
    }

    if (cfg.emit_stats_as_json) {
        stats.emitSimStatsToFile(transfer_state, model, cfg);
    }

    return stats;
}
}  // namespace tt_npe
