// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC

#pragma once

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
    Blackhole
};

class npeDeviceModel {
   public:
    virtual ~npeDeviceModel() {}

    // returns link-by-link route from startpoint to destination(s) for the specified noc type
    virtual nocRoute route(
        nocType noc_type, const Coord &startpoint, const NocDestination &destination) const = 0;

    // Initialize device state with appropriate dimensions for this device model.
    // When enable_dram_controller_model is false the DRAM controller demand grid is left
    // empty, which short-circuits every DRAM-controller code path in modelCongestion.
    virtual std::unique_ptr<npeDeviceState> initDeviceState(
        bool enable_dram_controller_model = false) const = 0;

    // Compute current transfer rate using device state
    virtual void computeCurrentTransferRate(
        Cycle start_timestep,
        Cycle end_timestep,
        std::vector<PETransferState> &transfer_state,
        const std::vector<PETransferID> &live_transfer_ids,
        npeDeviceState &device_state,
        bool enable_congestion_model,
        const DramCongestionParams &dram_params = {}) const = 0;

    virtual DeviceArch getArch() const = 0;

    // returns number of rows and columns
    virtual size_t getRows() const = 0;
    virtual size_t getCols() const = 0;
    virtual size_t getNumChips() const = 0;
    virtual const boost::unordered_flat_set<DeviceID>& getDeviceIDs() const = 0;
    virtual bool isValidDeviceID(DeviceID device_id) const = 0;

    virtual const nocLinkAttr& getLinkAttributes(const nocLinkID &link_id) const = 0;
    virtual nocLinkID getLinkID(const nocLinkAttr &link_attr) const = 0;
    virtual const std::vector<nocLinkType>& getLinkTypes() const = 0;
    
    virtual const nocNIUAttr& getNIUAttributes(const nocNIUID &niu_id) const = 0;
    virtual nocNIUID getNIUID(const nocNIUAttr &niu_attr) const = 0;
    virtual const std::vector<nocNIUType>& getNIUTypes() const = 0;

    virtual CoreType getCoreType(const Coord &c) const = 0;
    virtual uint32_t getDramControllerIDForCore(const Coord &c) const = 0;

    // Number of distinct DRAM controller IDs emitted by getDramControllerIDForCore() for a
    // single chip. NOTE: this is deliberately NOT the same thing as the model's
    // NUM_DRAM_CONTROLLERS constant used for aggregate bandwidth arithmetic -- under
    // Blackhole SINGLE_BANK_HARVESTING that constant is 7 while the coordinate map still
    // emits IDs 0..7. Grids must be sized off this value to stay in bounds.
    virtual size_t getNumDramControllers() const = 0;

    // Flattened index of a DRAM coordinate's controller in a DramDemandGrid.
    // The device_id stride is mandatory: multichip models delegate
    // getDramControllerIDForCore() to their single-chip member, whose implementation
    // hardcodes device_id 0, so controller IDs collide across chips without it.
    size_t getDramDemandID(const Coord &c) const {
        return size_t(c.device_id) * getNumDramControllers() + getDramControllerIDForCore(c);
    }

    // Total number of DramDemandGrid slots needed for this model.
    size_t getNumDramDemandSlots() const { return getNumChips() * getNumDramControllers(); }

    virtual BytesPerCycle getSrcInjectionRate(const Coord &c) const = 0;
    virtual BytesPerCycle getSinkAbsorptionRate(const Coord &c) const = 0;

    virtual float getLinkBandwidth(const nocLinkID &link_id) const = 0;

    virtual float getDRAMBandwidthPerChip() const = 0;
    virtual float getDRAMBandwidthPerController() const = 0;

    // Per-controller capacity used by the *congestion model only*. Deliberately routed
    // through a scale factor so that calibrating congestion never perturbs the published
    // dram_bw_util / dram_bw_util_per_controller reporting numbers.
    float getDRAMControllerCongestionCapacity(float capacity_scale) const {
        return getDRAMBandwidthPerController() * capacity_scale;
    }

    virtual float getEthBandwidthPerLink() const = 0;
};

}  // namespace tt_npe
