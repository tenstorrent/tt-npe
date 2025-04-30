// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include "npeStats.hpp"

#include <fstream>

#include "fmt/base.h"
#include "fmt/ostream.h"
#include "nlohmann/json.hpp"
#include "npeConfig.hpp"
#include "npeTransferState.hpp"
#include "npeDeviceModelIface.hpp"
#include "npeCompressionUtil.hpp"

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
    }
    overall_avg_link_demand /= num_timesteps;
    overall_avg_niu_demand /= num_timesteps;
    overall_avg_link_util /= num_timesteps;

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
nlohmann::json npeStats::v1TimelineSerialization(
    const npeConfig &cfg,
    const npeDeviceModel &model,
    const npeWorkload &wl,
    const std::vector<PETransferState> &transfer_state) const {
    nlohmann::ordered_json j;

    //---- emit common info ---------------------------------------------------
    j["common_info"] = {
        {"version", CURRENT_TIMELINE_SCHEMA_VERSION},
        {"mesh_device", cfg.device_name},
        {"arch", "wormhole_b0"},
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
         {{"NOC_0",
           {{"avg_link_demand", 0.0}, {"avg_link_util", 0.0}, {"max_link_demand", 0.0}}},
          {"NOC_1",
           {{"avg_link_demand", 0.0}, {"avg_link_util", 0.0}, {"max_link_demand", 0.0}}}}}};

    if (cfg.device_name == "T3K") {
        j["chips"] = {
            {"0", {1, 0, 0, 0}},
            {"1", {1, 1, 0, 0}},
            {"2", {2, 1, 0, 0}},
            {"3", {2, 0, 0, 0}},
            {"4", {0, 0, 0, 0}},
            {"5", {0, 1, 0, 0}},
            {"6", {3, 1, 0, 0}},
            {"7", {3, 0, 0, 0}}};
    } else if (cfg.device_name == "n300") {
        j["chips"] = {
            {"0", {0, 0, 0, 0}},
            {"1", {1, 0, 0, 0}},
        };
    } else if (cfg.device_name == "n150") {
        j["chips"] = {
            {"0", {0, 0, 0, 0}},
        };
    }

    j["noc_transfers"] = nlohmann::ordered_json::array();

    boost::unordered_flat_map<npeWorkloadTransferGroupID, std::vector<PETransferID>> transfer_groups;
    int dummy_transfer_group_id = wl.getNumTransferGroups() + 1;
    for (const auto &tr : transfer_state) {
        if (tr.params.transfer_group_id != -1 && tr.params.transfer_group_index != -1) {
            transfer_groups[tr.params.transfer_group_id].push_back(tr.params.getID());
        } else {
            // treat transfers without transfer group as being in their own dummy transfer group;
            // this simplifies the following transfer serialization code
            transfer_groups[dummy_transfer_group_id].push_back(tr.params.getID());
            dummy_transfer_group_id++;
        }
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

    int output_transfer_id =0; 
    for (auto& [_, component_transfers] : transfer_groups) {
        TT_ASSERT(component_transfers.size() >= 1);
        std::stable_sort(
            component_transfers.begin(),
            component_transfers.end(),
            [&transfer_state](const auto &lhs, const auto &rhs) {
                return transfer_state[lhs].params.transfer_group_index <
                       transfer_state[rhs].params.transfer_group_index;
            });

        nlohmann::ordered_json transfer;
        transfer["id"] = output_transfer_id++;

        // start point of transfer group is found in first route
        const auto& first_transfer = component_transfers.front();
        auto src_coord = transfer_state[first_transfer].params.src;
        transfer["src"] = {src_coord.device_id, src_coord.row, src_coord.col};
        transfer["total_bytes"] = transfer_state[first_transfer].params.total_bytes;
        transfer["start_cycle"] = transfer_state[first_transfer].start_cycle;
        transfer["noc_event_type"] = transfer_state[first_transfer].params.noc_event_type;

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

    //---- emit per timestep data ---------------------------------------------
    j["timestep_data"] = nlohmann::ordered_json::array();
    for (const auto &ts : per_timestep_stats) {
        nlohmann::ordered_json timestep;
        timestep["start_cycle"] = ts.start_cycle;
        timestep["end_cycle"] = ts.end_cycle;

        std::vector<int> active_transfers(ts.live_transfer_ids.begin(), ts.live_transfer_ids.end());
        std::sort(active_transfers.begin(), active_transfers.end());
        timestep["active_transfers"] = active_transfers;

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

        j["timestep_data"].push_back(timestep);
    }

    return j;
}

void npeStats::emitSimTimelineToFile(
    const std::vector<PETransferState> &transfer_state,
    const npeDeviceModel &model,
    const npeWorkload &wl,
    const npeConfig &cfg) const {

    nlohmann::json timeline_json_data;
    if (cfg.use_v1_timeline_format) {
        timeline_json_data = v0TimelineSerialization(cfg, model, wl, transfer_state);
    } else {
        timeline_json_data = v1TimelineSerialization(cfg, model, wl, transfer_state);
    }

    std::string filepath = cfg.timeline_filepath;
    if (filepath.empty()) {
        if (!cfg.workload_json.empty()) {
            auto last_dot = cfg.workload_json.find_last_of('.');
            filepath = "npe_timeline_" + cfg.workload_json.substr(0, last_dot) + ".json";
        } else {
            filepath = "npe_timeline.json";
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
