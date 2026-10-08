// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

#include <algorithm>

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include "npeCommon.hpp"
#include "npeDeviceTypes.hpp"
#include "npeDeviceState.hpp"
#include "npeTransferState.hpp"
#include "npeStats.hpp"
#include "npeUtil.hpp"

namespace tt_npe {

enum class DeviceArch: unsigned char {
    WormholeB0,
    Blackhole,
    Quasar
};

class npeDeviceModel {
   public:
    virtual ~npeDeviceModel() {}

    // returns link-by-link route from startpoint to destination(s) on the specified noc
    virtual nocRoute route(
        nocIndex noc, const Coord &startpoint, const NocDestination &destination) const = 0;

    // Initialize device state with appropriate dimensions for this device model
    virtual std::unique_ptr<npeDeviceState> initDeviceState() const = 0;

    // Compute current transfer rate using device state
    virtual void computeCurrentTransferRate(
        Cycle start_timestep,
        Cycle end_timestep,
        std::vector<PETransferState> &transfer_state,
        const std::vector<PETransferID> &live_transfer_ids,
        npeDeviceState &device_state,
        bool enable_congestion_model) const = 0;

    virtual Cycle getReadLatency(const Coord &source, const Coord &destination) const = 0;
    virtual Cycle getWriteLatency(
        const Coord &source, const Coord &destination, nocIndex noc) const = 0;

    virtual DeviceArch getArch() const = 0;

    // returns number of rows and columns
    virtual size_t getRows() const = 0;
    virtual size_t getCols() const = 0;
    virtual size_t getNumChips() const = 0;
    virtual const boost::unordered_flat_set<DeviceID>& getDeviceIDs() const = 0;
    virtual bool isValidDeviceID(DeviceID device_id) const = 0;

    virtual const nocLinkAttr& getLinkAttributes(const nocLinkID &link_id) const = 0;
    virtual nocLinkID getLinkID(const nocLinkAttr &link_attr) const = 0;
    virtual const std::vector<nocLinkKind>& getLinkKinds() const = 0;

    // default matches Wormhole and Blackhole, which have two NoCs
    virtual size_t getNumNocs() const { return 2; }
    // links on a single chip that belong to noc; used to normalize per-NoC stats.
    // The default assumes every tile has every link kind, as on the Wormhole and
    // Blackhole torus; models that omit links at some tiles must override it.
    virtual size_t getNumLinksPerChip(nocIndex noc) const {
        const auto &kinds = getLinkKinds();
        const auto kinds_on_noc = std::count_if(
            kinds.begin(), kinds.end(), [noc](const nocLinkKind &kind) { return kind.noc == noc; });
        return getRows() * getCols() * kinds_on_noc;
    }

    virtual const nocNIUAttr& getNIUAttributes(const nocNIUID &niu_id) const = 0;
    virtual nocNIUID getNIUID(const nocNIUAttr &niu_attr) const = 0;
    virtual const std::vector<nocNIUKind>& getNIUKinds() const = 0;

    virtual CoreType getCoreType(const Coord &c) const = 0;
    virtual uint32_t getDramControllerIDForCore(const Coord &c) const = 0;

    virtual BytesPerCycle getSrcInjectionRate(const Coord &c) const = 0;
    virtual BytesPerCycle getSinkAbsorptionRate(const Coord &c) const = 0;

    virtual float getLinkBandwidth(const nocLinkID &link_id) const = 0;

    virtual float getDRAMBandwidthPerChip() const = 0;
    virtual float getDRAMBandwidthPerController() const = 0;

    virtual float getEthBandwidthPerLink() const = 0;
};

}  // namespace tt_npe
