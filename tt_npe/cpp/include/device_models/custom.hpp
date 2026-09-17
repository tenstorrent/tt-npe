// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC

#pragma once

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include "grid.hpp"
#include "npeDeviceModelConfigResolver.hpp"
#include "npeDeviceModelIface.hpp"

namespace tt_npe {

class CustomDeviceModel final : public npeDeviceModel {
   public:
    explicit CustomDeviceModel(
        ResolvedNpeDeviceModelConfig resolved_config, size_t num_chips = 1);
    ~CustomDeviceModel() override = default;

    nocRoute route(
        nocType noc_type,
        const Coord& startpoint,
        const NocDestination& destination) const override;

    std::unique_ptr<npeDeviceState> initDeviceState() const override;

    void computeCurrentTransferRate(
        Cycle start_timestep,
        Cycle end_timestep,
        std::vector<PETransferState>& transfer_state,
        const std::vector<PETransferID>& live_transfer_ids,
        npeDeviceState& device_state,
        bool enable_congestion_model) const override;

    DeviceArch getArch() const override;

    size_t getRows() const override;
    size_t getCols() const override;
    size_t getNumChips() const override;
    const boost::unordered_flat_set<DeviceID>& getDeviceIDs() const override;
    bool isValidDeviceID(DeviceID device_id) const override;

    const nocLinkAttr& getLinkAttributes(const nocLinkID& link_id) const override;
    nocLinkID getLinkID(const nocLinkAttr& link_attr) const override;
    const std::vector<nocLinkType>& getLinkTypes() const override;

    const nocNIUAttr& getNIUAttributes(const nocNIUID& niu_id) const override;
    nocNIUID getNIUID(const nocNIUAttr& niu_attr) const override;
    const std::vector<nocNIUType>& getNIUTypes() const override;

    CoreType getCoreType(const Coord& coord) const override;
    uint32_t getDramControllerIDForCore(const Coord& coord) const override;
    BytesPerCycle getSrcInjectionRate(const Coord& coord) const override;
    BytesPerCycle getSinkAbsorptionRate(const Coord& coord) const override;

    float getLinkBandwidth(const nocLinkID& link_id) const override;
    float getDRAMBandwidthPerChip() const override;
    float getDRAMBandwidthPerController() const override;
    float getEthBandwidthPerLink() const override;

   private:
    void populateCoreLookups();
    void populateNoCLookups();
    nocRoute unicastRoute(
        nocType noc_type, const Coord& startpoint, const Coord& destination) const;

    ResolvedNpeDeviceModelConfig resolved_config_;
    size_t num_chips_;
    Grid2D<CoreType> core_types_;
    DramCoordToControllerMapping dram_controller_by_coord_;
    boost::unordered_flat_set<DeviceID> device_ids_;

    std::vector<nocLinkAttr> link_attributes_by_id_;
    boost::unordered_flat_map<nocLinkAttr, nocLinkID> link_id_by_attributes_;
    std::vector<nocNIUAttr> niu_attributes_by_id_;
    boost::unordered_flat_map<nocNIUAttr, nocNIUID> niu_id_by_attributes_;

    const std::vector<nocLinkType> link_types_ = {
        nocLinkType::NOC0_EAST,
        nocLinkType::NOC0_SOUTH,
        nocLinkType::NOC1_NORTH,
        nocLinkType::NOC1_WEST};
    const std::vector<nocNIUType> niu_types_ = {
        nocNIUType::NOC0_SRC,
        nocNIUType::NOC0_SINK,
        nocNIUType::NOC1_SRC,
        nocNIUType::NOC1_SINK};
};

}  // namespace tt_npe
