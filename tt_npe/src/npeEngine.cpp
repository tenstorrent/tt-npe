#include "npeEngine.hpp"

#include <fmt/ostream.h>

#include <fstream>
#include <map>

#include "ScopedTimer.hpp"
#include "fmt/base.h"
#include "grid.hpp"
#include "magic_enum.hpp"
#include "npeAPI.hpp"
#include "npeAssert.hpp"
#include "npeCommon.hpp"
#include "npeDeviceModel.hpp"
#include "npeDeviceNode.hpp"
#include "npeWorkload.hpp"
#include "util.hpp"

namespace tt_npe {

npeEngine::npeEngine(const std::string &device_name) : model(device_name) {}

std::string npeStats::to_string(bool verbose) const {
    std::string output;

    float pct_delta =
        (golden_cycles)
            ? 100.0 * float(abs(int64_t(golden_cycles) - int64_t(estimated_cycles))) / golden_cycles
            : -1;
    output.append(fmt::format("  estimated cycles : {:5d}\n", estimated_cycles));
    output.append(fmt::format("  % error vs golden (total cycles) : {:.2f}%\n", pct_delta));
    if (verbose) {
        output.append(fmt::format("  num timesteps:     {:5d}\n", num_timesteps));
        output.append(fmt::format("  wallclock runtime: {:5d} us\n", wallclock_runtime_us));
    }
    return output;
}

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
    NIUUtilGrid &niu_util_grid,
    LinkUtilGrid &link_util_grid,
    TimestepStats &sim_stats) const {
    size_t cycles_per_timestep = end_timestep - start_timestep;

    // assume all links have identical bandwidth
    float LINK_BANDWIDTH = model.getLinkBandwidth({{0, 0}, nocLinkType::NOC0_EAST});

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

            // account for transfers starting mid-way into timestep, and derate effective
            // utilization accordingly
            CycleCount predicted_start = std::max(start_timestep, lt.start_cycle);
            float effective_util =
                float(end_timestep - predicted_start) / float(cycles_per_timestep);
            effective_util *= lt.curr_bandwidth;

            // track util at src and sink NIU
            auto src_niu_idx = size_t(
                lt.params.noc_type == nocType::NOC0 ? nocNIUType::NOC0_SRC : nocNIUType::NOC1_SRC);
            niu_util_grid(lt.params.src.row, lt.params.src.col, src_niu_idx) += effective_util;
            auto sink_niu_idx = size_t(
                lt.params.noc_type == nocType::NOC0 ? nocNIUType::NOC0_SINK
                                                    : nocNIUType::NOC1_SINK);
            niu_util_grid(lt.params.dst.row, lt.params.dst.col, sink_niu_idx) += effective_util;

            for (const auto &link : lt.route) {
                auto [r, c] = link.coord;
                link_util_grid(r, c, size_t(link.type)) += effective_util;
            }
        }

        // find highest util resource on each route to set bandwidth
        for (auto ltid : live_transfer_ids) {
            auto &lt = transfers[ltid];
            float max_link_util_on_route = 0;
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
            auto src_niu_idx = size_t(
                lt.params.noc_type == nocType::NOC0 ? nocNIUType::NOC0_SRC : nocNIUType::NOC1_SRC);
            auto src_util = niu_util_grid(lt.params.src.row, lt.params.src.col, src_niu_idx);

            auto sink_niu_idx = size_t(
                lt.params.noc_type == nocType::NOC0 ? nocNIUType::NOC0_SINK
                                                    : nocNIUType::NOC1_SINK);
            auto sink_util = niu_util_grid(lt.params.dst.row, lt.params.dst.col, sink_niu_idx);

            auto max_niu_util = std::max(src_util, sink_util);

            if (max_link_util_on_route > LINK_BANDWIDTH ||
                max_niu_util > lt.params.injection_rate) {
                float link_bw_derate = LINK_BANDWIDTH / max_link_util_on_route;
                float niu_bw_derate = lt.params.injection_rate / max_niu_util;
                float bw_derate = std::min(link_bw_derate, niu_bw_derate);

                lt.curr_bandwidth *= 1.0 - (grad_fac * (1.0f - bw_derate));
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
    sim_stats.avg_link_util = avgutil;
    sim_stats.max_link_util = max_link_util;
    // NOTE: copying these is a 10% runtime overhead
    sim_stats.link_util_grid = link_util_grid;
    sim_stats.niu_util_grid = niu_util_grid;

    //---------- link util visualization --------------------------
    //
    // usleep(100000);
    // static bool first = true;
    // if (first) {
    //    fmt::print("{}", TTYColorCodes::clear_screen);
    //}
    // first = false;
    // fmt::print("{}{}{}", TTYColorCodes::hide_cursor,
    // TTYColorCodes::move_cursor_topleft,TTYColorCodes::bold);

    // fmt::print("{}", TTYColorCodes::bold);
    // std::vector<std::string> link_type_to_arrow = { "⬆", "⬅", "⮕", "⬇" };
    // for (int y = 0; y < 2*model.getRows(); y++) {
    //     for (int x = 0; x < 2*model.getCols(); x++) {
    //         auto t = 2*(x%2) + (y%2);

    //        auto r = y/2;
    //        auto c = x/2;

    //        auto link_util = link_util_grid(r, c, t);

    //        auto core_type = model.getCoreType({r,c});
    //        auto bg_color = core_type == CoreType::WORKER ? "\e[48;2;55;55;55m" : "";

    //        auto color = link_util > 2 * LINK_BANDWIDTH ? TTYColorCodes::red
    //                     : link_util > LINK_BANDWIDTH   ? TTYColorCodes::yellow
    //                     : link_util > 0                ? TTYColorCodes::green
    //                                                    : TTYColorCodes::gray;

    //        fmt::print("{}{}", color, bg_color);
    //        fmt::print("{} ", link_type_to_arrow[t]);
    //        fmt::print("{}", TTYColorCodes::reset);
    //    }
    //    fmt::println("");
    //}
    // fmt::print("\n\n\n\n");
    // fmt::print("{}", TTYColorCodes::show_cursor);
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

    std::map<std::tuple<nocType, int, int>, std::vector<PETransferID>> bucketed_transfers;

    for (auto &tr : transfer_state) {
        bucketed_transfers[{tr.params.noc_type, tr.params.src.col, tr.params.src.row}].push_back(
            tr.params.getID());
    }

    for (auto &[niu, transfers] : bucketed_transfers) {
        auto &[id, col, row] = niu;
        // fmt::println("---- {} {} {} ----",magic_enum::enum_name(id),col,row);

        std::stable_sort(
            transfers.begin(),
            transfers.end(),
            [&transfer_state](const auto &lhs, const auto &rhs) {
                return transfer_state[lhs].start_cycle < transfer_state[rhs].start_cycle;
            });

        // asserting that n-3 transfer is roughly approximate to 2-VC effects
        int stride = 3;
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
    ScopedTimer timer("", true);
    npeStats stats;

    // setup congestion tracking data structures
    bool enable_congestion_model = cfg.congestion_model_name != "none";
    NIUUtilGrid niu_util_grid =
        Grid3D<float>(model.getRows(), model.getCols(), size_t(nocNIUType::NUM_NIU_TYPES));
    LinkUtilGrid link_util_grid =
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
                niu_util_grid,
                link_util_grid,
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
            log_error("Exceeded max cycle limit!");
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
        auto max_cong_stats = *std::max_element(
            stats.per_timestep_stats.begin(),
            stats.per_timestep_stats.end(),
            [](const auto &lhs, const auto &rhs) { return lhs.avg_link_util < rhs.avg_link_util; });
        float bar_scale = 80.f / max_cong_stats.avg_link_util;

        for (const auto &ts_stat : stats.per_timestep_stats) {
            std::string bar;
            auto congestion = ts_stat.avg_link_util;
            bar.insert(0, bar_scale * congestion, '=');
            bar.append(fmt::format(" {:.2f}", congestion));
            ts++;
            fmt::println("{:3d}|{}", ts, bar);
        }
    }

    if (cfg.emit_stats_as_json) {
        emitSimStats(cfg.stats_json_filepath, transfer_state, stats, cfg);
    }

    return stats;
}

void npeEngine::visualizeTransferSources(
    const std::vector<PETransferState> &transfer_state,
    const std::vector<PETransferID> &live_transfer_ids,
    size_t curr_cycle) const {
    Grid3D<int> src_util_grid(
        model.getRows(), model.getCols(), magic_enum::enum_count<nocType>(), 0);
    for (auto ltid : live_transfer_ids) {
        auto [r, c] = transfer_state[ltid].params.src;
        src_util_grid(r, c, int(transfer_state[ltid].params.noc_type)) += 1;
    }
    usleep(200000);
    fmt::print("{}", TTYColorCodes::clear_screen);
    fmt::println(" CYCLE {} ", curr_cycle);
    for (int r = 0; r < model.getRows(); r++) {
        for (int c = 0; c < model.getCols(); c++) {
            for (int t = 0; t < magic_enum::enum_count<nocType>(); t++) {
                auto val = src_util_grid(r, c, t);
                auto color = val > 1 ? TTYColorCodes::yellow : TTYColorCodes::green;
                fmt::print(
                    " {}{}{}{} ",
                    color,
                    TTYColorCodes::bold,
                    val ? std::to_string(val) : " ",
                    TTYColorCodes::reset);
            }
        }
        fmt::println("");
    }
}

void npeEngine::emitSimStats(
    const std::string &filepath,
    const std::vector<PETransferState> &transfer_state,
    const npeStats &stats,
    const npeConfig &cfg) const {
    std::ofstream os(filepath);
    if (!os) {
        log_error("Was not able to open stats file '{}'", filepath);
        return;
    } else {
        log("Writing timeline data to '{}'", filepath);
    }

    fmt::println(os, "{{");

    //---- emit common info ---------------------------------------------------
    fmt::println(os, "");
    fmt::println(os, R"("common_info" : {{)");
    fmt::println(os, R"(  "device_name"           : "{}",)", cfg.device_name);
    fmt::println(os, R"(  "cycles_per_timestep"   : {},)", cfg.cycles_per_timestep);
    fmt::println(os, R"(  "congestion_model_name" : "{}",)", cfg.congestion_model_name);
    fmt::println(os, R"(  "num_rows"              : {},)", model.getRows());
    fmt::println(os, R"(  "num_cols"              : {})", model.getCols());
    fmt::println(os, R"(}},)");

    //---- emit per transfer data ---------------------------------------------
    fmt::println(os, "");
    fmt::println(os, R"("noc_transfers": [ )");
    for (const auto &[i, tr] : enumerate(transfer_state)) {
        // open transfer dict
        fmt::println(os, "  {{ ");
        {
            fmt::println(os, R"(    "id"  : {}, )", tr.params.getID());
            fmt::println(os, R"(    "src" : [{},{}], )", tr.params.src.row, tr.params.src.col);
            fmt::println(os, R"(    "dst" : [{},{}], )", tr.params.dst.row, tr.params.dst.col);
            fmt::println(os, R"(    "total_bytes"    : {}, )", tr.params.total_bytes);
            fmt::println(os, R"(    "transfer_type"  : "{}", )", "UNICAST");
            fmt::println(
                os, R"(    "noc_type"       : "{}", )", magic_enum::enum_name(tr.params.noc_type));
            fmt::println(os, R"(    "injection_rate" : {}, )", tr.params.injection_rate);
            fmt::println(os, R"(    "start_cycle"    : {}, )", tr.start_cycle);
            fmt::println(os, R"(    "end_cycle"      : {}, )", tr.end_cycle);

            fmt::println(os, R"(    "route" : [ )");
            for (const auto &[i, link] : enumerate(tr.route)) {
                fmt::println(
                    os,
                    R"(        [{},{},"{}"]{} )",
                    link.coord.row,
                    link.coord.col,
                    magic_enum::enum_name(nocLinkType(link.type)),
                    (i == tr.route.size() - 1) ? "" : ",");
            }
            fmt::println(os, R"(    ])");
        }
        // close transfer dict
        auto comma = (i == transfer_state.size() - 1) ? "" : ",";
        fmt::println(os, "  }}{} ", comma);
    }
    fmt::println(os, "],");

    //---- emit per timestep data ---------------------------------------------
    fmt::println(os, "");
    fmt::println(os, R"("timestep_data" : [ )");
    for (const auto &[i, ts] : enumerate(stats.per_timestep_stats)) {
        // open timestep dict
        fmt::println(os, R"(  {{)");
        {
            fmt::println(os, R"(    "start_cycle" : {}, )", ts.start_cycle);
            fmt::println(os, R"(    "end_cycle"   : {}, )", ts.end_cycle);

            fmt::print(os, R"(    "active_transfers" : [)");
            auto ltids = ts.live_transfer_ids;
            std::sort(ltids.begin(), ltids.end());
            for (auto [i, ltid] : enumerate(ltids)) {
                if (i % 8 == 0)
                    fmt::print(os, "\n      ");
                auto comma = (i < ltids.size() - 1) ? "," : " ";
                fmt::print(os, R"({}{})", ltid, comma);
            }
            fmt::println(os, "\n    ],");

            fmt::println(os, R"(    "link_utilization" : [)");
            size_t krows = ts.link_util_grid.getRows();
            size_t kcols = ts.link_util_grid.getCols();
            size_t klinks = ts.link_util_grid.getItems();
            bool first = true;
            for (size_t r = 0; r < krows; r++) {
                for (size_t c = 0; c < kcols; c++) {
                    for (size_t l = 0; l < klinks; l++) {
                        float util = ts.link_util_grid(r, c, l);
                        if (util > 0.001) {
                            auto comma = (first) ? "" : ",";
                            first = false;
                            auto pct_util =
                                100.0 * (util / model.getLinkBandwidth({{r, c}, nocLinkType(l)}));
                            fmt::println(
                                os,
                                R"(        {}[{}, {}, "{}", {:.1f}])",
                                comma,
                                r,
                                c,
                                magic_enum::enum_name<nocLinkType>(nocLinkType(l)),
                                pct_util);
                        }
                    }
                }
            }
            fmt::println(os, "    ]");
        }
        // close timestep dict
        auto comma = (i == stats.per_timestep_stats.size() - 1) ? "" : ",";
        fmt::println(os, "\n  }}{}", comma);
    }
    fmt::println(os, "]");
    fmt::println(os, "}}");
}

}  // namespace tt_npe
