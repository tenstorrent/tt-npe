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
        coord_to_core_type = Grid2D<CoreType>(getRows(), getCols());
        for (const auto &[coord, core_type] : coord_to_core_type_map) {
            coord_to_core_type(coord.row, coord.col) = core_type;
        }
    }

    size_t getCols() const override { return 10; }
    size_t getRows() const override { return 12; }

    float getLinkBandwidth(const nocLinkID &link_id) const override { return 30; }
    float getAggregateDRAMBandwidth() const override { return 256; }

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
                    route.push_back({{row, col}, nocLinkType::NOC0_EAST});
                    col = wrapToRange(col + 1, getCols());
                } else if (row != erow) {
                    route.push_back({{row, col}, nocLinkType::NOC0_SOUTH});
                    row = wrapToRange(row + 1, getRows());
                } else {
                    break;
                }
            }
        } else if (noc_type == nocType::NOC1) {
            while (true) {
                // for each movement, add the corresponding link to the vector
                if (row != erow) {
                    route.push_back({{row, col}, nocLinkType::NOC1_NORTH});
                    row = wrapToRange(row - 1, getRows());
                } else if (col != ecol) {
                    route.push_back({{row, col}, nocLinkType::NOC1_WEST});
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