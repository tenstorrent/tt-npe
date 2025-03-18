// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include <string>

#include "grid.hpp"
#include "npeCommon.hpp"
#include "npeDeviceNode.hpp"
#include "npeUtil.hpp"

namespace tt_npe {

using nocRoute = std::vector<nocLinkID>;

using CoordToCoreTypeMapping = std::unordered_map<Coord, CoreType>;
using CoreTypeToInjectionRate = std::unordered_map<CoreType, BytesPerCycle>;
using CoreTypeToAbsorptionRate = std::unordered_map<CoreType, BytesPerCycle>;

using TransferBandwidthTable = std::vector<std::pair<size_t, BytesPerCycle>>;

class npeDeviceModel {
   public:
    npeDeviceModel() = default;

    // throws an npeException if device model cannot be built for device_name
    npeDeviceModel(const std::string &device_name);

    // returns unicast route from startpoint to endpoint for the specified noc type
    nocRoute unicastRoute(nocType noc_type, const Coord &startpoint, const Coord &endpoint) const;

    // returns link-by-link route from startpoint to destination(s) for the specified noc type
    nocRoute route(
        nocType noc_type, const Coord &startpoint, const NocDestination &destination) const;

    size_t getRows() const { return device_grid.getRows(); }
    size_t getCols() const { return device_grid.getCols(); }

    // returns a table giving the steady state peak bandwidth for a given packet size
    const TransferBandwidthTable &getTransferBandwidthTable() const {
        return transfer_bandwidth_table;
    }

    // returns maximum possible bandwidth for a single noc transaction
    float getMaxNoCTransferBandwidth() const;

    float getLinkBandwidth(const nocLinkID &link_id) const;

    CoreType getCoreType(const Coord &c) const {
        if (coord_to_core_type.inBounds(c.row, c.col)) {
            return coord_to_core_type(c.row, c.col);
        } else {
            return CoreType::UNDEF;
        }
    }
    BytesPerCycle getSrcInjectionRateByCoreType(CoreType core_type) const {
        auto it = core_type_to_injection_rate.find(core_type);
        if (it == core_type_to_injection_rate.end()) {
            log_error(
                "Could not infer injection rate for; defaulting to WORKER core rate of {}",
                core_type_to_injection_rate.at(CoreType::WORKER));
            return core_type_to_injection_rate.at(CoreType::WORKER);
        } else {
            return core_type_to_injection_rate.at(core_type);
        }
    }
    BytesPerCycle getSrcInjectionRate(const Coord &c) const {
        return getSrcInjectionRateByCoreType(getCoreType(c));
    }
    BytesPerCycle getSinkAbsorptionRateByCoreType(CoreType core_type) const {
        auto it = core_type_to_absorption_rate.find(core_type);
        if (it == core_type_to_absorption_rate.end()) {
            log_error(
                "Could not infer absorption rate for; defaulting to WORKER core rate of {}",
                core_type_to_absorption_rate.at(CoreType::WORKER));
            return core_type_to_absorption_rate.at(CoreType::WORKER);
        } else {
            return core_type_to_absorption_rate.at(core_type);
        }
    }
    BytesPerCycle getSinkAbsorptionRate(const Coord &c) const {
        return getSinkAbsorptionRateByCoreType(getCoreType(c));
    }

    float getAggregateDRAMBandwidth() const;

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
    Grid2D<CoreType> coord_to_core_type;
    CoreTypeToInjectionRate core_type_to_injection_rate;
    CoreTypeToAbsorptionRate core_type_to_absorption_rate;
    TransferBandwidthTable transfer_bandwidth_table;
    double aggregate_dram_bandwidth;
};

}  // namespace tt_npe
