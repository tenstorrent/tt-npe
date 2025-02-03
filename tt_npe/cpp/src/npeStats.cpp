// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include "npeStats.hpp"

#include <fstream>

#include "fmt/base.h"
#include "fmt/ostream.h"
#include "nlohmann/json.hpp"
#include "npeConfig.hpp"
#include "npeTransferState.hpp"

namespace tt_npe {
std::string npeStats::to_string(bool verbose) const {
    std::string output;

    output.append(fmt::format(" estimated cycles: {:5d}\n", estimated_cycles));
    output.append(fmt::format("    golden cycles: {:5d}\n", golden_cycles));
    output.append(fmt::format(" cycle pred error: {:5.1f}%\n", cycle_prediction_error));
    output.append("\n");
    output.append(fmt::format("     DRAM BW Util: {:5.1f}%\n", dram_bw_util));
    output.append("\n");
    output.append(fmt::format("    avg Link util: {:5.1f}%\n", overall_avg_link_util));
    output.append(fmt::format("    max Link util: {:5.1f}%\n", overall_max_link_util));
    output.append("\n");
    output.append(fmt::format("  avg Link demand: {:5.1f}%\n", overall_avg_link_demand));
    output.append(fmt::format("  max Link demand: {:5.1f}%\n", overall_max_link_demand));
    output.append("\n");
    output.append(fmt::format("  avg NIU  demand: {:5.1f}%\n", overall_avg_niu_demand));
    output.append(fmt::format("  max NIU  demand: {:5.1f}%\n", overall_max_niu_demand));

    if (verbose) {
        output.append("\n");
        output.append(fmt::format("    num timesteps: {:5d}\n", num_timesteps));
        output.append(fmt::format("   wallclock time: {:5d} us\n", wallclock_runtime_us));
    }
    return output;
}

void npeStats::computeSummaryStats() {
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
}

void npeStats::emitSimStatsToFile(
    const std::string &filepath,
    const std::vector<PETransferState> &transfer_state,
    const npeDeviceModel &model,
    const npeConfig &cfg) const {
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
        if (std::holds_alternative<Coord>(tr.params.dst)) {
            auto dst = std::get<Coord>(tr.params.dst);
            transfer["dst"] = {dst.row, dst.col};
        } else {
            auto [start_coord, end_coord] = std::get<MCastCoordPair>(tr.params.dst);
            transfer["dst"] = {{start_coord.row, start_coord.col}, {end_coord.row, end_coord.col}};
        }
        transfer["total_bytes"] = tr.params.total_bytes;
        transfer["noc_type"] = magic_enum::enum_name(tr.params.noc_type);
        transfer["injection_rate"] = tr.params.injection_rate;
        transfer["start_cycle"] = tr.start_cycle;
        transfer["end_cycle"] = tr.end_cycle;
        transfer["noc_event_type"] = tr.params.noc_event_type;

        transfer["route"] = nlohmann::json::array();
        for (const auto &link : tr.route) {
            transfer["route"].push_back(
                {link.coord.row, link.coord.col, magic_enum::enum_name(nocLinkType(link.type))});
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
        size_t krows = ts.link_demand_grid.getRows();
        size_t kcols = ts.link_demand_grid.getCols();
        size_t klinks = ts.link_demand_grid.getItems();
        for (size_t r = 0; r < krows; r++) {
            for (size_t c = 0; c < kcols; c++) {
                for (size_t l = 0; l < klinks; l++) {
                    float demand = ts.link_demand_grid(r, c, l);
                    if (demand > 0.001) {
                        timestep["link_demand"].push_back(
                            {r, c, magic_enum::enum_name<nocLinkType>(nocLinkType(l)), demand});
                    }
                }
            }
        }
        timestep["avg_link_demand"] = ts.avg_link_demand;
        timestep["avg_link_util"] = ts.avg_link_util;

        j["timestep_data"].push_back(timestep);
    }

    std::ofstream os(filepath);
    if (!os) {
        log_error("Was not able to open stats file '{}'", filepath);
        return;
    }
    os << j.dump(4);
}

}  // namespace tt_npe