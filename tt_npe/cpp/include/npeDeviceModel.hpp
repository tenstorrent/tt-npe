// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include <string>

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
    virtual ~npeDeviceModel() {}

    // returns unicast route from startpoint to endpoint for the specified noc type
    virtual nocRoute unicastRoute(
        nocType noc_type, const Coord &startpoint, const Coord &endpoint) const = 0;

    // returns link-by-link route from startpoint to destination(s) for the specified noc type
    virtual nocRoute route(
        nocType noc_type, const Coord &startpoint, const NocDestination &destination) const = 0;

    virtual size_t getRows() const = 0;
    virtual size_t getCols() const = 0;

    // returns a table giving the steady state peak bandwidth for a given packet size
    virtual const TransferBandwidthTable &getTransferBandwidthTable() const = 0;

    // returns maximum possible bandwidth for a single noc transaction
    virtual float getMaxNoCTransferBandwidth() const = 0;

    virtual float getLinkBandwidth(const nocLinkID &link_id) const = 0;

    virtual CoreType getCoreType(const Coord &c) const = 0;

    virtual BytesPerCycle getSrcInjectionRateByCoreType(CoreType core_type) const = 0;
    virtual BytesPerCycle getSrcInjectionRate(const Coord &c) const = 0;
    virtual BytesPerCycle getSinkAbsorptionRateByCoreType(CoreType core_type) const = 0;
    virtual BytesPerCycle getSinkAbsorptionRate(const Coord &c) const = 0;

    virtual float getAggregateDRAMBandwidth() const = 0;
};

}  // namespace tt_npe
