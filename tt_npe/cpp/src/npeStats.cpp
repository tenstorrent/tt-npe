// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025 Tenstorrent AI ULC

#include "npeStats.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <boost/unordered/unordered_flat_set.hpp>
#include <utility>
#include <vector>

#include "fmt/base.h"
#include "fmt/ostream.h"
#include "nlohmann/json.hpp"
#include "npeCommon.hpp"
#include "npeConfig.hpp"
#include "npeTransferState.hpp"
#include "npeDeviceModelIface.hpp"
#include "npeCompressionUtil.hpp"
#include "npeTimelineJsonWriter.hpp"
#include "npeTimelineUtil.hpp"

namespace tt_npe {

void TimestepSummaryAccumulator::add(const TimestepStats& timestep) {
    link_demand_sum += timestep.avg_link_demand;
    max_link_demand =
        std::max(max_link_demand, timestep.avg_link_demand);
    link_util_sum += timestep.avg_link_util;
    max_link_util = std::max(max_link_util, timestep.avg_link_util);
    niu_demand_sum += timestep.avg_niu_demand;
    max_niu_demand =
        std::max(max_niu_demand, timestep.avg_niu_demand);
    noc0_link_demand_sum += timestep.avg_noc0_link_demand;
    noc0_link_util_sum += timestep.avg_noc0_link_util;
    max_noc0_link_demand =
        std::max(max_noc0_link_demand, timestep.avg_noc0_link_demand);
    noc1_link_demand_sum += timestep.avg_noc1_link_demand;
    noc1_link_util_sum += timestep.avg_noc1_link_util;
    max_noc1_link_demand =
        std::max(max_noc1_link_demand, timestep.avg_noc1_link_demand);
    mcast_write_link_util_sum += timestep.avg_mcast_write_link_util;
}

npeStats::npeStats(const npeDeviceModel* device_model): device_model(device_model) {
    // create per device and full mesh stats
    for (auto device_id: device_model->getDeviceIDs()) {
        per_device_stats.emplace(device_id, deviceStats()); 
    }
    per_device_stats.emplace(MESH_DEVICE, deviceStats());
}

std::string npeStats::to_string(bool verbose) const {
    return per_device_stats.at(MESH_DEVICE).to_string(verbose);
}

void npeStats::computeSummaryStats(const npeWorkload& wl) {
    for (auto& [device_id, deviceStats]: per_device_stats) {
        deviceStats.computeSummaryStats(wl, *device_model, device_id);
    }
}

void npeStats::insertTimestep(
    Cycle start_cycle,
    Cycle end_cycle,
    const npeWorkload& wl,
    bool retain_mesh_timeline_details) {
    retain_mesh_timeline_details_ = retain_mesh_timeline_details;
    for (auto& [device_id, deviceStats]: per_device_stats) { 
        auto [golden_start, golden_end] = wl.getGoldenResultCycles(device_id);
        if (end_cycle >= golden_start) {
            TimestepStats* timestep_stats;
            if (device_id == MESH_DEVICE && retain_mesh_timeline_details) {
                deviceStats.per_timestep_stats.push_back({});
                timestep_stats = &deviceStats.per_timestep_stats.back();
            } else {
                deviceStats.current_timestep_stats.emplace();
                timestep_stats = &deviceStats.current_timestep_stats.value();
            }
            timestep_stats->start_cycle = start_cycle;
            timestep_stats->end_cycle = end_cycle;
        }
    }
}

TimestepStats* npeStats::currentTimestepStats(DeviceID device_id) {
    auto device_stats = per_device_stats.find(device_id);
    if (device_stats == per_device_stats.end()) {
        return nullptr;
    }
    if (!device_stats->second.per_timestep_stats.empty()) {
        return &device_stats->second.per_timestep_stats.back();
    }
    if (device_stats->second.current_timestep_stats.has_value()) {
        return &device_stats->second.current_timestep_stats.value();
    }
    return nullptr;
}

void npeStats::accumulateCurrentTimestepStats() {
    for (auto& [device_id, device_stats] : per_device_stats) {
        auto timestep = currentTimestepStats(device_id);
        if (timestep != nullptr) {
            device_stats.running_summary.add(*timestep);
        }
        device_stats.current_timestep_stats.reset();
    }
}

void npeStats::updateWorstCaseTransferEndCycle(DeviceID device_id, PETransferState& tr, std::pair<Cycle, Cycle> golden_cycles) {
    updateWorstCaseTransferEndCycle(
        device_id,
        tr.params.phase_cycle_offset,
        tr.end_cycle,
        golden_cycles);
}

void npeStats::updateWorstCaseTransferEndCycle(
    DeviceID device_id,
    Cycle phase_cycle_offset,
    Cycle end_cycle,
    std::pair<Cycle, Cycle> golden_cycles) {
    auto [golden_start, golden_end] = golden_cycles;
    auto& device_stats = per_device_stats[device_id];
    if (golden_start <= phase_cycle_offset &&
        phase_cycle_offset <= golden_end &&
        end_cycle > device_stats.worst_case_transfer_end_cycle) {
        device_stats.worst_case_transfer_end_cycle = end_cycle;
        device_stats.committed_summary = device_stats.running_summary;
    }
}

void npeStats::finishSimulation(size_t getElapsedTimeMicroSeconds, Cycle cycles_per_timestep, const npeWorkload &wl) {
    for (auto& [device_id, deviceStats]: per_device_stats) {
        deviceStats.completed = true;
        deviceStats.wallclock_runtime_us = getElapsedTimeMicroSeconds;

        // specific golden counts for device from workload;
        auto [golden_start, golden_end] = wl.getGoldenResultCycles(device_id);
        deviceStats.golden_cycles = golden_end - golden_start;   
        
        // skip devices with no transfers (worst_case_transfer_end_cycle not updated)
        if (deviceStats.worst_case_transfer_end_cycle <= golden_start) {
            deviceStats.estimated_cycles = 0;
            deviceStats.per_timestep_stats.clear();
            deviceStats.current_timestep_stats.reset();
            deviceStats.summary_timestep_count = 0;
            continue;
        }

        deviceStats.estimated_cycles = deviceStats.worst_case_transfer_end_cycle - golden_start;

        auto start_idx = golden_start / cycles_per_timestep; // round down
        auto end_idx = (deviceStats.worst_case_transfer_end_cycle + cycles_per_timestep - 1) / cycles_per_timestep; // round up
        deviceStats.summary_timestep_count = end_idx - start_idx + 1;
        if (device_id == MESH_DEVICE && retain_mesh_timeline_details_) {
            deviceStats.per_timestep_stats.resize(
                deviceStats.summary_timestep_count);
        } else {
            deviceStats.per_timestep_stats.clear();
        }
        deviceStats.current_timestep_stats.reset();
    }
}

std::string npeStats::deviceStats::to_string(bool verbose) const {
    std::string output;

    output.append(fmt::format("  congestion impact: {:5.1f}%\n", getCongestionImpact()));
    output.append(fmt::format("   estimated cycles: {:5d}\n", estimated_cycles));
    output.append(fmt::format("      golden cycles: {:5d}\n", golden_cycles));
    if (golden_cycles > 0) {
    output.append(fmt::format("   cycle pred error: {:5.1f}%\n", cycle_prediction_error));
    }
    output.append("\n");
    output.append(fmt::format("       DRAM BW Util: {:5.1f}% (using golden)\n", dram_bw_util));
    output.append(fmt::format("       DRAM BW Util: {:5.1f}% (using estimated)\n", dram_bw_util_sim));
    output.append(fmt::format("        ETH BW Util: {:5.1f}%\n", getAggregateEthBwUtil()));
    output.append("\n");
    output.append(fmt::format("      avg Link util: {:5.1f}%\n", overall_avg_link_util));
    output.append(
        fmt::format(
            " avg Mcast link util: {:5.1f}%\n",
            overall_avg_mcast_write_link_util));
    output.append(fmt::format("      max Link util: {:5.1f}%\n", overall_max_link_util));
    output.append("\n");
    output.append(fmt::format("    avg Link demand: {:5.1f}%\n", overall_avg_link_demand));
    output.append(fmt::format("    max Link demand: {:5.1f}%\n", overall_max_link_demand));
    output.append("\n");
    output.append(fmt::format("    avg NIU  demand: {:5.1f}%\n", overall_avg_niu_demand));
    output.append(fmt::format("    max NIU  demand: {:5.1f}%\n", overall_max_niu_demand));

    if (verbose) {
        output.append("\n");
        //output.append(fmt::format("    num timesteps: {:5d}\n", num_timesteps));
        output.append(fmt::format("   wallclock time: {:5d} us\n", wallclock_runtime_us));
    }
    return output;
}

void npeStats::deviceStats::computeSummaryStats(const npeWorkload& wl, const npeDeviceModel& device_model, DeviceID device_id) {
    if (summary_timestep_count != 0) {
        overall_avg_link_demand =
            committed_summary.link_demand_sum / summary_timestep_count;
        overall_max_link_demand = committed_summary.max_link_demand;
        overall_avg_link_util =
            committed_summary.link_util_sum / summary_timestep_count;
        overall_max_link_util = committed_summary.max_link_util;
        overall_avg_niu_demand =
            committed_summary.niu_demand_sum / summary_timestep_count;
        overall_max_niu_demand = committed_summary.max_niu_demand;
        overall_avg_noc0_link_demand =
            committed_summary.noc0_link_demand_sum / summary_timestep_count;
        overall_avg_noc0_link_util =
            committed_summary.noc0_link_util_sum / summary_timestep_count;
        overall_max_noc0_link_demand =
            committed_summary.max_noc0_link_demand;
        overall_avg_noc1_link_demand =
            committed_summary.noc1_link_demand_sum / summary_timestep_count;
        overall_avg_noc1_link_util =
            committed_summary.noc1_link_util_sum / summary_timestep_count;
        overall_max_noc1_link_demand =
            committed_summary.max_noc1_link_demand;
        overall_avg_mcast_write_link_util =
            committed_summary.mcast_write_link_util_sum /
            summary_timestep_count;
    }
    cycle_prediction_error = golden_cycles == 0
        ? 0
        : 100.0 *
            float(int64_t(estimated_cycles) - int64_t(golden_cycles)) /
            golden_cycles;

    // compute aggregate and per controller dram bw utilization
    size_t read_bytes = 0;
    size_t write_bytes = 0;
    std::unordered_map<uint32_t, size_t> dram_tx_bytes_per_controller;
    for (const auto &phase : wl.getPhases()) {
        for (const auto &transfer : phase.transfers) {
            if (device_id == MESH_DEVICE || device_id == transfer.src.device_id) {
                if (device_model.getCoreType(transfer.src) == CoreType::DRAM) {
                    // read from DRAM
                    read_bytes += transfer.total_bytes;
                    dram_tx_bytes_per_controller[device_model.getDramControllerIDForCore(transfer.src)] += transfer.total_bytes;
                } else if (
                    // write to DRAM
                    std::holds_alternative<Coord>(transfer.dst) &&
                    device_model.getCoreType(std::get<Coord>(transfer.dst)) == CoreType::DRAM) {
                    write_bytes += transfer.total_bytes;
                    dram_tx_bytes_per_controller[device_model.getDramControllerIDForCore(std::get<Coord>(transfer.dst))] += transfer.total_bytes;
                }
            }
        }
    }
    size_t total_bytes = read_bytes + write_bytes;

    size_t num_chips = device_id == MESH_DEVICE ? device_model.getNumChips() : 1;
    double total_dram_bandwidth_over_golden_cycles = golden_cycles * device_model.getDRAMBandwidthPerChip() * num_chips;
    double total_dram_bandwidth_over_estimated_cycles = estimated_cycles * device_model.getDRAMBandwidthPerChip() * num_chips;
    this->dram_bw_util = total_dram_bandwidth_over_golden_cycles == 0
        ? 0
        : (total_bytes / total_dram_bandwidth_over_golden_cycles) * 100;
    this->dram_bw_util_sim = total_dram_bandwidth_over_estimated_cycles == 0
        ? 0
        : (total_bytes / total_dram_bandwidth_over_estimated_cycles) * 100;

    for (auto [controller, dram_tx_bytes]: dram_tx_bytes_per_controller) {
        double dram_bandwidth_per_controller_over_golden_cycles = golden_cycles * device_model.getDRAMBandwidthPerController();
        this->dram_bw_util_per_controller[controller] =
            dram_bandwidth_per_controller_over_golden_cycles == 0
            ? 0
            : (dram_tx_bytes /
               dram_bandwidth_per_controller_over_golden_cycles) *
                100;
    }

    // compute eth bw utilization per core
    std::unordered_map<Coord, size_t> eth_tx_bytes_per_core;
    for (const auto &phase : wl.getPhases()) {
        for (const auto &transfer : phase.transfers) {
            // write to ETH core will result in a tx from that core
            if (device_id == MESH_DEVICE || device_id == transfer.src.device_id) {
                if (std::holds_alternative<Coord>(transfer.dst) &&
                    device_model.getCoreType(std::get<Coord>(transfer.dst)) == CoreType::ETH) { 
                    eth_tx_bytes_per_core[std::get<Coord>(transfer.dst)] += transfer.total_bytes;
                }
            }
        }
    }

    for (auto [core, eth_tx_bytes]: eth_tx_bytes_per_core) {
        double total_eth_bandwidth_over_golden_cycles = golden_cycles * device_model.getEthBandwidthPerLink();
        this->eth_bw_util_per_core[core] =
            total_eth_bandwidth_over_golden_cycles == 0
            ? 0
            : (eth_tx_bytes / total_eth_bandwidth_over_golden_cycles) * 100;
    }
}

bool v0TimelineSerialization(
    TimelineJsonWriter& writer,
    const npeStats::deviceStats &device_stats,
    const npeConfig &cfg,
    const npeDeviceModel &model,
    const npeWorkload &wl,
    const std::vector<PETransferState> &transfer_state) {
    //---- emit common info ---------------------------------------------------
    nlohmann::json common_info = {
        {"device_name", cfg.device_name},
        {"cycles_per_timestep", cfg.cycles_per_timestep},
        {"congestion_model_name", cfg.congestion_model_name},
        {"num_rows", model.getRows()},
        {"num_cols", model.getCols()},
        // emit overall stats from the simulation
        {"dram_bw_util", device_stats.dram_bw_util},
        {"link_util", device_stats.overall_avg_link_util},
        {"mcast_write_link_util", device_stats.overall_avg_mcast_write_link_util},
        {"link_demand", device_stats.overall_avg_link_demand},
        {"max_link_demand", device_stats.overall_max_link_demand}};
    if (!writer.writeField("common_info", common_info)) {
        return false;
    }

    //---- emit per transfer data ---------------------------------------------
    if (!writer.beginArrayField("noc_transfers")) {
        return false;
    }
    for (const auto &tr : transfer_state) {
        nlohmann::json transfer;
        transfer["id"] = tr.params.getID();
        transfer["src"] = {tr.params.src.row, tr.params.src.col};
        transfer["dst"] = nlohmann::json::array();
        if (std::holds_alternative<Coord>(tr.params.dst)) {
            auto dst = std::get<Coord>(tr.params.dst);
            transfer["dst"].push_back({dst.row, dst.col});
        } else {
            auto mcast_pair = std::get<MulticastCoordSet>(tr.params.dst);
            for (const auto &c : mcast_pair) {
                if (model.getCoreType(c) == CoreType::WORKER) {
                    transfer["dst"].push_back({c.row, c.col});
                }
            }
        }
        transfer["total_bytes"] = tr.params.total_bytes;
        transfer["noc_type"] = magic_enum::enum_name(tr.params.noc_type);
        transfer["injection_rate"] = tr.params.injection_rate;
        transfer["start_cycle"] = tr.start_cycle;
        transfer["end_cycle"] = tr.end_cycle;
        transfer["noc_event_type"] = tr.params.noc_event_type;

        std::string route_src_entrypoint =
            tr.params.noc_type == nocType::NOC0 ? "NOC0_IN" : "NOC1_IN";
        std::string route_dst_exitpoint =
            tr.params.noc_type == nocType::NOC0 ? "NOC0_OUT" : "NOC1_OUT";

        transfer["route"] = nlohmann::json::array();
        auto &json_route = transfer["route"];

        json_route.push_back({tr.params.src.row, tr.params.src.col, route_src_entrypoint});
        for (const auto &link : tr.route) {
            auto link_attr = model.getLinkAttributes(link);
            json_route.push_back(
                {link_attr.coord.row, link_attr.coord.col, magic_enum::enum_name(nocLinkType(link_attr.type))});
        }

        // add destination exitpoint elements to route
        if (std::holds_alternative<Coord>(tr.params.dst)) {
            auto dst = std::get<Coord>(tr.params.dst);
            json_route.push_back({dst.row, dst.col, route_dst_exitpoint});
        } else {
            auto mcast_pair = std::get<MulticastCoordSet>(tr.params.dst);
            for (const auto &dst : mcast_pair) {
                if (model.getCoreType(dst) == CoreType::WORKER) {
                    json_route.push_back({dst.row, dst.col, route_dst_exitpoint});
                }
            }
        }

        if (!writer.writeArrayItem(transfer)) {
            return false;
        }
    }
    if (!writer.endArray()) {
        return false;
    }

    //---- emit per timestep data ---------------------------------------------
    auto& per_timestep_stats = device_stats.per_timestep_stats;
    if (!writer.beginArrayField("timestep_data")) {
        return false;
    }
    std::vector<TimelineTransferInterval> transfer_intervals;
    transfer_intervals.reserve(transfer_state.size());
    for (const auto& transfer : transfer_state) {
        transfer_intervals.push_back(
            {transfer.params.getID(),
             transfer.params.getID(),
             transfer.start_cycle,
             transfer.end_cycle});
    }
    TimelineActiveTransferSweep active_transfer_sweep(std::move(transfer_intervals));
    for (const auto &ts : per_timestep_stats) {
        if (!writer.beginObjectItem()) {
            return false;
        }
        active_transfer_sweep.update(ts.start_cycle, ts.end_cycle);
        if (!writer.beginArrayField("active_transfers")) {
            return false;
        }
        bool active_transfers_written = true;
        active_transfer_sweep.forEachActiveTransfer([&](int logical_id) {
            active_transfers_written =
                writer.writeArrayItem(logical_id) && active_transfers_written;
        });
        if (!active_transfers_written || !writer.endArray() ||
            !writer.writeField("avg_link_demand", ts.avg_link_demand) ||
            !writer.writeField("avg_link_util", ts.avg_link_util) ||
            !writer.writeField("end_cycle", ts.end_cycle) ||
            !writer.beginArrayField("link_demand")) {
            return false;
        }

        for (const auto& demand_entry : ts.significant_niu_demands) {
            nocNIUAttr attr =
                model.getNIUAttributes(static_cast<nocNIUID>(demand_entry.id));
            std::string terminal_name;
            switch (attr.type) {
                case nocNIUType::NOC0_SRC: terminal_name = "NOC0_IN"; break;
                case nocNIUType::NOC0_SINK: terminal_name = "NOC0_OUT"; break;
                case nocNIUType::NOC1_SRC: terminal_name = "NOC1_IN"; break;
                case nocNIUType::NOC1_SINK: terminal_name = "NOC1_OUT"; break;
                default: terminal_name = "UNKNOWN"; break;
            }
            if (!writer.writeArrayItem(
                    nlohmann::json::array(
                        {attr.coord.row,
                         attr.coord.col,
                         terminal_name,
                         demand_entry.demand}))) {
                return false;
            }
        }
        for (const auto& demand_entry : ts.significant_link_demands) {
            nocLinkAttr link_attr =
                model.getLinkAttributes(static_cast<nocLinkID>(demand_entry.id));
            if (!writer.writeArrayItem(
                    nlohmann::json::array(
                        {link_attr.coord.row,
                         link_attr.coord.col,
                         magic_enum::enum_name<nocLinkType>(link_attr.type),
                         demand_entry.demand}))) {
                return false;
            }
        }
        if (!writer.endArray() ||
            !writer.writeField(
                "mcast_write_link_util", ts.avg_mcast_write_link_util) ||
            !writer.writeField("start_cycle", ts.start_cycle) ||
            !writer.endObject()) {
            return false;
        }
    }

    return writer.endArray();
}

bool isFabricTransferType(const std::string& noc_event_type) {
    return noc_event_type.starts_with("FABRIC_");
}

// Region struct for split file generation
struct TimelineRegion {
    Timestep start_timestep_idx;  // inclusive
    Timestep end_timestep_idx;    // exclusive
    Cycle start_cycle;
    Cycle end_cycle;

    bool partiallyContainedInRegion(
        Cycle start,
        Cycle end) const {
        bool starts_in_region = (start >= start_cycle && start < end_cycle);
        bool ends_in_region = (end > start_cycle && end <= end_cycle);
        return starts_in_region || ends_in_region;
    }
    
    bool fullyContainedInRegion(
        Cycle start,
        Cycle end) const {
        return start >= start_cycle && end <= end_cycle;
    }
};

bool v1TimelineSerialization(
    TimelineJsonWriter& writer,
    const npeStats::deviceStats &device_stats,
    const npeConfig &cfg,
    const npeDeviceModel &model,
    const npeWorkload &wl,
    const std::vector<PETransferState> &transfer_state,
    const std::optional<TimelineRegion>& region = std::nullopt,
    const std::optional<nlohmann::ordered_json>& split_info = std::nullopt) {
    //---- emit common info ---------------------------------------------------
    std::string arch_string =
        model.getArch() == DeviceArch::WormholeB0 ? "wormhole_b0" : "blackhole";
    nlohmann::ordered_json common_info = {
        {"version", npeStats::CURRENT_TIMELINE_SCHEMA_VERSION},
        {"mesh_device", cfg.device_name},
        {"arch", arch_string},
        {"cycles_per_timestep", cfg.cycles_per_timestep},
        {"congestion_model_name", cfg.congestion_model_name},
        {"num_rows", model.getRows()},
        {"num_cols", model.getCols()},
        // emit overall stats from the simulation
        {"dram_bw_util", device_stats.dram_bw_util},
        {"link_util", device_stats.overall_avg_link_util},
        {"mcast_write_link_util", device_stats.overall_avg_mcast_write_link_util},
        {"link_demand", device_stats.overall_avg_link_demand},
        {"max_link_demand", device_stats.overall_max_link_demand},

        {"noc",
         {{"NOC0",
           {{"avg_link_demand", device_stats.overall_avg_noc0_link_demand},
            {"avg_link_util", device_stats.overall_avg_noc0_link_util},
            {"max_link_demand", device_stats.overall_max_noc0_link_demand}}},
          {"NOC1",
           {{"avg_link_demand", device_stats.overall_avg_noc1_link_demand},
            {"avg_link_util", device_stats.overall_avg_noc1_link_util},
            {"max_link_demand", device_stats.overall_max_noc1_link_demand}}}}}};
    if (split_info.has_value()) {
        common_info["split_info"] = split_info.value();
    }
    if (!writer.writeField("common_info", common_info)) {
        return false;
    }

    //---- emit topology info ---------------------------------------------------
    nlohmann::ordered_json chips = nlohmann::ordered_json::object();
    bool multichip = model.getNumChips() > 1;
    if (multichip) {
        if (cfg.topology_json.empty()){
            log_error("Cluster coordinates JSON file is required for serializing timeline for multichip devices\n");
            return false;
        }

        std::ifstream ifs(cfg.topology_json);
        if (!ifs.is_open()) {
            log_error("Failed to open cluster coordinates JSON file: {}\n", cfg.topology_json);
            return false;
        }

        try {
            nlohmann::json root_json_data = nlohmann::json::parse(ifs);
            if (root_json_data.is_object() &&
                root_json_data.contains("device_id_to_fabric_node_id") &&
                root_json_data["device_id_to_fabric_node_id"].is_object() &&
                root_json_data.contains("mesh_shapes") && 
                root_json_data["mesh_shapes"].is_array() &&
                root_json_data["mesh_shapes"].size() == 1 &&
                root_json_data["mesh_shapes"][0].contains("shape") &&
                root_json_data["mesh_shapes"][0]["shape"].is_array() &&
                root_json_data["mesh_shapes"][0]["shape"].size() == 2) {
                const auto &coords_map = root_json_data["device_id_to_fabric_node_id"];
                // assume single mesh (don't use mesh id)
                const auto &mesh_shape = std::pair(root_json_data["mesh_shapes"][0]["shape"][0].get<int>(), 
                    root_json_data["mesh_shapes"][0]["shape"][1].get<int>());
                for (auto const &[chip_id_str, coord_item] : coords_map.items()) {
                    if (coord_item.is_array() && coord_item.size() == 2 &&
                        coord_item[0].is_number_integer() && coord_item[1].is_number_integer()) {
                        auto ew_dim = mesh_shape.second;
                        chips[chip_id_str] = nlohmann::json::array(
                            {coord_item[1].get<int>() % ew_dim,
                             coord_item[1].get<int>() / ew_dim,
                             0, 
                             0});
                    } else {
                        log_error("Invalid cluster_coordinates.json entry: {} in cluster_coordinates.json file\n", chip_id_str);
                        return false;
                    }
                }
            }
        } catch (const nlohmann::json::parse_error &e) {
            log_error("Failed to parse cluster_coordinates.json file:\n{}\n", e.what());
            return false;
        }
    } else {
        // single chip case; set all coordinates to 0
        chips = nlohmann::ordered_json{{"0", {0, 0, 0, 0}}};
    }
    if (!writer.writeField("chips", chips)) {
        return false;
    }

    //---- emit noc transfer info ---------------------------------------------------
    // Construct mapping of transfer group IDs <-> transfer IDs. Timeline output
    // groups transfers that share the same transfer group ID into a single
    // logical transfer
    boost::unordered_flat_map<npeWorkloadTransferGroupID, std::vector<PETransferID>> transfer_groups;
    boost::unordered_flat_map<PETransferID, npeWorkloadTransferGroupID> transfer_id_to_transfer_group;
    int dummy_transfer_group_id = wl.getNumTransferGroups();
    for (const auto &tr : transfer_state) {
        if (tr.params.transfer_group_id != -1 && tr.params.transfer_group_index != -1) {
            transfer_groups[tr.params.transfer_group_id].push_back(tr.params.getID());
            transfer_id_to_transfer_group[tr.params.getID()] = tr.params.transfer_group_id;
        } else {
            // treat transfers without transfer group as being in their own dummy transfer group;
            // this simplifies the following transfer serialization code
            transfer_groups[dummy_transfer_group_id].push_back(tr.params.getID());
            transfer_id_to_transfer_group[tr.params.getID()] = dummy_transfer_group_id;
            dummy_transfer_group_id++;
        }
    }

    // Consistency checks for transfer group maps 
    boost::unordered_flat_set<PETransferID> all_transfers;
    for (const auto &[group_id, transfers] : transfer_groups) {

        // check that transfer group itself contains no duplicate transfer IDs
        boost::unordered_flat_set<PETransferID> internal_group_consistency_check;
        for (const auto &transfer_id : transfers) {
            TT_ASSERT(
                internal_group_consistency_check.insert(transfer_id).second,
                "Transfer ID {} exists in multiple transfer groups!",
                transfer_id);
        }

        // check that transfer exists in transfer_id_to_transfer_group and that it maps to the correct group
        for (const auto &transfer_id : transfers) {
            TT_ASSERT(transfer_id_to_transfer_group.contains(transfer_id));
            TT_ASSERT(transfer_id_to_transfer_group[transfer_id] == group_id);

            // Check for overlap between transfer groups
            TT_ASSERT(
                all_transfers.insert(transfer_id).second,
                "Transfer ID {} exists in multiple transfer groups!",
                transfer_id);
        }

        // Check if dummy transfer groups contain only one transfer
        if (group_id >= wl.getNumTransferGroups()) {
            TT_ASSERT(
                transfers.size() == 1, "Dummy transfer group contains more than one transfer!");
        }
    }

    // Check if all entries in transfer_id_to_transfer_group exist in transfer_groups
    for (const auto& [transfer_id, group_id] : transfer_id_to_transfer_group) {
        TT_ASSERT(transfer_groups.contains(group_id));
        TT_ASSERT(
            std::find(
                transfer_groups[group_id].begin(), transfer_groups[group_id].end(), transfer_id) !=
            transfer_groups[group_id].end());
    }

    // helper function to flatten noc destination into a list of coordinates
    auto get_destination_list = [&model](const NocDestination& destination){ 
        auto destination_list = nlohmann::ordered_json::array();
        if (std::holds_alternative<Coord>(destination)) {
            auto dst = std::get<Coord>(destination);
            destination_list.push_back({dst.device_id, dst.row, dst.col});
        } else {
            auto mcast_pair = std::get<MulticastCoordSet>(destination);
            for (const auto &c : mcast_pair) {
                if (model.getCoreType(c) == CoreType::WORKER) {
                    destination_list.push_back({c.device_id, c.row, c.col});
                }
            }
        }
        return destination_list;
    };

    boost::unordered_flat_set<npeWorkloadTransferGroupID> inactive_defined_groups;
    if (!writer.beginArrayField("noc_transfers")) {
        return false;
    }

    // iterate over all transfer groups - each transfer group in tt-npe is a
    // logical transfer in the output timeline
    for (auto& [transfer_group_id, component_transfers] : transfer_groups) {
        TT_ASSERT(component_transfers.size() >= 1);
        std::stable_sort(
            component_transfers.begin(),
            component_transfers.end(),
            [&transfer_state](const auto &lhs, const auto &rhs) {
                return transfer_state[lhs].params.transfer_group_index <
                       transfer_state[rhs].params.transfer_group_index;
            });

        // start point of transfer group is found in first route
        const auto& first_transfer = component_transfers.front();
        // end point of transfer group is found in last route
        const auto& last_transfer = component_transfers.back();

        // If region is specified, filter transfer groups: include if starts or ends within region
        if (region.has_value()) {
            Cycle group_start_cycle = transfer_state[first_transfer].start_cycle;
            Cycle group_end_cycle = transfer_state[last_transfer].end_cycle;
            if (!region.value().fullyContainedInRegion(group_start_cycle, group_end_cycle)) {
                continue;
            }
        }

        nlohmann::ordered_json transfer;
        transfer["id"] = transfer_group_id;

        auto src_coord = transfer_state[first_transfer].params.src;
        transfer["src"] = {src_coord.device_id, src_coord.row, src_coord.col};
        transfer["total_bytes"] = transfer_state[first_transfer].params.total_bytes;
        transfer["start_cycle"] = transfer_state[first_transfer].start_cycle;
        transfer["noc_event_type"] = transfer_state[first_transfer].params.noc_event_type;
        transfer["fabric_event_type"] = isFabricTransferType(transfer_state[first_transfer].params.noc_event_type);
        transfer["zones"] = transfer_state[first_transfer].params.enclosing_zone_path;

        auto destination = transfer_state[last_transfer].params.dst;
        transfer["end_cycle"] = transfer_state[last_transfer].end_cycle;
        transfer["dst"] = get_destination_list(destination);

        auto routes_in_transfer = nlohmann::ordered_json::array();
        for (const auto& component_id : component_transfers) {
            nlohmann::ordered_json route_segment;
            const auto& tr = transfer_state[component_id];
            route_segment["device_id"] = tr.params.src.device_id;
            route_segment["src"] = {tr.params.src.device_id, tr.params.src.row, tr.params.src.col};
            route_segment["dst"] = get_destination_list(tr.params.dst);
            route_segment["noc_type"] = magic_enum::enum_name(tr.params.noc_type);
            route_segment["injection_rate"] = tr.params.injection_rate;
            route_segment["start_cycle"] = tr.start_cycle;
            route_segment["end_cycle"] = tr.end_cycle;

            std::string route_src_entrypoint =
                tr.params.noc_type == nocType::NOC0 ? "NOC0_IN" : "NOC1_IN";
            std::string route_dst_exitpoint =
                tr.params.noc_type == nocType::NOC0 ? "NOC0_OUT" : "NOC1_OUT";

            auto route_segment_links = nlohmann::ordered_json::array();
            route_segment_links.push_back({tr.params.src.device_id, tr.params.src.row, tr.params.src.col, route_src_entrypoint});
            for (const auto& link : tr.route) {
                const auto& link_attr = model.getLinkAttributes(link);
                route_segment_links.push_back({link_attr.coord.device_id, link_attr.coord.row, link_attr.coord.col, magic_enum::enum_name(nocLinkType(link_attr.type))});
            }
            for (const auto& dst : get_destination_list(tr.params.dst)) {
                route_segment_links.push_back({dst[0], dst[1], dst[2], route_dst_exitpoint});
            }
            route_segment["links"] = route_segment_links;

            routes_in_transfer.push_back(route_segment);
        }
        transfer["route"] = routes_in_transfer;

        inactive_defined_groups.insert(transfer_group_id);
        if (!writer.writeArrayItem(transfer)) {
            return false;
        }
    }
    if (!writer.endArray()) {
        return false;
    }

    //---- emit zones ---------------------------------------------
    if (!writer.beginArrayField("zones")) {
        return false;
    }
    for (auto& [core_proc, zones]: wl.getZones()) {
        // this is the root json object for this core and proc, containing its nested structure of zones
        auto root_zone_json = nlohmann::ordered_json::object();
        root_zone_json["core"] = {core_proc.first.device_id, core_proc.first.row, core_proc.first.col};
        root_zone_json["proc"] = magic_enum::enum_name(core_proc.second);
        ZoneIterator zone_iterator(zones);
        std::unordered_map<npeZone, nlohmann::ordered_json*> zone_jsons;  
        while (!zone_iterator.isEnd()) {
            auto& next_zone = zone_iterator.getNextZone();

            // if start zone, add next_zone zone as a child to current parent zone
            if (next_zone.zone_phase == ZonePhase::ZONE_START) {
                // create child zone json
                auto& child_zone = next_zone;
                nlohmann::ordered_json child_zone_json = {
                    {"start", child_zone.timestamp},
                    {"zones", nlohmann::ordered_json::array()}
                };

                // add as child
                if (zone_iterator.getEnclosingZones().empty()) {
                    root_zone_json["zones"].push_back(child_zone_json);
                    zone_jsons[child_zone] = &(root_zone_json["zones"].back());
                }
                else {
                    auto& parent_zone = zone_iterator.getLastEnclosingZone().first;
                    auto& parent_zone_json = *zone_jsons[parent_zone];
                    parent_zone_json["zones"].push_back(child_zone_json);
                    zone_jsons[child_zone] = &(parent_zone_json["zones"].back());
                }
            }
            else { // if end zone, add id and end ts to corresponding json
                auto& corresponding_start_zone = zone_iterator.getLastEnclosingZone().first;
                auto& zone_count = zone_iterator.getLastEnclosingZone().second;
                auto& zone_json = *zone_jsons[corresponding_start_zone];
                zone_json["id"] = corresponding_start_zone.zone + "[" + std::to_string(zone_count) + "]";
                zone_json["end"] = next_zone.timestamp;

                // If region filtering is enabled, check if zone is fully contained
                // If not contained, remove from parent's zones array
                if (region.has_value() && !region.value().fullyContainedInRegion(zone_json["start"], zone_json["end"])) {
                    // Find parent's zone and remove this zone from it
                    nlohmann::ordered_json* parent_zone_json;
                    if (zone_iterator.getEnclosingZones().size() == 1) {
                        parent_zone_json = &root_zone_json;
                    } else {
                        // Get the second-to-last enclosing zone (the parent)
                        auto& parent_zone = zone_iterator.getEnclosingZones()[zone_iterator.getEnclosingZones().size() - 2].first;
                        parent_zone_json = zone_jsons[parent_zone];
                    }
                    // Remove this zone (last element)
                    auto& zones_array = (*parent_zone_json)["zones"];
                    zones_array.erase(zones_array.size() - 1);
                }
            }

            ++zone_iterator;
        }

        if (!writer.writeArrayItem(root_zone_json)) {
            return false;
        }
    }
    if (!writer.endArray()) {
        return false;
    }

    //---- emit per timestep data ---------------------------------------------
    auto& per_timestep_stats = device_stats.per_timestep_stats;
    if (!writer.beginArrayField("timestep_data")) {
        return false;
    }
    std::vector<TimelineTransferInterval> transfer_intervals;
    transfer_intervals.reserve(transfer_state.size());
    for (const auto& transfer : transfer_state) {
        transfer_intervals.push_back(
            {transfer.params.getID(),
             transfer_id_to_transfer_group.at(transfer.params.getID()),
             transfer.start_cycle,
             transfer.end_cycle});
    }
    TimelineActiveTransferSweep active_transfer_sweep(std::move(transfer_intervals));
    TimelineTimestepRange timestep_range{0, per_timestep_stats.size()};
    if (region.has_value()) {
        timestep_range = {
            region->start_timestep_idx, region->end_timestep_idx};
    }
    for (const auto& ts :
         timelineTimestepSubrange(per_timestep_stats, timestep_range)) {
        if (!writer.beginObjectItem() ||
            !writer.writeField("start_cycle", ts.start_cycle) ||
            !writer.writeField("end_cycle", ts.end_cycle) ||
            !writer.beginArrayField("active_transfers")) {
            return false;
        }
        active_transfer_sweep.update(ts.start_cycle, ts.end_cycle);
        bool active_transfers_written = true;
        active_transfer_sweep.forEachActiveTransfer([&](int logical_id) {
            inactive_defined_groups.erase(logical_id);
            active_transfers_written =
                writer.writeArrayItem(logical_id) && active_transfers_written;
        });
        if (!active_transfers_written || !writer.endArray() ||
            !writer.beginArrayField("link_demand")) {
            return false;
        }

        for (const auto& demand_entry : ts.significant_niu_demands) {
            nocNIUAttr attr =
                model.getNIUAttributes(static_cast<nocNIUID>(demand_entry.id));
            std::string terminal_name;
            switch (attr.type) {
                case nocNIUType::NOC0_SRC: terminal_name = "NOC0_IN"; break;
                case nocNIUType::NOC0_SINK: terminal_name = "NOC0_OUT"; break;
                case nocNIUType::NOC1_SRC: terminal_name = "NOC1_IN"; break;
                case nocNIUType::NOC1_SINK: terminal_name = "NOC1_OUT"; break;
                default: terminal_name = "UNKNOWN"; break;
            }
            if (!writer.writeArrayItem(
                    nlohmann::ordered_json::array(
                        {attr.coord.device_id,
                         attr.coord.row,
                         attr.coord.col,
                         terminal_name,
                         demand_entry.demand}))) {
                return false;
            }
        }

        for (const auto& demand_entry : ts.significant_link_demands) {
            nocLinkAttr link_attr =
                model.getLinkAttributes(static_cast<nocLinkID>(demand_entry.id));
            if (!writer.writeArrayItem(
                    nlohmann::ordered_json::array(
                        {link_attr.coord.device_id,
                         link_attr.coord.row,
                         link_attr.coord.col,
                         magic_enum::enum_name<nocLinkType>(link_attr.type),
                         demand_entry.demand}))) {
                return false;
            }
        }
        nlohmann::ordered_json noc = {
            {"NOC0",
             {{"avg_link_demand", ts.avg_noc0_link_demand},
              {"avg_link_util", ts.avg_noc0_link_util},
              {"max_link_demand", ts.max_noc0_link_demand}}},
            {"NOC1",
             {{"avg_link_demand", ts.avg_noc1_link_demand},
              {"avg_link_util", ts.avg_noc1_link_util},
              {"max_link_demand", ts.max_noc1_link_demand}}}};

        if (!writer.endArray() ||
            !writer.writeField("avg_link_demand", ts.avg_link_demand) ||
            !writer.writeField("avg_link_util", ts.avg_link_util) ||
            !writer.writeField(
                "mcast_write_link_util", ts.avg_mcast_write_link_util) ||
            !writer.writeField("noc", noc) || !writer.endObject()) {
            return false;
        }
    }
    if (!writer.endArray()) {
        return false;
    }

    // --- Consistency Checks ---
    for (const auto &defined_group : inactive_defined_groups) {
        log_error(
            "Timeline Consistency Check Failed: Transfer group ID {} is defined in noc_transfers "
            "but never appears in active_transfers of any timestep.",
            defined_group);
    }

    return true;
}
template <typename Serialize>
void writeTimelineToFile(const std::string& filepath, bool compress, Serialize&& serialize) {
    std::filesystem::path output_filepath = compress ? filepath + ".zst" : filepath;
    std::filesystem::path temp_filepath = output_filepath.string() + ".tmp";
    try {
        bool success = false;
        {
            TimelineJsonWriter writer(temp_filepath.string(), compress);
            success = serialize(writer);
            if (success) {
                success = writer.close();
            }
        }
        if (!success) {
            std::error_code cleanup_error;
            std::filesystem::remove(temp_filepath, cleanup_error);
            log_error("Was not able to write stats file '{}'", filepath);
            return;
        }

        std::error_code rename_error;
        std::filesystem::rename(temp_filepath, output_filepath, rename_error);
        if (rename_error) {
            std::error_code cleanup_error;
            std::filesystem::remove(temp_filepath, cleanup_error);
            log_error(
                "Was not able to publish stats file '{}': {}",
                output_filepath.string(),
                rename_error.message());
        }
    } catch (const std::exception &e) {
        std::error_code cleanup_error;
        std::filesystem::remove(temp_filepath, cleanup_error);
        log_error("Error writing stats file '{}': {}", filepath, e.what());
    }
}

void npeStats::emitSimTimelineToFile(
    const std::vector<PETransferState> &transfer_state,
    const npeWorkload &wl,
    const npeConfig &cfg) const {

    const auto& device_stats = per_device_stats.at(MESH_DEVICE);
    const auto& per_timestep_stats = device_stats.per_timestep_stats;
    
    // Determine base filepath
    std::string base_filepath = cfg.timeline_filepath;
    if (base_filepath.empty()) {
        if (!cfg.workload_json.empty()) {
            auto last_dot = cfg.workload_json.find_last_of('.');
            base_filepath = "npe_timeline_" + cfg.workload_json.substr(0, last_dot) + ".npeviz";
        } else {
            base_filepath = "npe_timeline.npeviz";
        }
    }

    // Always emit full timeline file
    if (cfg.use_legacy_timeline_format) {
        writeTimelineToFile(
            base_filepath,
            cfg.compress_timeline_output_file,
            [&](TimelineJsonWriter& writer) {
                return v0TimelineSerialization(
                    writer, device_stats, cfg, *device_model, wl, transfer_state);
            });
    } else {
        writeTimelineToFile(
            base_filepath,
            cfg.compress_timeline_output_file,
            [&](TimelineJsonWriter& writer) {
                return v1TimelineSerialization(
                    writer, device_stats, cfg, *device_model, wl, transfer_state);
            });
    }

    // Check if we need to emit split files (only for v1 format)
    Timestep num_timesteps = per_timestep_stats.size();
    Timestep split_threshold = cfg.timeline_split_threshold_timesteps;
    if (!cfg.use_legacy_timeline_format && num_timesteps > split_threshold) {
        // Calculate number of split files needed
        size_t num_splits = (num_timesteps + split_threshold - 1) / split_threshold;
        
        log("Timeline has {} timesteps, exceeding threshold of {}. Emitting {} split files.",
                 num_timesteps, split_threshold, num_splits);
        
        // Remove extension from base filepath for split files
        std::string base_without_ext = base_filepath;
        auto ext_pos = base_without_ext.find(".npeviz");
        if (ext_pos != std::string::npos) {
            base_without_ext = base_without_ext.substr(0, ext_pos);
        }
        
        for (Timestep split_idx = 0; split_idx < num_splits; ++split_idx) {
            Timestep start_timestep_idx = split_idx * split_threshold;
            Timestep end_timestep_idx = std::min((split_idx + 1) * split_threshold, num_timesteps);
            
            // Get cycle ranges from timestep stats
            Cycle start_cycle = per_timestep_stats[start_timestep_idx].start_cycle;
            Cycle end_cycle = per_timestep_stats[end_timestep_idx - 1].end_cycle;
            
            TimelineRegion region{
                start_timestep_idx,
                end_timestep_idx,
                start_cycle,
                end_cycle
            };
            
            nlohmann::ordered_json split_info = {
                {"split_index", split_idx},
                {"total_splits", num_splits},
                {"start_timestep_idx", start_timestep_idx},
                {"end_timestep_idx", end_timestep_idx},
                {"start_cycle", start_cycle},
                {"end_cycle", end_cycle}
            };
            
            std::string split_filepath = fmt::format("{}_split_{}.npeviz", base_without_ext, split_idx);
            writeTimelineToFile(
                split_filepath,
                cfg.compress_timeline_output_file,
                [&](TimelineJsonWriter& writer) {
                    return v1TimelineSerialization(
                        writer,
                        device_stats,
                        cfg,
                        *device_model,
                        wl,
                        transfer_state,
                        region,
                        split_info);
                });
        }
    }
}

double npeStats::deviceStats::getCongestionImpact() const {
    if (estimated_cycles == 0 || estimated_cong_free_cycles == 0) {
        return 0.0;
    } else {
        return 100.0 * (double(estimated_cycles) - double(estimated_cong_free_cycles)) /
               estimated_cycles;
    }
}

std::string npeStats::deviceStats::getEthBwUtilPerCoreStr() const {
    if (eth_bw_util_per_core.empty()) {
        return "-";
    }
    // Sort by coord for consistent output
    std::vector<std::pair<Coord, double>> sorted_cores(
        eth_bw_util_per_core.begin(), eth_bw_util_per_core.end());
    std::sort(sorted_cores.begin(), sorted_cores.end(),
        [](const auto& a, const auto& b) {
            return std::tie(a.first.device_id, a.first.row, a.first.col) <
                   std::tie(b.first.device_id, b.first.row, b.first.col);
        });
    std::string result;
    for (const auto& [coord, util] : sorted_cores) {
        if (!result.empty()) result += " ";
        result += fmt::format("({},{},{}):{:.1f}%", coord.device_id, coord.row, coord.col, util);
    }
    return result;
}

std::string npeStats::deviceStats::getDramBwUtilPerControllerStr() const {
    if (dram_bw_util_per_controller.empty()) {
        return "-";
    }
    std::vector<std::pair<uint32_t, double>> sorted_controllers(
        dram_bw_util_per_controller.begin(), dram_bw_util_per_controller.end());
    std::sort(sorted_controllers.begin(), sorted_controllers.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });
    std::string result;
    for (const auto& [controller, util] : sorted_controllers) {
        if (!result.empty()) result += " ";
        result += fmt::format("c{}:{:.1f}%", controller, util);
    }
    return result;
}

double npeStats::deviceStats::getAggregateEthBwUtil() const {
    if (eth_bw_util_per_core.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const auto& [coord, util] : eth_bw_util_per_core) {
        sum += util;
    }
    return sum / eth_bw_util_per_core.size();
}

}  // namespace tt_npe
