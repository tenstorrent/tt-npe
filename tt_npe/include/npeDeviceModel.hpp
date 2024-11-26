#pragma once

#include <string>

#include "grid.hpp"
#include "npeCommon.hpp"
#include "npeDeviceNode.hpp"

namespace tt_npe {

using nocRoute = std::vector<nocLinkID>;
class npeDeviceModel {
   public:
    npeDeviceModel() {}
    npeDeviceModel(const std::string &device_name);

    // returns link-by-link route from startpoint to endpoint for the specified noc type
    nocRoute route(nocType noc_type, const Coord startpoint, const Coord endpoint) const;

    size_t getRows() const { return device_grid.getRows(); }
    size_t getCols() const { return device_grid.getCols(); }

   private:
    // build wormhole_b0 device
    void buildWormholeB0Device();

    // wraps a coordinate to be within noc grid; useful for stitching torus together
    Coord wrapNoCCoord(const Coord &c) const {
        auto wrapped_rows = wrapToRange(c.row, getRows());
        auto wrapped_cols = wrapToRange(c.col, getCols());
        return Coord{wrapped_rows, wrapped_cols};
    }

    std::string _device_name;
    Grid2D<npeDeviceNode> device_grid;
};

}  // namespace tt_npe
