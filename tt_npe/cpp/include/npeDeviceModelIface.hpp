// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include <string>

#include "npeCommon.hpp"
#include "npeDeviceNode.hpp"
#include "npeTransferState.hpp"
#include "npeStats.hpp"
#include "npeUtil.hpp"
#include "grid.hpp"

namespace tt_npe {

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

    virtual void computeCurrentTransferRate(
        CycleCount start_timestep,
        CycleCount end_timestep,
        std::vector<PETransferState> &transfer_state,
        const std::vector<PETransferID> &live_transfer_ids,
        NIUDemandGrid &niu_demand_grid,
        LinkDemandGrid &link_demand_grid,
        TimestepStats &sim_stats,
        bool enable_congestion_model) const = 0;

    virtual size_t getRows() const = 0;
    virtual size_t getCols() const = 0;

    // returns a table giving the steady state peak bandwidth for a given packet size
    virtual const TransferBandwidthTable &getTransferBandwidthTable() const = 0;

    // returns maximum possible bandwidth for a single noc transaction
    virtual float getMaxNoCTransferBandwidth() const = 0;

    virtual float getLinkBandwidth(const nocLinkID &link_id) const = 0;
    
    virtual const nocLinkAttr& getLinkAttributes(const nocLinkID &link_id) const = 0;
    virtual nocLinkID getLinkID(const nocLinkAttr &link_attr) const = 0;

    virtual CoreType getCoreType(const Coord &c) const = 0;

    virtual BytesPerCycle getSrcInjectionRateByCoreType(CoreType core_type) const = 0;
    virtual BytesPerCycle getSrcInjectionRate(const Coord &c) const = 0;
    virtual BytesPerCycle getSinkAbsorptionRateByCoreType(CoreType core_type) const = 0;
    virtual BytesPerCycle getSinkAbsorptionRate(const Coord &c) const = 0;

    virtual float getAggregateDRAMBandwidth() const = 0;

    virtual DeviceID getDeviceID() const = 0;
};

}  // namespace tt_npe
