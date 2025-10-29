// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include "npeEngine.hpp"

#include <map>
#include <unordered_map>
#include <vector>

#include "ScopedTimer.hpp"
#include "fmt/base.h"
#include "grid.hpp"
#include "npeAssert.hpp"
#include "npeCommon.hpp"
#include "npeDeviceTypes.hpp"
#include "npeDeviceModelFactory.hpp"
#include "npeStats.hpp"
#include "npeUtil.hpp"
#include "npeWorkload.hpp"

#include <algorithm> // For std::remove_if

namespace tt_npe {

npeEngine::npeEngine(const std::string &device_name, bool single_device_op) {
    model = npeDeviceModelFactory::createDeviceModel(device_name, single_device_op);
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
                model->route(wl_transfer.noc_type, wl_transfer.src, wl_transfer.dst));
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

    boost::unordered_flat_map<std::tuple<nocType, int, int, int>, std::vector<PETransferID>> bucketed_transfers;

    // the categorization here is either a nocLinkType, or a special int type
    // that specifies a pure-local transfer within a Tensix. This leads to more
    // realistic behavior when debugging, even if it costs a little correlation
    // accuracy.
    constexpr int LOCAL_NOC0_TRANSFER_TYPE = 1000;
    constexpr int LOCAL_NOC1_TRANSFER_TYPE = 2000;
    for (auto &tr : transfer_state) {
        int link_type = (tr.route.size() > 0)
                            ? int(model->getLinkAttributes(tr.route[0]).type)
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
            npeCheckpointID chkpt_id = dep_tracker.createCheckpoint(1, 0);
            transfer_state[id].depends_on = chkpt_id;
            int dependency_id = i - stride;
            TT_ASSERT(dependency_id >= 0);
            transfer_state[transfers[dependency_id]].required_by.push_back(chkpt_id);
        }
    }

    // infer serial dependencies based on transfer group id and index
    // (first pass to map transfer group and index to transfer id, 
    // second pass to create dependencies)
    constexpr CycleCount ETH_HOP_CYCLE_DELAY_BASE = 600;
    constexpr float ETH_HOP_CYCLE_DELAY_PER_BYTE = 0.1055f;
    boost::unordered_flat_map<std::pair<npeWorkloadTransferGroupID, npeWorkloadTransferGroupIndex>, 
        PETransferID> transfer_group_and_index_to_id;
    for (const auto &tr : transfer_state) {
        if (tr.params.transfer_group_id != -1 && tr.params.transfer_group_index != -1) {
            transfer_group_and_index_to_id[{tr.params.transfer_group_id, tr.params.transfer_group_index}] = tr.params.getID();
        }
    }

    for (const auto &tr : transfer_state) {
        if (tr.params.transfer_group_id != -1 && tr.params.transfer_group_parent != -1) {
            auto id = tr.params.getID();
            auto parent_id = transfer_group_and_index_to_id[{tr.params.transfer_group_id, tr.params.transfer_group_parent}];
            CycleCount checkpoint_delay = 0;

            switch (model->getArch()) {
                case DeviceArch::WormholeB0:
                    checkpoint_delay += WormholeB0DeviceModel::get_write_latency(tr.params.src.col, tr.params.src.row, 
                        std::get<Coord>(tr.params.dst).col, std::get<Coord>(tr.params.dst).row, tr.params.noc_type == nocType::NOC0 ? "NOC_0" : "NOC_1");
                    break;
                case DeviceArch::Blackhole:
                    checkpoint_delay += BlackholeDeviceModel::get_write_latency(tr.params.src.col, tr.params.src.row, 
                        std::get<Coord>(tr.params.dst).col, std::get<Coord>(tr.params.dst).row, tr.params.noc_type == nocType::NOC0 ? "NOC_0" : "NOC_1");
                default:
                    log_error("Unsupported architecture: {}", static_cast<unsigned char>(model->getArch()));
                    throw npeException(npeErrorCode::DEPENDENCY_GEN_FAILED);
            }

            // Only add ethernet hop delay if this is not a fabric mux route (i.e. the route is on a different device to previous)
            if (tr.params.src.device_id != transfer_state[parent_id].params.src.device_id) {
                checkpoint_delay += ETH_HOP_CYCLE_DELAY_BASE + ETH_HOP_CYCLE_DELAY_PER_BYTE*tr.params.packet_size;
            }
            
            npeCheckpointID chkpt_id = dep_tracker.createCheckpoint(1, checkpoint_delay);
            transfer_state[id].depends_on = chkpt_id;
            transfer_state[parent_id].required_by.push_back(chkpt_id);
        }
    }

    // check dependencies are exactly satisfied by iterating over all transfers required_by
    for (const auto& tr : transfer_state) {
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
        auto stats = std::get<std::unordered_map<DeviceID, npeStats>>(cong_result);

        // run congestion-free simulation just to estimate the number of cycles
        auto tmp_cfg = cfg;
        tmp_cfg.congestion_model_name = "none";
        tmp_cfg.emit_timeline_file = false;
        npeResult cong_free_result = runSinglePerfSim(wl, tmp_cfg);
        if (std::holds_alternative<npeException>(cong_free_result)) {
            return cong_result;
        }

        // combine results from congestion and congestion-free simulations and return
        auto cong_free_stats = std::get<std::unordered_map<DeviceID, npeStats>>(cong_free_result);
        for (auto& [device_id, deviceStats]: stats) { 
            deviceStats.estimated_cong_free_cycles = cong_free_stats[device_id].estimated_cycles;
        }
        return stats;

    } else {
        return runSinglePerfSim(wl, cfg);
    }
}

#define MESH_DEVICE -1
npeResult npeEngine::runSinglePerfSim(const npeWorkload &wl, const npeConfig &cfg) const {
    ScopedTimer timer("",true);
    // per device and full mesh stats
    std::unordered_map<DeviceID, npeStats> stats;
    for (auto device_id: model->getDeviceIDs()) {
        stats.emplace(device_id, npeStats()); 
    }
    stats.emplace(MESH_DEVICE, npeStats());

    // setup congestion tracking data structures
    bool enable_congestion_model = cfg.congestion_model_name != "none";
    // Initialize device state with appropriate dimensions for this device model
    auto device_state = model->initDeviceState();

    // create flattened list of transfers from workload
    auto transfer_state = initTransferState(wl);

    // create sorted queue of transfers to dispatch in main sim loop
    std::vector<TransferQueuePair> transfer_queue = createTransferQueue(transfer_state);

    // setup transfer dependencies within a single NIU
    npeTransferDependencyTracker dep_tracker = genDependencies(transfer_state);

    // main simulation loop
    std::vector<PETransferID> live_transfer_ids;
    live_transfer_ids.reserve(transfer_state.size());
    size_t timestep_idx = 0;
    CycleCount curr_cycle = cfg.cycles_per_timestep;
    while (true) {
        size_t start_of_timestep = (curr_cycle - cfg.cycles_per_timestep);
        size_t prev_start_of_timestep = start_of_timestep - cfg.cycles_per_timestep;
        auto in_prev_timestep = [&](size_t cycle) {
            return cycle >= prev_start_of_timestep && cycle < start_of_timestep;
        };

        // per device for stats, mesh for viz file
        for (auto& [device_id, deviceStats]: stats) { 
            deviceStats.insertTimestep(start_of_timestep, curr_cycle);
        }

        // transfer now-active transfers to live_transfers
        int transfers_activated = 0;
        size_t swap_pos = transfer_queue.size() - 1;
        for (int i = swap_pos; i >= 0 && transfer_queue[i].start_cycle <= curr_cycle; i--) {
            const auto &transfer = transfer_state[transfer_queue[i].id];
            if (dep_tracker.done(transfer.depends_on, curr_cycle)) {
                live_transfer_ids.push_back(transfer_queue[i].id);

                // if dependency is defined, adjust transfer start_cycle to be after dependency was completed
                if (dep_tracker.defined(transfer.depends_on)) {
                    transfer_state[transfer_queue[i].id].start_cycle = std::max(
                        transfer_state[transfer_queue[i].id].start_cycle,
                        dep_tracker.end_cycle_plus_delay(transfer.depends_on));
                }

                // move inserted element to the end of the transfer_queue to be discarded afterwards
                std::swap(transfer_queue[swap_pos--], transfer_queue[i]);
                transfers_activated++;
            }
        }

        // discard now inactive transfers from the end of the queue
        transfer_queue.resize(transfer_queue.size() - transfers_activated);

        // save list of live transfers
        // only update for mesh (for viz file)
        stats[MESH_DEVICE].per_timestep_stats.back().live_transfer_ids = live_transfer_ids;

        model->computeCurrentTransferRate(
            start_of_timestep,
            curr_cycle,
            transfer_state,
            live_transfer_ids,
            *device_state,
            enable_congestion_model);
        
        // update all stats
        for (auto& [device_id, deviceStats]: stats) { 
            TimestepStats &timestep_stats = deviceStats.per_timestep_stats.back();
            updateSimulationStats(
                *model,
                device_id,
                device_state->getLinkDemandGrid(),
                device_state->getNIUDemandGrid(),
                timestep_stats,
                model->getLinkBandwidth(nocLinkID(0)));
        }

        // Update all live transfer state
        for (auto ltid : live_transfer_ids) {
            auto &lt = transfer_state[ltid];
            TT_ASSERT(dep_tracker.done(lt.depends_on, curr_cycle));

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
            
            // per device for stats, mesh for viz file
            for (auto& [device_id, deviceStats]: stats) {
                if (device_id == MESH_DEVICE) 
                    continue; // fix later 
                deviceStats.completed = true;
                deviceStats.wallclock_runtime_us = timer.getElapsedTimeMicroSeconds();

                // specific golden counts for device from workload;
                auto [golden_start, golden_end] = wl.getGoldenResultCycles(device_id);
                // there is at least a ~20 cycle overhead between last noc event and kernel end timestamp
                deviceStats.golden_cycles = golden_end - golden_start - 20;

                // get simulated end specifically for this device (last event on this device issued during it's golden region)
                size_t worst_case_transfer_end_cycle = 0;
                for (const auto &tr : transfer_state) {
                    if (golden_start <= tr.params.phase_cycle_offset && tr.params.phase_cycle_offset <= golden_end
                        && (device_id == MESH_DEVICE || tr.params.src.device_id == device_id)) {
                        worst_case_transfer_end_cycle =
                        std::max(worst_case_transfer_end_cycle, (size_t)tr.end_cycle);
                    }
                }   
                deviceStats.estimated_cycles = worst_case_transfer_end_cycle;

                // use simulation start (= golden_start) and end (= worst_case_transfer_end_cycle) to
                // filter out timestep stats that are not part of device specfic simulation
                auto& per_timestep_stats = deviceStats.per_timestep_stats;
                auto start_idx = golden_start / cfg.cycles_per_timestep; // round down
                auto end_idx = (worst_case_transfer_end_cycle + cfg.cycles_per_timestep - 1) / cfg.cycles_per_timestep; // round up
                deviceStats.per_timestep_stats = std::vector<TimestepStats>(deviceStats.per_timestep_stats.begin() + start_idx, 
                    deviceStats.per_timestep_stats.begin() + end_idx);
                //for (int i = per_timestep_stats.size() - 1; i >= 0; i--) {
                //    if (!(golden_start <= per_timestep_stats[i].start_cycle && per_timestep_stats[i].start_cycle <= golden_end)) {
                //        per_timestep_stats.erase(per_timestep_stats.begin() + i);
                //    }
                //}

                // deviceStats.num_timesteps = timestep_idx + 1; // can be determined from timestep stats

                
            }

            break;
        }

        if (curr_cycle > MAX_CYCLE_LIMIT) {
            return npeException(npeErrorCode::EXCEEDED_SIM_CYCLE_LIMIT);
        }

        // Advance time step
        curr_cycle += cfg.cycles_per_timestep;
        timestep_idx++;
    }

    // all (might be used in viz file, can use mesh later as well)
    for (auto& [device_id, deviceStats]: stats) {
        deviceStats.computeSummaryStats(wl,*model);
    }

    //std::cout << stats[0].to_string() << std::endl;

    // only update for mesh (for viz file)
    if (cfg.emit_timeline_file) {
        stats[MESH_DEVICE].emitSimTimelineToFile(transfer_state, *model, wl, cfg);
    }

    return stats;
}
}  // namespace tt_npe
