// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include <unordered_map>
#include <unordered_set>

#include "grid.hpp"
#include "npeCommon.hpp"
#include "npeDeviceModelIface.hpp"

namespace tt_npe {

class WormholeB0DeviceModel : public npeDeviceModel {
   public:
    WormholeB0DeviceModel() {
        coord_to_core_type = Grid2D<CoreType>(_num_rows, _num_cols);
        for (const auto &[coord, core_type] : coord_to_core_type_map) {
            coord_to_core_type(coord.row, coord.col) = core_type;
        }
        populateNoCLinkLookups();
    }

    void populateNoCLinkLookups() {
        for (size_t r=0; r < getRows(); r++){
            for (size_t c=0; c < getCols(); c++){
                for (size_t i=0; i < size_t(nocLinkType::NUM_LINK_TYPES); i++){
                    nocLinkAttr attr = {Coord(r,c), nocLinkType(i)};
                    link_id_to_attr_lookup.push_back(attr);
                    link_attr_to_id_lookup[attr] = link_id_to_attr_lookup.size()-1;
                }
            }
        }
    }

    float interpolateBW(
        const TransferBandwidthTable &tbt, float max_transfer_bw, size_t packet_size, size_t num_packets) const {
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

    void updateTransferBandwidth(
        std::vector<PETransferState> *transfers,
        const std::vector<PETransferID> &live_transfer_ids) const {
        const auto &tbt = getTransferBandwidthTable();
        const float max_transfer_bw = getMaxNoCTransferBandwidth();
        for (auto &ltid : live_transfer_ids) {
            auto &lt = (*transfers)[ltid];
            auto noc_limited_bw = interpolateBW(tbt, max_transfer_bw, lt.params.packet_size, lt.params.num_packets);
            lt.curr_bandwidth = std::fmin(lt.params.injection_rate, noc_limited_bw);
        }
    }

    void modelCongestion(
        CycleCount start_timestep,
        CycleCount end_timestep,
        std::vector<PETransferState> &transfers,
        const std::vector<PETransferID> &live_transfer_ids,
        NIUDemandGrid &niu_demand_grid,
        LinkDemandGrid &link_demand_grid,
        TimestepStats &sim_stats) const {
        size_t cycles_per_timestep = end_timestep - start_timestep;

        // assume all links have identical bandwidth
        float LINK_BANDWIDTH = getLinkBandwidth(nocLinkID());
        static auto worker_sink_absorption_rate =
            getSinkAbsorptionRateByCoreType(CoreType::WORKER);

        // Note: for now doing gradient descent to determine link bandwidth doesn't
        // appear necessary. Base algorithm devolves to running just a single
        // iteration (first order congestion only).
        constexpr int NUM_ITERS = 1;
        constexpr float grad_fac = 1.0;

        for (int iter = 0; iter < NUM_ITERS; iter++) {
            // determine effective demand through each link
            link_demand_grid.reset(0.0f);
            niu_demand_grid.reset(0.0f);
            for (auto ltid : live_transfer_ids) {
                auto &lt = transfers[ltid];

                // account for transfers starting mid-way into timestep, and derate effective
                // utilization accordingly
                CycleCount predicted_start = std::max(start_timestep, lt.start_cycle);
                float effective_demand =
                    float(end_timestep - predicted_start) / float(cycles_per_timestep);
                effective_demand *= lt.curr_bandwidth;

                // track demand at src and sink NIU
                auto src_niu_idx = size_t(
                    lt.params.noc_type == nocType::NOC0 ? nocNIUType::NOC0_SRC : nocNIUType::NOC1_SRC);
                niu_demand_grid(lt.params.src.row, lt.params.src.col, src_niu_idx) += effective_demand;
                auto sink_niu_idx = size_t(
                    lt.params.noc_type == nocType::NOC0 ? nocNIUType::NOC0_SINK
                                                        : nocNIUType::NOC1_SINK);

                if (std::holds_alternative<Coord>(lt.params.dst)) {
                    const auto &dst = std::get<Coord>(lt.params.dst);
                    niu_demand_grid(dst.row, dst.col, sink_niu_idx) += effective_demand;
                } else {
                    const auto &mcast_dst = std::get<MCastCoordPair>(lt.params.dst);
                    for (auto c : mcast_dst) {
                        // multicast only loads on WORKER NIUs; other NIUS ignore traffic
                        if (getCoreType(c) == CoreType::WORKER) {
                            niu_demand_grid(c.row, c.col, sink_niu_idx) += effective_demand;
                        }
                    }
                }

                for (const auto &link : lt.route) {
                    const auto& link_attr = getLinkAttributes(link);
                    auto [r, c] = link_attr.coord;
                    link_demand_grid(r, c, size_t(link_attr.type)) += effective_demand;
                }
            }

            // find highest demand resource on each route to set bandwidth
            for (auto ltid : live_transfer_ids) {
                auto &lt = transfers[ltid];

                // find max link demand on route
                float max_link_demand_on_route = 0;
                auto update_max_link_demand = [&max_link_demand_on_route](float demand) -> bool {
                    if (demand > max_link_demand_on_route) {
                        max_link_demand_on_route = demand;
                        return true;
                    } else {
                        return false;
                    }
                };
                for (const auto &link : lt.route) {
                    const auto& link_attr = getLinkAttributes(link);
                    auto [r, c] = link_attr.coord;
                    float link_demand = link_demand_grid(r, c, size_t(link_attr.type));
                    update_max_link_demand(link_demand);
                }
                auto min_link_bw_derate = LINK_BANDWIDTH / max_link_demand_on_route;

                // compute bottleneck (min derate factor) for source and sink NIUs
                auto src_niu_idx = size_t(
                    lt.params.noc_type == nocType::NOC0 ? nocNIUType::NOC0_SRC : nocNIUType::NOC1_SRC);
                auto src_bw_demand = niu_demand_grid(lt.params.src.row, lt.params.src.col, src_niu_idx);
                auto src_bw_derate = lt.params.injection_rate / src_bw_demand;

                auto sink_niu_idx = size_t(
                    lt.params.noc_type == nocType::NOC0 ? nocNIUType::NOC0_SINK
                                                        : nocNIUType::NOC1_SINK);

                float sink_bw_derate = 1;
                if (std::holds_alternative<Coord>(lt.params.dst)) {
                    const auto &dst = std::get<Coord>(lt.params.dst);
                    auto sink_bw_demand = niu_demand_grid(dst.row, dst.col, sink_niu_idx);
                    sink_bw_derate = getSinkAbsorptionRate(dst) / sink_bw_demand;
                } else {
                    // multicast transfer speed is set by the slowest sink NIU
                    const auto &mcast_dst = std::get<MCastCoordPair>(lt.params.dst);
                    float sink_demand = 0;
                    for (const auto &loc : mcast_dst) {
                        if (getCoreType(loc) == CoreType::WORKER) {
                            sink_demand =
                                std::min(sink_demand, niu_demand_grid(loc.row, loc.col, sink_niu_idx));
                        }
                    }
                    sink_bw_derate = worker_sink_absorption_rate / sink_demand;
                }

                auto min_niu_bw_derate = std::min(src_bw_derate, sink_bw_derate);

                if (min_link_bw_derate < 1.0 || min_niu_bw_derate < 1.0) {
                    float overall_bw_derate = std::min(min_link_bw_derate, min_niu_bw_derate);

                    lt.curr_bandwidth *= 1.0 - (grad_fac * (1.0f - overall_bw_derate));
                }
            }
        }

        // track statistics; see npeStats.hpp for an explanation of how util and demand differ
        float avg_link_demand = 0;
        float avg_link_util = 0;
        float max_link_demand = 0;
        for (const auto &link_demand : link_demand_grid) {
            avg_link_demand += link_demand;
            avg_link_util += std::min(link_demand, LINK_BANDWIDTH);
            max_link_demand = std::fmax(max_link_demand, link_demand);
        }
        avg_link_demand *= 100. / (LINK_BANDWIDTH * link_demand_grid.size());
        avg_link_util *= 100. / (LINK_BANDWIDTH * link_demand_grid.size());
        max_link_demand *= 100. / LINK_BANDWIDTH;

        float avg_niu_demand = 0;
        float max_niu_demand = 0;
        for (const auto &niu_demand : niu_demand_grid) {
            avg_niu_demand += niu_demand;
            max_niu_demand = std::fmax(max_niu_demand, niu_demand);
        }
        // Hack: LINK_BANDWIDTH is not always a good approximation of NIU bandwidth
        avg_niu_demand *= 100. / (LINK_BANDWIDTH * niu_demand_grid.size());
        max_niu_demand *= 100. / LINK_BANDWIDTH;

        sim_stats.avg_link_demand = avg_link_demand;
        sim_stats.max_link_demand = max_link_demand;
        sim_stats.avg_link_util = avg_link_util;
        sim_stats.avg_niu_demand = avg_niu_demand;
        sim_stats.max_niu_demand = max_niu_demand;

        // NOTE: copying these is a 10% runtime overhead
        sim_stats.link_demand_grid = link_demand_grid;
        sim_stats.niu_demand_grid = niu_demand_grid;
    }


    void computeCurrentTransferRate(
        CycleCount start_timestep,
        CycleCount end_timestep,
        std::vector<PETransferState> &transfer_state,
        const std::vector<PETransferID> &live_transfer_ids,
        NIUDemandGrid &niu_demand_grid,
        LinkDemandGrid &link_demand_grid,
        TimestepStats &sim_stats,
        bool enable_congestion_model) const override {

        // Compute bandwidth for this timestep for all live transfers
        updateTransferBandwidth(&transfer_state, live_transfer_ids);

        // model congestion and derate bandwidth
        if (enable_congestion_model) {
            modelCongestion(
                start_timestep,
                end_timestep,
                transfer_state,
                live_transfer_ids,
                niu_demand_grid,
                link_demand_grid,
                sim_stats);
        }
    }

    size_t getCols() const override { return _num_cols; }
    size_t getRows() const override { return _num_rows; }

    float getLinkBandwidth(const nocLinkID &link_id) const override { return 30; }
    float getAggregateDRAMBandwidth() const override { return 256; }

    const nocLinkAttr &getLinkAttributes(const nocLinkID &link_id) const override {
        TT_ASSERT(link_id < link_id_to_attr_lookup.size());
        return link_id_to_attr_lookup[link_id];
    }

    nocLinkID getLinkID(const nocLinkAttr &link_attr) const override {
        auto it = link_attr_to_id_lookup.find(link_attr);
        TT_ASSERT(
            it != link_attr_to_id_lookup.end(),
            "Could not find Link ID for nocLinkAttr {{ {}, {} }}",
            link_attr.coord,
            magic_enum::enum_name(link_attr.type));
        return it->second;
    }

    const TransferBandwidthTable &getTransferBandwidthTable() const override { return tbt; }

    CoreType getCoreType(const Coord &c) const override {
        return coord_to_core_type(c.row, c.col);
    }

    BytesPerCycle getSrcInjectionRateByCoreType(CoreType core_type) const override {
        auto it = core_type_to_inj_rate.find(core_type);
        if (it == core_type_to_inj_rate.end()) {
            log_error(
                "Could not infer injection rate for; defaulting to WORKER core rate of {}",
                core_type_to_inj_rate.at(CoreType::WORKER));
            return core_type_to_inj_rate.at(CoreType::WORKER);
        } else {
            return core_type_to_inj_rate.at(core_type);
        }
    }
    BytesPerCycle getSrcInjectionRate(const Coord &c) const override {
        return getSrcInjectionRateByCoreType(getCoreType(c));
    }
    BytesPerCycle getSinkAbsorptionRateByCoreType(CoreType core_type) const override {
        auto it = core_type_to_abs_rate.find(core_type);
        if (it == core_type_to_abs_rate.end()) {
            log_error(
                "Could not infer absorption rate for; defaulting to WORKER core rate of {}",
                core_type_to_abs_rate.at(CoreType::WORKER));
            return core_type_to_abs_rate.at(CoreType::WORKER);
        } else {
            return core_type_to_abs_rate.at(core_type);
        }
    }
    BytesPerCycle getSinkAbsorptionRate(const Coord &c) const override {
        return getSinkAbsorptionRateByCoreType(getCoreType(c));
    }

    nocRoute unicastRoute(
        nocType noc_type, const Coord& startpoint, const Coord& endpoint) const override {
        nocRoute route;
        int32_t row = startpoint.row;
        int32_t col = startpoint.col;

        const int32_t erow = endpoint.row;
        const int32_t ecol = endpoint.col;

        if (noc_type == nocType::NOC0) {
            while (true) {
                // for each movement, add the corresponding link to the vector
                if (col != ecol) {
                    route.push_back(getLinkID({{row, col}, nocLinkType::NOC0_EAST}));
                    col = wrapToRange(col + 1, getCols());
                } else if (row != erow) {
                    route.push_back(getLinkID({{row, col}, nocLinkType::NOC0_SOUTH}));
                    row = wrapToRange(row + 1, getRows());
                } else {
                    break;
                }
            }
        } else if (noc_type == nocType::NOC1) {
            while (true) {
                // for each movement, add the corresponding link to the vector
                if (row != erow) {
                    route.push_back(getLinkID({{row, col}, nocLinkType::NOC1_NORTH}));
                    row = wrapToRange(row - 1, getRows());
                } else if (col != ecol) {
                    route.push_back(getLinkID({{row, col}, nocLinkType::NOC1_WEST}));
                    col = wrapToRange(col - 1, getCols());
                } else {
                    break;
                }
            }
        }
        return route;
    }

    nocRoute route(nocType noc_type, const Coord& startpoint, const NocDestination& destination)
        const override {
        if (std::holds_alternative<Coord>(destination)) {
            return unicastRoute(noc_type, startpoint, std::get<Coord>(destination));
        } else {
            auto mcast_pair = std::get<MCastCoordPair>(destination);
            const auto& start_coord = mcast_pair.start_coord;
            const auto& end_coord = mcast_pair.end_coord;
            nocRoute route;

            std::unordered_set<nocLinkID> unique_links;
            if (noc_type == nocType::NOC0) {
                for (int col = start_coord.col; col <= end_coord.col; col++) {
                    auto partial_route = unicastRoute(noc_type, startpoint, {end_coord.row, col});
                    unique_links.insert(partial_route.begin(), partial_route.end());
                }
            } else {
                for (int row = start_coord.row; row <= end_coord.row; row++) {
                    auto partial_route = unicastRoute(noc_type, startpoint, {row, end_coord.col});
                    unique_links.insert(partial_route.begin(), partial_route.end());
                }
            }
            return nocRoute(unique_links.begin(), unique_links.end());
        }
    }

    // returns maximum possible bandwidth for a single noc transaction based on transfer bandwidth table
    float getMaxNoCTransferBandwidth() const override {
        float max_bw = 0;
        for (const auto& [_, bw] : tbt) {
            max_bw = std::fmax(max_bw, bw);
        }
        return max_bw;
    }

    protected:
    const size_t _num_rows = 12;
    const size_t _num_cols = 10;

    std::vector<nocLinkAttr> link_id_to_attr_lookup;
    std::unordered_map<nocLinkAttr,nocLinkID> link_attr_to_id_lookup;

    TransferBandwidthTable tbt = {
        {0, 0}, {128, 5.5}, {256, 10.1}, {512, 18.0}, {1024, 27.4}, {2048, 30.0}, {8192, 30.0}};
    Grid2D<CoreType> coord_to_core_type;
    CoreTypeToInjectionRate core_type_to_inj_rate = {
        {CoreType::DRAM, 23.2},
        {CoreType::ETH, 23.2},
        {CoreType::UNDEF, 28.1},
        {CoreType::WORKER, 28.1}};
    CoreTypeToAbsorptionRate core_type_to_abs_rate = {
        {CoreType::DRAM, 24.0},
        {CoreType::ETH, 24.0},
        {CoreType::UNDEF, 28.1},
        {CoreType::WORKER, 28.1}};

     CoordToCoreTypeMapping coord_to_core_type_map = { 
                {{0,0},{CoreType::DRAM}},
                {{0,1},{CoreType::ETH}},
                {{0,2},{CoreType::ETH}},
                {{0,3},{CoreType::ETH}},
                {{0,4},{CoreType::ETH}},
                {{0,5},{CoreType::DRAM}},
                {{0,6},{CoreType::ETH}},
                {{0,7},{CoreType::ETH}},
                {{0,8},{CoreType::ETH}},
                {{0,9},{CoreType::ETH}},

                {{1,0},{CoreType::DRAM}},
                {{1,1},{CoreType::WORKER}},
                {{1,2},{CoreType::WORKER}},
                {{1,3},{CoreType::WORKER}},
                {{1,4},{CoreType::WORKER}},
                {{1,5},{CoreType::DRAM}},
                {{1,6},{CoreType::WORKER}},
                {{1,7},{CoreType::WORKER}},
                {{1,8},{CoreType::WORKER}},
                {{1,9},{CoreType::WORKER}},

                {{2,0},{CoreType::UNDEF}},
                {{2,1},{CoreType::WORKER}},
                {{2,2},{CoreType::WORKER}},
                {{2,3},{CoreType::WORKER}},
                {{2,4},{CoreType::WORKER}},
                {{2,5},{CoreType::DRAM}},
                {{2,6},{CoreType::WORKER}},
                {{2,7},{CoreType::WORKER}},
                {{2,8},{CoreType::WORKER}},
                {{2,9},{CoreType::WORKER}},

                {{3,0},{CoreType::UNDEF}},
                {{3,1},{CoreType::WORKER}},
                {{3,2},{CoreType::WORKER}},
                {{3,3},{CoreType::WORKER}},
                {{3,4},{CoreType::WORKER}},
                {{3,5},{CoreType::DRAM}},
                {{3,6},{CoreType::WORKER}},
                {{3,7},{CoreType::WORKER}},
                {{3,8},{CoreType::WORKER}},
                {{3,9},{CoreType::WORKER}},

                {{4,0},{CoreType::UNDEF}},
                {{4,1},{CoreType::WORKER}},
                {{4,2},{CoreType::WORKER}},
                {{4,3},{CoreType::WORKER}},
                {{4,4},{CoreType::WORKER}},
                {{4,5},{CoreType::DRAM}},
                {{4,6},{CoreType::WORKER}},
                {{4,7},{CoreType::WORKER}},
                {{4,8},{CoreType::WORKER}},
                {{4,9},{CoreType::WORKER}},

                {{5,0},{CoreType::DRAM}},
                {{5,1},{CoreType::WORKER}},
                {{5,2},{CoreType::WORKER}},
                {{5,3},{CoreType::WORKER}},
                {{5,4},{CoreType::WORKER}},
                {{5,5},{CoreType::DRAM}},
                {{5,6},{CoreType::WORKER}},
                {{5,7},{CoreType::WORKER}},
                {{5,8},{CoreType::WORKER}},
                {{5,9},{CoreType::WORKER}},

                {{6,0},{CoreType::DRAM}},
                {{6,1},{CoreType::ETH}},
                {{6,2},{CoreType::ETH}},
                {{6,3},{CoreType::ETH}},
                {{6,4},{CoreType::ETH}},
                {{6,5},{CoreType::DRAM}},
                {{6,6},{CoreType::ETH}},
                {{6,7},{CoreType::ETH}},
                {{6,8},{CoreType::ETH}},
                {{6,9},{CoreType::ETH}},

                {{7,0},{CoreType::DRAM}},
                {{7,1},{CoreType::WORKER}},
                {{7,2},{CoreType::WORKER}},
                {{7,3},{CoreType::WORKER}},
                {{7,4},{CoreType::WORKER}},
                {{7,5},{CoreType::DRAM}},
                {{7,6},{CoreType::WORKER}},
                {{7,7},{CoreType::WORKER}},
                {{7,8},{CoreType::WORKER}},
                {{7,9},{CoreType::WORKER}},

                {{8,0},{CoreType::UNDEF}},
                {{8,1},{CoreType::WORKER}},
                {{8,2},{CoreType::WORKER}},
                {{8,3},{CoreType::WORKER}},
                {{8,4},{CoreType::WORKER}},
                {{8,5},{CoreType::DRAM}},
                {{8,6},{CoreType::WORKER}},
                {{8,7},{CoreType::WORKER}},
                {{8,8},{CoreType::WORKER}},
                {{8,9},{CoreType::WORKER}},

                {{9,0},{CoreType::UNDEF}},
                {{9,1},{CoreType::WORKER}},
                {{9,2},{CoreType::WORKER}},
                {{9,3},{CoreType::WORKER}},
                {{9,4},{CoreType::WORKER}},
                {{9,5},{CoreType::DRAM}},
                {{9,6},{CoreType::WORKER}},
                {{9,7},{CoreType::WORKER}},
                {{9,8},{CoreType::WORKER}},
                {{9,9},{CoreType::WORKER}},

                {{10,0},{CoreType::UNDEF}},
                {{10,1},{CoreType::WORKER}},
                {{10,2},{CoreType::WORKER}},
                {{10,3},{CoreType::WORKER}},
                {{10,4},{CoreType::WORKER}},
                {{10,5},{CoreType::DRAM}},
                {{10,6},{CoreType::WORKER}},
                {{10,7},{CoreType::WORKER}},
                {{10,8},{CoreType::WORKER}},
                {{10,9},{CoreType::WORKER}},

                {{11,0},{CoreType::DRAM}},
                {{11,1},{CoreType::WORKER}},
                {{11,2},{CoreType::WORKER}},
                {{11,3},{CoreType::WORKER}},
                {{11,4},{CoreType::WORKER}},
                {{11,5},{CoreType::DRAM}},
                {{11,6},{CoreType::WORKER}},
                {{11,7},{CoreType::WORKER}},
                {{11,8},{CoreType::WORKER}},
                {{11,9},{CoreType::WORKER}},
          };
};
}  // namespace tt_npe
