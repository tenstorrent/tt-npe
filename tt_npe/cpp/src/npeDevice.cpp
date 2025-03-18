// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#include <unordered_set>

#include "device_data/wormhole_b0.hpp"
#include "npeCommon.hpp"
#include "npeDeviceModel.hpp"
#include "npeDeviceNode.hpp"
#include "npeUtil.hpp"

namespace tt_npe {

npeDeviceModel::npeDeviceModel(const std::string& device_name) {
    if (device_name == "wormhole_b0") {
        buildWormholeB0Device();
    } else {
        throw npeException(
            npeErrorCode::DEVICE_MODEL_INIT_FAILED,
            fmt::format("Device '{}' is unsupported; npeDeviceModel init aborted.", device_name));
    }
}

float npeDeviceModel::getLinkBandwidth(const nocLinkID& link_id) const {
    // for now hardcode this!
    return wormhole_b0::LINK_BANDWIDTH;
}

float npeDeviceModel::getAggregateDRAMBandwidth() const {
    return wormhole_b0::AGGREGATE_DRAM_BANDWIDTH;
}

nocRoute npeDeviceModel::unicastRoute(
    nocType noc_type, const Coord& startpoint, const Coord& endpoint) const {
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

nocRoute npeDeviceModel::route(
    nocType noc_type, const Coord& startpoint, const NocDestination& destination) const {
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

// build small device for proof of concept
void npeDeviceModel::buildWormholeB0Device() {
    // lookup table for packet_size->avg_bandwidth for wormhole_b0 based on empirical measurement
    transfer_bandwidth_table = wormhole_b0::TRANSFER_BW_TABLE;

    device_grid = Grid2D<npeDeviceNode>(wormhole_b0::NUM_ROWS, wormhole_b0::NUM_COLS);
    coord_to_core_type = Grid2D<CoreType>(wormhole_b0::NUM_ROWS, wormhole_b0::NUM_COLS);

    for (const auto& [coord, core_type] : wormhole_b0::CORE_TO_TYPE_MAP) {
        coord_to_core_type(coord.row, coord.col) = core_type;
    }
    core_type_to_injection_rate = wormhole_b0::CORE_TYPE_TO_INJECTION_RATE;
    core_type_to_absorption_rate = wormhole_b0::CORE_TYPE_TO_INJECTION_RATE;

    // gen noc links between nocNode
    for (int row = 0; row < device_grid.getRows(); row++) {
        for (int col = 0; col < device_grid.getCols(); col++) {
            // assign coordinates within node
            auto& src_node = device_grid(row, col);
            src_node.coord = {row, col};

            src_node.links.resize(4);

            // build NOC0 links
            src_node.getLink(nocLinkType::NOC0_EAST) = {
                .src_coord = {row, col}, .dst_coord = wrapNoCCoord({row + 1, col})};
            src_node.getLink(nocLinkType::NOC0_SOUTH) = {
                .src_coord = {row, col}, .dst_coord = wrapNoCCoord({row, col - 1})};

            // build NOC1
            src_node.getLink(nocLinkType::NOC1_WEST) = {
                .src_coord = {row, col}, .dst_coord = wrapNoCCoord({row - 1, col})};
            src_node.getLink(nocLinkType::NOC1_NORTH) = {
                .src_coord = {row, col}, .dst_coord = wrapNoCCoord({row, col + 1})};
        }
    }
}

// returns maximum possible bandwidth for a single noc transaction based on transfer bandwidth table
float npeDeviceModel::getMaxNoCTransferBandwidth() const {
    float max_bw = 0;
    for (const auto& [_,bw] : transfer_bandwidth_table){
        max_bw = std::fmax(max_bw,bw);
    }
    return max_bw;
}

}  // namespace tt_npe