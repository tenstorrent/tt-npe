// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2025 Tenstorrent AI ULC

#include "npeStats.hpp"

#include <fstream>
#include <boost/unordered/unordered_flat_set.hpp>
#include <string>

#include "fmt/base.h"
#include "fmt/ostream.h"
#include "nlohmann/json.hpp"
#include "npeConfig.hpp"
#include "npeTransferState.hpp"
#include "npeDeviceModelIface.hpp"
#include "npeCompressionUtil.hpp"
#include "npeUtil.hpp"

namespace tt_npe {
std::string npeStats::to_string(bool verbose) const {
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
        output.append(fmt::format("    num timesteps: {:5d}\n", num_timesteps));
        output.append(fmt::format("   wallclock time: {:5d} us\n", wallclock_runtime_us));
    }
    return output;
}

void npeStats::computeSummaryStats(const npeWorkload& wl, const npeDeviceModel& device_model) {
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
    size_t total_bytes = read_bytes + write_bytes;

    double total_dram_bandwidth_over_golden_cycles = golden_cycles * device_model.getAggregateDRAMBandwidth();
    double total_dram_bandwidth_over_estimated_cycles = estimated_cycles * device_model.getAggregateDRAMBandwidth();
    this->dram_bw_util = (total_bytes / total_dram_bandwidth_over_golden_cycles) * 100;
    this->dram_bw_util_sim = (total_bytes / total_dram_bandwidth_over_estimated_cycles) * 100;
}

nlohmann::json npeStats::v0TimelineSerialization(
    const npeConfig &cfg,
    const npeDeviceModel &model,
    const npeWorkload &wl,
    const std::vector<PETransferState> &transfer_state) const {
    nlohmann::json j;

    //---- emit common info ---------------------------------------------------
    j["common_info"] = {
        {"device_name", cfg.device_name},
        {"cycles_per_timestep", cfg.cycles_per_timestep},
        {"congestion_model_name", cfg.congestion_model_name},
        {"num_rows", model.getRows()},
        {"num_cols", model.getCols()},
        // emit overall stats from the simulation
        {"dram_bw_util", dram_bw_util},
        {"link_util", overall_avg_link_util},
        {"link_demand", overall_avg_link_demand},
        {"max_link_demand", overall_max_link_demand}};

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

nlohmann::json npeStats::v1TimelineSerialization(
    const npeConfig &cfg,
    const npeDeviceModel &model,
    const npeWorkload &wl,
    const std::vector<PETransferState> &transfer_state) const {
    nlohmann::ordered_json j;

    //---- emit common info ---------------------------------------------------
    std::string arch_string =
        model.getArch() == DeviceArch::WormholeB0 ? "wormhole_b0" : "blackhole";
    j["common_info"] = {
        {"version", CURRENT_TIMELINE_SCHEMA_VERSION},
        {"mesh_device", cfg.device_name},
        {"arch", arch_string},
        {"cycles_per_timestep", cfg.cycles_per_timestep},
        {"congestion_model_name", cfg.congestion_model_name},
        {"num_rows", model.getRows()},
        {"num_cols", model.getCols()},
        // emit overall stats from the simulation
        {"dram_bw_util", dram_bw_util},
        {"link_util", overall_avg_link_util},
        {"link_demand", overall_avg_link_demand},
        {"max_link_demand", overall_max_link_demand},

        {"noc",
         {{"NOC0",
           {{"avg_link_demand", overall_avg_noc0_link_demand},
            {"avg_link_util", overall_avg_noc0_link_util},
            {"max_link_demand", overall_max_noc0_link_demand}}},
          {"NOC1",
           {{"avg_link_demand", overall_avg_noc1_link_demand},
            {"avg_link_util", overall_avg_noc1_link_util},
            {"max_link_demand", overall_max_noc1_link_demand}}}}}};

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

        nlohmann::ordered_json transfer;
        transfer["id"] = transfer_group_id;

        // start point of transfer group is found in first route
        const auto& first_transfer = component_transfers.front();
        auto src_coord = transfer_state[first_transfer].params.src;
        transfer["src"] = {src_coord.device_id, src_coord.row, src_coord.col};
        transfer["total_bytes"] = transfer_state[first_transfer].params.total_bytes;
        transfer["start_cycle"] = transfer_state[first_transfer].start_cycle;
        transfer["noc_event_type"] = transfer_state[first_transfer].params.noc_event_type;
        transfer["fabric_event_type"] = isFabricTransferType(transfer_state[first_transfer].params.noc_event_type);
        transfer["zones"] = transfer_state[first_transfer].params.enclosing_zone_path;

        // end point of transfer group is found in last route
        const auto& last_transfer = component_transfers.back();
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
        std::vector<nlohmann::ordered_json*> parent_zones;
        boost::unordered_flat_map<std::string, int> zone_counts; 
        // this is the root zone
        auto core_proc_zones = nlohmann::ordered_json::object();
        parent_zones.push_back(&core_proc_zones);
        for (auto& zone : zones) {
            auto& parent_zone = *parent_zones.back();
            if (zone.zone_phase == ZonePhase::ZONE_START) {
                int zone_count = zone_counts[zone.zone];
                zone_counts[zone.zone]++;
                parent_zone["zones"].push_back({
                    {"id", zone.zone},
                    {"zone_count", zone_count},
                    {"start", zone.timestamp},
                    {"zones", nlohmann::ordered_json::array()}
                });
                parent_zones.push_back(&parent_zone["zones"].back());
            }
            else if (zone.zone == parent_zone["id"]) {
                parent_zone["end"] = zone.timestamp;
                // combine zone and zone count
                parent_zone["id"] = parent_zone["id"].get<std::string>() + "[" + 
                    std::to_string(parent_zone["zone_count"].get<int>()) + "]";
                parent_zone.erase("zone_count");
                parent_zones.pop_back();
            }
            else {
                TT_ASSERT(false);
            }
        }

        core_proc_zones["core"] = {core_proc.first.device_id, core_proc.first.row, core_proc.first.col};
        core_proc_zones["proc"] = magic_enum::enum_name(core_proc.second);
        j["zones"].push_back(core_proc_zones);
    }

    //---- emit per timestep data ---------------------------------------------
    j["timestep_data"] = nlohmann::ordered_json::array();
    for (const auto &ts : per_timestep_stats) {
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
void npeStats::emitSimTimelineToFile(
    const std::vector<PETransferState> &transfer_state,
    const npeDeviceModel &model,
    const npeWorkload &wl,
    const npeConfig &cfg) const {

    nlohmann::json timeline_json_data;
    if (cfg.use_legacy_timeline_format) {
        timeline_json_data = v0TimelineSerialization(cfg, model, wl, transfer_state);
    } else {
        timeline_json_data = v1TimelineSerialization(cfg, model, wl, transfer_state);
    }

    std::string filepath = cfg.timeline_filepath;
    if (filepath.empty()) {
        if (!cfg.workload_json.empty()) {
            auto last_dot = cfg.workload_json.find_last_of('.');
            filepath = "npe_timeline_" + cfg.workload_json.substr(0, last_dot) + ".npeviz";
        } else {
            filepath = "npe_timeline.npeviz";
        }
    }

    try {
        if (cfg.compress_timeline_output_file) {
            filepath += ".zst";
            npeCompressionUtil::compressToFile(timeline_json_data.dump(-1), filepath);
        } else {
            std::ofstream os(filepath);
            if (!os) {
                log_error("Was not able to open stats file '{}'", filepath);
                return;
            }
            os << timeline_json_data.dump(2);
        }
    } catch (const std::exception &e) {
        log_error("Error writing stats file '{}': {}", filepath, e.what());
    }
}

double npeStats::getCongestionImpact() const {
    if (estimated_cycles == 0 || estimated_cong_free_cycles == 0) {
        return 0.0;
    } else {
        return 100.0 * (double(estimated_cycles) - double(estimated_cong_free_cycles)) /
               estimated_cycles;
    }
}

}  // namespace tt_npe
