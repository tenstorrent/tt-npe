// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include <boost/unordered/unordered_flat_map.hpp>

#include "npeCommon.hpp"
#include "npeDeviceModelIface.hpp"
#include "npeStats.hpp"
#include "npeTransferState.hpp"
#include "npeUtil.hpp"

namespace tt_npe {

inline float interpolateBW(
    const TransferBandwidthTable &tbt,
    float max_transfer_bw,
    size_t packet_size,
    size_t num_packets) {
    TT_ASSERT(packet_size > 0);
    for (int fst = 0; fst < tbt.size() - 1; fst++) {
        size_t start_range = tbt[fst].first;
        size_t end_range = tbt[fst + 1].first;
        if (packet_size >= start_range && packet_size <= end_range) {
            float delta = end_range - start_range;
            float pct = (packet_size - start_range) / delta;
            float val_delta = tbt[fst + 1].second - tbt[fst].second;
            float steady_state_bw = (val_delta * pct) + tbt[fst].second;

            float first_transfer_bw = max_transfer_bw;
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
inline void updateTransferBandwidth(
    std::vector<PETransferState> *transfers,
    const std::vector<PETransferID> &live_transfer_ids,
    const TransferBandwidthTable &transfer_bandwidth_table,
    float max_transfer_bandwidth) {
    for (auto &ltid : live_transfer_ids) {
        auto &lt = (*transfers)[ltid];
        auto noc_limited_bw = interpolateBW(
            transfer_bandwidth_table,
            max_transfer_bandwidth,
            lt.params.packet_size,
            lt.params.num_packets);
        lt.curr_bandwidth = std::fmin(lt.params.injection_rate, noc_limited_bw);
    }
}

inline void updateSimulationStats(
    const npeDeviceModel &device_model,
    const LinkDemandGrid &link_demand_grid,
    const NIUDemandGrid &niu_demand_grid,
    TimestepStats &sim_stats,
    float max_link_bandwidth) {

    // Compute link demand and util
    for (const auto &[link_id, link_demand] : enumerate(link_demand_grid)) {
        nocLinkAttr link_attr = device_model.getLinkAttributes(link_id);
        sim_stats.avg_link_demand += link_demand;
        sim_stats.avg_link_util += std::fmin(link_demand, max_link_bandwidth);
        sim_stats.max_link_demand = std::fmax(sim_stats.max_link_demand, link_demand);
        if (link_attr.type == nocLinkType::NOC0_EAST || link_attr.type == nocLinkType::NOC0_SOUTH) {
            sim_stats.avg_noc0_link_demand += link_demand;
            sim_stats.avg_noc0_link_util += std::fmin(link_demand, max_link_bandwidth);
            sim_stats.max_noc0_link_demand = std::fmax(sim_stats.max_noc0_link_demand, link_demand);
        } else if (
            link_attr.type == nocLinkType::NOC1_NORTH || link_attr.type == nocLinkType::NOC1_WEST) {
            sim_stats.avg_noc1_link_demand += link_demand;
            sim_stats.avg_noc1_link_util += std::fmin(link_demand, max_link_bandwidth);
            sim_stats.max_noc1_link_demand = std::fmax(sim_stats.max_noc1_link_demand, link_demand);
        }
    }
    sim_stats.avg_link_demand *= 100. / (max_link_bandwidth * link_demand_grid.size());
    sim_stats.avg_link_util *= 100. / (max_link_bandwidth * link_demand_grid.size());
    sim_stats.max_link_demand *= 100. / max_link_bandwidth;

    size_t num_noc0_links = link_demand_grid.size() / 2;
    sim_stats.avg_noc0_link_demand *= 100. / (max_link_bandwidth * num_noc0_links);
    sim_stats.avg_noc0_link_util *= 100. / (max_link_bandwidth * num_noc0_links);
    sim_stats.max_noc0_link_demand *= 100. / max_link_bandwidth;

    size_t num_noc1_links = link_demand_grid.size() / 2;
    sim_stats.avg_noc1_link_demand *= 100. / (max_link_bandwidth * num_noc1_links);
    sim_stats.avg_noc1_link_util *= 100. / (max_link_bandwidth * num_noc1_links);
    sim_stats.max_noc1_link_demand *= 100. / max_link_bandwidth;

    // Compute NIU demand and util
    for (const auto &niu_demand : niu_demand_grid) {
        sim_stats.avg_niu_demand += niu_demand;
        sim_stats.max_niu_demand = std::fmax(sim_stats.max_niu_demand, niu_demand);
    }
    // Hack: LINK_BANDWIDTH is not always a good approximation of NIU bandwidth
    sim_stats.avg_niu_demand *= 100. / (max_link_bandwidth * niu_demand_grid.size());
    sim_stats.max_niu_demand *= 100. / max_link_bandwidth;

    // NOTE: copying these is a 10% runtime overhead
    sim_stats.link_demand_grid = link_demand_grid;
    sim_stats.niu_demand_grid = niu_demand_grid;
}

}  // namespace tt_npe
