// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025 Tenstorrent AI ULC

#include "npeStats.hpp"

#include <cstddef>
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

namespace tt_npe {

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

void npeStats::insertTimestep(size_t start_cycle, size_t end_cycle, const npeWorkload& wl) {
    for (auto& [device_id, deviceStats]: per_device_stats) { 
        // skip timesteps that start before first transfer on device
        auto [golden_start, golden_end] = wl.getGoldenResultCycles(device_id);
        if (end_cycle >= golden_start) {
            deviceStats.per_timestep_stats.push_back({});
            TimestepStats &timestep_stats = deviceStats.per_timestep_stats.back();
            timestep_stats.start_cycle = start_cycle;
            timestep_stats.end_cycle = end_cycle;
        }
    }
}

void npeStats::updateWorstCaseTransferEndCycle(DeviceID device_id, PETransferState& tr, std::pair<CycleCount, CycleCount> golden_cycles) {
    // updated simulated end for device_id and MESH_DEVICE (last event on this device issued during it's golden region)
    auto [golden_start, golden_end] = golden_cycles;
    if (golden_start <= tr.params.phase_cycle_offset && tr.params.phase_cycle_offset <= golden_end)
        per_device_stats[device_id].worst_case_transfer_end_cycle = std::max(per_device_stats[device_id].worst_case_transfer_end_cycle, (size_t)tr.end_cycle);
}

void npeStats::finishSimulation(size_t getElapsedTimeMicroSeconds, uint32_t cycles_per_timestep, const npeWorkload &wl) {
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
            continue;
        }

        deviceStats.estimated_cycles = deviceStats.worst_case_transfer_end_cycle - golden_start;

        // filter out timestep stats that are after the last transfer on the device
        auto start_idx = golden_start / cycles_per_timestep; // round down
        auto end_idx = (deviceStats.worst_case_transfer_end_cycle + cycles_per_timestep - 1) / cycles_per_timestep; // round up
        deviceStats.per_timestep_stats.resize(end_idx - start_idx + 1);
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
    for (const auto &ts : per_timestep_stats) {
        overall_avg_niu_demand += ts.avg_niu_demand;
        overall_max_niu_demand = std::max(overall_max_niu_demand, ts.avg_niu_demand);

        overall_avg_link_demand += ts.avg_link_demand;
        overall_max_link_demand = std::max(overall_max_link_demand, ts.avg_link_demand);

        overall_avg_link_util += ts.avg_link_util;
        overall_max_link_util = std::max(overall_max_link_util, ts.avg_link_util);

        overall_avg_noc0_link_demand += ts.avg_noc0_link_demand;
        overall_avg_noc0_link_util += ts.avg_noc0_link_util;
        overall_max_noc0_link_demand = std::max(overall_max_noc0_link_demand, ts.avg_noc0_link_demand);

        overall_avg_noc1_link_demand += ts.avg_noc1_link_demand;
        overall_avg_noc1_link_util += ts.avg_noc1_link_util;
        overall_max_noc1_link_demand = std::max(overall_max_noc1_link_demand, ts.avg_noc1_link_demand);
    }

    size_t num_timesteps = per_timestep_stats.size();
    overall_avg_link_demand /= num_timesteps;
    overall_avg_niu_demand /= num_timesteps;
    overall_avg_link_util /= num_timesteps;

    overall_avg_noc0_link_demand /= num_timesteps;
    overall_avg_noc0_link_util /= num_timesteps;
    overall_avg_noc1_link_demand /= num_timesteps;
    overall_avg_noc1_link_util /= num_timesteps;

    cycle_prediction_error =
        100.0 * float(int64_t(estimated_cycles) - int64_t(golden_cycles)) / golden_cycles;

    // compute aggregate dram bw utilization
    size_t read_bytes = 0;
    size_t write_bytes = 0;
    for (const auto &phase : wl.getPhases()) {
        for (const auto &transfer : phase.transfers) {
            if (device_id == MESH_DEVICE || device_id == transfer.src.device_id) {
                if (device_model.getCoreType(transfer.src) == CoreType::DRAM) {
                    // read from DRAM
                    read_bytes += transfer.total_bytes;
                } else if (
                    // write to DRAM
                    std::holds_alternative<Coord>(transfer.dst) &&
                    device_model.getCoreType(std::get<Coord>(transfer.dst)) == CoreType::DRAM) {
                    write_bytes += transfer.total_bytes;
                }
            }
        }
    }
    size_t total_bytes = read_bytes + write_bytes;

    double total_dram_bandwidth_over_golden_cycles = golden_cycles * device_model.getAggregateDRAMBandwidth();
    double total_dram_bandwidth_over_estimated_cycles = estimated_cycles * device_model.getAggregateDRAMBandwidth();
    this->dram_bw_util = (total_bytes / total_dram_bandwidth_over_golden_cycles) * 100;
    this->dram_bw_util_sim = (total_bytes / total_dram_bandwidth_over_estimated_cycles) * 100;

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
        double total_eth_bandwidth_over_golden_cycles = golden_cycles * device_model.getEthBandwidth();
        this->eth_bw_util_per_core[core] = (eth_tx_bytes / total_eth_bandwidth_over_golden_cycles) * 100;    
    }
}

nlohmann::json v0TimelineSerialization(
    const npeStats::deviceStats &device_stats,
    const npeConfig &cfg,
    const npeDeviceModel &model,
    const npeWorkload &wl,
    const std::vector<PETransferState> &transfer_state) {
    nlohmann::json j;

    //---- emit common info ---------------------------------------------------
    j["common_info"] = {
        {"device_name", cfg.device_name},
        {"cycles_per_timestep", cfg.cycles_per_timestep},
        {"congestion_model_name", cfg.congestion_model_name},
        {"num_rows", model.getRows()},
        {"num_cols", model.getCols()},
        // emit overall stats from the simulation
        {"dram_bw_util", device_stats.dram_bw_util},
        {"link_util", device_stats.overall_avg_link_util},
        {"link_demand", device_stats.overall_avg_link_demand},
        {"max_link_demand", device_stats.overall_max_link_demand}};

    //---- emit per transfer data ---------------------------------------------
    j["noc_transfers"] = nlohmann::json::array();
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

        j["noc_transfers"].push_back(transfer);
    }

    //---- emit per timestep data ---------------------------------------------
    auto& per_timestep_stats = device_stats.per_timestep_stats;
    j["timestep_data"] = nlohmann::json::array();
    for (const auto &ts : per_timestep_stats) {
        nlohmann::json timestep;
        timestep["start_cycle"] = ts.start_cycle;
        timestep["end_cycle"] = ts.end_cycle;

        std::vector<int> active_transfers(ts.live_transfer_ids.begin(), ts.live_transfer_ids.end());
        std::sort(active_transfers.begin(), active_transfers.end());
        timestep["active_transfers"] = active_transfers;

        timestep["link_demand"] = nlohmann::json::array();
        size_t kRows = model.getRows();
        size_t kCols = model.getCols();
        auto &ts_link_demand = timestep["link_demand"];

        constexpr float DEMAND_SIGNIFICANCE_THRESHOLD = 0.001;
        for (const auto &[niu_id, demand] : enumerate(ts.niu_demand_grid)) {
            if (demand > DEMAND_SIGNIFICANCE_THRESHOLD) {
                nocNIUAttr attr = model.getNIUAttributes(niu_id);
                std::string terminal_name;
                switch (attr.type) {
                    case nocNIUType::NOC0_SRC: terminal_name = "NOC0_IN"; break;
                    case nocNIUType::NOC0_SINK: terminal_name = "NOC0_OUT"; break;
                    case nocNIUType::NOC1_SRC: terminal_name = "NOC1_IN"; break;
                    case nocNIUType::NOC1_SINK: terminal_name = "NOC1_OUT"; break;
                    default: terminal_name = "UNKNOWN"; break;
                }
                ts_link_demand.push_back({attr.coord.row, attr.coord.col, terminal_name, demand});
            }
        }
        for (const auto& [link_id, demand] : enumerate(ts.link_demand_grid)) {
            if (demand > DEMAND_SIGNIFICANCE_THRESHOLD) {
                nocLinkAttr link_attr = model.getLinkAttributes(link_id);
                ts_link_demand.push_back(
                    {link_attr.coord.row,
                     link_attr.coord.col,
                     magic_enum::enum_name<nocLinkType>(link_attr.type),
                     demand});
            }
        }
        timestep["avg_link_demand"] = ts.avg_link_demand;
        timestep["avg_link_util"] = ts.avg_link_util;

        j["timestep_data"].push_back(timestep);
    }

    return j;
}

bool isFabricTransferType(const std::string& noc_event_type) {
    return noc_event_type.starts_with("FABRIC_");
}

// Region struct for split file generation
struct TimelineRegion {
    size_t start_timestep_idx;  // inclusive
    size_t end_timestep_idx;    // exclusive
    size_t start_cycle;
    size_t end_cycle;

    bool partiallyContainedInRegion(
        double start,
        double end) const {
        bool starts_in_region = (start >= start_cycle && start < end_cycle);
        bool ends_in_region = (end > start_cycle && end <= end_cycle);
        return starts_in_region || ends_in_region;
    }
    
    bool fullyContainedInRegion(
        double start,
        double end) const {
        return start >= start_cycle && end <= end_cycle;
    }
};

nlohmann::json v1TimelineSerialization(
    const npeStats::deviceStats &device_stats,
    const npeConfig &cfg,
    const npeDeviceModel &model,
    const npeWorkload &wl,
    const std::vector<PETransferState> &transfer_state,
    const std::optional<TimelineRegion>& region = std::nullopt) {
    nlohmann::ordered_json j;

    //---- emit common info ---------------------------------------------------
    std::string arch_string =
        model.getArch() == DeviceArch::WormholeB0 ? "wormhole_b0" : "blackhole";
    j["common_info"] = {
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

    //---- emit topology info ---------------------------------------------------
    j["chips"] = nlohmann::json::object(); // Initialize as an empty object
    bool multichip = model.getNumChips() > 1;
    if (multichip) {
        if (cfg.topology_json.empty()){
            log_error("Cluster coordinates JSON file is required for serializing timeline for multichip devices\n");
            return nlohmann::json{};
        }

        std::ifstream ifs(cfg.topology_json);
        if (!ifs.is_open()) {
            log_error("Failed to open cluster coordinates JSON file: {}\n", cfg.topology_json);
            return nlohmann::json{};
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
                        j["chips"][chip_id_str] = nlohmann::json::array(
                            {coord_item[1].get<int>() % ew_dim,
                             coord_item[1].get<int>() / ew_dim,
                             0, 
                             0});
                    } else {
                        log_error("Invalid cluster_coordinates.json entry: {} in cluster_coordinates.json file\n", chip_id_str);
                        return nlohmann::json{};
                    }
                }
            }
        } catch (const nlohmann::json::parse_error &e) {
            log_error("Failed to parse cluster_coordinates.json file:\n{}\n", e.what());
            return nlohmann::json{};
        }
    } else {
        // single chip case; set all coordinates to 0
        j["chips"] = nlohmann::json{{"0", {0, 0, 0, 0}}};
    }

    //---- emit noc transfer info ---------------------------------------------------
    j["noc_transfers"] = nlohmann::ordered_json::array();

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
            size_t group_start_cycle = transfer_state[first_transfer].start_cycle;
            size_t group_end_cycle = transfer_state[last_transfer].end_cycle;
            if (!region.value().partiallyContainedInRegion(group_start_cycle, group_end_cycle)) {
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

        j["noc_transfers"].push_back(transfer);
    }

    //---- emit zones ---------------------------------------------
    j["zones"] = nlohmann::ordered_json::array();
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

        j["zones"].push_back(root_zone_json);
    }

    //---- emit per timestep data ---------------------------------------------
    auto& per_timestep_stats = device_stats.per_timestep_stats;
    j["timestep_data"] = nlohmann::ordered_json::array();
    for (const auto &ts : per_timestep_stats) {
        // If region is specified, filter timesteps: include only if fully contained in region
        if (region.has_value()) {
            if (!region.value().fullyContainedInRegion(ts.start_cycle, ts.end_cycle)) {
                continue;
            }
        }
        
        nlohmann::ordered_json timestep;
        timestep["start_cycle"] = ts.start_cycle;
        timestep["end_cycle"] = ts.end_cycle;

        std::vector<npeWorkloadTransferGroupID> active_transfer_groups;
        active_transfer_groups.reserve(ts.live_transfer_ids.size());
        for (const auto& live_transfer_id : ts.live_transfer_ids) {
            active_transfer_groups.push_back(transfer_id_to_transfer_group[live_transfer_id]);
        }
        uniquify(active_transfer_groups);
        timestep["active_transfers"] = active_transfer_groups;

        timestep["link_demand"] = nlohmann::ordered_json::array();
        size_t kRows = model.getRows();
        size_t kCols = model.getCols();
        auto &ts_link_demand = timestep["link_demand"];

        constexpr float DEMAND_SIGNIFICANCE_THRESHOLD = 0.001;
        for (const auto &[niu_id, demand] : enumerate(ts.niu_demand_grid)) {
            if (demand > DEMAND_SIGNIFICANCE_THRESHOLD) {
                nocNIUAttr attr = model.getNIUAttributes(niu_id);
                std::string terminal_name;
                switch (attr.type) {
                    case nocNIUType::NOC0_SRC: terminal_name = "NOC0_IN"; break;
                    case nocNIUType::NOC0_SINK: terminal_name = "NOC0_OUT"; break;
                    case nocNIUType::NOC1_SRC: terminal_name = "NOC1_IN"; break;
                    case nocNIUType::NOC1_SINK: terminal_name = "NOC1_OUT"; break;
                    default: terminal_name = "UNKNOWN"; break;
                }
                ts_link_demand.push_back({attr.coord.device_id, attr.coord.row, attr.coord.col, terminal_name, demand});
            }
        }

        for (const auto &[link_id, demand] : enumerate(ts.link_demand_grid)) {
            if (demand > DEMAND_SIGNIFICANCE_THRESHOLD) {
                nocLinkAttr link_attr = model.getLinkAttributes(link_id);
                ts_link_demand.push_back(
                    {link_attr.coord.device_id,
                     link_attr.coord.row,
                     link_attr.coord.col,
                     magic_enum::enum_name<nocLinkType>(link_attr.type),
                     demand});
            }
        }
        timestep["avg_link_demand"] = ts.avg_link_demand;
        timestep["avg_link_util"] = ts.avg_link_util;
        timestep["noc"] = {
            {"NOC0",
             {{"avg_link_demand", ts.avg_noc0_link_demand},
              {"avg_link_util", ts.avg_noc0_link_util},
              {"max_link_demand", ts.max_noc0_link_demand}}},
            {"NOC1",
             {{"avg_link_demand", ts.avg_noc1_link_demand},
              {"avg_link_util", ts.avg_noc1_link_util},
              {"max_link_demand", ts.max_noc1_link_demand}}}};

        j["timestep_data"].push_back(timestep);
    }

    // --- Consistency Checks ---
    if (j.contains("noc_transfers") && j.contains("timestep_data")) {
        boost::unordered_flat_set<npeWorkloadTransferGroupID> defined_groups;
        if (j["noc_transfers"].is_array()) {
            for (const auto &transfer : j["noc_transfers"]) {
                if (transfer.contains("id")) {
                    defined_groups.insert(transfer["id"].get<npeWorkloadTransferGroupID>());
                }
            }
        }

        boost::unordered_flat_set<npeWorkloadTransferGroupID> active_groups;
        if (j["timestep_data"].is_array()) {
            for (const auto &timestep : j["timestep_data"]) {
                if (timestep.contains("active_transfers") &&
                    timestep["active_transfers"].is_array()) {
                    for (const auto &group_id : timestep["active_transfers"]) {
                        active_groups.insert(group_id.get<npeWorkloadTransferGroupID>());
                    }
                }
            }
        }

        for (const auto &defined_group : defined_groups) {
            if (active_groups.find(defined_group) == active_groups.end()) {
                log_error(
                    "Timeline Consistency Check Failed: Transfer group ID {} is defined in noc_transfers "
                    "but never appears in active_transfers of any timestep.",
                    defined_group);
            }
        }
    }

    return j;
}
// Helper to write timeline JSON to file (with optional compression)
void writeTimelineToFile(
    const nlohmann::json& timeline_json_data,
    const std::string& filepath,
    bool compress) {
    try {
        std::string output_filepath = filepath;
        if (compress) {
            output_filepath += ".zst";
            npeCompressionUtil::compressToFile(timeline_json_data.dump(-1), output_filepath);
        } else {
            std::ofstream os(output_filepath);
            if (!os) {
                log_error("Was not able to open stats file '{}'", output_filepath);
                return;
            }
            os << timeline_json_data.dump(2);
        }
    } catch (const std::exception &e) {
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
    nlohmann::json full_timeline_json_data;
    if (cfg.use_legacy_timeline_format) {
        full_timeline_json_data = v0TimelineSerialization(device_stats, cfg, *device_model, wl, transfer_state);
    } else {
        full_timeline_json_data = v1TimelineSerialization(device_stats, cfg, *device_model, wl, transfer_state);
    }
    writeTimelineToFile(full_timeline_json_data, base_filepath, cfg.compress_timeline_output_file);

    // Check if we need to emit split files (only for v1 format)
    size_t num_timesteps = per_timestep_stats.size();
    size_t split_threshold = cfg.timeline_split_threshold_timesteps;
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
        
        for (size_t split_idx = 0; split_idx < num_splits; ++split_idx) {
            size_t start_timestep_idx = split_idx * split_threshold;
            size_t end_timestep_idx = std::min((split_idx + 1) * split_threshold, num_timesteps);
            
            // Get cycle ranges from timestep stats
            size_t start_cycle = per_timestep_stats[start_timestep_idx].start_cycle;
            size_t end_cycle = per_timestep_stats[end_timestep_idx - 1].end_cycle;
            
            TimelineRegion region{
                start_timestep_idx,
                end_timestep_idx,
                start_cycle,
                end_cycle
            };
            
            nlohmann::json split_timeline_json = v1TimelineSerialization(
                device_stats, cfg, *device_model, wl, transfer_state, region);
            
            // Add split metadata to common_info
            split_timeline_json["common_info"]["split_info"] = {
                {"split_index", split_idx},
                {"total_splits", num_splits},
                {"start_timestep_idx", start_timestep_idx},
                {"end_timestep_idx", end_timestep_idx},
                {"start_cycle", start_cycle},
                {"end_cycle", end_cycle}
            };
            
            std::string split_filepath = fmt::format("{}_split_{}.npeviz", base_without_ext, split_idx);
            writeTimelineToFile(split_timeline_json, split_filepath, cfg.compress_timeline_output_file);
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
