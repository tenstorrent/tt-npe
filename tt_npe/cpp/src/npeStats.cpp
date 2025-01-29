#include "npeStats.hpp"

#include <fstream>

#include "fmt/base.h"
#include "fmt/ostream.h"
#include "npeConfig.hpp"
#include "npeTransferState.hpp"

namespace tt_npe {
std::string npeStats::to_string(bool verbose) const {
    std::string output;

    output.append(fmt::format(" estimated cycles: {:5d}\n", estimated_cycles));
    output.append(fmt::format(" cycle pred error: {:5.1f}%\n", cycle_prediction_error));

    output.append("\n");
    output.append(fmt::format("    avg Link util: {:5.0f}%\n", overall_avg_link_util));
    output.append(fmt::format("    max Link util: {:5.0f}%\n", overall_max_link_util));
    output.append("\n");
    output.append(fmt::format("  avg Link demand: {:5.0f}%\n", overall_avg_link_demand));
    output.append(fmt::format("  max Link demand: {:5.0f}%\n", overall_max_link_demand));
    output.append("\n");
    output.append(fmt::format("  avg NIU  demand: {:5.0f}%\n", overall_avg_niu_demand));
    output.append(fmt::format("  max NIU  demand: {:5.0f}%\n", overall_max_niu_demand));

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
        100.0 * float(int64_t(golden_cycles) - int64_t(estimated_cycles)) / golden_cycles;
}

void npeStats::emitSimStatsToFile(
    const std::string &filepath,
    const std::vector<PETransferState> &transfer_state,
    const npeConfig &cfg) const {
    std::ofstream os(filepath);
    if (!os) {
        log_error("Was not able to open stats file '{}'", filepath);
        return;
    }

    fmt::println(os, "{{");

    //---- emit common info ---------------------------------------------------
    fmt::println(os, "");
    fmt::println(os, R"("common_info" : {{)");
    fmt::println(os, R"(  "device_name"           : "{}",)", cfg.device_name);
    fmt::println(os, R"(  "cycles_per_timestep"   : {},)", cfg.cycles_per_timestep);
    fmt::println(os, R"(  "congestion_model_name" : "{}",)", cfg.congestion_model_name);
    // fmt::println(os, R"(  "num_rows"              : {},)", model.getRows());
    // fmt::println(os, R"(  "num_cols"              : {})", model.getCols());
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
            if (std::holds_alternative<Coord>(tr.params.dst)) {
                auto dst = std::get<Coord>(tr.params.dst);
                fmt::println(os, R"(    "dst" : [{},{}], )", dst.row, dst.col);
            } else {
                auto [start_coord, end_coord] = std::get<MCastCoordPair>(tr.params.dst);
                fmt::println(
                    os,
                    R"(    "dst" : [[{},{}],[{},{}]], )",
                    start_coord.row,
                    start_coord.col,
                    end_coord.row,
                    end_coord.col);
            }
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
    double overall_avg_link_demand = 0;
    double overall_max_link_demand = 0;
    double overall_avg_niu_demand = 0;
    double overall_max_niu_demand = 0;
    for (const auto &[i, ts] : enumerate(per_timestep_stats)) {
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

            // compute link utilization
            fmt::println(os, R"(    "link_utilization" : [)");
            size_t krows = ts.link_demand_grid.getRows();
            size_t kcols = ts.link_demand_grid.getCols();
            size_t klinks = ts.link_demand_grid.getItems();
            double avg_link_demand = 0;
            bool first = true;
            for (size_t r = 0; r < krows; r++) {
                for (size_t c = 0; c < kcols; c++) {
                    for (size_t l = 0; l < klinks; l++) {
                        float demand = ts.link_demand_grid(r, c, l);
                        if (demand > 0.001) {
                            auto comma = (first) ? "" : ",";
                            first = false;
                            avg_link_demand += demand;
                            TT_ASSERT(demand <= 100.0);
                            fmt::println(
                                os,
                                R"(        {}[{}, {}, "{}", {:.1f}])",
                                comma,
                                r,
                                c,
                                magic_enum::enum_name<nocLinkType>(nocLinkType(l)),
                                demand);
                        }
                    }
                }
            }
            avg_link_demand /= (krows * kcols * klinks);
            overall_avg_link_demand += avg_link_demand;
            overall_max_link_demand = std::max(overall_max_link_demand, avg_link_demand);

            fmt::println(os, "    ]");
        }
        // close timestep dict
        auto comma = (i == per_timestep_stats.size() - 1) ? "" : ",";
        fmt::println(os, "\n  }}{}", comma);
    }

    fmt::println(os, "]");
    fmt::println(os, "}}");
}

}  // namespace tt_npe