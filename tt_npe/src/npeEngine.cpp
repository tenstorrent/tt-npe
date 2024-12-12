#include "npeEngine.hpp"

#include "ScopedTimer.hpp"
#include "fmt/base.h"
#include "npeAPI.hpp"
#include "npeCommon.hpp"
#include "npeDeviceModel.hpp"
#include "npeDeviceNode.hpp"
#include "npeWorkload.hpp"

namespace tt_npe {

std::optional<npeEngine> npeEngine::makeEngine(const std::string &device_name) {
    if (auto optional_model = npeDeviceModel::makeDeviceModel(device_name)) {
        npeEngine engine;
        engine.model = optional_model.value();
        return engine;
    } else {
        return {};
    }
}

std::string npeStats::to_string(bool verbose) const {
    std::string output;
    output.append(fmt::format("  estimated cycles:  {:5d}\n", estimated_cycles));
    if (verbose) {
        output.append(fmt::format("  simulation cycles: {:5d}\n", simulated_cycles));
        output.append(fmt::format("  num timesteps:     {:5d}\n", num_timesteps));
        output.append(fmt::format("  wallclock runtime: {:5d} us\n", wallclock_runtime_us));
    }
    return output;
}

float npeEngine::interpolateBW(const TransferBandwidthTable &tbt, size_t packet_size, size_t num_packets) const {
    assert(packet_size > 0);
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
            float interpolated_bw = (first_transfer_ratio * first_transfer_bw) + (steady_state_ratio * steady_state_bw);

            return interpolated_bw;
        }
    }
    // if packet is larger than table, assume it has same peak bw as last table entry 
    auto max_table_packet_size = tbt.back().first;
    if (packet_size >= max_table_packet_size) {
        return tbt.back().second;
    }
    assert(0 && "interpolation of bandwidth failed");
}

void npeEngine::updateTransferBandwidth(
    std::vector<PETransferState> *transfers, const std::vector<PETransferID> &live_transfer_ids) const {

    const auto& tbt = model.getTransferBandwidthTable();
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
    NIUUtilGrid &niu_util_grid,
    LinkUtilGrid &link_util_grid,
    CongestionStats &cong_stats) const {
    const float LINK_BANDWIDTH = 30;

    size_t cycles_per_timestep = end_timestep - start_timestep;

    // Note: for now doing gradient descent to determine link bandwidth doesn't
    // appear necessary. Base algorithm devolves to running just a single
    // iteration (first order congestion only).
    constexpr int NUM_ITERS = 1;
    constexpr float grad_fac = 1.0;

    for (int iter = 0; iter < NUM_ITERS; iter++) {
        // determine effective demand through each link
        link_util_grid.reset(0.0f);
        niu_util_grid.reset(0.0f);
        for (auto ltid : live_transfer_ids) {
            auto &lt = transfers[ltid];

            // account for transfers starting mid-way into timestep, and derate effective utilization accordingly
            CycleCount predicted_start = std::max(start_timestep, lt.start_cycle);
            float effective_util = float(end_timestep - predicted_start) / float(cycles_per_timestep);
            effective_util *= lt.curr_bandwidth;

            // track util at src and sink NIU
            auto src_niu_idx = size_t(lt.params.noc_type == nocType::NOC0 ? nocNIUType::NOC0_SRC : nocNIUType::NOC1_SRC);
            niu_util_grid(
                lt.params.src.row,
                lt.params.src.col,
                src_niu_idx) += effective_util;
            auto sink_niu_idx = size_t(lt.params.noc_type == nocType::NOC0 ? nocNIUType::NOC0_SINK : nocNIUType::NOC1_SINK);
            niu_util_grid(
                lt.params.dst.row,
                lt.params.dst.col,
                sink_niu_idx) += effective_util;

            for (const auto &link : lt.route) {
                auto [r, c] = link.coord;
                link_util_grid(r, c, size_t(link.type)) += effective_util;
            }
        }

        // find highest util resource on each route to set bandwidth
        for (auto ltid : live_transfer_ids) {
            auto &lt = transfers[ltid];
            float max_link_util_on_route = LINK_BANDWIDTH;
            auto update_max_util = [&max_link_util_on_route](float util) -> bool { 
                if (util > max_link_util_on_route) {
                    max_link_util_on_route = util;
                    return true;
                } else {
                    return false;
                }
            };

            // find max link util on route
            for (const auto &link : lt.route) {
                auto [r, c] = link.coord;
                float link_util = link_util_grid(r, c, size_t(link.type));
                update_max_util(link_util);
            }
            auto link_only_max_util = max_link_util_on_route;

            // find max util at source and sink
            auto src_niu_idx = size_t(lt.params.noc_type == nocType::NOC0 ? nocNIUType::NOC0_SRC : nocNIUType::NOC1_SRC);
            auto src_util = niu_util_grid(lt.params.src.row, lt.params.src.col, src_niu_idx);

            auto sink_niu_idx = size_t(lt.params.noc_type == nocType::NOC0 ? nocNIUType::NOC0_SINK : nocNIUType::NOC1_SINK);
            auto sink_util = niu_util_grid(lt.params.dst.row, lt.params.dst.col, sink_niu_idx);

            auto max_niu_util = std::max(src_util,sink_util);

            // TODO: this should take into account actual total demand through a node
            // this is fine if all transfers have similar bandwidth
            if (max_link_util_on_route > LINK_BANDWIDTH || max_niu_util > lt.params.injection_rate) {
                float link_bw_derate = LINK_BANDWIDTH / max_link_util_on_route;
                float niu_bw_derate = lt.params.injection_rate / max_niu_util;
                float bw_derate = std::min(link_bw_derate,niu_bw_derate); 

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
    float avgutil = 0;
    size_t active_links = 0;
    float max_link_util = 0;
    for (const auto &link_util : link_util_grid) {
        if (link_util > 0.0) {
            avgutil += link_util;
            active_links++;
            max_link_util = std::fmax(max_link_util, link_util);
        }
    }
    avgutil /= (active_links ? active_links : 1);
    cong_stats.avg_link_utilization.push_back(avgutil);
    cong_stats.max_link_utilization.push_back(max_link_util);
}

bool npeEngine::validateConfig(const npeConfig &cfg) const {
    if (cfg.cycles_per_timestep <= 0) {
        log_error("Illegal cycles per timestep '{}' in npeConfig", cfg.cycles_per_timestep);
        return false;
    }
    if (cfg.congestion_model_name != "none" && cfg.congestion_model_name != "fast") {
        log_error("Illegal congestion model name '{}' in npeConfig", cfg.congestion_model_name);
        return false;
    }
    return true;
}

std::vector<npeEngine::PETransferState> npeEngine::initTransferState(const npeWorkload &wl) const {
    // construct flat vector of all transfers from workload
    size_t num_transfers = 0;
    for (const auto &ph : wl.getPhases()) {
        num_transfers += ph.transfers.size();
    }

    std::vector<PETransferState> transfer_state;
    transfer_state.resize(num_transfers);

    for (const auto &ph : wl.getPhases()) {
        for (const auto &wl_transfer : ph.transfers) {
            assert(wl_transfer.getID() < num_transfers);
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
        transfer_queue.begin(), transfer_queue.end(), [](const TransferQueuePair &lhs, const TransferQueuePair &rhs) {
            return std::make_pair(lhs.start_cycle, lhs.id) > std::make_pair(rhs.start_cycle, rhs.id);
        });
    return transfer_queue;
}

npeResult npeEngine::runPerfEstimation(const npeWorkload &wl, const npeConfig &cfg) const {
    ScopedTimer timer("", true);
    npeStats stats;

    if (not validateConfig(cfg)) {
        return npeException(npeErrorCode::INVALID_CONFIG);
    }

    // setup congestion tracking data structures
    bool enable_congestion_model = cfg.congestion_model_name != "none";
    NIUUtilGrid niu_util_grid = Grid3D<float>(model.getRows(), model.getCols(), size_t(nocNIUType::NUM_NIU_TYPES));
    LinkUtilGrid link_util_grid = Grid3D<float>(model.getRows(), model.getCols(), size_t(nocLinkType::NUM_LINK_TYPES));
    CongestionStats cong_stats;

    // create flattened list of transfers from workload
    auto transfer_state = initTransferState(wl);

    // create sorted queue of transfers to dispatch in main sim loop 
    std::vector<TransferQueuePair> transfer_queue = createTransferQueue(transfer_state);

    // main simulation loop
    std::vector<PETransferID> live_transfer_ids;
    live_transfer_ids.reserve(transfer_state.size());
    size_t timestep = 0;
    CycleCount curr_cycle = cfg.cycles_per_timestep;
    while (true) {
        size_t start_of_timestep = (curr_cycle - cfg.cycles_per_timestep);

        // transfer now-active transfers to live_transfers
        while (transfer_queue.size() && transfer_queue.back().start_cycle <= curr_cycle) {
            auto id = transfer_queue.back().id;
            live_transfer_ids.push_back(id);
            transfer_queue.pop_back();
        }

        // Compute bandwidth for this timestep for all live transfers
        updateTransferBandwidth(&transfer_state, live_transfer_ids);

        // model congestion and derate bandwidth
        if (enable_congestion_model) {
            modelCongestion(
                start_of_timestep,
                curr_cycle,
                transfer_state,
                live_transfer_ids,
                niu_util_grid,
                link_util_grid,
                cong_stats);
        }

        // Update all live transfer state
        size_t worst_case_transfer_end_cycle = 0;
        for (auto ltid : live_transfer_ids) {
            auto &lt = transfer_state[ltid];

            size_t remaining_bytes = lt.params.total_bytes - lt.total_bytes_transferred;
            size_t cycles_active_in_curr_timestep = std::min(cfg.cycles_per_timestep, curr_cycle - lt.start_cycle);
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
                size_t start_cycle_of_transfer_within_timestep = std::max(size_t(lt.start_cycle), start_of_timestep);
                size_t transfer_end_cycle = start_cycle_of_transfer_within_timestep + cycles_transferring;

                // fmt::println(
                //     "Transfer {} ended on cycle {} cycles_active_in_timestep = {}",
                //     ltid, transfer_end_cycle, cycles_active_in_curr_timestep);
                worst_case_transfer_end_cycle = std::max(worst_case_transfer_end_cycle, transfer_end_cycle);
            }
        }

        // compact live transfer list, removing completed transfers
        auto transfer_complete = [&transfer_state](const PETransferID id) {
            return transfer_state[id].total_bytes_transferred == transfer_state[id].params.total_bytes;
        };
        live_transfer_ids.erase(
            std::remove_if(live_transfer_ids.begin(), live_transfer_ids.end(), transfer_complete),
            live_transfer_ids.end());

        // TODO: if new phase is unlocked, add phase transfer's to tr_queue

        // end sim loop if all transfers have been completed
        if (live_transfer_ids.size() == 0 and transfer_queue.size() == 0) {
            timer.stop();
            stats.completed = true;
            stats.estimated_cycles = worst_case_transfer_end_cycle;
            stats.num_timesteps = timestep + 1;
            stats.simulated_cycles = stats.num_timesteps * cfg.cycles_per_timestep;
            stats.wallclock_runtime_us = timer.getElapsedTimeMicroSeconds();

            break;
        }

        if (curr_cycle > MAX_CYCLE_LIMIT) {
            log_error("Exceeded max cycle limit!");
            stats.completed = false;
            stats.estimated_cycles = MAX_CYCLE_LIMIT;
            stats.num_timesteps = timestep + 1;
            stats.simulated_cycles = stats.num_timesteps * cfg.cycles_per_timestep;
            return npeException(npeErrorCode::EXCEEDED_SIM_CYCLE_LIMIT);
        }

        // Advance time step
        curr_cycle += cfg.cycles_per_timestep;
        timestep++;
    }

    // visualize link congestion
    if (cfg.enable_visualizations) {
        printDiv("Average Link Utilization");
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

}  // namespace tt_npe
