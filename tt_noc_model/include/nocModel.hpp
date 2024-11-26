#pragma once

#include <fmt/core.h>

#include <string>

#include "ScopedTimer.hpp"
#include "grid.hpp"
#include "nocCommon.hpp"
#include "nocNode.hpp"
#include "util.hpp"

namespace tt_npe {

using namespace magic_enum;

using nocRoute = std::vector<nocLinkID>;
class nocModel {
   public:
    nocModel() {}
    nocModel(const std::string &device_name) {
        if (device_name == "wormhole_b0") {
            buildWormholeB0Device();
        }
    }

    nocRoute route(nocType noc_type, const Coord startpoint, const Coord endpoint) {
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
                    row = wrapToRange(row - 1, getRows());
                } else {
                    break;
                }
            }
        } else if (noc_type == nocType::NOC1) {
            while (true) {
                // for each movement, add the corresponding link to the vector
                if (row != erow) {
                    route.push_back({{row, col}, nocLinkType::NOC1_NORTH});
                    row = wrapToRange(row + 1, getRows());
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

    // build small device for proof of concept
    void buildWormholeB0Device() {
        const size_t kcols = 12;
        const size_t krows = 10;
        _noc_grid = Grid2D<nocNode>(krows, kcols);

        // gen noc links between nocNode
        for (int row = 0; row < krows; row++) {
            for (int col = 0; col < kcols; col++) {
                // assign coordinates within node
                auto &src_node = _noc_grid(row, col);
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

    size_t getRows() const { return _noc_grid.getRows(); }
    size_t getCols() const { return _noc_grid.getCols(); }

    Coord wrapNoCCoord(const Coord &c) const {
        auto wrapped_rows = wrapToRange(c.row, getRows());
        auto wrapped_cols = wrapToRange(c.col, getCols());
        return Coord{wrapped_rows, wrapped_cols};
    }

   private:
    std::string _device_name;
    Grid2D<nocNode> _noc_grid;
};

}  // namespace tt_npe