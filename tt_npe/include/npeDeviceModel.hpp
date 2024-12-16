#pragma once

#include <string>

#include "grid.hpp"
#include "npeCommon.hpp"
#include "npeDeviceNode.hpp"
#include "util.hpp"

namespace tt_npe {

using nocRoute = std::vector<nocLinkID>;

using CoordToTypeMapping = std::unordered_map<Coord, CoreType>;
using CoreTypeToInjectionRate = std::unordered_map<CoreType, BytesPerCycle>;

using TransferBandwidthTable = std::vector<std::pair<size_t, BytesPerCycle>>;

class npeDeviceModel {
   public:
    npeDeviceModel() = default;

    // throws an npeException if device model cannot be built for device_name 
    npeDeviceModel(const std::string &device_name);

    // returns link-by-link route from startpoint to endpoint for the specified noc type
    nocRoute route(nocType noc_type, const Coord startpoint, const Coord endpoint) const;

    size_t getRows() const { return device_grid.getRows(); }
    size_t getCols() const { return device_grid.getCols(); }

    // returns a table giving the steady state peak bandwidth for a given packet size
    const TransferBandwidthTable& getTransferBandwidthTable() const { return transfer_bandwidth_table; }

    CoreType getCoreType(const Coord &c) const {
        auto it = core_to_type_mapping.find(c);
        if (it != core_to_type_mapping.end()) {
            return it->second;
        } else {
            return CoreType::UNDEF;
        }
    }
    BytesPerCycle getSrcInjectionRate(const Coord &c) const {
        auto core_type = getCoreType(c);
        auto it = core_type_to_ir.find(core_type);
        if (it == core_type_to_ir.end()) {
            log_error(
                "Could not infer injection rate for; defaulting to WORKER core rate of {}",
                core_type_to_ir.at(CoreType::WORKER));
            return core_type_to_ir.at(CoreType::WORKER);
        } else { 
            return core_type_to_ir.at(core_type);
        }
    }

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
    CoordToTypeMapping core_to_type_mapping;
    CoreTypeToInjectionRate core_type_to_ir;
    TransferBandwidthTable transfer_bandwidth_table;
};

}  // namespace tt_npe
